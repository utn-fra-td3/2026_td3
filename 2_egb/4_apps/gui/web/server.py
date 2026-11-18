from flask import Flask, request, jsonify, send_from_directory
import os
import select
import threading
import time

DEVICE = "/dev/egalink"
TIMEOUT = 2.0

app = Flask(__name__)

lock = threading.Lock()

fd = os.open(
    DEVICE,
    os.O_RDWR | os.O_NONBLOCK
)


def clear_rx():
    """Descarta bytes viejos que pudieran haber quedado en el FIFO."""
    while True:
        try:
            data = os.read(fd, 512)

            if not data:
                break

        except BlockingIOError:
            break


def ega_command(command):
    with lock:
        clear_rx()

        message = (command.strip() + "\n").encode()

        os.write(fd, message)

        poller = select.poll()
        poller.register(fd, select.POLLIN)

        deadline = time.monotonic() + TIMEOUT

        response = bytearray()

        while time.monotonic() < deadline:
            remaining_ms = int(
                (deadline - time.monotonic()) * 1000
            )

            events = poller.poll(max(1, remaining_ms))

            if not events:
                break

            try:
                data = os.read(fd, 512)
            except BlockingIOError:
                continue

            response.extend(data)

            if b"\n" in response:
                line = response.split(b"\n", 1)[0]
                return line.decode(
                    errors="replace"
                ).rstrip("\r")

        raise TimeoutError(
            "La EGA no respondió dentro del tiempo esperado"
        )

@app.route("/")
def index():
    return send_from_directory(".", "index.html")

@app.route("/api/command", methods=["POST"])
def command():
    data = request.get_json(silent=True)

    if not data or "command" not in data:
        return jsonify({
            "ok": False,
            "error": "Falta el campo command"
        }), 400

    command = str(data["command"]).strip()

    if not command:
        return jsonify({
            "ok": False,
            "error": "Comando vacío"
        }), 400

    try:
        response = ega_command(command)

        return jsonify({
            "ok": True,
            "response": response
        })

    except TimeoutError as e:
        return jsonify({
            "ok": False,
            "error": str(e)
        }), 504

    except Exception as e:
        return jsonify({
            "ok": False,
            "error": str(e)
        }), 500


if __name__ == "__main__":
    print("Servidor EGB iniciado")
    print("Dispositivo:", DEVICE)

    app.run(
        host="0.0.0.0",
        port=8080,
        debug=False,
        use_reloader=False
    )
