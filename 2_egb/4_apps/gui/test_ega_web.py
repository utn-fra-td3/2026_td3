import unittest

from ega_web import HTML, parse_status, validate_command


class CommandValidationTests(unittest.TestCase):
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

    def test_rejects_limits_outside_firmware_ranges(self):
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

    def test_page_coalesces_limit_clicks_without_command_queues_or_extra_get(self):
        self.assertIn("LIMIT_SEND_DELAY_MS", HTML)
        self.assertIn("scheduleLimitCommand", HTML)
        self.assertIn("clearTimeout(limitTimers.get(key))", HTML)
        self.assertNotIn("await request(command);await request('GET',true)", HTML)
        self.assertNotIn("controlChain", HTML)
        self.assertNotIn("limitInFlight", HTML)
        self.assertIn(
            "if(pendingLimitValues.get(key)===pending)pendingLimitValues.delete(key)",
            HTML,
        )


if __name__ == "__main__":
    unittest.main()
