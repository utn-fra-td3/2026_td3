#!/usr/bin/env python3
import sys
import os
import time
import argparse
import struct
import fcntl

DEVICE_PATH = "/dev/generador_dds"
TIMEOUT = 1.0

# Definición IOCTL: _IOR('d', 1, struct dds_status_t)
# struct dds_status_t = 3 x int32 (12 bytes) -> código: 0x800c6401
DDS_IOCTL_GET_STATE = 0x800c6401

def ioctl_sync_state():
    """Ejecuta una syscall ioctl() para leer el estado directamente del driver."""
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
        # Buffer de 12 bytes vacío para que el kernel escriba la estructura
        buf = bytearray(12)
        fcntl.ioctl(fd, DDS_IOCTL_GET_STATE, buf, True)
        os.close(fd)

        # Desempaquetar 3 enteros con signo (little-endian)
        freq, wave, step = struct.unpack("iii", buf)
        nombres = {0: "SENO", 1: "TRIANGULAR", 2: "CUADRADA"}
        
        print("\n=== [IOCTL SYNC STATUS] ===")
        print(f"  Frecuencia : {freq} Hz")
        print(f"  Forma Onda : {nombres.get(wave, 'DESCONOCIDA')} ({wave})")
        print(f"  Paso       : {step} Hz")
        print("===========================\n")
    except Exception as e:
        print(f"[ERROR IOCTL] Falló la syscall: {e}")

def send_and_receive(cmd: str):
    full_cmd = f"{cmd.strip()}\r\n"
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except Exception as e:
        print(f"[ERROR] No se pudo abrir {DEVICE_PATH}: {e}")
        sys.exit(1)

    try:
        while True:
            try:
                chunk = os.read(fd, 256)
                if not chunk: break
            except (BlockingIOError, InterruptedError):
                break

        os.write(fd, full_cmd.encode('ascii'))

        t_start = time.time()
        rx_data = b""
        while (time.time() - t_start) < TIMEOUT:
            try:
                chunk = os.read(fd, 128)
                if chunk:
                    rx_data += chunk
                    if b"\n" in rx_data: break
            except (BlockingIOError, InterruptedError):
                pass
            time.sleep(0.01)

        if not rx_data:
            print(f"[TX: {cmd.strip()}] -> ❌ TIMEOUT")
            return

        resp = rx_data.decode('ascii', errors='replace').strip()
        if resp.startswith("OK:"):
            print(f"[TX: {cmd.strip()}] -> ✅ {resp}")
        elif resp.startswith("ERROR:"):
            print(f"[TX: {cmd.strip()}] -> ⚠️ {resp}")
        else:
            print(f"[TX: {cmd.strip()}] -> ℹ️ {resp}")
    finally:
        os.close(fd)

def monitor_telemetry():
    print(f"[*] Escuchando eventos físicos y NVS en {DEVICE_PATH} (Ctrl+C para salir)...")
    try:
        fd = os.open(DEVICE_PATH, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    except Exception as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    buffer = ""
    try:
        while True:
            try:
                chunk = os.read(fd, 128).decode('ascii', errors='replace')
                if chunk:
                    buffer += chunk
                    while "\n" in buffer:
                        line, buffer = buffer.split("\n", 1)
                        line = line.strip()
                        if line:
                            if "STATUS:FREQ=" in line:
                                print(f"[ENCODER GIRADO] -> Frecuencia: {line.split('=')[1]} Hz")
                            elif "STATUS:WAVE=" in line:
                                w = line.split("=")[1]
                                nombres = {"0": "SENO", "1": "TRIANGULAR", "2": "CUADRADA"}
                                print(f"[BOTÓN ONDA] -> Forma de onda: {nombres.get(w, w)}")
                            elif "STATUS:STEP=" in line:
                                print(f"[BOTÓN ENCODER] -> Paso: {line.split('=')[1]} Hz")
                            elif "STATUS:NVS_SAVED:" in line:
                                vals = line.split(":")[2].split(",")
                                print(f"💾 [NVS FLASH COMMIT] -> Guardado: F={vals[0]}Hz, W={vals[1]}, P={vals[2]}Hz")
                            else:
                                print(f"[ESP32] {line}")
            except (BlockingIOError, InterruptedError):
                pass
            time.sleep(0.02)
    except KeyboardInterrupt:
        print("\n[*] Monitoreo finalizado.")
    finally:
        os.close(fd)

def main():
    parser = argparse.ArgumentParser(description="CLI DDS con IOCTL y Telemetría")
    parser.add_argument("--set-freq", type=int, metavar="HZ")
    parser.add_argument("--set-wave", type=int, choices=[0, 1, 2], metavar="TIPO")
    parser.add_argument("--sync", action="store_true", help="Consulta de sincronización por IOCTL")
    parser.add_argument("--monitor", action="store_true", help="Escucha continua de telemetría")

    args = parser.parse_args()

    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(0)

    if args.sync:
        ioctl_sync_state()
    if args.set_freq is not None:
        send_and_receive(f"FREQ={args.set_freq}")
    if args.set_wave is not None:
        send_and_receive(f"WAVE={args.set_wave}")
    if args.monitor:
        monitor_telemetry()

if __name__ == "__main__":
    main()
