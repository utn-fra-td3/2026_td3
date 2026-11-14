#!/usr/bin/env python3
"""Interactive UART terminal and simple GUI control for the ESP32 frequency-response analyzer."""

import argparse
import csv
import math
import os
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

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
MEASUREMENT_FIELDS = (
    "F", "SEQ", "T_US", "SOURCE", "MAG_RAW", "PHASE_RAW", "MAG_MV", "PHASE_MV",
    "MAG_SPAN", "PHASE_SPAN", "FLAGS",
)


def parse_measurement_line(text):
    if not text.startswith("DATA:"):
        return None
    columns = next(csv.reader([text], strict=True))
    if columns[0] != "DATA:V1":
        raise ValueError("unsupported measurement protocol")
    sample = {}
    for column in columns[1:]:
        key, separator, value = column.partition("=")
        if not separator or key not in MEASUREMENT_FIELDS or key in sample:
            raise ValueError("invalid or duplicate measurement field")
        sample[key] = value if key == "SOURCE" else int(value)
    if set(sample) != set(MEASUREMENT_FIELDS):
        raise ValueError("incomplete measurement")
    if sample["SOURCE"] not in {"SIM", "ADC"}:
        raise ValueError("unknown measurement source")
    if not 1 <= sample["F"] <= MAX_DDS_FREQUENCY_HZ or sample["SEQ"] < 0 or sample["T_US"] < 0:
        raise ValueError("invalid measurement frequency or sequence")
    if not 0 <= sample["FLAGS"] <= 15:
        raise ValueError("unknown measurement flags")
    simulated = sample["SOURCE"] == "SIM"
    if bool(sample["FLAGS"] & 1) != simulated:
        raise ValueError("measurement source/flags mismatch")
    for key in ("MAG_RAW", "PHASE_RAW", "MAG_SPAN", "PHASE_SPAN"):
        if (simulated and sample[key] != -1) or (not simulated and not 0 <= sample[key] <= 4095):
            raise ValueError("invalid raw measurement")
    for key in ("MAG_MV", "PHASE_MV"):
        if sample["FLAGS"] & 2:
            if sample[key] != -1:
                raise ValueError("uncalibrated ADC must not report millivolts")
        elif not 0 <= sample[key] <= 3300:
            raise ValueError("invalid measurement voltage")
    sample["GAIN_DB"] = None if sample["MAG_MV"] < 0 else (sample["MAG_MV"] - 900.0) / 30.0
    sample["PHASE_FOLDED_DEG"] = None if sample["PHASE_MV"] < 0 else (1800.0 - sample["PHASE_MV"]) / 10.0
    sample["CALIBRATION"] = "NOMINAL_DETECTOR"
    return sample


def export_measurements(path, samples):
    fields = (*MEASUREMENT_FIELDS, "GAIN_DB", "PHASE_FOLDED_DEG", "CALIBRATION")
    with open(path, "w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(samples)


def sweep_plan(cmd):
    tokens = cmd.split()
    command = tokens[0].upper() if tokens else ""
    if command not in {"SWEEP", "MSWEEP", "MSWEEP_POINTS"}:
        raise ValueError("unknown sweep command")
    offset = 1 if command == "SWEEP" else 2
    if len(tokens) != offset + 4 or (offset == 2 and tokens[1] not in {"SIM", "ADC"}):
        raise ValueError("invalid sweep arguments")
    values = tokens[offset:]
    if any(not token.isascii() or not token.isdecimal() for token in values):
        raise ValueError("sweep arguments must be unsigned integers")
    start, stop, spacing, timing = map(int, values)
    if not 1 <= start <= stop <= MAX_DDS_FREQUENCY_HZ:
        raise ValueError("invalid sweep range")
    if command == "MSWEEP_POINTS":
        if not (1 <= spacing <= min(10000, stop - start + 1) and
                (spacing != 1 or start == stop) and spacing * 10 <= timing <= 3600000):
            raise ValueError("invalid point count or sweep duration")
        frequencies = [start + (stop - start) * index // (spacing - 1) for index in range(spacing)] if spacing > 1 else [start]
        return frequencies, timing, timing / spacing
    if not (1 <= spacing <= MAX_DDS_FREQUENCY_HZ and 1 <= timing <= 60000):
        raise ValueError("invalid sweep step or dwell")
    frequencies = range(start, stop + 1, spacing)
    return frequencies, len(frequencies) * timing, timing


def simulate_command(cmd, frequency_hz=1000, on_line=None):
    tokens = cmd.split()
    command = tokens[0].upper() if tokens else ""
    if command == "PING":
        lines = ["OK:PONG"]
    elif command == "STATUS":
        lines = [f"OK:FREQ={frequency_hz},MODE=SINE,SOURCE=SIM"]
    elif command in {"MEAS", "MSWEEP", "MSWEEP_POINTS"} and len(tokens) > 1 and tokens[1] == "SIM":
        if command == "MEAS" and len(tokens) == 2:
            frequencies, duration_ms, dwell = [frequency_hz], 0, 0
        elif command in {"MSWEEP", "MSWEEP_POINTS"}:
            frequencies, duration_ms, dwell = sweep_plan(cmd)
        else:
            raise ValueError("invalid simulated measurement command")
        lines = []
        for sequence, frequency in enumerate(frequencies):
            ratio = frequency / 100000.0
            mag_mv = 900.0 - 300.0 * math.log10(1.0 + ratio * ratio)
            phase_mv = 1800.0 - 10.0 * math.degrees(math.atan(ratio))
            flags = 9
            if mag_mv < 30 or mag_mv > 1770 or phase_mv < 30 or phase_mv > 1770:
                flags |= 4
            mag_mv = int(math.floor(max(30.0, min(1800.0, mag_mv)) + 0.5))
            phase_mv = int(math.floor(max(30.0, min(1800.0, phase_mv)) + 0.5))
            timestamp_us = sequence * dwell * 1000
            if command == "MSWEEP_POINTS":
                total_ticks = (duration_ms + 9) // 10
                timestamp_us = (sequence + 1) * total_ticks // len(frequencies) * 10000
            lines.append(f"DATA:V1,F={frequency},SEQ={sequence},T_US={int(timestamp_us)},"
                         f"SOURCE=SIM,MAG_RAW=-1,PHASE_RAW=-1,MAG_MV={mag_mv},PHASE_MV={phase_mv},"
                         f"MAG_SPAN=-1,PHASE_SPAN=-1,FLAGS={flags}")
        lines.append(f"OK:{command}_DONE")
    else:
        lines = ["ERR:offline simulator supports PING, STATUS and SIM measurements only"]
    if on_line is not None:
        for line in lines:
            on_line(line)
    return lines


def validate_measurement_sweep(samples, cmd):
    tokens = cmd.split()
    if tokens and tokens[0].upper() == "MEAS":
        if len(tokens) != 2 or len(samples) != 1 or samples[0]["SEQ"] != 0 or samples[0]["SOURCE"] != tokens[1]:
            raise ValueError("incomplete or mismatched single measurement")
    if tokens and tokens[0].upper() in {"MSWEEP", "MSWEEP_POINTS"}:
        frequencies, _, _ = sweep_plan(cmd)
        if len(samples) != len(frequencies):
            raise ValueError("incomplete measurement sweep")
        for sequence, sample in enumerate(samples):
            if sample["SEQ"] != sequence or sample["F"] != frequencies[sequence] or sample["SOURCE"] != tokens[1]:
                raise ValueError("measurement sequence, frequency or source mismatch")


def command_timing(cmd):
    tokens = cmd.split()
    command = tokens[0].upper() if tokens else ""
    if command == "MEAS":
        return 5.0, 5.0, "OK:MEAS_DONE"
    if command in {"SWEEP", "MSWEEP", "MSWEEP_POINTS"}:
        try:
            frequencies, duration_ms, dwell = sweep_plan(cmd)
        except ValueError:
            return 5.0, 5.0, "OK:" + command + "_DONE"
        return 10.0 + duration_ms / 1000.0 + len(frequencies) * 0.1, max(5.0, dwell / 1000.0 + 3.0), "OK:" + command + "_DONE"
    return 1.5, 1.5, None


def parse_device_status(text):
    if not text.startswith("OK:FREQ="):
        return None
    fields = {}
    for column in next(csv.reader([text[3:]], strict=True)):
        key, separator, value = column.partition("=")
        if not separator or key in fields:
            raise ValueError("invalid or duplicate status field")
        fields[key] = value
    if "MODE" not in fields or fields["MODE"] not in {"SINE", "TRI", "SQUARE"}:
        raise ValueError("invalid device waveform")
    if not fields["FREQ"].isascii() or not fields["FREQ"].isdecimal() or not 1 <= int(fields["FREQ"]) <= MAX_DDS_FREQUENCY_HZ:
        raise ValueError("invalid device frequency")
    sweep_fields = {"START", "STOP", "STEP", "DWELL_MS", "POINTS", "TOTAL_MS", "SOURCE"}
    if (sweep_fields - {"SOURCE"}) & fields.keys():
        if not sweep_fields <= fields.keys():
            raise ValueError("incomplete device sweep settings")
        numeric = sweep_fields - {"SOURCE"}
        if any(not fields[key].isascii() or not fields[key].isdecimal() for key in numeric):
            raise ValueError("invalid device sweep settings")
        counted = int(fields["POINTS"]) != 0
        command = "MSWEEP_POINTS" if counted else "MSWEEP"
        spacing = fields["POINTS"] if counted else fields["STEP"]
        timing = fields["TOTAL_MS"] if counted else fields["DWELL_MS"]
        sweep_plan(f"{command} {fields['SOURCE']} {fields['START']} {fields['STOP']} {spacing} {timing}")
    if "STORAGE" in fields and fields["STORAGE"] not in {"READY", "FAULT"}:
        raise ValueError("invalid storage status")
    if "RESTORED" in fields and fields["RESTORED"] not in {"0", "1"}:
        raise ValueError("invalid restore status")
    return fields


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


def read_serial_lines(ser, timeout=1.0, idle_timeout=None, completion=None, on_line=None):
    deadline = time.monotonic() + timeout
    idle_deadline = time.monotonic() + (idle_timeout or timeout)
    chunks = []
    pending = bytearray()
    while time.monotonic() < min(deadline, idle_deadline):
        raw = ser.read(ser.in_waiting or 1)
        if raw:
            idle_deadline = time.monotonic() + (idle_timeout or timeout)
            pending.extend(raw)
        while b"\n" in pending:
            line, _, remainder = pending.partition(b"\n")
            pending = bytearray(remainder)
            if len(line) > 4096:
                raise ValueError("UART line exceeds maximum size")
            text = line.decode("utf-8", errors="replace").strip()
            if text:
                chunks.append(text)
                if on_line is not None:
                    on_line(text)
                if text.startswith("ERR:") or (text == completion if completion else text.startswith("OK:")):
                    return chunks
        if len(pending) > 4096:
            raise ValueError("UART line exceeds maximum size")
    raise TimeoutError("UART response timed out before completion")


def send_cmd(ser, cmd, on_line=None):
    if not cmd:
        return []
    ser.write((cmd + "\r\n").encode("ascii"))
    ser.flush()
    timeout, idle_timeout, completion = command_timing(cmd)
    return read_serial_lines(ser, timeout, idle_timeout, completion, on_line)


class UARTTerminalApp:
    def __init__(self, port, baud, simulate=False):
        self.root = tk.Tk()
        self.root.title(f"ESP32 UART Control - {'OFFLINE SIM' if simulate else port}")
        self.root.geometry("900x600")
        self.root.minsize(700, 500)

        self.port = port
        self.baud = baud
        self.ser = None
        self.simulate = simulate
        self.transaction_busy = False
        self.transaction_events = queue.Queue()
        self.measurements = []
        self.measurements_complete = False
        self.legacy_points = []
        self.active_command = ""
        self.transaction_error = False
        self.history = self._load_history()
        self.history_index = 0
        self.current_frequency = 1000
        self.current_wave = "SINE"
        self.signal_label_var = tk.StringVar(value="DDS: 1000 Hz | SINE")
        self.storage_var = tk.StringVar(value="Storage: unknown")
        self.last_manual_state_update = 0.0
        self._status_lock_seconds = 2.0

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._build_ui()
        self._connect_serial()
        self.root.after(50, self._poll_transaction)

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
        ttk.Label(status_frame, textvariable=self.storage_var, wraplength=190).pack(anchor="w", pady=(4, 0))

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
        ttk.Label(sweep_frame, text="Start(Hz)/Stop(Hz)/Step(Hz)/Dwell(ms):").pack(anchor="w")
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

        ttk.Label(sweep_frame, text="Points / Total(ms):").pack(anchor="w", pady=(8, 0))
        self.sweep_points = tk.StringVar(value="100")
        self.sweep_total_ms = tk.StringVar(value="5000")
        point_entries = ttk.Frame(sweep_frame)
        point_entries.pack(fill="x", pady=(4, 0))
        ttk.Entry(point_entries, textvariable=self.sweep_points, width=10).pack(side="left", padx=(0, 4))
        ttk.Entry(point_entries, textvariable=self.sweep_total_ms, width=10).pack(side="left", padx=4)
        ttk.Button(sweep_frame, text="MSWEEP_POINTS", command=self._measurement_sweep_points).pack(fill="x", pady=(8, 0))

        measurement_frame = ttk.LabelFrame(parent, text="AD8302", padding=10)
        measurement_frame.pack(fill="x", pady=(0, 8))
        self.measurement_source = tk.StringVar(value="SIM")
        ttk.Combobox(measurement_frame, textvariable=self.measurement_source,
                 values=["SIM"] if self.simulate else ["SIM", "ADC"], state="readonly", width=8).pack(side="left", padx=(0, 6))
        ttk.Button(measurement_frame, text="MEAS", command=lambda: self._send_cmd(f"MEAS {self.measurement_source.get()}")).pack(side="left", padx=(0, 6))
        ttk.Button(measurement_frame, text="MSWEEP", command=self._measurement_sweep).pack(side="left", padx=(0, 6))
        ttk.Button(measurement_frame, text="Export CSV", command=self._export_measurements).pack(side="left")

        signal_frame = ttk.LabelFrame(parent, text="DDS preview (synthetic)", padding=10)
        signal_frame.pack(fill="x", pady=(0, 8))
        ttk.Label(signal_frame, textvariable=self.signal_label_var, font=("TkDefaultFont", 10, "bold")).pack(anchor="w")
        self.signal_canvas = tk.Canvas(signal_frame, bg="#f8fafc", height=180, width=320)
        self.signal_canvas.pack(fill="both", expand=True)
        self._draw_signal_monitor()

        plot_frame = ttk.LabelFrame(parent, text="Plot", padding=10)
        plot_frame.pack(fill="both", expand=True)
        self.plot_canvas = tk.Canvas(plot_frame, bg="white", height=220, width=320)
        self.plot_canvas.pack(fill="both", expand=True)
        self.phase_canvas = tk.Canvas(plot_frame, bg="white", height=220, width=320)
        self.phase_canvas.pack(fill="both", expand=True)
        self.plot_canvas.bind("<Configure>", lambda event: self._redraw_measurements())
        self.phase_canvas.bind("<Configure>", lambda event: self._redraw_measurements())

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
        if not text or not text.startswith("OK:"):
            return
        try:
            settings = parse_device_status(text)
        except (ValueError, csv.Error) as exc:
            self._add_output(f"ERR:invalid device status: {exc}")
            return
        if settings is not None:
            if "STORAGE" in settings:
                restored = "yes" if settings.get("RESTORED") == "1" else "no"
                self.storage_var.set(f"Storage: {settings['STORAGE']} | Restored: {restored}")
            if "START" in settings:
                self.sweep_start.set(settings["START"])
                self.sweep_stop.set(settings["STOP"])
                if int(settings["POINTS"]) > 0:
                    self.sweep_points.set(settings["POINTS"])
                    self.sweep_total_ms.set(settings["TOTAL_MS"])
                else:
                    self.sweep_step.set(settings["STEP"])
                    self.sweep_dwell.set(settings["DWELL_MS"])
                if not self.simulate:
                    self.measurement_source.set(settings["SOURCE"])
        freq, wave = parse_frequency_status(text)
        if freq is not None:
            self.current_frequency = freq
            self.freq_var.set(str(freq))
        if wave is not None:
            self.current_wave = wave
            self.wave_var.set(wave)

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
        if self.transaction_busy:
            self._add_output("ERR: transaction in progress")
            return
        if self.simulate:
            self.status_var.set("SIM | offline | nominal detector")
            self._add_output("SOURCE=SIM: offline 100 kHz low-pass DUT model")
            return
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
        if self.transaction_busy:
            self._add_output("ERR: transaction in progress")
            return
        if not self.simulate and (self.ser is None or not self.ser.is_open):
            self._add_output("ERR: serial port is not open")
            return
        self._add_output(f"> {cmd}")
        self.history = [h for h in self.history if h != cmd]
        self.history.append(cmd)
        self._save_history()
        self.history_index = len(self.history)
        self.active_command = cmd
        self.transaction_error = False
        command = cmd.split()[0].upper()
        if command in {"MEAS", "MSWEEP", "MSWEEP_POINTS"}:
            self.measurements = []
            self.measurements_complete = False
        if command == "SWEEP":
            self.measurements = []
            self.measurements_complete = False
            self.legacy_points = []
        self.transaction_busy = True
        def worker():
            try:
                callback = lambda line: self.transaction_events.put(("line", line))
                if self.simulate:
                    simulate_command(cmd, self.current_frequency, callback)
                else:
                    send_cmd(self.ser, cmd, callback)
            except (serial.SerialException, OSError, ValueError, TimeoutError) as exc:
                self.transaction_events.put(("error", str(exc)))
            finally:
                self.transaction_events.put(("done", None))
        threading.Thread(target=worker, daemon=True).start()

    def _poll_transaction(self):
        plot_dirty = False
        for event_index in range(128):
            try:
                kind, value = self.transaction_events.get_nowait()
            except queue.Empty:
                break
            if kind == "done":
                self.transaction_busy = False
                if not self.transaction_error:
                    try:
                        validate_measurement_sweep(self.measurements, self.active_command)
                    except ValueError as exc:
                        self.transaction_error = True
                        self._add_output(f"ERR:{exc}")
                self.status_var.set("INCOMPLETE" if self.transaction_error else "COMPLETE")
                if self.active_command.split()[0].upper() in {"MEAS", "MSWEEP", "MSWEEP_POINTS"}:
                    self.measurements_complete = not self.transaction_error
                plot_dirty = True
            elif kind == "error":
                self.transaction_error = True
                self._add_output(f"ERR:{value}")
            else:
                self._add_output(value)
                if value.startswith("ERR:"):
                    self.transaction_error = True
                try:
                    sample = parse_measurement_line(value)
                    if sample is not None:
                        self.measurements.append(sample)
                        plot_dirty = True
                    match = re.fullmatch(r"f\s*=\s*(\d+)\s*Hz\s*\|\s*ADC\s*=\s*(\d+)", value, re.IGNORECASE)
                    if match:
                        self.legacy_points.append((int(match[1]), int(match[2])))
                        plot_dirty = True
                except (ValueError, csv.Error) as exc:
                    self.transaction_error = True
                    self._add_output(f"ERR:invalid measurement: {exc}")
        if plot_dirty:
            self._redraw_measurements()
        self.root.after(50, self._poll_transaction)

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

    def _draw_plot(self, points, canvas=None, label="ADC raw", color="#16856b", phase=False):
        canvas = self.plot_canvas if canvas is None else canvas
        canvas.delete("all")
        canvas.create_text(8, 8, anchor="nw", text=label, fill="#374151")
        if not points:
            return

        w = max(320, canvas.winfo_width())
        h = max(180, canvas.winfo_height())

        left_pad, right_pad, top_pad, bottom_pad = 48, 20, 35, 30
        plot_w = max(1, w - left_pad - right_pad)
        plot_h = max(1, h - top_pad - bottom_pad)

        xs = [math.log10(point[0]) for point in points]
        ys = [point[1] for point in points if point[1] is not None]
        if not ys:
            canvas.create_text(8, 28, anchor="nw", text="No valid converted samples", fill="#b45309")
            return
        x_min, x_max = min(xs), max(xs)
        y_min, y_max = min(ys), max(ys)
        if phase:
            y_min, y_max = 0, 180
        if x_min == x_max:
            x_min -= 1
            x_max += 1
        if y_min == y_max:
            y_min -= 1
            y_max += 1

        canvas.create_line(left_pad, top_pad, left_pad, h - bottom_pad, fill="#374151")
        canvas.create_line(left_pad, h - bottom_pad, w - right_pad, h - bottom_pad, fill="#374151")

        for i in range(5):
            py = top_pad + (plot_h * i / 4)
            canvas.create_line(left_pad, py, w - right_pad, py, fill="#e5e7eb")
            canvas.create_text(left_pad - 4, py, anchor="e", text=f"{y_max - (y_max - y_min) * i / 4:.1f}")
        canvas.create_text(left_pad, h - 12, anchor="w", text=f"{points[0][0]:g} Hz")
        canvas.create_text(w - right_pad, h - 12, anchor="e", text=f"{points[-1][0]:g} Hz")

        plot_points = []
        for freq, adc in points:
            if adc is None:
                plot_points.append(None)
                continue
            px = left_pad + ((math.log10(freq) - x_min) / (x_max - x_min + 1e-9)) * plot_w
            py = h - bottom_pad - ((adc - y_min) / (y_max - y_min + 1e-9)) * plot_h
            plot_points.append((px, py))

        for i in range(1, len(plot_points)):
            if plot_points[i - 1] is None or plot_points[i] is None:
                continue
            x1, y1 = plot_points[i - 1]
            x2, y2 = plot_points[i]
            canvas.create_line(x1, y1, x2, y2, fill=color, width=2)

        for point in plot_points[:1] + plot_points[-1:]:
            if point is not None:
                x, y = point
                canvas.create_oval(x - 3, y - 3, x + 3, y + 3, fill=color)

    def _redraw_measurements(self):
        if self.measurements:
            source = self.measurements[0]["SOURCE"]
            state = "nominal" if self.measurements_complete else "incomplete"
            gain = [(sample["F"], None if sample["FLAGS"] & 6 else sample["GAIN_DB"]) for sample in self.measurements]
            phase = [(sample["F"], None if sample["FLAGS"] & 6 else sample["PHASE_FOLDED_DEG"]) for sample in self.measurements]
            self._draw_plot(gain, label=f"Gain (dB) | {source} | {state}")
            self._draw_plot(phase, self.phase_canvas, f"Folded phase (deg) | {source} | {state}", "#c15b39", True)
        else:
            self._draw_plot(self.legacy_points)
            self._draw_plot([], self.phase_canvas, "Folded phase (deg)")

    def _export_measurements(self):
        if self.transaction_busy or not self.measurements_complete or not self.measurements:
            self._add_output("ERR: export requires a completed measurement transaction")
            return
        path = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if path:
            try:
                export_measurements(path, self.measurements)
                self._add_output(f"CSV:{path}")
            except OSError as exc:
                self._add_output(f"ERR:{exc}")

    def _set_frequency(self):
        if self.transaction_busy:
            self._add_output("ERR: transaction in progress")
            return
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
        if self.transaction_busy:
            self._add_output("ERR: transaction in progress")
            return
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

    def _measurement_sweep(self):
        self._send_cmd(f"MSWEEP {self.measurement_source.get()} {self.sweep_start.get()} "
                       f"{self.sweep_stop.get()} {self.sweep_step.get()} {self.sweep_dwell.get()}")

    def _measurement_sweep_points(self):
        command = (f"MSWEEP_POINTS {self.measurement_source.get()} {self.sweep_start.get()} "
                   f"{self.sweep_stop.get()} {self.sweep_points.get()} {self.sweep_total_ms.get()}")
        try:
            sweep_plan(command)
        except ValueError as exc:
            self._add_output(f"ERR:{exc}")
            return
        self._send_cmd(command)

    def run(self):
        self.root.mainloop()


def cli_main():
    parser = argparse.ArgumentParser(description="ESP32 UART terminal and GUI")
    parser.add_argument("--port", default="COM5", help="Serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate")
    parser.add_argument("--gui", action="store_true", help="Launch the Tkinter GUI")
    parser.add_argument("--simulate", action="store_true", help="Offline detector simulation; no serial port opened")
    parser.add_argument("--command", help="Execute one command and exit")
    parser.add_argument("--csv", help="Export a completed --command measurement to CSV")
    args = parser.parse_args()

    if args.gui and (args.command or args.csv):
        parser.error("--command and --csv cannot be combined with --gui")

    if args.gui:
        app = UARTTerminalApp(args.port, args.baud, args.simulate)
        app.run()
        return

    if args.command:
        try:
            if args.simulate:
                lines = simulate_command(args.command)
            else:
                with serial.Serial(args.port, args.baud, timeout=0.1) as port:
                    lines = send_cmd(port, args.command)
            for line in lines:
                print(line)
            if any(line.startswith("ERR:") for line in lines):
                raise ValueError("command rejected")
            samples = [sample for line in lines if (sample := parse_measurement_line(line)) is not None]
            validate_measurement_sweep(samples, args.command)
            if args.csv:
                if not samples:
                    raise ValueError("CSV export requires measurement samples")
                export_measurements(args.csv, samples)
        except (serial.SerialException, OSError, ValueError, TimeoutError, csv.Error) as exc:
            raise SystemExit(str(exc))
        return
    if args.simulate or args.csv:
        parser.error("--simulate requires --gui or --command; --csv requires --command")

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

        try:
            lines = send_cmd(ser, cmd)
        except (serial.SerialException, OSError, ValueError, TimeoutError) as exc:
            print(f"ERR:{exc}")
            continue
        for line in lines:
            print(f"ESP32> {line}")

    ser.close()
    print("Serial port closed.")


if __name__ == "__main__":
    cli_main()
