#!/usr/bin/env python3
import os
import time
import asyncio
import threading
import struct
import fcntl
from datetime import datetime
from typing import Set
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse

DEVICE_PATH = "/dev/generador_dds"
DDS_IOCTL_GET_STATE = 0x800c6401

app = FastAPI()
connected_clients: Set[WebSocket] = set()
main_loop = None

current_state = {
    "freq": 1000,
    "wave": 0,
    "step": 100
}

nvs_history = []
dev_fd = None
io_lock = threading.Lock()

def get_ioctl_state():
    global dev_fd
    if dev_fd is None:
        return None
    try:
        buf = bytearray(12)
        with io_lock:
            fcntl.ioctl(dev_fd, DDS_IOCTL_GET_STATE, buf, True)
        freq, wave, step = struct.unpack("iii", buf)
        return {"freq": freq, "wave": wave, "step": step}
    except Exception as e:
        print(f"[ERROR IOCTL] {e}")
        return None

def write_to_device(cmd: str):
    global dev_fd
    if dev_fd is None:
        return
    full_cmd = f"{cmd.strip()}\r\n"
    with io_lock:
        try:
            os.write(dev_fd, full_cmd.encode('ascii'))
        except Exception as e:
            print(f"[ERROR TX] {e}")

def broadcast_threadsafe(data: dict):
    if main_loop and connected_clients:
        for client in list(connected_clients):
            asyncio.run_coroutine_threadsafe(client.send_json(data), main_loop)

def telemetry_worker():
    global dev_fd
    print("[*] Hilo de telemetría RX iniciado sobre descriptor persistente...")
    buffer = ""
    while True:
        if dev_fd is None:
            time.sleep(0.1)
            continue
        try:
            chunk = b""
            with io_lock:
                try:
                    chunk = os.read(dev_fd, 128)
                except (BlockingIOError, InterruptedError):
                    pass

            if chunk:
                buffer += chunk.decode('ascii', errors='replace')
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue

                    updated = False

                    if "STATUS:FREQ=" in line:
                        current_state["freq"] = int(line.split("=")[1])
                        updated = True
                    elif "STATUS:WAVE=" in line:
                        current_state["wave"] = int(line.split("=")[1])
                        updated = True
                    elif "STATUS:STEP=" in line:
                        current_state["step"] = int(line.split("=")[1])
                        updated = True
                    elif "OK: FREQ =" in line:
                        val = line.split("=")[1].replace("Hz", "").strip()
                        current_state["freq"] = int(val)
                        updated = True
                    elif "OK: WAVE =" in line:
                        val = line.split("=")[1].strip()
                        current_state["wave"] = int(val)
                        updated = True
                    elif "STATUS:NVS_SAVED:" in line:
                        vals = line.split(":")[2].split(",")
                        f, w, s = int(vals[0]), int(vals[1]), int(vals[2])
                        current_state["freq"] = f
                        current_state["wave"] = w
                        current_state["step"] = s

                        record = {
                            "timestamp": datetime.now().strftime("%H:%M:%S"),
                            "freq": f,
                            "wave": w,
                            "step": s
                        }
                        nvs_history.insert(0, record)
                        if len(nvs_history) > 10:
                            nvs_history.pop()

                        broadcast_threadsafe({"type": "NVS_RECORD", "payload": record})
                        updated = True

                    if updated:
                        broadcast_threadsafe({"type": "STATE_UPDATE", "payload": current_state})

        except Exception as e:
            time.sleep(0.05)

        time.sleep(0.01)

@app.on_event("startup")
async def startup_event():
    global main_loop, dev_fd
    main_loop = asyncio.get_running_loop()

    try:
        dev_fd = os.open(DEVICE_PATH, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except Exception as e:
        print(f"[FATAL] No se pudo abrir {DEVICE_PATH}: {e}")

    # Enviar SYNC inicial al ESP32
    write_to_device("SYNC")

    t = threading.Thread(target=telemetry_worker, daemon=True)
    t.start()

@app.on_event("shutdown")
async def shutdown_event():
    global dev_fd
    if dev_fd:
        os.close(dev_fd)

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.add(websocket)

    await websocket.send_json({"type": "STATE_UPDATE", "payload": current_state})
    await websocket.send_json({"type": "NVS_HISTORY_INIT", "payload": nvs_history})

    try:
        while True:
            data = await websocket.receive_json()
            action = data.get("action")

            if action == "SYNC_IOCTL":
                print("[WS] Solicitud de IOCTL Sync -> Consultando al ESP32...")
                # 1. Pedirle al microcontrolador que transmita su estado actual por UART
                write_to_device("SYNC")
                # Breve pausa (50 ms) para que el ESP32 responda y el driver parsee
                await asyncio.sleep(0.05)

                # 2. Leer el estado fresco consolidado en el driver mediante IOCTL
                kstate = get_ioctl_state()
                if kstate:
                    current_state.update(kstate)
                    broadcast_threadsafe({"type": "STATE_UPDATE", "payload": current_state})
                    await websocket.send_json({"type": "IOCTL_SYNC_CONFIRMED", "payload": current_state})

            elif action == "SET_FREQ":
                val = int(data.get("value", 1000))
                write_to_device(f"FREQ={val}")

            elif action == "SET_WAVE":
                val = int(data.get("value", 0))
                write_to_device(f"WAVE={val}")

    except WebSocketDisconnect:
        connected_clients.discard(websocket)
    except Exception:
        connected_clients.discard(websocket)

@app.get("/", response_class=HTMLResponse)
async def get_index():
    with open("index.html", "r", encoding="utf-8") as f:
        return f.read()

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("app:app", host="0.0.0.0", port=8000, reload=False)
