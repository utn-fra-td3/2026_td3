import unittest

from ega_web import (
    EgaDevice,
    HTML,
    parse_status,
    select_command_response,
    translate_command,
    validate_command,
)


class CommandValidationTests(unittest.TestCase):
    def test_translates_public_commands_to_final_firmware_protocol(self):
        expected = {
            "GET": "GET",
            "SAVE": "SAVE",
            "SET P 5": "V 0 5",
            "SET N 12.5": "V 1 12.5",
            "SET ILP 0.50": "IL 0 0.50",
            "SET ILN 0.55": "IL 1 0.55",
            "SET VLP 35": "VL 0 35",
            "SET VLN 36": "VL 1 36",
            "OUT P ON": "OUT 0 ON",
            "OUT N OFF": "OUT 1 OFF",
            "RESET P": "RST 0",
            "RESET N": "RST 1",
        }

        for public_command, wire_command in expected.items():
            with self.subTest(command=public_command):
                self.assertEqual(translate_command(public_command), wire_command)

    def test_keeps_existing_commands_compatible(self):
        accepted = (
            "GET",
            "SAVE",
            "SET P 5",
            "SET N 30",
            "OUT P ON",
            "OUT N OFF",
        )

        for command in accepted:
            with self.subTest(command=command):
                self.assertTrue(validate_command(command))

    def test_accepts_limit_and_reset_commands(self):
        accepted = (
            "SET ILP 0.10",
            "SET ILN 0.75",
            "SET P 0.0",
            "SET N 30.0",
            "SET VLP 5.0",
            "SET VLN 40.0",
            "RESET P",
            "RESET N",
        )

        for command in accepted:
            with self.subTest(command=command):
                self.assertTrue(validate_command(command))

    def test_rejects_values_outside_web_ranges(self):
        rejected = (
            "SET P -0.5",
            "SET N 30.5",
            "SET ILP 0.09",
            "SET ILN 0.76",
            "SET ILP 0.47",
            "SET ILN 0.72",
            "SET VLP 4.5",
            "SET VLN 40.5",
            "RESET X",
        )

        for command in rejected:
            with self.subTest(command=command):
                self.assertFalse(validate_command(command))

    def test_rejects_non_finite_and_extra_arguments(self):
        rejected = ("SET ILP nan", "SET VLP inf", "RESET P NOW", "GET NOW")

        for command in rejected:
            with self.subTest(command=command):
                self.assertFalse(validate_command(command))


class TelemetryTests(unittest.TestCase):
    def test_response_selector_keeps_protection_notifications_separate(self):
        response, notifications = select_command_response(
            ["PROT 0 OC", "PROT 1 OV", "OK QUEUED V 0 5"]
        )
        self.assertEqual(response, "OK QUEUED V 0 5")
        self.assertEqual(notifications, ["PROT 0 OC", "PROT 1 OV"])

        with self.assertRaisesRegex(RuntimeError, "ERR RANGE"):
            select_command_response(["PROT 0 OC", "ERR RANGE"])
        with self.assertRaises(TimeoutError):
            select_command_response(["PROT 0 OC"])

    def test_parses_legacy_telemetry_without_voltage_limits(self):
        response = "OK OCP=0 OCN=0 ILP=0.50 ILN=0.50"

        status = parse_status(response)

        self.assertNotIn("VLP", status)
        self.assertNotIn("VLN", status)

    def test_parses_extended_limits_and_faults(self):
        response = (
            "OK VI=10.04 VP=0.02 VN=-0.03 IP=0.004 IN=0.000 "
            "SP=10.00 SN=-10.00 EP=0 EN=0 OCP=1 OCN=0 OVP=0 OVN=0 "
            "ILP=0.50 ILN=0.50 VLP=35.0 VLN=35.0"
        )

        status = parse_status(response)

        self.assertEqual(status["OCP"], "1")
        self.assertEqual(status["VLP"], "35.0")
        self.assertEqual(status["VLN"], "35.0")

    def test_parses_final_firmware_diagnostics(self):
        response = (
            "OK VI=10.00 VP=5.00 VN=-5.00 IP=0.220 IN=0.220 "
            "SP=5.00 SN=-5.00 EP=1 EN=1 OCP=0 OCN=0 OVP=0 OVN=0 "
            "ILP=0.50 ILN=0.50 VLP=35.00 VLN=35.00 "
            "VPV=1 VNV=1 RP=1 RN=1 DP=25.27 DN=26.06"
        )

        status = parse_status(response)

        self.assertEqual(status["VPV"], "1")
        self.assertEqual(status["VNV"], "1")
        self.assertEqual(status["DP"], "25.27")
        self.assertEqual(status["DN"], "26.06")

    def test_fake_transport_uses_wire_protocol_and_skips_async_protection(self):
        sent = []

        def fake_transport(command):
            sent.append(command)
            return ["PROT 0 OC", "OK QUEUED V 0 5"]

        device = EgaDevice("fake", transport=fake_transport)

        response = device.command("SET P 5")

        self.assertEqual(sent, ["V 0 5"])
        self.assertEqual(response, "OK QUEUED SET P 5")
        self.assertEqual(device.last_notifications, ["PROT 0 OC"])

    def test_page_has_per_branch_fault_status_and_limit_controls(self):
        for element_id in (
            'id="status-p"',
            'id="status-n"',
            'id="ilp-minus"',
            'id="ilp-plus"',
            'id="iln-minus"',
            'id="iln-plus"',
            'id="vlp-minus"',
            'id="vlp-plus"',
            'id="vln-minus"',
            'id="vln-plus"',
            'id="reset-p"',
            'id="reset-n"',
        ):
            with self.subTest(element_id=element_id):
                self.assertIn(element_id, HTML)

        self.assertNotIn('class="limit-controls"', HTML)

    def test_configured_voltage_is_shown_before_measured_voltage(self):
        self.assertLess(HTML.index('id="sp"'), HTML.index('id="vp"'))
        self.assertLess(HTML.index('id="sn"'), HTML.index('id="vn"'))
        self.assertNotIn("<small>Consignas</small>", HTML)

    def test_page_sends_control_then_gets_new_telemetry(self):
        start = HTML.index("async function sendCommand(command){")
        end = HTML.index("function setVoltage(branch){", start)
        send_command = HTML[start:end]
        self.assertIn("busy=true", send_command)
        self.assertIn("await request(command);await request('GET',true)", send_command)
        self.assertLess(
            send_command.index("await request(command)"),
            send_command.index("await request('GET',true)"),
        )
        self.assertIn("finally{busy=false}", send_command)
        self.assertIn("async function refresh(){if(busy)return", HTML)
        self.assertIn("setInterval(refresh,1000)", HTML)

    def test_limit_click_sends_one_command_immediately(self):
        start = HTML.index("function adjustLimit(key,delta){")
        end = HTML.index("refresh();setInterval(refresh,1000)", start)
        adjust_limit = HTML[start:end]
        self.assertIn("sendCommand(`SET ${key} ${next.toFixed(decimals)}`)", adjust_limit)
        self.assertNotIn("setTimeout(", adjust_limit)
        self.assertNotIn("pendingLimitValues", adjust_limit)


if __name__ == "__main__":
    unittest.main()
