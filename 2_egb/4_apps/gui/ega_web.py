#!/usr/bin/env python3
"""Interfaz web sin dependencias externas para controlar la EGA por char device."""

import argparse
import json
import math
import os
import select
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


HTML = r"""<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fuente partida EGA</title><style>
:root{color-scheme:dark;--bg:#08111f;--panel:#111e31;--line:#263a55;--text:#eaf2ff;--muted:#91a4bd;--cyan:#42d3ff;--green:#48dc91;--red:#ff6b75;--yellow:#ffd166}
*{box-sizing:border-box}body{margin:0;min-height:100vh;font:16px system-ui,sans-serif;color:var(--text);background:radial-gradient(circle at 50% -20%,#18385b,var(--bg) 55%)}
main{width:min(1050px,94vw);margin:auto;padding:32px 0 48px}header{display:flex;justify-content:space-between;align-items:end;gap:20px;margin-bottom:24px}
h1{margin:0;font-size:clamp(1.8rem,5vw,3rem);letter-spacing:-.04em}h1 span{color:var(--cyan)}.connection{color:var(--muted)}
.dot{display:inline-block;width:10px;height:10px;margin-right:7px;border-radius:50%;background:var(--yellow);box-shadow:0 0 14px currentColor}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:18px}.card{padding:22px;border:1px solid var(--line);border-radius:18px;background:linear-gradient(145deg,rgba(24,42,65,.96),rgba(12,24,40,.96));box-shadow:0 18px 45px rgba(0,0,0,.2)}
.wide{grid-column:1/-1}.branch-head{display:flex;justify-content:space-between;align-items:center}h2{margin:0;font-size:1.15rem;color:var(--muted);font-weight:600}
.badge{padding:5px 10px;border-radius:999px;font-weight:700;font-size:.78rem;background:#243246;color:var(--muted)}.badge.on{background:rgba(72,220,145,.15);color:var(--green)}.badge.fault{background:rgba(255,107,117,.16);color:var(--red)}
.branch-status{margin-top:14px;padding:10px 12px;border:1px solid var(--line);border-radius:10px;color:var(--muted);background:rgba(4,12,23,.28)}.branch-status.ok{color:var(--green);border-color:rgba(72,220,145,.4)}.branch-status.fault{color:var(--red);border-color:rgba(255,107,117,.55);font-weight:700}
.setpoint{display:grid;grid-template-columns:auto minmax(220px,1fr);align-items:end;gap:14px;margin:18px 0 12px;padding:14px;border:1px solid var(--line);border-radius:12px;background:rgba(4,12,23,.38)}.setpoint small{display:block;color:var(--muted);margin-bottom:5px}.setpoint strong{font-size:1.65rem;font-variant-numeric:tabular-nums}
.metrics{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin:12px 0 22px}.metric{padding:14px;border-radius:12px;background:rgba(4,12,23,.48)}
.metric small{display:block;color:var(--muted);margin-bottom:5px}.metric strong{font-size:1.65rem;font-variant-numeric:tabular-nums}.controls{display:grid;grid-template-columns:1fr auto;gap:10px}
.details{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin:-8px 0 18px}.detail{padding:10px;border:1px solid var(--line);border-radius:10px;background:rgba(4,12,23,.28)}
.detail small{display:block;color:var(--muted);font-size:.72rem;margin-bottom:8px}.detail strong{font-size:.95rem;font-variant-numeric:tabular-nums}.limit-value{display:grid;grid-template-columns:34px 1fr 34px;align-items:center;gap:6px}.limit-value strong{text-align:center}.limit-step{width:34px;height:34px;padding:0;font-size:1.15rem;line-height:1}
input,button{border:1px solid var(--line);border-radius:10px;padding:11px 13px;color:var(--text);background:#0b1728;font:inherit}button{cursor:pointer;font-weight:700;transition:.15s transform,.15s border-color}button:hover{transform:translateY(-1px);border-color:var(--cyan)}
.helper{min-height:1.2em;margin:8px 0 0;color:var(--yellow);font-size:.78rem}
input:disabled,button:disabled{cursor:not-allowed;opacity:.48}button:disabled:hover{transform:none;border-color:var(--line)}.reset-button{color:var(--yellow)}
.buttons{display:flex;gap:9px;margin-top:10px}.buttons button{flex:1}.on-button{color:var(--green)}.off-button{color:var(--red)}.status-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px}
.status-grid div{background:rgba(4,12,23,.48);border-radius:12px;padding:13px}.status-grid small{display:block;color:var(--muted)}#message{min-height:24px;margin:18px 2px 0;color:var(--muted)}#message.error{color:var(--red)}
@media(max-width:900px){.grid{grid-template-columns:1fr}.wide{grid-column:auto}header{align-items:start;flex-direction:column}}@media(max-width:520px){.setpoint{grid-template-columns:1fr}.details{grid-template-columns:1fr}}
</style></head><body><main>
<header><div><h1>Fuente partida <span>EGA</span></h1><div class="connection"><i class="dot" id="dot"></i><span id="connection">Conectando...</span></div></div><button onclick="sendCommand('SAVE')">Guardar configuración</button></header>
<section class="grid"><article class="card"><div class="branch-head"><h2>Salida positiva</h2><span class="badge" id="ep">OFF</span></div>
<div class="branch-status" id="status-p" role="status">Estado: esperando telemetría</div>
<div class="setpoint"><div><small>Tensión configurada</small><strong><span id="sp">--</span> V</strong></div><div class="controls"><input id="sp-input" aria-label="Nueva tensión positiva" type="number" min="0" max="30" step="0.5" placeholder="Nueva tensión 0-30 V"><button onclick="setVoltage('P')">Aplicar</button></div></div>
<div class="metrics"><div class="metric"><small>Tensión actual</small><strong><span id="vp">--</span> V</strong></div><div class="metric"><small>Corriente actual</small><strong><span id="ip">--</span> A</strong></div></div>
<div class="details"><div class="detail"><small>Límite de corriente</small><div class="limit-value"><button class="limit-step" id="ilp-minus" aria-label="Disminuir límite de corriente positivo" onclick="adjustLimit('ILP',-0.05)" disabled>−</button><strong><span id="ilp">--</span> A</strong><button class="limit-step" id="ilp-plus" aria-label="Aumentar límite de corriente positivo" onclick="adjustLimit('ILP',0.05)" disabled>+</button></div></div><div class="detail"><small>Límite de sobretensión</small><div class="limit-value"><button class="limit-step" id="vlp-minus" aria-label="Disminuir límite de sobretensión positivo" onclick="adjustLimit('VLP',-0.5)" disabled>−</button><strong><span id="vlp">--</span> V</strong><button class="limit-step" id="vlp-plus" aria-label="Aumentar límite de sobretensión positivo" onclick="adjustLimit('VLP',0.5)" disabled>+</button></div></div></div><p class="helper" id="limits-p">La edición requiere el firmware UART extendido.</p>
<div class="buttons"><button class="on-button" onclick="sendCommand('OUT P ON')">Encender</button><button class="off-button" onclick="sendCommand('OUT P OFF')">Apagar</button><button class="reset-button" id="reset-p" onclick="sendCommand('RESET P')" disabled>Restablecer</button></div></article>
<article class="card"><div class="branch-head"><h2>Salida negativa</h2><span class="badge" id="en">OFF</span></div>
<div class="branch-status" id="status-n" role="status">Estado: esperando telemetría</div>
<div class="setpoint"><div><small>Tensión configurada</small><strong><span id="sn">--</span> V</strong></div><div class="controls"><input id="sn-input" aria-label="Nueva magnitud de tensión negativa" type="number" min="0" max="30" step="0.5" placeholder="Nueva magnitud 0-30 V"><button onclick="setVoltage('N')">Aplicar</button></div></div>
<div class="metrics"><div class="metric"><small>Tensión actual</small><strong><span id="vn">--</span> V</strong></div><div class="metric"><small>Corriente actual</small><strong><span id="in">--</span> A</strong></div></div>
<div class="details"><div class="detail"><small>Límite de corriente</small><div class="limit-value"><button class="limit-step" id="iln-minus" aria-label="Disminuir límite de corriente negativo" onclick="adjustLimit('ILN',-0.05)" disabled>−</button><strong><span id="iln">--</span> A</strong><button class="limit-step" id="iln-plus" aria-label="Aumentar límite de corriente negativo" onclick="adjustLimit('ILN',0.05)" disabled>+</button></div></div><div class="detail"><small>Límite de sobretensión</small><div class="limit-value"><button class="limit-step" id="vln-minus" aria-label="Disminuir límite de sobretensión negativo" onclick="adjustLimit('VLN',-0.5)" disabled>−</button><strong><span id="vln">--</span> V</strong><button class="limit-step" id="vln-plus" aria-label="Aumentar límite de sobretensión negativo" onclick="adjustLimit('VLN',0.5)" disabled>+</button></div></div></div><p class="helper" id="limits-n">La edición requiere el firmware UART extendido.</p>
<div class="buttons"><button class="on-button" onclick="sendCommand('OUT N ON')">Encender</button><button class="off-button" onclick="sendCommand('OUT N OFF')">Apagar</button><button class="reset-button" id="reset-n" onclick="sendCommand('RESET N')" disabled>Restablecer</button></div></article>
<article class="card wide"><div class="status-grid"><div><small>Entrada</small><strong><span id="vi">--</span> V</strong></div><div><small>Protecciones</small><strong id="faults">Sin datos</strong></div></div></article></section><p id="message"></p>
</main><script>
let busy=false;const $=id=>document.getElementById(id);function showMessage(text,error=false){$('message').textContent=text;$('message').className=error?'error':''}function value(id,data,key){$(id).textContent=data[key]??'--'}
function renderBranch(branch,data,extended){const outputKey=branch==='P'?'EP':'EN';const ocKey=branch==='P'?'OCP':'OCN';const ovKey=branch==='P'?'OVP':'OVN';const name=branch==='P'?'Positiva':'Negativa';const faults=[];if(data[ocKey]==='1')faults.push('Sobrecorriente (OC)');if(data[ovKey]==='1')faults.push('Sobretensión (OV)');const badge=$(outputKey.toLowerCase());const status=$(`status-${branch.toLowerCase()}`);const on=data[outputKey]==='1';badge.textContent=faults.length?faults.map(item=>item.match(/\((.+)\)/)[1]).join(' + '):(on?'ON':'OFF');badge.className='badge '+(faults.length?'fault':on?'on':'');status.textContent=faults.length?`Protección activa: ${faults.join(' y ')}`:on?'Estado: salida habilitada':'Estado: salida apagada, sin fallas';status.className='branch-status '+(faults.length?'fault':'ok');$(`reset-${branch.toLowerCase()}`).disabled=!extended||faults.length===0;return faults.map(fault=>`${name}: ${fault}`)}
function render(data){['vi','vp','vn','ip','in','sp','sn','ilp','iln','vlp','vln'].forEach(id=>value(id,data,id.toUpperCase()));const extended=['VLP','VLN'].every(key=>Object.hasOwn(data,key));const faults=[...renderBranch('P',data,extended),...renderBranch('N',data,extended)];for(const key of ['ILP','ILN','VLP','VLN']){const branch=key.endsWith('P')?'P':'N';const disabled=!extended||data[`E${branch}`]==='1';$(`${key.toLowerCase()}-minus`).disabled=disabled;$(`${key.toLowerCase()}-plus`).disabled=disabled}for(const branch of ['P','N']){$(`limits-${branch.toLowerCase()}`).textContent=!extended?'La edición requiere el firmware UART extendido.':data[`E${branch}`]==='1'?'Apagá la salida para modificar sus límites.':'Ajustá el límite con − y +.'}$('faults').textContent=faults.length?faults.join(' - '):'Sin fallas';$('faults').style.color=faults.length?'var(--red)':'var(--green)';$('dot').style.background='var(--green)';$('connection').textContent='EGA en línea'}
async function request(command,quiet=false){const response=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command})});const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||result.response||'Error');if(result.data)render(result.data);if(!quiet)showMessage(result.response);return result}
async function refresh(){if(busy)return;busy=true;try{await request('GET',true)}catch(error){$('dot').style.background='var(--red)';$('connection').textContent='Sin respuesta';showMessage(error.message,true)}finally{busy=false}}
async function sendCommand(command){busy=true;try{await request(command);await request('GET',true)}catch(error){showMessage(error.message,true)}finally{busy=false}}function setVoltage(branch){const input=$(branch==='P'?'sp-input':'sn-input');sendCommand(`SET ${branch} ${input.value}`)}function adjustLimit(key,delta){const current=Number.parseFloat($(key.toLowerCase()).textContent);if(!Number.isFinite(current)){showMessage('Esperando el valor actual del límite',true);return}const currentLimit=key.startsWith('IL');const minimum=currentLimit?0.10:5.0;const maximum=currentLimit?0.75:40.0;const decimals=currentLimit?2:1;const next=Math.min(maximum,Math.max(minimum,current+delta));if(Math.abs(next-current)<0.001){showMessage('El límite ya está en el extremo permitido');return}sendCommand(`SET ${key} ${next.toFixed(decimals)}`)}refresh();setInterval(refresh,1000);
</script></body></html>"""


def validate_command(command: str) -> bool:
    parts = command.split()
    if parts in (["GET"], ["SAVE"]):
        return True
    if len(parts) == 2 and parts[0] == "RESET":
        return parts[1] in ("P", "N")
    if len(parts) == 3 and parts[0] == "OUT":
        return parts[1] in ("P", "N") and parts[2] in ("ON", "OFF")
    if len(parts) != 3 or parts[0] != "SET":
        return False
    try:
        value = float(parts[2])
    except ValueError:
        return False
    if not math.isfinite(value):
        return False
    ranges = {
        "P": (0.0, 30.0),
        "N": (0.0, 30.0),
        "ILP": (0.10, 0.75),
        "ILN": (0.10, 0.75),
        "VLP": (5.0, 40.0),
        "VLN": (5.0, 40.0),
    }
    limits = ranges.get(parts[1])
    if limits is None or not limits[0] <= value <= limits[1]:
        return False
    if parts[1] in ("ILP", "ILN"):
        steps = round((value - 0.10) / 0.05)
        return math.isclose(value, 0.10 + steps * 0.05, abs_tol=1e-6)
    return True


def translate_command(command: str) -> str:
    """Translate the stable P/N user syntax to the final Pico 0/1 protocol."""
    command = " ".join(command.upper().strip().split())
    if not validate_command(command):
        raise ValueError("Comando no permitido")
    parts = command.split()
    if len(parts) == 1:
        return command
    branch = "0" if parts[1].endswith("P") or parts[1] == "P" else "1"
    if parts[0] == "RESET":
        return f"RST {branch}"
    if parts[0] == "OUT":
        return f"OUT {branch} {parts[2]}"
    setting = parts[1]
    if setting in ("P", "N"):
        wire_command = "V"
    elif setting.startswith("IL"):
        wire_command = "IL"
    else:
        wire_command = "VL"
    return f"{wire_command} {branch} {parts[2]}"


def select_command_response(lines) -> tuple[str, list[str]]:
    notifications = []
    for line in lines:
        if line.startswith("PROT "):
            notifications.append(line)
            continue
        if not line.startswith("OK"):
            raise RuntimeError(line)
        return line, notifications
    raise TimeoutError("La EGA no respondió dentro del timeout")


class EgaDevice:
    def __init__(self, path: str, timeout: float = 1.5, transport=None):
        self.path = path
        self.timeout = timeout
        self.transport = transport
        self.lock = threading.Lock()
        self.last_notifications = []

    def close(self):
        pass

    @staticmethod
    def _drain(fd: int):
        while True:
            try:
                if not os.read(fd, 512):
                    return
            except BlockingIOError:
                return

    def command(self, command: str) -> str:
        command = " ".join(command.upper().strip().split())
        wire_command = translate_command(command)
        if self.transport is not None:
            self.last_notifications = []
            response, self.last_notifications = select_command_response(
                self.transport(wire_command)
            )
            if response.startswith("OK QUEUED "):
                return "OK QUEUED " + command
            return response
        with self.lock:
            self.last_notifications = []
            fd = os.open(self.path, os.O_RDWR | os.O_NONBLOCK)
            try:
                self._drain(fd)
                payload = (wire_command + "\n").encode("ascii")
                sent = 0
                while sent < len(payload):
                    sent += os.write(fd, payload[sent:])

                poller = select.poll()
                poller.register(fd, select.POLLIN | select.POLLERR | select.POLLHUP)
                deadline = time.monotonic() + self.timeout
                response = bytearray()
                while time.monotonic() < deadline:
                    remaining = max(1, int((deadline - time.monotonic()) * 1000))
                    events = poller.poll(remaining)
                    if not events:
                        break
                    if events[0][1] & (select.POLLERR | select.POLLHUP):
                        raise OSError("El char device dejó de estar disponible")
                    chunk = os.read(fd, 256)
                    for byte in chunk:
                        if byte == 10:
                            text = response.decode("ascii", errors="replace").rstrip("\r")
                            response.clear()
                            if text.startswith("PROT "):
                                self.last_notifications.append(text)
                                continue
                            selected, _ = select_command_response([text])
                            if selected.startswith("OK QUEUED "):
                                return "OK QUEUED " + command
                            return selected
                        response.append(byte)
                raise TimeoutError("La EGA no respondió dentro del timeout")
            finally:
                os.close(fd)


def parse_status(response: str) -> dict[str, str]:
    return dict(item.split("=", 1) for item in response.split()[1:] if "=" in item)


class RequestHandler(BaseHTTPRequestHandler):
    device: EgaDevice

    def send_json(self, status: int, payload: dict):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path != "/":
            self.send_error(404)
            return
        body = HTML.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/api/command":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 512:
                raise ValueError("Solicitud inválida")
            request = json.loads(self.rfile.read(length))
            response = self.device.command(str(request.get("command", "")))
            payload = {"ok": True, "response": response}
            if self.device.last_notifications:
                payload["notifications"] = list(self.device.last_notifications)
            if response.startswith("OK VI="):
                payload["data"] = parse_status(response)
            self.send_json(200, payload)
        except (ValueError, RuntimeError, TimeoutError, OSError) as error:
            payload = {"ok": False, "error": str(error)}
            if self.device.last_notifications:
                payload["notifications"] = list(self.device.last_notifications)
            self.send_json(400, payload)

    def log_message(self, message, *args):
        print(f"{self.address_string()} - {message % args}")


def main():
    parser = argparse.ArgumentParser(description="Panel web para la fuente EGA")
    parser.add_argument("--device", default="/dev/ega_uart")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--timeout", type=float, default=1.5)
    args = parser.parse_args()

    device = EgaDevice(args.device, args.timeout)
    RequestHandler.device = device
    server = ThreadingHTTPServer((args.host, args.port), RequestHandler)
    print(f"Panel EGA disponible en http://{args.host}:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nCerrando panel EGA")
    finally:
        server.server_close()
        device.close()


if __name__ == "__main__":
    main()
