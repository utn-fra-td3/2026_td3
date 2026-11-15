import csv
import io
import queue
import tempfile
import unittest
from collections import deque
from pathlib import Path
from unittest.mock import Mock, patch

import uart_terminal as terminal


def measurement_line(source="SIM", mag_mv=810, phase_mv=1350, flags=9, sequence=0):
    raw = -1 if source == "SIM" else 1200
    span = -1 if source == "SIM" else 8
    return (f"DATA:V1,F=100000,SEQ={sequence},T_US=123,SOURCE={source},"
            f"MAG_RAW={raw},PHASE_RAW={raw},MAG_MV={mag_mv},PHASE_MV={phase_mv},"
            f"MAG_SPAN={span},PHASE_SPAN={span},FLAGS={flags}")


class FakeSerial:
    def __init__(self, fragments):
        self.fragments = deque(fragments)
        self.written = []

    @property
    def in_waiting(self):
        return len(self.fragments[0]) if self.fragments else 0

    def read(self, size):
        return self.fragments.popleft() if self.fragments else b""

    def write(self, data):
        self.written.append(data)

    def flush(self):
        pass


class MeasurementTests(unittest.TestCase):
    def test_nominal_gain_and_folded_phase(self):
        for mag_mv, gain in ((900, 0), (600, -10), (1200, 10)):
            for phase_mv, phase in ((1350, 45), (900, 90), (450, 135)):
                with self.subTest(gain=gain, phase=phase):
                    sample = terminal.parse_measurement_line(measurement_line(mag_mv=mag_mv, phase_mv=phase_mv))
                    self.assertEqual(sample["GAIN_DB"], gain)
                    self.assertEqual(sample["PHASE_FOLDED_DEG"], phase)
                    self.assertEqual(sample["CALIBRATION"], "NOMINAL_DETECTOR")

    def test_raw_only_never_fabricates_volts(self):
        sample = terminal.parse_measurement_line(measurement_line("ADC", -1, -1, 10))
        self.assertIsNone(sample["GAIN_DB"])
        self.assertIsNone(sample["PHASE_FOLDED_DEG"])

    def test_range_flags_are_preserved(self):
        sample = terminal.parse_measurement_line(measurement_line(mag_mv=30, flags=13))
        self.assertTrue(sample["FLAGS"] & 4)

    def test_offline_simulator_and_sequence_validation(self):
        lines = terminal.simulate_command("MSWEEP SIM 100000 300000 100000 50")
        samples = [terminal.parse_measurement_line(line) for line in lines[:-1]]
        terminal.validate_measurement_sweep(samples, "MSWEEP SIM 100000 300000 100000 50")
        self.assertEqual(samples[0]["MAG_MV"], 810)
        self.assertEqual(samples[0]["PHASE_MV"], 1350)
        self.assertEqual(lines[-1], "OK:MSWEEP_DONE")
        with self.assertRaises(ValueError):
            terminal.validate_measurement_sweep(samples[1:], "MSWEEP SIM 100000 300000 100000 50")
        samples[1]["SEQ"] = 7
        with self.assertRaises(ValueError):
            terminal.validate_measurement_sweep(samples, "MSWEEP SIM 100000 300000 100000 50")

    def test_simulator_rejects_real_adc(self):
        self.assertTrue(terminal.simulate_command("MEAS ADC")[0].startswith("ERR:"))
        with self.assertRaises(ValueError):
            terminal.simulate_command("MSWEEP SIM 0 1000 1 50")

    def test_single_measurement_requires_one_matching_sample(self):
        sample = terminal.parse_measurement_line(measurement_line())
        terminal.validate_measurement_sweep([sample], "MEAS SIM")
        for samples, command in (([], "MEAS SIM"), ([sample, sample], "MEAS SIM"), ([sample], "MEAS ADC")):
            with self.subTest(command=command), self.assertRaises(ValueError):
                terminal.validate_measurement_sweep(samples, command)

    def test_malformed_measurements_rejected(self):
        valid = measurement_line()
        for line in (valid.replace("V1", "V2"), valid + ",F=1", valid.rsplit(",", 1)[0],
                     valid.replace("FLAGS=9", "FLAGS=8"), valid.replace("F=100000", "F=0"),
                     valid.replace("MAG_MV=810", "MAG_MV=nan"), valid.replace("SEQ=0", "SEQ=-1")):
            with self.subTest(line=line), self.assertRaises(ValueError):
                terminal.parse_measurement_line(line)
        self.assertIsNone(terminal.parse_measurement_line("OK:PONG"))

    def test_csv_preserves_units_source_and_calibration(self):
        sample = terminal.parse_measurement_line(measurement_line())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "measurement.csv"
            terminal.export_measurements(path, [sample])
            rows = list(csv.DictReader(io.StringIO(path.read_text(encoding="utf-8"))))
        self.assertEqual(rows[0]["SOURCE"], "SIM")
        self.assertEqual(rows[0]["MAG_RAW"], "-1")
        self.assertEqual(rows[0]["PHASE_FOLDED_DEG"], "45.0")
        self.assertEqual(rows[0]["CALIBRATION"], "NOMINAL_DETECTOR")


class DeviceStatusTests(unittest.TestCase):
    def test_restored_defaults_and_sweep_settings_are_authoritative(self):
        app = object.__new__(terminal.UARTTerminalApp)
        app.current_frequency = 2000
        app.current_wave = "TRI"
        app.simulate = False
        for name in ("freq_var", "wave_var", "storage_var", "signal_label_var", "sweep_start", "sweep_stop", "sweep_points",
                     "sweep_total_ms", "sweep_step", "sweep_dwell", "measurement_source"):
            setattr(app, name, Mock())
        frame = ("OK:FREQ=100000,MODE=SINE,STATUS=READY,STORAGE=READY,RESTORED=1,"
             "START=100000,STOP=101003,STEP=0,DWELL_MS=0,POINTS=4,TOTAL_MS=201,SOURCE=ADC")
        app._update_signal_monitor(frame)
        self.assertEqual((app.current_frequency, app.current_wave), (100000, "SINE"))
        app.freq_var.set.assert_called_once_with("100000")
        app.wave_var.set.assert_called_once_with("SINE")
        app.sweep_points.set.assert_called_once_with("4")
        app.sweep_total_ms.set.assert_called_once_with("201")
        app.measurement_source.set.assert_called_once_with("ADC")
        app.storage_var.set.assert_called_once_with("Storage: READY | Restored: yes")
        app.sweep_step.set.assert_not_called()

    def test_legacy_status_and_storage_fault_are_accepted(self):
        fields = terminal.parse_device_status("OK:FREQ=2000,MODE=TRI,STATUS=READY,SOURCE=SIM")
        self.assertEqual(fields["FREQ"], "2000")
        fields = terminal.parse_device_status("OK:FREQ=1000,MODE=SINE,STORAGE=FAULT,RESTORED=0")
        self.assertEqual(fields["STORAGE"], "FAULT")

    def test_malformed_status_is_rejected(self):
        for frame in ("OK:FREQ=0,MODE=SINE", "OK:FREQ=1000,MODE=INVALID",
                      "OK:FREQ=1000,MODE=SINE,FREQ=2000", "OK:FREQ=1000,MODE=SINE,START=1",
                      "OK:FREQ=1000,MODE=SINE,STORAGE=INVALID"):
            with self.subTest(frame=frame), self.assertRaises(ValueError):
                terminal.parse_device_status(frame)
        self.assertIsNone(terminal.parse_device_status("DATA:V1,F=1000"))


class SimplifiedGuiTests(unittest.TestCase):
    def setUp(self):
        with patch.object(terminal.UARTTerminalApp, "_connect_serial"), \
             patch.object(terminal.UARTTerminalApp, "_load_history", return_value=[]):
            try:
                self.app = terminal.UARTTerminalApp("COM5", 115200, simulate=True)
            except terminal.tk.TclError as exc:
                self.skipTest(f"Tk display unavailable: {exc}")
        self.addCleanup(self.close_app)
        self.app.root.update()

    def close_app(self):
        for callback in self.app.root.tk.call("after", "info"):
            self.app.root.after_cancel(callback)
        self.app.root.destroy()

    def test_main_screen_fits_minimum_window_and_has_only_counted_sweep_controls(self):
        self.app.root.geometry("800x620")
        self.app.root.update()
        self.assertFalse(hasattr(self.app, "sweep_step"))
        self.assertFalse(hasattr(self.app, "sweep_dwell"))
        self.assertFalse(hasattr(self.app, "signal_canvas"))
        pending = list(self.app.root.winfo_children())
        buttons = []
        while pending:
            widget = pending.pop()
            pending.extend(widget.winfo_children())
            if not widget.winfo_viewable():
                continue
            if isinstance(widget, terminal.ttk.Button):
                buttons.append(widget.cget("text"))
                self.assertGreaterEqual(widget.winfo_height(), widget.winfo_reqheight())
            if isinstance(widget, (terminal.ttk.Entry, terminal.ttk.Button, terminal.ttk.Label,
                                   terminal.ttk.Radiobutton, terminal.tk.Canvas)):
                self.assertLessEqual(widget.winfo_rootx() + widget.winfo_width(),
                                     self.app.root.winfo_rootx() + self.app.root.winfo_width())
                self.assertLessEqual(widget.winfo_rooty() + widget.winfo_height(),
                                     self.app.root.winfo_rooty() + self.app.root.winfo_height())
        self.assertCountEqual(buttons, ["Connect", "DDS configuration", "Set", "Set", "Set", "Set", "Start sweep", "Export CSV"])

    def test_status_colors_distinguish_fault_disconnected_and_ready(self):
        for message, color in (("ERR: invalid settings", "#b43e3b"),
                               ("Disconnected", "#966514"), ("Sweep complete", "#126b62")):
            with self.subTest(message=message):
                self.app.status_var.set(message)
                self.assertEqual(str(self.app.status_label.cget("foreground")), color)

    def test_device_changes_sync_but_unchanged_status_preserves_drafts(self):
        self.app.simulate = False
        frame = ("OK:FREQ=100000,MODE=SINE,STATUS=READY,STORAGE=READY,RESTORED=1,"
                 "START=100000,STOP=101003,STEP=0,DWELL_MS=0,POINTS=4,TOTAL_MS=201,SOURCE=ADC")
        self.app._update_signal_monitor(frame)
        self.assertEqual(self.app.sweep_points.get(), "4")
        self.assertEqual(self.app.measurement_source.get(), "ADC")
        self.app.sweep_points.set("20")
        self.app.freq_var.set("200000")
        self.app._update_signal_monitor(frame)
        self.assertEqual(self.app.sweep_points.get(), "20")
        self.assertEqual(self.app.freq_var.get(), "200000")
        self.app._update_signal_monitor(frame.replace("POINTS=4", "POINTS=8").replace("SOURCE=ADC", "SOURCE=SIM"))
        self.assertEqual(self.app.sweep_points.get(), "8")
        self.assertEqual(self.app.measurement_source.get(), "SIM")

    def test_background_poll_is_idle_only_and_preserves_edit_focus(self):
        self.app.simulate = False
        self.app.ser = Mock(is_open=True)
        self.app._send_cmd = Mock()
        self.app.transaction_busy = True
        self.app._poll_device_status()
        self.app._send_cmd.assert_not_called()
        self.app.transaction_busy = False
        with patch.object(self.app.root, "focus_get", return_value=self.app.root):
            self.app._poll_device_status()
        self.app._send_cmd.assert_called_once_with("STATUS", background=True)
        self.app._send_cmd.reset_mock()
        entry = terminal.ttk.Entry(self.app.root)
        with patch.object(self.app.root, "focus_get", return_value=entry):
            self.app._poll_device_status()
        self.app._send_cmd.assert_not_called()
        entry.destroy()

    def test_background_status_preserves_results_and_completion_message(self):
        sample = terminal.parse_measurement_line(measurement_line())
        self.app.measurements = [sample]
        self.app.measurements_complete = True
        self.app.status_var.set("COMPLETE")
        self.app.transaction_busy = True
        self.app._background_status = True
        self.app.transaction_events.put(("line", "OK:FREQ=250000,MODE=SINE,SOURCE=ADC"))
        self.app.transaction_events.put(("done", None))
        with patch.object(self.app, "_redraw_measurements") as redraw:
            self.app._poll_transaction()
        redraw.assert_not_called()
        self.assertEqual(self.app.current_frequency, 250000)
        self.assertEqual(self.app.status_var.get(), "COMPLETE")
        self.assertEqual(self.app.measurements, [sample])
        self.assertTrue(self.app.measurements_complete)
        self.assertFalse(self.app.transaction_busy)

    def test_background_reply_defers_changes_if_editing_started_after_request(self):
        self.app._background_status = True
        entry = terminal.ttk.Entry(self.app.root)
        frame = ("OK:FREQ=250000,MODE=TRI,STATUS=READY,"
                 "START=100000,STOP=101003,STEP=0,DWELL_MS=0,POINTS=4,TOTAL_MS=201,SOURCE=ADC")
        self.app.sweep_points.set("20")
        with patch.object(self.app.root, "focus_get", return_value=entry):
            self.app._update_signal_monitor(frame)
        self.assertEqual(self.app.sweep_points.get(), "20")
        self.assertIsNone(self.app._device_sweep)
        with patch.object(self.app.root, "focus_get", return_value=self.app.root):
            self.app._update_signal_monitor(frame)
        self.assertEqual(self.app.sweep_points.get(), "4")
        entry.destroy()

    def test_dds_configuration_reuses_window_and_shared_settings(self):
        self.app._open_dds_configuration()
        first_window = self.app.dds_window
        self.app._open_dds_configuration()
        self.assertIs(self.app.dds_window, first_window)
        self.app._send_cmd = Mock()
        self.app.freq_var.set("250000")
        self.app._set_frequency()
        self.app._send_cmd.assert_called_with("SETF 250000")
        self.app.wave_var.set("TRI")
        self.app._set_waveform()
        self.app._send_cmd.assert_called_with("SETW TRI")
        first_window.destroy()
        self.app._open_dds_configuration()
        self.assertIsNot(self.app.dds_window, first_window)

    def test_dds_frequency_minimum_is_independent_of_sweep_minimum(self):
        self.app._send_cmd = Mock()
        for frequency in (1, 99999, 100000, 37500000):
            with self.subTest(frequency=frequency):
                self.app.freq_var.set(str(frequency))
                self.app._set_frequency()
                self.app._send_cmd.assert_called_with(f"SETF {frequency}")
                self.assertEqual(self.app.dds_error_var.get(), "")
        for command in ("SWEEP", "MSWEEP SIM", "MSWEEP_POINTS SIM"):
            with self.subTest(command=command), self.assertRaises(ValueError):
                terminal.sweep_plan(f"{command} 99999 100000 2 50")

    def test_invalid_dds_settings_persist_through_status_and_clear_on_next_submission(self):
        self.app._open_dds_configuration()
        self.app._send_cmd = Mock()
        for frequency in ("0", "-1", "37500001", "", "abc", "1.5"):
            with self.subTest(frequency=frequency):
                self.app.freq_var.set(frequency)
                self.app._set_frequency()
                error = self.app.dds_error_var.get()
                self.assertIn("1..37500000", error)
                self.app._update_signal_monitor("OK:FREQ=100000,MODE=SINE,SOURCE=ADC")
                self.assertEqual(self.app.dds_error_var.get(), error)
        self.app._send_cmd.assert_not_called()
        self.app.freq_var.set("1")
        self.app._set_frequency()
        self.assertEqual(self.app.dds_error_var.get(), "")
        self.app.wave_var.set("INVALID")
        self.app._set_waveform()
        error = self.app.dds_error_var.get()
        self.assertIn("Waveform", error)
        self.app._update_signal_monitor("OK:FREQ=1,MODE=SINE,SOURCE=ADC")
        self.assertEqual(self.app.dds_error_var.get(), error)
        self.app.wave_var.set("TRI")
        self.app._set_waveform()
        self.assertEqual(self.app.dds_error_var.get(), "")

    def test_dds_device_error_survives_transaction_completion_and_polling(self):
        self.app.active_command = "SETF 1"
        self.app.transaction_busy = True
        self.app.transaction_events.put(("line", "ERR:invalid frequency"))
        self.app.transaction_events.put(("done", None))
        self.app._poll_transaction()
        self.assertEqual(self.app.dds_error_var.get(), "ERR:invalid frequency")
        self.app._background_status = True
        with patch.object(self.app.root, "focus_get", return_value=self.app.root):
            self.app._update_signal_monitor("OK:FREQ=100000,MODE=SINE,SOURCE=ADC")
        self.assertEqual(self.app.dds_error_var.get(), "ERR:invalid frequency")

    def test_sweep_set_buttons_save_only_selected_value_without_acquisition(self):
        self.app._send_cmd = Mock()
        self.app.sweep_start.set("200000")
        self.app.sweep_stop.set("30000000")
        self.app._set_sweep_parameter(0)
        self.app._send_cmd.assert_called_once_with("SWEEP_CONFIG SIM 200000 37500000 100 10000")
        self.app.active_command = self.app._send_cmd.call_args.args[0]
        lines = terminal.simulate_command(self.app.active_command)
        self.assertEqual(len(lines), 1)
        self.assertFalse(lines[0].startswith("DATA:"))
        self.app._update_signal_monitor(lines[0])
        self.assertEqual(self.app._applied_sweep, ("200000", "37500000", "100", "10000"))
        self.assertEqual(self.app.sweep_stop.get(), "30000000")
        self.app._set_sweep_parameter(1)
        self.app._send_cmd.assert_called_with("SWEEP_CONFIG SIM 200000 30000000 100 10000")

    def test_source_selection_saves_immediately_without_applying_numeric_drafts(self):
        self.app.simulate = False
        self.app._send_cmd = Mock()
        self.app.sweep_start.set("200000")
        pending = list(self.app.root.winfo_children())
        radios = []
        while pending:
            widget = pending.pop()
            pending.extend(widget.winfo_children())
            if isinstance(widget, terminal.ttk.Radiobutton):
                radios.append(widget)
        simulation = next(widget for widget in radios if widget.cget("value") == "SIM")
        simulation.invoke()
        self.app._send_cmd.assert_called_with("SWEEP_CONFIG SIM 100000 37500000 100 10000")
        self.app.measurement_source.set("ADC")
        self.app._set_sweep_parameter()
        self.app._send_cmd.assert_called_with("SWEEP_CONFIG ADC 100000 37500000 100 10000")
        self.assertEqual(self.app.sweep_start.get(), "200000")

    def test_source_selection_retains_applied_source_until_acknowledged(self):
        self.app.simulate = False
        frame = ("OK:FREQ=100000,MODE=SINE,STATUS=READY,"
                 "START=100000,STOP=37500000,STEP=0,DWELL_MS=0,POINTS=100,TOTAL_MS=10000,SOURCE=ADC")
        self.app._update_signal_monitor(frame)
        self.app._send_cmd = Mock()
        self.app.measurement_source.set("SIM")
        self.app.transaction_busy = True
        self.app._set_sweep_parameter()
        self.assertEqual(self.app.measurement_source.get(), "ADC")
        self.app._send_cmd.assert_not_called()
        self.app.transaction_busy = False
        self.app.measurement_source.set("SIM")
        self.app._set_sweep_parameter()
        self.assertEqual(self.app.measurement_source.get(), "ADC")
        self.app._send_cmd.assert_called_once_with("SWEEP_CONFIG SIM 100000 37500000 100 10000")
        self.app._update_signal_monitor(frame.replace("SOURCE=ADC", "SOURCE=SIM"))
        self.assertEqual(self.app.measurement_source.get(), "SIM")

    def test_sweep_set_rejects_invalid_values_and_busy_transaction(self):
        self.app._send_cmd = Mock()
        for index, variable, value in ((0, self.app.sweep_start, "99999"),
                                       (1, self.app.sweep_stop, "37500001"),
                                       (2, self.app.sweep_points, "0"),
                                       (3, self.app.sweep_total_ms, "180001")):
            with self.subTest(index=index):
                variable.set(value)
                self.app._set_sweep_parameter(index)
                self.assertIn("ERR:", self.app.status_var.get())
        self.app._send_cmd.assert_not_called()
        self.app.transaction_busy = True
        self.app._set_sweep_parameter(0)
        self.assertEqual(self.app.status_var.get(), "ERR: transaction in progress")
        self.app._send_cmd.assert_not_called()

    def test_sweep_set_transaction_preserves_completed_samples_and_other_drafts(self):
        sample = terminal.parse_measurement_line(measurement_line())
        self.app.measurements = [sample]
        self.app.measurements_complete = True
        self.app.sweep_stop.set("30000000")
        self.app._save_history = Mock()
        with patch.object(terminal.threading, "Thread") as thread:
            thread.return_value.start.side_effect = lambda: thread.call_args.kwargs["target"]()
            for index, variable, value in ((3, self.app.sweep_total_ms, "180000"),
                                           (2, self.app.sweep_points, "200")):
                variable.set(value)
                self.app._set_sweep_parameter(index)
                self.assertTrue(self.app.active_command.startswith("SWEEP_CONFIG SIM "))
                self.app._poll_transaction()
                self.assertFalse(self.app.transaction_error)
                self.assertEqual(self.app.measurements, [sample])
                self.assertTrue(self.app.measurements_complete)
                self.assertEqual(self.app.sweep_stop.get(), "30000000")
        self.assertEqual(self.app._applied_sweep, ("100000", "37500000", "200", "180000"))

    def test_source_and_four_fields_build_one_validated_command(self):
        self.app._send_cmd = Mock()
        for source in ("SIM", "ADC"):
            self.app.measurement_source.set(source)
            self.app._measurement_sweep_points()
            self.app._send_cmd.assert_called_with(f"MSWEEP_POINTS {source} 100000 37500000 100 10000")
        self.app._send_cmd.reset_mock()
        self.app.sweep_total_ms.set("999")
        self.app._measurement_sweep_points()
        self.app._send_cmd.assert_not_called()
        self.assertIn("invalid point count or sweep duration", self.app.status_var.get())

    def test_legacy_saved_sweep_populates_points_and_total_time(self):
        self.app._update_signal_monitor(
            "OK:FREQ=100000,MODE=SINE,START=100000,STOP=101003,STEP=334,"
            "DWELL_MS=50,POINTS=0,TOTAL_MS=0,SOURCE=SIM"
        )
        self.assertEqual(self.app.sweep_points.get(), "4")
        self.assertEqual(self.app.sweep_total_ms.get(), "200")

    def test_simulation_sweep_plots_and_csv_export(self):
        command = "MSWEEP_POINTS SIM 100000 37500000 100 10000"
        self.app._save_history = Mock()
        with patch.object(terminal.threading, "Thread") as thread:
            thread.return_value.start.side_effect = lambda: thread.call_args.kwargs["target"]()
            self.app._measurement_sweep_points()
        self.assertEqual(self.app.active_command, command)
        self.app._poll_transaction()
        self.assertTrue(self.app.measurements_complete)
        self.assertEqual(len(self.app.measurements), 100)
        self.assertTrue(self.app.plot_canvas.find_all())
        self.assertTrue(self.app.phase_canvas.find_all())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sweep.csv"
            with patch.object(terminal.filedialog, "asksaveasfilename", return_value=str(path)):
                self.app._export_measurements()
            with path.open(newline="", encoding="utf-8") as exported:
                self.assertEqual(len(list(csv.DictReader(exported))), 100)

    def test_uncalibrated_adc_plots_raw_counts_without_fabricated_conversion(self):
        self.app.measurements = [terminal.parse_measurement_line(measurement_line("ADC", -1, -1, 14))]
        self.app.measurements_complete = True
        with patch.object(self.app, "_draw_plot", wraps=self.app._draw_plot) as draw:
            self.app._redraw_measurements()
        magnitude, phase = draw.call_args_list
        self.assertEqual(magnitude.args[0], [(100000, 1200)])
        self.assertIn("raw (counts)", magnitude.kwargs["label"])
        self.assertIn("calibration unavailable", magnitude.kwargs["label"])
        self.assertIn("range warnings", magnitude.kwargs["label"])
        self.assertEqual(phase.args[0], [(100000, 1200)])
        self.assertEqual(len(phase.args), 4)
        self.assertIsNone(self.app.measurements[0]["GAIN_DB"])
        self.assertTrue(any(self.app.plot_canvas.type(item) == "oval" for item in self.app.plot_canvas.find_all()))
        heading = self.app.plot_canvas.find_all()[0]
        axis = next(item for item in self.app.plot_canvas.find_all() if self.app.plot_canvas.type(item) == "line")
        self.assertGreater(self.app.plot_canvas.coords(axis)[1], self.app.plot_canvas.bbox(heading)[3])

    def test_all_range_warning_samples_keep_nominal_curves_like_hmi(self):
        self.app.measurements = [terminal.parse_measurement_line(measurement_line("ADC", 30, 1800, 12))]
        with patch.object(self.app, "_draw_plot", wraps=self.app._draw_plot) as draw:
            self.app._redraw_measurements()
        magnitude, phase = draw.call_args_list
        self.assertEqual(magnitude.args[0], [(100000, -29.0)])
        self.assertIn("Gain (dB), nominal", magnitude.kwargs["label"])
        self.assertIn("RANGE!", magnitude.kwargs["label"])
        self.assertIn("suspect values shown", magnitude.kwargs["label"])
        self.assertEqual(phase.args[0], [(100000, 0.0)])
        self.assertTrue(phase.args[4])

    def test_mixed_range_flags_keep_all_points_and_persist_warning(self):
        valid = terminal.parse_measurement_line(measurement_line("ADC", 810, 1350, 8))
        suspect = terminal.parse_measurement_line(measurement_line("ADC", 30, 1800, 12, 1))
        suspect["F"] = 200000
        clean = dict(valid, F=300000, SEQ=2)
        self.app.measurements = [valid, suspect, clean]
        with patch.object(self.app, "_draw_plot", wraps=self.app._draw_plot) as draw:
            self.app._redraw_measurements()
        magnitude, phase = draw.call_args_list
        self.assertEqual(magnitude.args[0], [(100000, -3.0), (200000, -29.0), (300000, -3.0)])
        self.assertIn("1 input range warnings", magnitude.kwargs["label"])
        self.assertIn("suspect values shown", magnitude.kwargs["label"])
        self.assertEqual(phase.args[0], [(100000, 45.0), (200000, 0.0), (300000, 45.0)])
        self.assertTrue(phase.args[4])


class CountedSweepTests(unittest.TestCase):
    def test_supported_frequency_boundaries(self):
        for command in ("SWEEP", "MSWEEP SIM", "MSWEEP_POINTS SIM"):
            for arguments in ("99999 100000 2 50", "100000 37500001 2 50"):
                with self.subTest(command=command, arguments=arguments), self.assertRaises(ValueError):
                    terminal.sweep_plan(f"{command} {arguments}")
            spacing = 2 if command == "MSWEEP_POINTS SIM" else 37400000
            frequencies, _, _ = terminal.sweep_plan(f"{command} 100000 37500000 {spacing} 50")
            self.assertEqual(frequencies[0], 100000)
            self.assertLessEqual(frequencies[-1], 37500000)
        frequencies, _, _ = terminal.sweep_plan("MSWEEP_POINTS SIM 100000 37500000 2 50")
        self.assertEqual(frequencies, [100000, 37500000])
        self.assertEqual(terminal.simulate_command("STATUS"), ["OK:FREQ=100000,MODE=SINE,SOURCE=SIM"])
        for frequency in (99999, 37500001):
            with self.subTest(frequency=frequency), self.assertRaises(ValueError):
                terminal.simulate_command("MEAS SIM", frequency_hz=frequency)

    def test_nondivisible_range_includes_both_endpoints(self):
        command = "MSWEEP_POINTS SIM 100000 101003 4 201"
        lines = terminal.simulate_command(command)
        samples = [terminal.parse_measurement_line(line) for line in lines[:-1]]
        self.assertEqual([sample["F"] for sample in samples], [100000, 100334, 100668, 101003])
        self.assertEqual(samples[-1]["T_US"], 210000)
        self.assertEqual(lines[-1], "OK:MSWEEP_POINTS_DONE")
        terminal.validate_measurement_sweep(samples, command)
        samples[-1]["F"] -= 1
        with self.assertRaises(ValueError):
            terminal.validate_measurement_sweep(samples, command)

    def test_single_point_and_invalid_counts_or_timing(self):
        self.assertEqual(terminal.sweep_plan("MSWEEP_POINTS SIM 100000 100000 1 10")[0], [100000])
        for arguments in ("100000 101000 1 100", "100000 100001 3 100", "100000 101000 0 100",
                  "100000 101000 4 39", "101000 100000 4 100", "100000 101000 4 180001",
                  "100000 101000 -4 100", "100000 120000 10001 200000"):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                terminal.sweep_plan(f"MSWEEP_POINTS SIM {arguments}")

    def test_legacy_step_behavior_is_unchanged(self):
        frequencies, duration, dwell = terminal.sweep_plan("MSWEEP SIM 100000 101003 334 50")
        self.assertEqual(list(frequencies), [100000, 100334, 100668, 101002])
        self.assertEqual((duration, dwell), (200, 50))

    def test_three_minute_limit_includes_boundary_for_all_sweep_commands(self):
        self.assertEqual(terminal.sweep_plan("MSWEEP_POINTS SIM 100000 101000 4 180000")[1], 180000)
        for command in ("SWEEP", "MSWEEP SIM", "MSWEEP ADC"):
            with self.subTest(command=command):
                self.assertEqual(terminal.sweep_plan(f"{command} 100000 100002 1 60000")[1], 180000)
                with self.assertRaises(ValueError):
                    terminal.sweep_plan(f"{command} 100000 100003 1 60000")
                with self.assertRaises(ValueError):
                    terminal.sweep_plan(f"{command} 100000 118000 1 1")

    def test_timing_and_completion_sentinel(self):
        total, idle, sentinel = terminal.command_timing("MSWEEP_POINTS ADC 100000 101000 3 9000")
        self.assertGreater(total, 9)
        self.assertGreaterEqual(idle, 6)
        self.assertEqual(sentinel, "OK:MSWEEP_POINTS_DONE")
        response = b"OK:MSWEEP_POINTS_DONE\r\n"
        self.assertEqual(terminal.send_cmd(FakeSerial([response]), "MSWEEP_POINTS SIM 1 3 3 30"),
                         ["OK:MSWEEP_POINTS_DONE"])


class LivePlotTests(unittest.TestCase):
    def make_app(self):
        app = object.__new__(terminal.UARTTerminalApp)
        app.transaction_events = queue.Queue()
        app.transaction_busy = True
        app.transaction_error = False
        app.measurements = []
        app.measurements_complete = False
        app.legacy_points = []
        app.active_command = "MEAS SIM"
        app.root = Mock()
        app.status_var = Mock()
        app._add_output = Mock()
        app._redraw_measurements = Mock()
        return app

    def test_partial_measurement_batch_redraws_before_completion(self):
        app = self.make_app()
        for sequence in range(3):
            app.transaction_events.put(("line", measurement_line(sequence=sequence)))
        app._poll_transaction()
        self.assertEqual(len(app.measurements), 3)
        self.assertTrue(app.transaction_busy)
        self.assertFalse(app.measurements_complete)
        app._redraw_measurements.assert_called_once_with()
        app.root.after.assert_called_once_with(50, app._poll_transaction)

    def test_partial_legacy_sweep_redraws(self):
        app = self.make_app()
        app.transaction_events.put(("line", "f = 1000 Hz | ADC = 123"))
        app._poll_transaction()
        self.assertEqual(app.legacy_points, [(1000, 123)])
        app._redraw_measurements.assert_called_once_with()

    def test_nonmeasurement_messages_do_not_redraw(self):
        app = self.make_app()
        app.transaction_events.put(("line", "OK:PONG"))
        app._poll_transaction()
        app._redraw_measurements.assert_not_called()

    def test_backlog_is_bounded_and_remaining_events_are_processed(self):
        app = self.make_app()
        for sequence in range(130):
            app.transaction_events.put(("line", measurement_line(sequence=sequence)))
        app._poll_transaction()
        self.assertEqual(len(app.measurements), 128)
        self.assertEqual(app.transaction_events.qsize(), 2)
        app._redraw_measurements.assert_called_once_with()
        app._poll_transaction()
        self.assertEqual(len(app.measurements), 130)
        self.assertTrue(app.transaction_events.empty())
        self.assertEqual(app._redraw_measurements.call_count, 2)

    def test_completed_measurement_redraws_once_and_allows_export(self):
        app = self.make_app()
        app.transaction_events.put(("line", measurement_line()))
        app.transaction_events.put(("line", "OK:MEAS_DONE"))
        app.transaction_events.put(("done", None))
        app._poll_transaction()
        self.assertTrue(app.measurements_complete)
        self.assertFalse(app.transaction_busy)
        app.status_var.set.assert_called_once_with("COMPLETE")
        app._redraw_measurements.assert_called_once_with()


class TransactionTests(unittest.TestCase):
    def test_first_last_and_completion_are_collected(self):
        lines = [measurement_line(sequence=0), measurement_line(sequence=1), "OK:MSWEEP_DONE"]
        port = FakeSerial([(line + "\r\n").encode() for line in lines])
        observed = []
        self.assertEqual(terminal.send_cmd(port, "MSWEEP SIM 100000 200000 100000 50", observed.append), lines)
        self.assertEqual(observed, lines)
        self.assertEqual(port.written, [b"MSWEEP SIM 100000 200000 100000 50\r\n"])

    def test_fragmented_response(self):
        port = FakeSerial([b"f = 1000 Hz | AD", b"C = 123\r", b"\nOK:SW", b"EEP_DONE\r\n"])
        self.assertEqual(terminal.send_cmd(port, "SWEEP 1000 1000 1000 50"),
                         ["f = 1000 Hz | ADC = 123", "OK:SWEEP_DONE"])

    def test_large_batch_of_short_lines(self):
        lines = [measurement_line(sequence=index) for index in range(40)] + ["OK:MSWEEP_DONE"]
        payload = ("\n".join(lines) + "\n").encode()
        self.assertGreater(len(payload), 4096)
        self.assertEqual(terminal.send_cmd(FakeSerial([payload]), "MSWEEP SIM 100000 4000000 100000 50"), lines)

    def test_sweep_longer_than_eight_seconds(self):
        clock = [0.0]
        port = FakeSerial([b"f = 1000 Hz | ADC = 123\r\n", b"f = 2000 Hz | ADC = 124\r\n", b"OK:SWEEP_DONE\r\n"])
        original_read = port.read
        def delayed_read(size):
            clock[0] += 5.0
            return original_read(size)
        port.read = delayed_read
        with patch.object(terminal.time, "monotonic", side_effect=lambda: clock[0]):
            lines = terminal.send_cmd(port, "SWEEP 100000 101000 1000 5000")
        self.assertEqual(lines[-1], "OK:SWEEP_DONE")
        self.assertGreater(clock[0], 8.0)

    def test_missing_completion_times_out(self):
        clock = iter(range(100))
        with patch.object(terminal.time, "monotonic", side_effect=lambda: next(clock)), self.assertRaises(TimeoutError):
            terminal.send_cmd(FakeSerial([b"f = 1000 Hz | ADC = 123\n"]), "SWEEP 1000 1000 1000 50")

    def test_error_finishes_without_fake_completion(self):
        self.assertEqual(terminal.send_cmd(FakeSerial([b"ERR:invalid sweep\n"]), "MSWEEP SIM -1 0 0 0"),
                         ["ERR:invalid sweep"])

    def test_regular_commands_and_single_measurement(self):
        self.assertEqual(terminal.send_cmd(FakeSerial([b"OK:PONG\n"]), "PING"), ["OK:PONG"])
        lines = [measurement_line(), "OK:MEAS_DONE"]
        self.assertEqual(terminal.send_cmd(FakeSerial([("\n".join(lines) + "\n").encode()]), "MEAS SIM"), lines)

    def test_idle_timeout_and_disconnect(self):
        clock = iter(range(100))
        with patch.object(terminal.time, "monotonic", side_effect=lambda: next(clock)), self.assertRaises(TimeoutError):
            terminal.send_cmd(FakeSerial([]), "MSWEEP SIM 1000 100000 1000 50")
        port = FakeSerial([])
        with patch.object(port, "read", side_effect=terminal.serial.SerialException("disconnected")), self.assertRaises(terminal.serial.SerialException):
            terminal.send_cmd(port, "PING")


if __name__ == "__main__":
    unittest.main()