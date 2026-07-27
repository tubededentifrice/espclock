import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnose_esp import DiagnosticState, evaluate, parse_diag_line, update_state


class DiagnosticParserTests(unittest.TestCase):
    def test_parses_machine_readable_line(self):
        fields = parse_diag_line(
            "ESP_DIAG event=wifi_ap started=yes "
            "ssid=ESPClock-RadioTest-1234 channel=1"
        )
        self.assertEqual(fields["event"], "wifi_ap")
        self.assertEqual(fields["started"], "yes")
        self.assertEqual(fields["ssid"], "ESPClock-RadioTest-1234")

    def test_ignores_rom_and_malformed_lines(self):
        self.assertIsNone(parse_diag_line("ESP-ROM:esp32c3-api1-20210207"))
        self.assertIsNone(parse_diag_line("ESP_DIAG no-event=yes"))

    def test_updates_automatic_state(self):
        state = DiagnosticState()
        lines = [
            "ESP_DIAG event=boot result=pass",
            "ESP_DIAG event=wifi_ap started=yes ssid=ESPClock-RadioTest-1234",
            "ESP_DIAG event=wifi_scan complete=yes networks=8 strongest_rssi=-42",
            "ESP_DIAG event=ble_ready started=yes name=KidsClock-1234",
            "ESP_DIAG event=heartbeat free_heap=190000 ble_round_trip=no",
            "ESP_DIAG event=heartbeat free_heap=189000 ble_round_trip=no",
            "ESP_DIAG event=heartbeat free_heap=188000 ble_round_trip=yes",
        ]
        for line in lines:
            update_state(state, parse_diag_line(line))
        self.assertTrue(state.boot_passed)
        self.assertEqual(state.wifi_networks, 8)
        self.assertEqual(state.minimum_free_heap, 188000)
        self.assertEqual(state.heartbeat_count, 3)
        self.assertTrue(state.ble_round_trip)


class DiagnosticEvaluationTests(unittest.TestCase):
    def healthy_state(self):
        return DiagnosticState(
            boot_count=1,
            boot_passed=True,
            wifi_ap_started=True,
            wifi_scan_complete=True,
            wifi_networks=5,
            ble_started=True,
            heartbeat_count=3,
            ble_round_trip=True,
        )

    def test_full_external_evidence_passes(self):
        outcome, failures, incomplete = evaluate(
            self.healthy_state(), "yes", "yes"
        )
        self.assertEqual(outcome, "PASS")
        self.assertEqual(failures, [])
        self.assertEqual(incomplete, [])

    def test_api_success_without_external_evidence_is_incomplete(self):
        outcome, failures, incomplete = evaluate(
            self.healthy_state(), "skip", "skip"
        )
        self.assertEqual(outcome, "INCOMPLETE")
        self.assertEqual(failures, [])
        self.assertEqual(len(incomplete), 2)

    def test_missing_wifi_beacon_fails(self):
        outcome, failures, _ = evaluate(self.healthy_state(), "no", "yes")
        self.assertEqual(outcome, "FAIL")
        self.assertTrue(any("Wi-Fi SSID" in failure for failure in failures))

    def test_reported_iphone_success_requires_firmware_round_trip(self):
        state = self.healthy_state()
        state.ble_round_trip = False
        outcome, failures, _ = evaluate(state, "yes", "yes")
        self.assertEqual(outcome, "FAIL")
        self.assertTrue(any("encrypted BLE write" in failure for failure in failures))

    def test_manual_upload_recovery_fails_qualification(self):
        state = self.healthy_state()
        state.upload_recovery_used = True
        outcome, failures, _ = evaluate(state, "yes", "yes")
        self.assertEqual(outcome, "FAIL")
        self.assertTrue(any("manual BOOT" in failure for failure in failures))

    def test_no_erase_cannot_produce_qualification_pass(self):
        outcome, failures, incomplete = evaluate(
            self.healthy_state(), "yes", "yes", clean_flash_erased=False
        )
        self.assertEqual(outcome, "INCOMPLETE")
        self.assertEqual(failures, [])
        self.assertIn("clean-flash erase was skipped", incomplete)


if __name__ == "__main__":
    unittest.main()
