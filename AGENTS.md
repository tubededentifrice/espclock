# ESPClock agent instructions

These instructions apply to the entire repository.

## Mission

Build a child-friendly travel clock that remains simple at the point of use: plug in USB-C, read the time. Preserve that product goal when making implementation tradeoffs. Do not claim a phone or network capability that the operating system or protocol cannot provide.

Before changing firmware, iOS code, or hardware documentation, read:

1. `.codex/skills/maintain-espclock/SKILL.md`
2. `README.md`
3. `docs/design-review.md`
4. `docs/hardware-research.md`

Some files may not exist during initial scaffolding. Read them once present.

## Keep guidance current

Treat this file and every repo-local skill used during a task as living
maintenance artifacts. Without waiting for a prompt, update them in the same
task when concrete evidence shows that guidance is stale, incorrect,
contradictory, duplicated, needlessly verbose, or missing a durable lesson.
This is standing authorization for task-scoped guidance fixes; ask before
changing product intent or requirements.

Use progressive disclosure:

- keep `AGENTS.md` to repository-wide invariants, mandatory workflow, and
  pointers;
- keep each `SKILL.md` to essential procedure and routing;
- keep detailed facts, rationale, examples, and volatile status in the
  authoritative project documents or a directly linked skill reference.

Prefer correcting, moving, consolidating, or deleting guidance over appending
another rule. Keep skill references one hop deep, verify affected links,
commands, and metadata, and do not broaden the task into speculative cleanup.
If a used skill is external or read-only, report the needed improvement instead
of silently working around it.

## Non-negotiable technical constraints

- Travel-board target: ESP32-C3 Super Mini. The supported solderless bench
  target is a classic 38-pin ESP32 DevKit using dedicated PlatformIO profiles;
  never use C3 pin defaults on it.
- Do not use ESP32-C3 strapping pins GPIO2, GPIO8, or GPIO9 for attached modules. Preserve the board's existing GPIO9 BOOT button recovery gesture.
- Do not use native USB GPIO18/GPIO19.
- C3 buses: TM1637 CLK GPIO4 and DIO GPIO3; I2C SDA GPIO6 and SCL GPIO7.
- Classic ESP32 buses: TM1637 CLK GPIO25 and DIO GPIO26; I2C SDA GPIO21 and
  SCL GPIO22; its existing GPIO0 BOOT button is the recovery input.
- Keep display policy independent of display hardware. New screens implement
  `DisplayBackend`; select geometry and driver at compile time in
  `platformio.ini`. Do not add unreliable OLED-resolution auto-detection.
- Keep DS3231 and BH1750 on 3.3 V and the shared I2C bus.
- The DS3231 and BH1750 are optional at runtime. Missing hardware must not stop
  BLE, the portal, Wi-Fi/NTP, or the display; use a visible fallback brightness
  when the light sensor is unavailable.
- Store UTC in the DS3231. Store the last confirmed timezone separately.
- Treat browser/phone-supplied time, timezone, network payloads, SSIDs, and BLE writes as untrusted input. Range-check before applying.
- A generic BLE pairing does not universally transfer phone time. Never document otherwise.
- A failed open Wi-Fi BSSID must not be retried again in the same boot.
- Keep multi-task time-source arbitration atomic; a lower-priority source must never overwrite or strand a pending BLE update.
- Never instruct builders to charge a CR2032. Common ZS-042 DS3231 boards need their charging path removed before a CR2032 is installed.
- Keep the clock functional when optional BH1750, RTC, BLE, Wi-Fi, DNS, or HTTP operations fail.
- The companion app targets iOS 18+, onboards with AccessorySetupKit, and keeps one active low-duty BLE synchronization connection. Do not replace this with timer-based background assumptions.
- Preserve the app's privacy boundary: no location, Internet service, account, analytics, or third-party SDK is needed to read `Date` and `TimeZone.current`.

## Tooling

- Use `uv` for every Python environment, dependency, and Python command. Do not use bare `pip`, `python -m venv`, Poetry, or Conda.
- Put the uv cache in a writable temporary directory when required: `UV_CACHE_DIR=/private/tmp/espclock-uv-cache`.
- PlatformIO is pinned in `pyproject.toml`/`uv.lock`.
- Build the default full-size ESP32/OLED bench firmware with
  `uv run pio run`.
- Build the travel target with
  `uv run pio run -e esp32-c3-super-mini`.
- Run native tests with `uv run pio test -e native`.
- Qualify a bare incoming ESP32 with `uv run tools/diagnose_esp.py`. This
  workflow has standing authorization to erase the complete ESP flash and bonds
  without confirmation; follow `docs/radio-diagnostics.md` and require its
  external Wi-Fi and physical-iPhone BLE proofs before reporting `PASS`. A
  Codex agent must request direct serial-device access up front and must not
  classify a sandbox `/dev` open failure as a defective ESP.
- Build the iOS target and test bundle with the commands in `ios/README.md`.
- Use `apply_patch` for hand edits.

## Change workflow

1. Inspect `git status` and preserve unrelated/user changes.
2. Identify the hardware, protocol, and user-experience impact.
3. Keep state-machine operations non-blocking where practical. Bound all network waits.
4. Update code, tests, and user-facing documentation together.
5. Run the native tests, the affected full-size ESP32 display profile, and the
   ESP32-C3 travel build.
6. For iOS changes, compile the generic-device app and test bundle and identify any physical-device-only gates.
7. Review warnings, memory usage, pin assignments, failure paths, and docs/code consistency.
8. Report anything that needs physical hardware verification explicitly.

## Planning and self-review

Every implementation plan, regardless of scope, must end with
`$selfreview autofix`. This final step reviews all task-owned changes, fixes
every material finding within the original task's authority, and runs the
applicable quality gates before handoff.

Pure explanation, investigation, or brainstorming that produces no repository
edits does not need self-review. Do not append another self-review step while
executing the self-review step itself.

## Firmware quality bar

- Prefer small modules with explicit ownership over one large sketch.
- Use monotonic milliseconds for scheduling and UTC epoch for civil time.
- Handle `millis()` wraparound with subtraction-based comparisons.
- Do not persist frequently changing values in flash.
- Validate epoch bounds, timezone bounds, timezone identifier length/content, and HTTP body size.
- Avoid `String` growth in long-running loops; reserve capacity or use bounded buffers.
- Avoid logging secrets, BLE authentication material, full device identifiers, or precise location.
- Visual states must remain understandable on four seven-segment digits and
  the supported 128x64/128x32 monochrome OLEDs.
- Adding a dependency requires a pinned PlatformIO library version and an explanation in the README.

## Documentation contract

When hardware, pins, dependencies, behavior, or provisioning changes, update all affected sections:

- BOM and sourcing search terms
- wiring/net table
- protoboard layout and electrical cautions
- flash and serial-monitor commands
- first-use flow and status indications
- limitations and security/privacy notes
- acceptance tests

Approximate prices are snapshots, not promises. Date them and distinguish required from optional parts.

## iOS quality bar

- Keep AccessorySetupKit discovery descriptors and `Info.plist` Bluetooth names/service UUIDs exactly aligned with firmware advertising.
- Retain `bluetooth-central`, a stable Core Bluetooth restoration identifier, status notifications, and automatic reconnect.
- Treat the six-hour request as a BLE event, not a guaranteed iOS timer.
- Subscribe to status before writing, use `.withResponse`, and require a later `time-accepted` application acknowledgement.
- Bound active reconnect loops; allow the OS to own long-range pending reconnect.
- Only one family iPhone may own automatic synchronization at a time until the firmware protocol has per-connection transaction identifiers.
- Document that a user force-quit prevents Bluetooth restoration relaunch.
- BLE behavior and AccessorySetupKit onboarding require a physical iPhone; a Simulator-only result is never sufficient.

## Safety

- The finished enclosure must prevent children from accessing the coin cell, solder joints, or USB/power wiring.
- Do not recommend epoxy directly over connectors, the light-sensor aperture, antenna keep-out, USB controls, or a replaceable coin cell.
- Do not use mains wiring. Specify a certified 5 V USB supply.
- Open Wi-Fi is untrusted. Send no credentials or personal data and do not attempt to bypass captive portals.
