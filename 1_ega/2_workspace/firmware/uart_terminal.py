#!/usr/bin/env python3
"""Interactive UART terminal and simple GUI control for the ESP32 frequency-response analyzer."""

import argparse
import math
import os
import re
import time
import tkinter as tk
from tkinter import ttk, messagebox

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit(f"pyserial is required: pip install pyserial\n{exc}")

try:
    import matplotlib
    matplotlib.use("TkAgg")
    import matplotlib.pyplot as plt
except Exception:  # pragma: no cover
    plt = None

HISTORY_FILE = os.path.join(os.path.expanduser("~"), ".esp32_uart_history.txt")
MAX_DDS_FREQUENCY_HZ = 37_500_000


def parse_frequency_status(text):
    if not text:
        return None, None

    freq_value = None
    match = re.search(r"FREQ\s*[:=]\s*(\d+)", text, re.IGNORECASE)
    if match:
        freq_value = int(match.group(1))
    else:
        match = re.search(r"SETF\s*[:=]\s*(\d+)", text, re.IGNORECASE)
        if match:
            freq_value = int(match.group(1))

    wave_value = None
    match = re.search(r"MODE\s*[:=]\s*(SINE|TRI|TRIANGLE|SQUARE)", text, re.IGNORECASE)
    if match:
        wave_value = match.group(1).upper()
        if wave_value == "TRIANGLE":
            wave_value = "TRI"
    else:
        match = re.search(r"WAVE\s*[:=]\s*(SINE|TRI|TRIANGLE|SQUARE)", text, re.IGNORECASE)
        if match:
            wave_value = match.group(1).upper()
            if wave_value == "TRIANGLE":
                wave_value = "TRI"

    if wave_value is None:
        wave_value = "SINE"

    return freq_value, wave_value


def generate_waveform_points(freq_hz, wave="SINE", sample_count=200, amplitude=1.0):
    if freq_hz is None or freq_hz <= 0:
        freq_hz = 1

    points = []
    for i in range(sample_count):
        t = i / sample_count
        phase = 2.0 * math.pi * t

        if wave == "SQUARE":
            y = 1.0 if math.sin(phase) >= 0 else -1.0
        elif wave == "TRI":
            x = (phase / (2.0 * math.pi)) % 1.0
            y = 2.0 * abs(2.0 * x - 1.0) - 1.0
        else:
            y = math.sin(phase)

        points.append((t, amplitude * y))

    return points


def read_serial_lines(ser, timeout=1.0):
    end = time.time() + timeout
    chunks = []
    while time.time() < end:
        if ser.in_waiting:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="ignore").strip()
            if text:
                chunks.append(text)
                if text.startswith("OK:") or text.startswith("ERR:"):
                    break
        else:
            time.sleep(0.02)
    return chunks


def send_cmd(ser, cmd):
    if not cmd:
        return []
    ser.write((cmd + "\r\n").encode("ascii"))
    ser.flush()
    time.sleep(0.05)
    lines = read_serial_lines(ser, timeout=1.5)
    return lines


class UARTTerminalApp:
    def __init__(self, port, baud):
        self.root = tk.Tk()
        self.root.title(f"ESP32 UART Control - {port}")
        self.root.geometry("900x600")
        self.root.minsize(700, 500)

        self.port = port
        self.baud = baud
        self.ser = None
        self.history = self._load_history()
        self.history_index = 0
        self.current_frequency = 1000
        self.current_wave = "SINE"
        self.signal_label_var = tk.StringVar(value="DDS: 1000 Hz | SINE")
        self.last_manual_state_update = 0.0
        self._status_lock_seconds = 2.0

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._build_ui()
        self._connect_serial()

    def _load_history(self):
        if not os.path.exists(HISTORY_FILE):
            return []
        try:
            with open(HISTORY_FILE, "r", encoding="utf-8") as f:
                return [line.rstrip("\n") for line in f if line.strip()]
        except OSError:
            return []

    def _save_history(self):
        try:
            with open(HISTORY_FILE, "w", encoding="utf-8") as f:
                for item in self.history[-200:]:
                    f.write(item + "\n")
        except OSError:
            pass

    def _build_ui(self):
        self.root.geometry("1200x760")
        self.root.minsize(900, 600)

        top = ttk.Frame(self.root, padding=(10, 10, 10, 6))
        top.pack(fill="x")

        ttk.Label(top, text="Port:").grid(row=0, column=0, sticky="w")
        self.port_var = tk.StringVar(value=self.port)
        ttk.Entry(top, textvariable=self.port_var, width=18).grid(row=0, column=1, padx=(0, 10), sticky="ew")

        ttk.Label(top, text="Baud:").grid(row=0, column=2, sticky="w")
        self.baud_var = tk.StringVar(value=str(self.baud))
        ttk.Entry(top, textvariable=self.baud_var, width=12).grid(row=0, column=3, padx=(0, 10), sticky="ew")

        ttk.Button(top, text="Connect", command=self._connect_serial).grid(row=0, column=4, padx=(0, 8), sticky="ew")
        ttk.Button(top, text="Send", command=self._send_current_cmd).grid(row=0, column=5, padx=(0, 8), sticky="ew")
        ttk.Button(top, text="Clear", command=self._clear_output).grid(row=0, column=6, sticky="ew")

        for i in range(7):
            top.grid_columnconfigure(i, weight=0)

        viewport = ttk.Frame(self.root)
        viewport.pack(fill="both", expand=True, padx=10, pady=(0, 8))

        self.canvas = tk.Canvas(viewport, highlightthickness=0)
        self.canvas.pack(side="left", fill="both", expand=True)

        self.h_scroll = ttk.Scrollbar(viewport, orient="horizontal", command=self.canvas.xview)
        self.h_scroll.pack(side="bottom", fill="x")
        self.v_scroll = ttk.Scrollbar(viewport, orient="vertical", command=self.canvas.yview)
        self.v_scroll.pack(side="right", fill="y")

        self.canvas.configure(xscrollcommand=self.h_scroll.set, yscrollcommand=self.v_scroll.set)

        self.content = ttk.Frame(self.canvas)
        self.canvas.create_window((0, 0), window=self.content, anchor="nw")
        self.content.bind("<Configure>", self._on_content_configure)
        self.root.bind("<Configure>", self._on_window_resize)

        self._build_dashboard(self.content)

        command_bar = ttk.Frame(self.root, padding=(10, 0, 10, 10))
        command_bar.pack(fill="x")

        ttk.Label(command_bar, text="Command:").pack(side="left")
        self.command_var = tk.StringVar()
        self.command_entry = ttk.Entry(command_bar, textvariable=self.command_var)
        self.command_entry.pack(side="left", fill="x", expand=True, padx=(8, 8))
        self.command_entry.bind("<Return>", lambda event: self._send_current_cmd())
        self.command_entry.bind("<Up>", self._history_up)
        self.command_entry.bind("<Down>", self._history_down)

        self._add_output("ESP32 UART Control ready. Type HELP or use the GUI buttons.")

    def _on_content_configure(self, event):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_window_resize(self, event):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _build_dashboard(self, parent):
        main_row = ttk.Frame(parent)
        main_row.pack(fill="both", expand=True)

        console_panel = ttk.LabelFrame(main_row, text="Console", padding=6)
        console_panel.pack(side="left", fill="both", expand=True, padx=(0, 10))
        console_panel.configure(width=720)

        self.output = tk.Text(console_panel, wrap="word", state="disabled", bg="#111827", fg="#e5e7eb", font=("Consolas", 10), width=90, height=20)
        self.output.pack(fill="both", expand=True)

        sidebar = ttk.Frame(main_row)
        sidebar.pack(side="left", fill="y")

        self.status_var = tk.StringVar(value="Disconnected")
        status_frame = ttk.LabelFrame(sidebar, text="Status", padding=(8, 6))
        status_frame.pack(fill="x", pady=(0, 8))
        ttk.Label(status_frame, textvariable=self.status_var, foreground="#1d4ed8").pack(anchor="w")

        self._build_gui_controls(sidebar)

    def _build_gui_controls(self, parent):
        controls = ttk.LabelFrame(parent, text="Control", padding=10)
        controls.pack(fill="x", pady=(0, 8))

        row = ttk.Frame(controls)
        row.pack(fill="x")
        ttk.Button(row, text="PING", command=lambda: self._send_cmd("PING")).pack(side="left", padx=(0, 8))
        ttk.Button(row, text="STATUS", command=lambda: self._send_cmd("STATUS")).pack(side="left", padx=(0, 8))
        ttk.Button(row, text="ADC", command=lambda: self._send_cmd("ADC")).pack(side="left")

        freq_frame = ttk.Frame(controls)
        freq_frame.pack(fill="x", pady=(10, 0))
        ttk.Label(freq_frame, text="Frequency (Hz):").pack(side="left")
        self.freq_var = tk.StringVar(value="1000")
        ttk.Entry(freq_frame, textvariable=self.freq_var, width=12).pack(side="left", padx=(8, 8))
        ttk.Button(freq_frame, text="SETF", command=self._set_frequency).pack(side="left")

        wave_frame = ttk.Frame(controls)
        wave_frame.pack(fill="x", pady=(10, 0))
        ttk.Label(wave_frame, text="Waveform:").pack(side="left")
        self.wave_var = tk.StringVar(value="SINE")
        ttk.Combobox(wave_frame, textvariable=self.wave_var, values=["SINE", "TRI", "SQUARE"], state="readonly", width=12).pack(side="left", padx=(8, 8))
        ttk.Button(wave_frame, text="SETW", command=self._set_waveform).pack(side="left")

        sweep_frame = ttk.LabelFrame(parent, text="Sweep", padding=10)
        sweep_frame.pack(fill="x", pady=(0, 8))
        ttk.Label(sweep_frame, text="Start/Stop/Step/Dwell:").pack(anchor="w")
        self.sweep_start = tk.StringVar(value="1000")
        self.sweep_stop = tk.StringVar(value="200000")
        self.sweep_step = tk.StringVar(value="1000")
        self.sweep_dwell = tk.StringVar(value="50")
        entries = ttk.Frame(sweep_frame)
        entries.pack(fill="x", pady=(8, 0))
        ttk.Entry(entries, textvariable=self.sweep_start, width=8).pack(side="left", padx=(0, 4))
        ttk.Entry(entries, textvariable=self.sweep_stop, width=8).pack(side="left", padx=4)
        ttk.Entry(entries, textvariable=self.sweep_step, width=8).pack(side="left", padx=4)
        ttk.Entry(entries, textvariable=self.sweep_dwell, width=8).pack(side="left", padx=4)
        ttk.Button(sweep_frame, text="SWEEP", command=self._sweep).pack(fill="x", pady=(8, 0))

        signal_frame = ttk.LabelFrame(parent, text="DDS signal", padding=10)
        signal_frame.pack(fill="x", pady=(0, 8))
        ttk.Label(signal_frame, textvariable=self.signal_label_var, font=("TkDefaultFont", 10, "bold")).pack(anchor="w")
        self.signal_canvas = tk.Canvas(signal_frame, bg="#f8fafc", height=180, width=320)
        self.signal_canvas.pack(fill="both", expand=True)
        self._draw_signal_monitor()

        plot_frame = ttk.LabelFrame(parent, text="Plot", padding=10)
        plot_frame.pack(fill="both", expand=True)
        self.plot_canvas = tk.Canvas(plot_frame, bg="white", height=220, width=320)
        self.plot_canvas.pack(fill="both", expand=True)

    def _add_output(self, msg):
        self.output.configure(state="normal")
        self.output.insert(tk.END, msg + "\n")
        self.output.see(tk.END)
        self.output.configure(state="disabled")
        self._update_signal_monitor(msg)

    def _on_close(self):
        if self.ser is not None and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass
        self.root.destroy()

    def _update_signal_monitor(self, text):
        if not text:
            return

        if text.startswith("> "):
            return

        if text.startswith("OK:") or text.startswith("ERR:"):
            if (time.monotonic() - self.last_manual_state_update) < self._status_lock_seconds:
                return

        freq, wave = parse_frequency_status(text)
        if freq is not None and freq != 1000:
            self.current_frequency = freq
        elif freq is not None and freq == 1000 and self.current_frequency != 1000:
            return
        if wave is not None and wave != "SINE":
            self.current_wave = wave
        elif wave is not None and wave == "SINE" and self.current_wave not in {"TRI", "SQUARE"}:
            self.current_wave = wave

        self.signal_label_var.set(f"DDS: {self.current_frequency} Hz | {self.current_wave}")
        if hasattr(self, "signal_canvas"):
            self._draw_signal_monitor()

    def _draw_signal_monitor(self):
        if not hasattr(self, "signal_canvas"):
            return

        self.signal_canvas.delete("all")
        w = self.signal_canvas.winfo_width() or 640
        h = self.signal_canvas.winfo_height() or 180

        left_pad, right_pad, top_pad, bottom_pad = 20, 18, 12, 20
        plot_w = max(1, w - left_pad - right_pad)
        plot_h = max(1, h - top_pad - bottom_pad)

        self.signal_canvas.create_line(left_pad, top_pad, left_pad, h - bottom_pad, fill="#374151")
        self.signal_canvas.create_line(left_pad, h - bottom_pad, w - right_pad, h - bottom_pad, fill="#374151")

        for i in range(5):
            y = top_pad + (plot_h * i / 4.0)
            self.signal_canvas.create_line(left_pad, y, w - right_pad, y, fill="#e5e7eb")

        points = generate_waveform_points(self.current_frequency, self.current_wave, sample_count=200)
        plot_points = []
        for t, value in points:
            x = left_pad + t * plot_w
            y = h - bottom_pad - ((value + 1.0) / 2.0) * plot_h
            plot_points.append((x, y))

        for i in range(1, len(plot_points)):
            x1, y1 = plot_points[i - 1]
            x2, y2 = plot_points[i]
            self.signal_canvas.create_line(x1, y1, x2, y2, fill="#2563eb", width=2)

    def _connect_serial(self):
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("Port missing", "Please enter a valid serial port.")
            return

        if self.ser is not None and self.ser.is_open:
            self.status_var.set(f"Already connected to {port}")
            self._add_output(f"INFO: already connected to {port}")
            return

        baud = self.baud_var.get()
        try:
            baud_val = int(baud)
        except ValueError:
            messagebox.showerror("Bad baud", "Baud must be an integer.")
            return

        try:
            self.ser = serial.Serial(port, baud_val, timeout=0.1)
            self.status_var.set(f"Connected to {port} @ {baud_val}")
            self._add_output(f"Connected to {port} @ {baud_val}")
            self._send_cmd("STATUS")
        except serial.SerialException as exc:
            if self.ser is not None:
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
            self.status_var.set("Disconnected")
            self._add_output(f"Connection error: could not open port '{port}': {exc}")

    def _send_cmd(self, cmd):
        if self.ser is None or not self.ser.is_open:
            self._add_output("ERR: serial port is not open")
            return
        self._add_output(f"> {cmd}")
        self.history = [h for h in self.history if h != cmd]
        self.history.append(cmd)
        self._save_history()
        self.history_index = len(self.history)
        lines = send_cmd(self.ser, cmd)
        for line in lines:
            self._add_output(line)

    def _send_current_cmd(self):
        cmd = self.command_var.get().strip()
        if not cmd:
            return
        self.command_var.set("")
        self._send_cmd(cmd)

    def _history_up(self, event):
        if not self.history:
            return "break"
        if self.history_index > 0:
            self.history_index -= 1
        self.command_var.set(self.history[self.history_index])
        return "break"

    def _history_down(self, event):
        if not self.history:
            return "break"
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            self.command_var.set(self.history[self.history_index])
        else:
            self.command_var.set("")
            self.history_index = len(self.history)
        return "break"

    def _clear_output(self):
        self.output.configure(state="normal")
        self.output.delete("1.0", tk.END)
        self.output.configure(state="disabled")

    def _draw_plot(self, points):
        self.plot_canvas.delete("all")
        if not points:
            self.plot_canvas.create_text(10, 10, anchor="nw", text="No data yet", fill="#3b82f6")
            return

        w = self.plot_canvas.winfo_width() or 600
        h = self.plot_canvas.winfo_height() or 220

        left_pad, right_pad, top_pad, bottom_pad = 30, 20, 20, 30
        plot_w = max(1, w - left_pad - right_pad)
        plot_h = max(1, h - top_pad - bottom_pad)

        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        x_min, x_max = min(xs), max(xs)
        y_min, y_max = min(ys), max(ys)
        if x_min == x_max:
            x_min -= 1
            x_max += 1
        if y_min == y_max:
            y_min -= 1
            y_max += 1

        self.plot_canvas.create_line(left_pad, top_pad, left_pad, h - bottom_pad, fill="#374151")
        self.plot_canvas.create_line(left_pad, h - bottom_pad, w - right_pad, h - bottom_pad, fill="#374151")

        for i in range(5):
            py = top_pad + (plot_h * i / 4)
            self.plot_canvas.create_line(left_pad, py, w - right_pad, py, fill="#e5e7eb")

        plot_points = []
        for freq, adc in points:
            px = left_pad + ((freq - x_min) / (x_max - x_min + 1e-9)) * plot_w
            py = h - bottom_pad - ((adc - y_min) / (y_max - y_min + 1e-9)) * plot_h
            plot_points.append((px, py))

        for i in range(1, len(plot_points)):
            x1, y1 = plot_points[i - 1]
            x2, y2 = plot_points[i]
            self.plot_canvas.create_line(x1, y1, x2, y2, fill="#22c55e", width=2)

        for x, y in plot_points[:1]:
            self.plot_canvas.create_oval(x - 3, y - 3, x + 3, y + 3, fill="#22c55e")
        for x, y in plot_points[-1:]:
            self.plot_canvas.create_oval(x - 3, y - 3, x + 3, y + 3, fill="#22c55e")

    def _consume_sweep_points(self):
        points = []
        if self.ser is None or not self.ser.is_open:
            return points

        deadline = time.time() + 8.0
        while time.time() < deadline:
            if self.ser.in_waiting:
                raw = self.ser.readline()
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="ignore").strip()
                if not text:
                    continue
                self._add_output(text)
                match = re.search(r"f\s*=\s*(\d+)\s*Hz\s*\|\s*ADC\s*=\s*(\d+)", text, re.IGNORECASE)
                if match:
                    points.append((int(match.group(1)), int(match.group(2))))
            else:
                time.sleep(0.05)
        self._draw_plot(points)
        return points

    def _set_frequency(self):
        freq = self.freq_var.get().strip()
        if freq:
            try:
                requested_frequency = int(freq)
                if not 1 <= requested_frequency <= MAX_DDS_FREQUENCY_HZ:
                    self.signal_label_var.set(
                        f"Frequency must be 1..{MAX_DDS_FREQUENCY_HZ} Hz"
                    )
                    return
                self.current_frequency = requested_frequency
                self.last_manual_state_update = time.monotonic()
                self.signal_label_var.set(f"DDS: {self.current_frequency} Hz | {self.current_wave}")
                if hasattr(self, "signal_canvas"):
                    self._draw_signal_monitor()
            except ValueError:
                return
            self._send_cmd(f"SETF {freq}")

    def _set_waveform(self):
        wave = self.wave_var.get().strip().upper()
        self.current_wave = wave if wave in {"SINE", "TRI", "SQUARE"} else "SINE"
        self.last_manual_state_update = time.monotonic()
        self.signal_label_var.set(f"DDS: {self.current_frequency} Hz | {self.current_wave}")
        if hasattr(self, "signal_canvas"):
            self._draw_signal_monitor()
        self._send_cmd(f"SETW {wave}")

    def _sweep(self):
        cmd = (
            f"SWEEP {self.sweep_start.get()} {self.sweep_stop.get()} "
            f"{self.sweep_step.get()} {self.sweep_dwell.get()}"
        )
        self._send_cmd(cmd)
        self._consume_sweep_points()

    def run(self):
        self.root.mainloop()


def cli_main():
    parser = argparse.ArgumentParser(description="ESP32 UART terminal and GUI")
    parser.add_argument("--port", default="COM5", help="Serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate")
    parser.add_argument("--gui", action="store_true", help="Launch the Tkinter GUI")
    args = parser.parse_args()

    if args.gui:
        app = UARTTerminalApp(args.port, args.baud)
        app.run()
        return

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as exc:
        raise SystemExit(f"Could not open {args.port}: {exc}")

    print(f"Connected to {args.port} at {args.baud} baud")
    print("Type 'HELP', 'STATUS', 'SETF 1000', 'SETW SINE', 'ADC', 'SWEEP 1000 200000 1000 50', or 'exit'")
    history = []
    while True:
        try:
            cmd = input("ESP32> ").strip()
        except KeyboardInterrupt:
            print("\nExiting")
            break
        except EOFError:
            print("\nExiting")
            break

        if not cmd:
            continue
        if cmd.lower() in {"exit", "quit"}:
            break

        history.append(cmd)
        if len(history) > 200:
            history = history[-200:]
        try:
            with open(HISTORY_FILE, "w", encoding="utf-8") as f:
                for item in history:
                    f.write(item + "\n")
        except OSError:
            pass

        lines = send_cmd(ser, cmd)
        for line in lines:
            print(f"ESP32> {line}")

    ser.close()
    print("Serial port closed.")


if __name__ == "__main__":
    cli_main()
