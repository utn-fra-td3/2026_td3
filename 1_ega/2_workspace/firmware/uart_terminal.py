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
MIN_DDS_FREQUENCY_HZ = 100_000
MIN_DDS_OUTPUT_FREQUENCY_HZ = 1
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
    if not MIN_DDS_FREQUENCY_HZ <= start <= stop <= MAX_DDS_FREQUENCY_HZ:
        raise ValueError("invalid sweep range")
    if command == "MSWEEP_POINTS":
        if not (1 <= spacing <= min(10000, stop - start + 1) and
                (spacing != 1 or start == stop) and spacing * 10 <= timing <= 180000):
            raise ValueError("invalid point count or sweep duration")
        frequencies = [start + (stop - start) * index // (spacing - 1) for index in range(spacing)] if spacing > 1 else [start]
        return frequencies, timing, timing / spacing
    if not (1 <= spacing <= MAX_DDS_FREQUENCY_HZ and 1 <= timing <= 60000):
        raise ValueError("invalid sweep step or dwell")
    frequencies = range(start, stop + 1, spacing)
    if len(frequencies) * ((timing + 9) // 10 * 10) > 180000:
        raise ValueError("sweep duration exceeds 180000 ms")
    return frequencies, len(frequencies) * timing, timing


def simulate_command(cmd, frequency_hz=MIN_DDS_FREQUENCY_HZ, on_line=None):
    if not MIN_DDS_FREQUENCY_HZ <= frequency_hz <= MAX_DDS_FREQUENCY_HZ:
        raise ValueError("frequency must be 100000..37500000 Hz")
    tokens = cmd.split()
    command = tokens[0].upper() if tokens else ""
    if command == "PING":
        lines = ["OK:PONG"]
    elif command == "STATUS":
        lines = [f"OK:FREQ={frequency_hz},MODE=SINE,SOURCE=SIM"]
    elif command == "SWEEP_CONFIG":
        sweep_plan("MSWEEP_POINTS " + " ".join(tokens[1:]))
        source, start, stop, points, total_ms = tokens[1:]
        if source != "SIM":
            raise ValueError("offline simulator supports SIM configuration only")
        lines = [f"OK:FREQ={frequency_hz},MODE=SINE,START={start},STOP={stop},"
                 f"STEP=0,DWELL_MS=0,POINTS={points},TOTAL_MS={total_ms},SOURCE={source}"]
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
        self._background_status = False
        self._device_sweep = None
        self.history = self._load_history()
        self.history_index = 0
        self.current_frequency = MIN_DDS_FREQUENCY_HZ
        self.current_wave = "SINE"
        self.signal_label_var = tk.StringVar(value=f"DDS: {MIN_DDS_FREQUENCY_HZ} Hz | SINE")
        self.dds_error_var = tk.StringVar(value="")
        self.storage_var = tk.StringVar(value="Storage: unknown")
        self.last_manual_state_update = 0.0
        self._status_lock_seconds = 2.0

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._build_ui()
        self._connect_serial()
        self.root.after(50, self._poll_transaction)
        self.root.after(1000, self._poll_device_status)

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
        style = ttk.Style(self.root)
        style.theme_use("clam")
        self.root.configure(background="#edf1f2")
        style.configure(".", background="#edf1f2", foreground="#243238", font=("Bahnschrift", 10))
        style.configure("TFrame", background="#edf1f2")
        style.configure("TLabel", background="#edf1f2")
        style.configure("Title.TLabel", font=("Bahnschrift", 17, "bold"), foreground="#155f59")
        style.configure("Muted.TLabel", foreground="#596c73")
        style.configure("TButton", padding=(10, 6), background="#ffffff", borderwidth=1)
        style.map("TButton", background=[("active", "#e0ece9")])
        style.configure("Primary.TButton", background="#126b62", foreground="#ffffff", borderwidth=0)
        style.map("Primary.TButton", background=[("active", "#0c514b"), ("disabled", "#d8e1e2")],
                  foreground=[("disabled", "#65777d")])
        style.configure("TEntry", padding=5, fieldbackground="#ffffff", bordercolor="#c7d2d6")
        style.configure("TCombobox", padding=5, fieldbackground="#ffffff")
        style.configure("TNotebook", borderwidth=0, tabmargins=(0, 0, 0, 4))
        style.configure("TNotebook.Tab", padding=(18, 7), background="#e0e7e8")
        style.map("TNotebook.Tab", background=[("selected", "#ffffff")],
                  foreground=[("selected", "#126b62")])
        style.configure("TLabelframe", bordercolor="#c7d2d6", relief="solid")
        style.configure("TLabelframe.Label", foreground="#126b62", font=("Bahnschrift", 10, "bold"))
        self.root.geometry("1040x740")
        self.root.minsize(800, 620)
        self.dds_window = None
        self.freq_var = tk.StringVar(value=str(self.current_frequency))
        self.wave_var = tk.StringVar(value=self.current_wave)

        heading = ttk.Frame(self.root, padding=(14, 10, 14, 0))
        heading.pack(fill="x")
        ttk.Label(heading, text="Frequency Response", style="Title.TLabel").pack(side="left")
        ttk.Label(heading, text="EGA  /  AD9834 + AD8302", style="Muted.TLabel").pack(side="right")
        top = ttk.Frame(self.root, padding=(10, 10, 10, 6))
        top.pack(fill="x")

        ttk.Label(top, text="Port:").grid(row=0, column=0, sticky="w")
        self.port_var = tk.StringVar(value=self.port)
        ttk.Entry(top, textvariable=self.port_var, width=18).grid(row=0, column=1, padx=(0, 10), sticky="ew")

        ttk.Label(top, text="Baud:").grid(row=0, column=2, sticky="w")
        self.baud_var = tk.StringVar(value=str(self.baud))
        ttk.Entry(top, textvariable=self.baud_var, width=12).grid(row=0, column=3, padx=(0, 10), sticky="ew")

        ttk.Button(top, text="Connect", command=self._connect_serial).grid(row=0, column=4, padx=(0, 8), sticky="ew")
        ttk.Button(top, text="DDS configuration", command=self._open_dds_configuration).grid(row=0, column=5, sticky="ew")

        for i in range(7):
            top.grid_columnconfigure(i, weight=0)

        tabs = ttk.Notebook(self.root)
        tabs.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        dashboard = ttk.Frame(tabs, padding=10)
        diagnostics = ttk.Frame(tabs, padding=10)
        tabs.add(dashboard, text="Sweep")
        tabs.add(diagnostics, text="Diagnostics")

        self.output = tk.Text(diagnostics, wrap="word", state="disabled", font=("Consolas", 10),
                      background="#ffffff", foreground="#243238", relief="flat",
                      padx=12, pady=10, insertbackground="#126b62", selectbackground="#cce5e0")
        self.output.pack(fill="both", expand=True)
        command_bar = ttk.Frame(diagnostics, padding=(0, 8, 0, 0))
        command_bar.pack(fill="x")

        ttk.Label(command_bar, text="Command:").pack(side="left")
        self.command_var = tk.StringVar()
        self.command_entry = ttk.Entry(command_bar, textvariable=self.command_var)
        self.command_entry.pack(side="left", fill="x", expand=True, padx=(8, 8))
        self.command_entry.bind("<Return>", lambda event: self._send_current_cmd())
        self.command_entry.bind("<Up>", self._history_up)
        self.command_entry.bind("<Down>", self._history_down)
        ttk.Button(command_bar, text="Send", command=self._send_current_cmd).pack(side="left")
        ttk.Button(command_bar, text="Clear", command=self._clear_output).pack(side="left", padx=(8, 0))

        self._build_dashboard(dashboard)

    def _build_dashboard(self, parent):
        main_row = ttk.Frame(parent)
        main_row.pack(fill="both", expand=True)

        sidebar = ttk.Frame(main_row, width=260)
        sidebar.pack(side="left", fill="y", padx=(0, 16))

        self.status_var = tk.StringVar(value="Disconnected")
        status_frame = ttk.LabelFrame(sidebar, text="Status", padding=(8, 6))
        status_frame.pack(fill="x", pady=(0, 8))
        self.status_label = ttk.Label(status_frame, textvariable=self.status_var, wraplength=224)
        self.status_label.pack(anchor="w")
        self.status_var.trace_add("write", self._refresh_status_style)
        ttk.Label(status_frame, textvariable=self.storage_var, wraplength=224, style="Muted.TLabel").pack(anchor="w", pady=(4, 0))
        ttk.Label(status_frame, textvariable=self.signal_label_var, wraplength=224, style="Muted.TLabel").pack(anchor="w", pady=(4, 0))

        self._build_gui_controls(sidebar)
        plots = ttk.Frame(main_row)
        plots.pack(side="left", fill="both", expand=True)
        self.plot_canvas = tk.Canvas(plots, bg="white", height=240, width=400,
                         highlightthickness=1, highlightbackground="#d8e1e2")
        self.plot_canvas.pack(fill="both", expand=True, pady=(0, 10))
        self.phase_canvas = tk.Canvas(plots, bg="white", height=240, width=400,
                          highlightthickness=1, highlightbackground="#d8e1e2")
        self.phase_canvas.pack(fill="both", expand=True)
        self.plot_canvas.bind("<Configure>", lambda event: self._redraw_measurements())
        self.phase_canvas.bind("<Configure>", lambda event: self._redraw_measurements())

    def _refresh_status_style(self, *_):
        status = self.status_var.get().lower()
        color = "#b43e3b" if "err" in status or "fault" in status else (
            "#966514" if "disconnected" in status else "#126b62")
        self.status_label.configure(foreground=color)

    def _build_gui_controls(self, parent):
        sweep_frame = ttk.Frame(parent)
        sweep_frame.pack(fill="x")
        self.sweep_start = tk.StringVar(value=str(MIN_DDS_FREQUENCY_HZ))
        self.sweep_stop = tk.StringVar(value=str(MAX_DDS_FREQUENCY_HZ))
        self.sweep_points = tk.StringVar(value="100")
        self.sweep_total_ms = tk.StringVar(value="10000")
        self._applied_sweep = (str(MIN_DDS_FREQUENCY_HZ), str(MAX_DDS_FREQUENCY_HZ), "100", "10000")
        self._sweep_set_index = None
        for index, (label, variable) in enumerate((("Start (Hz)", self.sweep_start), ("Stop (Hz)", self.sweep_stop),
                    ("Points", self.sweep_points), ("Total sweep time (ms)", self.sweep_total_ms))):
            ttk.Label(sweep_frame, text=label, style="Muted.TLabel").pack(anchor="w", pady=(4, 2))
            field_row = ttk.Frame(sweep_frame)
            field_row.pack(fill="x")
            ttk.Entry(field_row, textvariable=variable, width=18).pack(side="left", fill="x", expand=True)
            ttk.Button(field_row, text="Set", width=5, style="Primary.TButton",
                   command=lambda selected=index: self._set_sweep_parameter(selected)).pack(side="left", padx=(6, 0))
        self.measurement_source = tk.StringVar(value="SIM")
        source_row = ttk.Frame(sweep_frame)
        source_row.pack(fill="x", pady=(10, 8))
        ttk.Radiobutton(source_row, text="Simulation", variable=self.measurement_source, value="SIM",
                command=self._set_sweep_parameter).pack(side="left")
        ttk.Radiobutton(source_row, text="Measurement", variable=self.measurement_source, value="ADC",
                command=self._set_sweep_parameter,
                        state="disabled" if self.simulate else "normal").pack(side="left")
        ttk.Button(sweep_frame, text="Start sweep", style="Primary.TButton",
               command=self._measurement_sweep_points).pack(fill="x", pady=(8, 0))
        ttk.Button(sweep_frame, text="Export CSV", command=self._export_measurements).pack(fill="x", pady=(8, 0))

    def _open_dds_configuration(self):
        if self.dds_window is not None and self.dds_window.winfo_exists():
            self.dds_window.lift()
            return
        window = self.dds_window = tk.Toplevel(self.root)
        window.title("DDS configuration")
        window.transient(self.root)
        window.resizable(False, False)
        fields = ttk.Frame(window, padding=16)
        fields.pack(fill="both", expand=True)
        ttk.Label(fields, text="Frequency (Hz)").grid(row=0, column=0, sticky="w", pady=(0, 4))
        ttk.Entry(fields, textvariable=self.freq_var, width=22).grid(row=1, column=0, sticky="ew")
        ttk.Button(fields, text="Set frequency", style="Primary.TButton", command=self._set_frequency).grid(row=1, column=1, padx=(10, 0))
        ttk.Label(fields, text="Waveform").grid(row=2, column=0, sticky="w", pady=(14, 4))
        ttk.Combobox(fields, textvariable=self.wave_var, values=["SINE", "TRI", "SQUARE"],
                     state="readonly", width=20).grid(row=3, column=0, sticky="ew")
        ttk.Button(fields, text="Set waveform", style="Primary.TButton", command=self._set_waveform).grid(row=3, column=1, padx=(10, 0))
        ttk.Label(fields, textvariable=self.signal_label_var, wraplength=340).grid(row=4, column=0, columnspan=2, sticky="w", pady=(16, 0))
        ttk.Label(fields, textvariable=self.dds_error_var, foreground="#b43e3b",
              wraplength=340).grid(row=5, column=0, columnspan=2, sticky="w", pady=(4, 0))
        ttk.Button(fields, text="Close", command=window.destroy).grid(row=6, column=1, sticky="e", pady=(16, 0))

    def _add_output(self, msg):
        self.output.configure(state="normal")
        self.output.insert(tk.END, msg + "\n")
        self.output.see(tk.END)
        self.output.configure(state="disabled")
        if msg.startswith(("ERR:", "Connection error:")):
            self.status_var.set(msg)
            if self.active_command.split()[:1] in (["SETF"], ["SETW"]):
                self.dds_error_var.set(msg)
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
        if getattr(self, "_background_status", False):
            focused = self.root.focus_get()
            if isinstance(focused, (ttk.Entry, ttk.Combobox, tk.Entry)):
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
            sweep_settings = tuple(settings.get(key) for key in (
                "START", "STOP", "STEP", "DWELL_MS", "POINTS", "TOTAL_MS", "SOURCE"))
            if "START" in settings and sweep_settings != getattr(self, "_device_sweep", None):
                if int(settings["POINTS"]) > 0:
                    applied = (settings["START"], settings["STOP"], settings["POINTS"], settings["TOTAL_MS"])
                else:
                    frequencies, duration, _ = sweep_plan(
                        f"MSWEEP {settings['SOURCE']} {settings['START']} {settings['STOP']} "
                        f"{settings['STEP']} {settings['DWELL_MS']}"
                    )
                    applied = (settings["START"], settings["STOP"], str(len(frequencies)), str(duration))
                previous = getattr(self, "_applied_sweep", None)
                selected = getattr(self, "_sweep_set_index", None)
                setting_reply = (getattr(self, "active_command", "").startswith("SWEEP_CONFIG ") and
                                 not getattr(self, "_background_status", False))
                for index, variable in enumerate((self.sweep_start, self.sweep_stop, self.sweep_points, self.sweep_total_ms)):
                    if not setting_reply or previous is None or index == selected or variable.get() == previous[index]:
                        variable.set(applied[index])
                self._applied_sweep = applied
                if not self.simulate:
                    self.measurement_source.set(settings["SOURCE"])
                self._device_sweep = sweep_settings
        freq, wave = parse_frequency_status(text)
        if freq is not None and freq != self.current_frequency:
            self.current_frequency = freq
            self.freq_var.set(str(freq))
        if wave is not None and wave != self.current_wave:
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

    def _poll_device_status(self):
        if not self.simulate and self.ser is not None and self.ser.is_open and not self.transaction_busy:
            focused = self.root.focus_get()
            if not isinstance(focused, (ttk.Entry, ttk.Combobox, tk.Entry)):
                self._send_cmd("STATUS", background=True)
        self.root.after(1000, self._poll_device_status)

    def _send_cmd(self, cmd, background=False):
        if self.transaction_busy:
            self._add_output("ERR: transaction in progress")
            return
        if not self.simulate and (self.ser is None or not self.ser.is_open):
            self._add_output("ERR: serial port is not open")
            return
        self._background_status = background
        if not background:
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
        if not background:
            self.status_var.set("Sweep running" if command == "MSWEEP_POINTS" else "Working")
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
                if getattr(self, "_background_status", False):
                    self._background_status = False
                    continue
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
                if getattr(self, "_background_status", False) and not value.startswith("ERR:"):
                    self._update_signal_monitor(value)
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
        heading_color = "#966514" if "RANGE!" in label or "calibration unavailable" in label else color
        heading = canvas.create_text(12, 12, anchor="nw", text=label, fill=heading_color,
                         font=("Bahnschrift", 10, "bold"),
                         width=max(304, canvas.winfo_width() - 16))
        if not points:
            canvas.create_text(12, max(50, canvas.bbox(heading)[3] + 16), anchor="nw",
                               text="No sweep data", fill="#6b7d83", font=("Bahnschrift", 10))
            return

        w = max(320, canvas.winfo_width())
        h = max(180, canvas.winfo_height())

        left_pad, right_pad, top_pad, bottom_pad = 48, 20, max(35, canvas.bbox(heading)[3] + 12), 30
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
            canvas.create_line(left_pad, py, w - right_pad, py, fill="#e2e9eb")
            canvas.create_text(left_pad - 6, py, anchor="e", fill="#596c73",
                               font=("Bahnschrift", 9), text=f"{y_max - (y_max - y_min) * i / 4:.1f}")
            if 0 < i < 4:
                grid_x = left_pad + plot_w * i / 4
                canvas.create_line(grid_x, top_pad, grid_x, h - bottom_pad, fill="#edf1f2")
        canvas.create_text(left_pad, h - 12, anchor="w", fill="#596c73",
                           font=("Bahnschrift", 9), text=f"{points[0][0]:g} Hz")
        canvas.create_text(w - right_pad, h - 12, anchor="e", fill="#596c73",
                           font=("Bahnschrift", 9), text=f"{points[-1][0]:g} Hz")

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
            state = "complete" if self.measurements_complete else "acquiring / incomplete"
            uncalibrated = any(sample["FLAGS"] & 2 for sample in self.measurements)
            suspect_count = sum(bool(sample["FLAGS"] & 4) for sample in self.measurements)
            if source == "ADC" and uncalibrated:
                magnitude = [(sample["F"], sample["MAG_RAW"]) for sample in self.measurements]
                phase = [(sample["F"], sample["PHASE_RAW"]) for sample in self.measurements]
                warning = "ADC voltage calibration unavailable"
                if suspect_count:
                    warning += f"; {suspect_count} range warnings"
                self._draw_plot(magnitude, label=f"MAG ADC raw (counts) | {source} | {state}\n{warning}")
                self._draw_plot(phase, self.phase_canvas,
                                f"PHS ADC raw (counts) | {source} | {state}\n{warning}", "#c15b39")
                return
            warning = f"\nRANGE! {suspect_count} input range warnings; suspect values shown" if suspect_count else ""
            gain = [(sample["F"], sample["GAIN_DB"]) for sample in self.measurements]
            phase = [(sample["F"], sample["PHASE_FOLDED_DEG"]) for sample in self.measurements]
            self._draw_plot(gain, label=f"Gain (dB), nominal | {source} | {state}{warning}")
            self._draw_plot(phase, self.phase_canvas,
                            f"Folded phase (deg), nominal | {source} | {state}{warning}", "#c15b39", True)
        else:
            self._draw_plot(self.legacy_points, label="ADC raw (counts)" if self.legacy_points else "Gain (dB), nominal")
            self._draw_plot([], self.phase_canvas, "Folded phase (deg), nominal", "#c15b39")

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
            self.dds_error_var.set("ERR: transaction in progress")
            self._add_output("ERR: transaction in progress")
            return
        freq = self.freq_var.get().strip()
        try:
            if not freq.isascii() or not freq.isdecimal():
                raise ValueError
            requested_frequency = int(freq)
            if not MIN_DDS_OUTPUT_FREQUENCY_HZ <= requested_frequency <= MAX_DDS_FREQUENCY_HZ:
                raise ValueError
        except ValueError:
            self.dds_error_var.set(
                f"ERR: Frequency must be an integer {MIN_DDS_OUTPUT_FREQUENCY_HZ}..{MAX_DDS_FREQUENCY_HZ} Hz"
            )
            return
        self.dds_error_var.set("")
        self.current_frequency = requested_frequency
        self.last_manual_state_update = time.monotonic()
        self.signal_label_var.set(f"DDS: {self.current_frequency} Hz | {self.current_wave}")
        if hasattr(self, "signal_canvas"):
            self._draw_signal_monitor()
        self._send_cmd(f"SETF {freq}")

    def _set_waveform(self):
        if self.transaction_busy:
            self.dds_error_var.set("ERR: transaction in progress")
            self._add_output("ERR: transaction in progress")
            return
        wave = self.wave_var.get().strip().upper()
        if wave not in {"SINE", "TRI", "SQUARE"}:
            self.dds_error_var.set("ERR: Waveform must be SINE, TRI or SQUARE")
            return
        self.dds_error_var.set("")
        self.current_wave = wave
        self.last_manual_state_update = time.monotonic()
        self.signal_label_var.set(f"DDS: {self.current_frequency} Hz | {self.current_wave}")
        if hasattr(self, "signal_canvas"):
            self._draw_signal_monitor()
        self._send_cmd(f"SETW {wave}")

    def _set_sweep_parameter(self, index=None):
        source = self.measurement_source.get()
        if index is None:
            applied_source = self._device_sweep[-1] if self._device_sweep else ("SIM" if self.simulate else "ADC")
            self.measurement_source.set(applied_source)
        if self.transaction_busy:
            self._add_output("ERR: transaction in progress")
            return
        variables = (self.sweep_start, self.sweep_stop, self.sweep_points, self.sweep_total_ms)
        values = list(self._applied_sweep)
        if index is not None:
            values[index] = variables[index].get().strip()
        arguments = f"{source} {' '.join(values)}"
        try:
            sweep_plan(f"MSWEEP_POINTS {arguments}")
        except ValueError as exc:
            self._add_output(f"ERR:{exc}")
            return
        self._sweep_set_index = index
        self._send_cmd(f"SWEEP_CONFIG {arguments}")

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
    print("Type 'HELP', 'STATUS', 'SETF 100000', 'SETW SINE', 'ADC', 'SWEEP 100000 200000 1000 50', or 'exit'")
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
