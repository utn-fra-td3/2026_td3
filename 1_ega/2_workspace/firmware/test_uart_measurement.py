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
        frame = ("OK:FREQ=1000,MODE=SINE,STATUS=READY,STORAGE=READY,RESTORED=1,"
                 "START=1000,STOP=2003,STEP=0,DWELL_MS=0,POINTS=4,TOTAL_MS=201,SOURCE=ADC")
        app._update_signal_monitor(frame)
        self.assertEqual((app.current_frequency, app.current_wave), (1000, "SINE"))
        app.freq_var.set.assert_called_once_with("1000")
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


class CountedSweepTests(unittest.TestCase):
    def test_nondivisible_range_includes_both_endpoints(self):
        command = "MSWEEP_POINTS SIM 1000 2003 4 201"
        lines = terminal.simulate_command(command)
        samples = [terminal.parse_measurement_line(line) for line in lines[:-1]]
        self.assertEqual([sample["F"] for sample in samples], [1000, 1334, 1668, 2003])
        self.assertEqual(samples[-1]["T_US"], 210000)
        self.assertEqual(lines[-1], "OK:MSWEEP_POINTS_DONE")
        terminal.validate_measurement_sweep(samples, command)
        samples[-1]["F"] -= 1
        with self.assertRaises(ValueError):
            terminal.validate_measurement_sweep(samples, command)

    def test_single_point_and_invalid_counts_or_timing(self):
        self.assertEqual(terminal.sweep_plan("MSWEEP_POINTS SIM 1000 1000 1 10")[0], [1000])
        for arguments in ("1000 2000 1 100", "1000 1001 3 100", "1000 2000 0 100",
                          "1000 2000 4 39", "2000 1000 4 100", "1000 2000 4 3600001",
                          "1000 2000 -4 100", "1 20000 10001 200000"):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                terminal.sweep_plan(f"MSWEEP_POINTS SIM {arguments}")

    def test_legacy_step_behavior_is_unchanged(self):
        frequencies, duration, dwell = terminal.sweep_plan("MSWEEP SIM 1000 2003 334 50")
        self.assertEqual(list(frequencies), [1000, 1334, 1668, 2002])
        self.assertEqual((duration, dwell), (200, 50))

    def test_timing_and_completion_sentinel(self):
        total, idle, sentinel = terminal.command_timing("MSWEEP_POINTS ADC 1000 2000 3 9000")
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
            lines = terminal.send_cmd(port, "SWEEP 1000 2000 1000 5000")
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