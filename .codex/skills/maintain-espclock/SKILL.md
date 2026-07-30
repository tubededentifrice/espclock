---
name: maintain-espclock
description: Maintain, extend, diagnose, or review the ESPClock firmware, iPhone companion, hardware wiring, provisioning flow, tests, and build documentation. Use for changes involving PlatformIO, classic ESP32 or ESP32-C3, Swift/iOS, AccessorySetupKit, Core Bluetooth, TM1637 or SSD1306 displays, BH1750, DS3231, BLE time sync, captive-portal phone sync, open-Wi-Fi/NTP fallback, automatic brightness, protoboard assembly, or flash/build readiness in this repository.
---

# Maintain ESPClock

## Load project context

Read `AGENTS.md`, `README.md`, `docs/design-review.md`, and `docs/hardware-research.md`. Inspect `git status` before editing. Treat the checked-in pin map, protocol, and safety notes as a single contract across code and documentation.

## Maintain the guidance

Apply `AGENTS.md`'s guidance-maintenance policy to this skill and every other
repo-local skill used. Without prompting, improve a task-relevant skill when
using it exposes stale, incorrect, unclear, duplicated, overly long, or missing
guidance.

Keep this file to essential procedure and routing. Move detailed or volatile
facts to an authoritative project document or directly linked reference, and
prefer replacement or consolidation over added rules. Verify affected links,
commands, frontmatter, and `agents/openai.yaml`; report needed improvements to
external or read-only skills.

## Classify the change

- Firmware-only: preserve public protocol and pins; update tests.
- Hardware/pin/dependency: update firmware, BOM, wiring, protoboard layout, and flashing notes.
- Provisioning/timezone: update the state machine, threat checks, visible states, limitations, and acceptance tests.
- iOS companion: preserve AccessorySetupKit onboarding, background Core Bluetooth restoration, exact UUID/name declarations, one active sync phone, and physical-device acceptance tests.
- Bug diagnosis: reproduce or trace first; do not change behavior until the cause is supported.

For a new or suspect ESP32 board, route first to
`docs/radio-diagnostics.md` and `uv run tools/diagnose_esp.py`. The workflow is
preauthorized to erase the complete ESP flash and bonds. Treat an internal
Wi-Fi/BLE “started” result as provisional: only the scripted external Wi-Fi
observation plus physical-iPhone encrypted BLE round trip can produce `PASS`.
When running through Codex, request direct serial-device access for the first
invocation and follow the document's agent recovery ladder. A restricted
process that lists `/dev/cu.*` but cannot open it is a host-permission failure,
not evidence against the ESP. If BOOT or a manual post-flash reset is needed,
finish the investigation, then rerun cleanly without recovery before awarding
`PASS`.

## Preserve the invariants

- Use the selected board profile's safe pins. On C3 avoid GPIO2/8/9 and native
  USB GPIO18/19. On a classic ESP32 DevKit use I2C GPIO21/22 and never use its
  flash-connected GPIO6/7.
- Keep display policy behind `DisplayBackend`. Select SSD1306 geometry at
  compile time; do not guess 128x32 versus 128x64 at runtime.
- Keep the RTC in UTC and apply only a validated, confirmed timezone for display.
- Treat BLE, HTTP, Wi-Fi, and browser values as hostile input.
- Keep optional-sensor and radio failures from stopping the displayed clock.
- Keep 80/40 MHz dynamic frequency scaling and Bluetooth modem sleep enabled
  in C3 application profiles. Do not enable automatic CPU light sleep. Keep
  the detailed power policy in `README.md`.
- Never promise universal automatic phone time over generic BLE pairing.
- Never promise guaranteed periodic iOS execution. Background updates are driven by the clock's BLE notification and can be prevented by a user force-quit.
- Never retry a failed open-network BSSID in the same boot.
- Keep captive-portal form automation within the documented response, request,
  redirect, cookie, submission, and time limits. It may send only per-attempt
  synthetic values; never credentials, payment details, user/family data, or
  persisted portal state.
- Never charge a CR2032 or omit the common DS3231 module warning.

## Implement

Use bounded, non-blocking state transitions. Use monotonic time for scheduling. Validate epoch, UTC offset, timezone text, payload sizes, and retry counts at every external boundary. Keep visible four-character states legible and documented.

Use `uv` for all Python work. Build with:

```sh
uv sync
uv run pio test -e native
uv run pio run
uv run pio run -e esp32-c3-super-mini
```

If the project needs a Python tool, create or update its environment with `uv`; never fall back to bare pip or venv.

## Verify

Require:

- native unit tests pass;
- affected classic-ESP32 display profiles and the ESP32-C3 travel firmware
  compile without new warnings;
- flash/RAM usage remains reasonable;
- docs and source agree on pins, dependencies, timing, and status codes;
- failure paths remain bounded;
- hardware-only claims are labeled for bench verification.

For radio or electrical changes, walk the relevant acceptance tests in the README and identify the exact physical tests still required.

For iOS changes, also compile the unsigned generic-device app and test bundle
with the commands in `ios/README.md`. Verify that AccessorySetupKit declarations
match firmware advertising and explicitly leave pairing, restoration, and
background behavior as physical-iPhone gates until tested.
