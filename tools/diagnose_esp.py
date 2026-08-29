#!/usr/bin/env python3
"""Destructive, over-the-air acceptance test for ESPClock ESP32 boards."""

from __future__ import annotations

import argparse
import json
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
from pathlib import Path
import serial
from serial.tools import list_ports


ROOT = Path(__file__).resolve().parents[1]
DIAGNOSTIC_ENVS = {
    "c3": "esp32-c3-radio-diagnostic",
    "classic": "esp32-devkit-radio-diagnostic",
}
ESPRESSIF_USB_VID = 0x303A
KNOWN_USB_SERIAL_VIDS = {0x0403, 0x10C4, 0x1A86, ESPRESSIF_USB_VID}


@dataclass
class DiagnosticState:
    upload_recovery_used: bool = False
    boot_count: int = 0
    boot_passed: bool = False
    wifi_ap_started: bool = False
    wifi_ssid: str | None = None
    wifi_scan_complete: bool = False
    wifi_networks: int | None = None
    wifi_strongest_rssi: int | None = None
    ble_started: bool = False
    ble_name: str | None = None
    heartbeat_count: int = 0
    minimum_free_heap: int | None = None
    ble_round_trip: bool = False
    serial_errors: list[str] = field(default_factory=list)


@dataclass
class DiagnosticReport:
    started_at: str
    board: str
    environment: str
    port: str
    destructive_erase: bool
    automatic: DiagnosticState
    external_wifi: str
    iphone_ble: str
    outcome: str
    failures: list[str]
    incomplete: list[str]


def parse_diag_line(line: str) -> dict[str, str] | None:
    """Parse one machine-readable firmware line."""
    if not line.startswith("ESP_DIAG "):
        return None
    fields: dict[str, str] = {}
    try:
        tokens = shlex.split(line[len("ESP_DIAG ") :])
    except ValueError:
        return None
    for token in tokens:
        key, separator, value = token.partition("=")
        if separator and key:
            fields[key] = value
    return fields if "event" in fields else None


def _as_int(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def update_state(state: DiagnosticState, fields: dict[str, str]) -> None:
    event = fields.get("event")
    if event == "boot":
        state.boot_count += 1
        state.boot_passed = fields.get("result") == "pass"
    elif event == "wifi_ap":
        state.wifi_ap_started = fields.get("started") == "yes"
        state.wifi_ssid = fields.get("ssid")
    elif event == "wifi_scan":
        state.wifi_scan_complete = fields.get("complete") == "yes"
        state.wifi_networks = _as_int(fields.get("networks"))
        state.wifi_strongest_rssi = _as_int(fields.get("strongest_rssi"))
    elif event == "ble_ready":
        state.ble_started = fields.get("started") == "yes"
        state.ble_name = fields.get("name")
    elif event == "ble_time_write" and fields.get("result") == "pass":
        state.ble_round_trip = True
    elif event == "heartbeat":
        state.heartbeat_count += 1
        free_heap = _as_int(fields.get("free_heap"))
        if free_heap is not None:
            if state.minimum_free_heap is None:
                state.minimum_free_heap = free_heap
            else:
                state.minimum_free_heap = min(state.minimum_free_heap, free_heap)
        state.ble_round_trip |= fields.get("ble_round_trip") == "yes"


def evaluate(
    state: DiagnosticState,
    external_wifi: str,
    iphone_ble: str,
    *,
    allow_empty_wifi_scan: bool = False,
    clean_flash_erased: bool = True,
) -> tuple[str, list[str], list[str]]:
    failures: list[str] = []
    incomplete: list[str] = []
    if state.upload_recovery_used:
        failures.append("erase/upload required a retry or manual BOOT recovery")
    if not clean_flash_erased:
        incomplete.append("clean-flash erase was skipped")
    if state.boot_count != 1 or not state.boot_passed:
        failures.append("diagnostic firmware did not produce one stable boot")
    if not state.wifi_ap_started:
        failures.append("ESP32 Wi-Fi SoftAP did not start")
    if not state.wifi_scan_complete:
        failures.append("ESP32 Wi-Fi receive scan did not complete")
    elif (state.wifi_networks or 0) < 1:
        message = "ESP32 Wi-Fi receive scan found no surrounding networks"
        (incomplete if allow_empty_wifi_scan else failures).append(message)
    if not state.ble_started:
        failures.append("ESP32 BLE advertising did not start")
    if state.heartbeat_count < 3:
        failures.append("fewer than three stable diagnostic heartbeats were read")
    if state.serial_errors:
        failures.extend(state.serial_errors)

    if external_wifi == "no":
        failures.append("external device could not see the diagnostic Wi-Fi SSID")
    elif external_wifi == "skip":
        incomplete.append("external Wi-Fi transmission was not observed")

    if iphone_ble == "no":
        failures.append("iPhone onboarding/time acknowledgement failed")
    elif iphone_ble == "yes" and not state.ble_round_trip:
        failures.append(
            "iPhone was reported successful but no encrypted BLE write was logged"
        )
    elif iphone_ble == "skip":
        incomplete.append("encrypted iPhone BLE round trip was not tested")

    if failures:
        return "FAIL", failures, incomplete
    if incomplete:
        return "INCOMPLETE", failures, incomplete
    return "PASS", failures, incomplete


def _candidate_ports() -> list:
    candidates = []
    for port in list_ports.comports():
        description = " ".join(
            part
            for part in (port.description, port.manufacturer, port.product)
            if part
        ).lower()
        if port.vid in KNOWN_USB_SERIAL_VIDS or any(
            marker in description
            for marker in ("esp32", "cp210", "ch340", "usb serial", "usb jtag")
        ):
            candidates.append(port)
    return candidates


def choose_port(requested: str | None) -> object:
    if requested:
        matching = [port for port in list_ports.comports() if port.device == requested]
        if not matching:
            raise RuntimeError(f"serial port is not present: {requested}")
        return matching[0]
    candidates = _candidate_ports()
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise RuntimeError("no ESP32 or USB-serial port was found")
    devices = ", ".join(port.device for port in candidates)
    raise RuntimeError(f"multiple possible ESP32 ports found ({devices}); use --port")


def infer_board(port: object, requested: str) -> str:
    if requested != "auto":
        return requested
    description = " ".join(
        part
        for part in (port.description, port.manufacturer, port.product)
        if part
    ).lower()
    if (
        port.vid == ESPRESSIF_USB_VID
        or "jtag" in description
        or "esp32-c3" in description
    ):
        return "c3"
    if any(marker in description for marker in ("cp210", "ch340", "usb serial")):
        return "classic"
    raise RuntimeError("could not infer C3 versus classic ESP32; use --board")


def _recovery_text(board: str) -> str:
    if board == "c3":
        return (
            "Hold BOOT, briefly press RESET (or reconnect USB while holding BOOT), "
            "then release BOOT."
        )
    return "Hold BOOT, tap EN/RESET, then release BOOT."


def run_platformio_step(
    arguments: list[str],
    *,
    board: str,
    interactive: bool,
) -> bool:
    recovery_used = False
    while True:
        result = subprocess.run(arguments, cwd=ROOT, check=False)
        if result.returncode == 0:
            return recovery_used
        if not interactive:
            raise RuntimeError(f"command failed with exit code {result.returncode}")
        recovery_used = True
        print(
            "\nThe PlatformIO command failed. Before touching BOOT, verify that "
            "this process has direct serial-device access and that no serial "
            "monitor owns the port. If the port is still listed but PlatformIO "
            "could not open it, restart with direct device access; that is a "
            "host failure, not an ESP failure."
        )
        print(
            "Only when esptool can open the port but cannot connect, use this "
            f"upload recovery: {_recovery_text(board)}"
        )
        response = input("Press Enter to retry, or type q to stop: ").strip().lower()
        if response == "q":
            raise RuntimeError("operator stopped after upload failure")


def wait_for_port(previous_device: str, timeout: float = 20.0) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ports = list_ports.comports()
        if any(port.device == previous_device for port in ports):
            return previous_device
        candidates = _candidate_ports()
        if len(candidates) == 1:
            return candidates[0].device
        time.sleep(0.25)
    raise RuntimeError("diagnostic serial port did not reappear after flashing")


class SerialMonitor:
    """Keep one serial session alive without resetting native-USB C3 boards."""

    def __init__(self, device: str):
        self.device = device
        self.connection: serial.Serial | None = None

    def _open(self, timeout: float) -> None:
        self.device = wait_for_port(self.device, timeout=timeout)
        connection = serial.Serial()
        connection.port = self.device
        connection.baudrate = 115200
        connection.timeout = 0.25
        connection.dsrdtr = False
        connection.rtscts = False
        # Configure the inactive modem-control levels before opening. Opening
        # with pyserial's defaults can reset a native-USB ESP32-C3.
        connection.dtr = False
        connection.rts = False
        connection.open()
        self.connection = connection

    def read(
        self,
        seconds: float,
        state: DiagnosticState,
        *,
        echo: bool = True,
    ) -> None:
        deadline = time.monotonic() + seconds
        received_line = False
        while time.monotonic() < deadline:
            if self.connection is None:
                try:
                    self._open(
                        min(2.0, max(0.25, deadline - time.monotonic()))
                    )
                except (RuntimeError, serial.SerialException, OSError):
                    time.sleep(0.25)
                    continue
            try:
                raw = self.connection.readline()
            except (serial.SerialException, OSError):
                self.close()
                time.sleep(0.25)
                continue
            if not raw:
                continue
            received_line = True
            line = raw.decode("utf-8", errors="replace").strip()
            if echo:
                print(line)
            fields = parse_diag_line(line)
            if fields is not None:
                update_state(state, fields)
        if not received_line:
            state.serial_errors.append("no serial output was received")

    def close(self) -> None:
        if self.connection is not None:
            self.connection.close()
            self.connection = None


def ask_observation(prompt: str) -> str:
    while True:
        response = input(f"{prompt} [y/n/s]: ").strip().lower()
        if response in {"y", "yes"}:
            return "yes"
        if response in {"n", "no"}:
            return "no"
        if response in {"s", "skip"}:
            return "skip"
        print("Enter y for yes, n for no, or s to leave the result incomplete.")


def default_report_path() -> Path:
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("/private/tmp/espclock-diagnostics") / f"radio-{stamp}.json"


def write_report(report: DiagnosticReport, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(asdict(report), indent=2) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Erase an ESP32, install the ESPClock radio diagnostic, and perform "
            "Wi-Fi/BLE acceptance checks."
        )
    )
    parser.add_argument("--port", help="serial device; auto-detected when unique")
    parser.add_argument(
        "--board",
        choices=("auto", "c3", "classic"),
        default="auto",
        help="board family (default: infer from USB interface)",
    )
    parser.add_argument(
        "--no-erase",
        action="store_true",
        help="developer shortcut: do not erase all flash before uploading",
    )
    parser.add_argument(
        "--non-interactive",
        action="store_true",
        help="skip external observations and never wait for upload recovery",
    )
    parser.add_argument(
        "--external-wifi",
        choices=("ask", "yes", "no", "skip"),
        default="ask",
        help="pre-answer whether another device sees the diagnostic SSID",
    )
    parser.add_argument(
        "--iphone",
        choices=("ask", "skip"),
        default="ask",
        help="run or skip the physical-iPhone BLE round trip",
    )
    parser.add_argument(
        "--allow-empty-wifi-scan",
        action="store_true",
        help="mark a zero-network receive scan incomplete instead of failed",
    )
    parser.add_argument(
        "--serial-seconds",
        type=float,
        default=12.0,
        help="initial serial observation duration (default: 12)",
    )
    parser.add_argument("--report", type=Path, help="JSON report output path")
    parser.add_argument(
        "--restore-env",
        help="optional normal PlatformIO environment to upload after reporting",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    interactive = not args.non_interactive
    if not interactive:
        if args.external_wifi == "ask":
            args.external_wifi = "skip"
        if args.iphone == "ask":
            args.iphone = "skip"
    if shutil.which("pio") is None:
        print(
            "error: run this through `uv run --locked tools/diagnose_esp.py`",
            file=sys.stderr,
        )
        return 2

    policy_check = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "check_dependency_age.py")],
        cwd=ROOT,
        check=False,
    )
    if policy_check.returncode != 0:
        print("error: the dependency policy check failed", file=sys.stderr)
        return 2

    try:
        selected_port = choose_port(args.port)
        board = infer_board(selected_port, args.board)
        environment = DIAGNOSTIC_ENVS[board]
        device = selected_port.device
        print(
            "\nESPClock destructive board acceptance\n"
            f"  board: {board}\n"
            f"  port:  {device}\n"
            f"  image: {environment}\n"
            "The application firmware and stored configuration/bonds may be erased. "
            "This workflow has standing authorization and does not ask for confirmation.\n"
        )

        if not args.no_erase:
            state_upload_recovery = run_platformio_step(
                [
                    "pio",
                    "run",
                    "-e",
                    environment,
                    "-t",
                    "erase",
                    "--upload-port",
                    device,
                ],
                board=board,
                interactive=interactive,
            )
            device = wait_for_port(device)
        else:
            state_upload_recovery = False

        state_upload_recovery |= run_platformio_step(
            [
                "pio",
                "run",
                "-e",
                environment,
                "-t",
                "upload",
                "--upload-port",
                device,
            ],
            board=board,
            interactive=interactive,
        )
        device = wait_for_port(device)

        state = DiagnosticState(upload_recovery_used=state_upload_recovery)
        print(f"\nReading diagnostic output from {device}…")
        monitor = SerialMonitor(device)
        monitor.read(args.serial_seconds, state)
        if interactive and not state.boot_passed:
            state_upload_recovery = True
            print(
                "\nNo diagnostic application boot was captured. Release BOOT, "
                "tap RESET once without holding BOOT, and wait for the USB port "
                "to return."
            )
            input("Press Enter after tapping RESET: ")
            state = DiagnosticState(upload_recovery_used=state_upload_recovery)
            monitor.close()
            monitor.read(args.serial_seconds, state)

        external_wifi = args.external_wifi
        if external_wifi == "ask":
            external_wifi = ask_observation(
                f"On a phone or computer, is Wi-Fi network "
                f"`{state.wifi_ssid or 'ESPClock-RadioTest-xxxx'}` visible?"
            )

        iphone_ble = "skip"
        if args.iphone == "ask":
            print(
                "\nOn a physical iPhone, use Add ESPClock for a new board or "
                "Sync Now for an already authorized board. Select "
                f"{state.ble_name or 'ESPClock-xxxx'} and wait for the time "
                "acknowledgement."
            )
            iphone_ble = ask_observation(
                "Did onboarding finish and did the app report a successful sync?"
            )
            monitor.read(4.0, state)
        device = monitor.device
        monitor.close()

        outcome, failures, incomplete = evaluate(
            state,
            external_wifi,
            iphone_ble,
            allow_empty_wifi_scan=args.allow_empty_wifi_scan,
            clean_flash_erased=not args.no_erase,
        )
        report = DiagnosticReport(
            started_at=datetime.now(UTC).isoformat(),
            board=board,
            environment=environment,
            port=device,
            destructive_erase=not args.no_erase,
            automatic=state,
            external_wifi=external_wifi,
            iphone_ble=iphone_ble,
            outcome=outcome,
            failures=failures,
            incomplete=incomplete,
        )
        report_path = args.report or default_report_path()
        write_report(report, report_path)

        print(f"\nRESULT: {outcome}")
        for failure in failures:
            print(f"  FAIL: {failure}")
        for item in incomplete:
            print(f"  NOT TESTED: {item}")
        print(f"  report: {report_path}")

        if args.restore_env:
            print(f"\nRestoring firmware environment {args.restore_env}…")
            run_platformio_step(
                [
                    "pio",
                    "run",
                    "-e",
                    args.restore_env,
                    "-t",
                    "upload",
                    "--upload-port",
                    device,
                ],
                board=board,
                interactive=interactive,
            )
        return 0 if outcome == "PASS" else (1 if outcome == "FAIL" else 2)
    except (RuntimeError, serial.SerialException, OSError) as error:
        print(f"\nERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
