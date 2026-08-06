# ESP32 incoming-board radio diagnostic

Use this destructive acceptance test on every newly received ESP32-C3 Super
Mini or classic ESP32 DevKit before attaching modules or soldering it into the
clock. It answers a narrow question: can this board flash, boot stably, receive
Wi-Fi, transmit Wi-Fi, advertise/connect over BLE, and complete the companion
app's encrypted GATT exchange?

An ESP-IDF or Arduino API returning success is not an over-the-air test. The
workflow therefore cannot report `PASS` until another device sees the Wi-Fi
beacon and a physical iPhone completes the BLE write and receives the
firmware's `time-accepted` acknowledgement.

## One-command test

Disconnect the display, light sensor, RTC, and other modules. Connect only the
ESP32 using a known-good data cable, then run:

```sh
uv sync
uv run --locked tools/diagnose_esp.py
```

The tool finds a single connected board, infers C3 versus classic from its USB
interface when possible, erases the complete flash without asking, uploads the
matching diagnostic image, and reads its serial results. Erasing the
application, configuration, RF calibration data, and BLE bonds is a deliberate
part of this workflow and has standing project authorization. The ESP rebuilds
RF calibration data on its clean diagnostic boot.

### Agent recovery ladder

An agent must use this order. Do not jump directly from an upload error to
declaring the board defective or asking the user to press BOOT.

1. **Obtain direct serial-device access before starting.** A Codex process may
   be able to list `/dev/cu.*` from its restricted shell while `esptool` still
   cannot open the device. Run the wrapper with approved direct device access
   from the first invocation.
2. **Classify an open failure as a host problem first.** If PlatformIO says
   `Could not open ... the port doesn't exist` but
   `uv run --locked python tools/pio.py device list`
   still shows that port, stop the restricted run and restart it with direct
   serial access. Also exclude another serial monitor owning the port. This is
   not a BOOT-mode or radio failure.
3. **Use BOOT only after `esptool` can open the port but cannot connect.** Apply
   the board-specific gesture below and let the wrapper retry. A retry is
   recorded, so that run cannot qualify the board.
4. **Recover a board left in the ROM loader correctly.** If serial shows
   `boot: ... DOWNLOAD` or `waiting for download` after upload, release BOOT and
   tap RESET once without touching BOOT. Wait for the wrapper's explicit
   post-flash reset prompt instead of intervening early, so recovery is recorded.
5. **Rerun the complete command from the beginning.** A board may receive
   diagnostic evidence during a recovered run, but it earns `PASS` only when
   erase, upload, normal application boot, heartbeats, and both external radio
   proofs all succeed without manual recovery.

The reference C3 incident established this exact sequence:

1. A sandboxed attempt listed the USB port but could not open it. The agent
   stopped that run instead of treating it as an ESP or BOOT failure.
2. With direct serial access, the full-chip erase and diagnostic upload worked.
3. BOOT had been held through reset, leaving the C3 at `waiting for download`.
   The user released BOOT and tapped RESET once without holding it.
4. The agent kept one serial session open with DTR/RTS inactive so reopening
   the C3's native USB port could not reset the board and destroy BLE evidence.
5. The diagnostic then received nearby Wi-Fi, its SoftAP was visible on the
   phone, and the companion completed an encrypted time write and
   acknowledgement.

Those steps reset software, NVS, bonds, and RF calibration; they did not repair
hardware. Because the board then proved both radios over the air, it was not
classified as defective. The final wrapper performs the non-resetting serial
handling itself. Future agents should run it with direct device access from the
start and rerun from scratch after any manual recovery, rather than repeating
the intermediate tool-debugging attempts above.

If more than one serial device is attached or inference is ambiguous, specify
both:

```sh
uv run --locked tools/diagnose_esp.py \
  --board c3 \
  --port /dev/cu.usbmodem1101
```

The script prints two board-specific names:

- `ESPClock-RadioTest-xxxx`: merely observe this SSID in the phone or
  computer's Wi-Fi picker; joining it is unnecessary.
- `KidsClock-xxxx`: select this in **Add Kids Clock** for a new board, or use
  **Sync Now** when that board is already authorized in the physical-iPhone
  companion app; wait for a successful sync.

Answer the two prompts only from what the external device actually did. The
JSON report is written outside the repository under
`/private/tmp/espclock-diagnostics/` unless `--report` selects another path.

## Download-mode recovery

The runner retries a failed erase or upload after showing the appropriate
gesture:

- C3 Super Mini: hold **BOOT**, briefly press **RESET** (or reconnect USB while
  holding BOOT), then release **BOOT**.
- Classic ESP32 DevKit: hold **BOOT**, tap **EN/RESET**, then release **BOOT**.

This powered-reset download gesture is different from the normal firmware's
five-second BOOT recovery gesture. A clone that repeatedly requires manual
download mode can still be diagnosed, but it fails the separate automatic
flashing/cold-start acceptance requirement. Always rerun from the beginning
after recovery; never convert the recovered run itself into `PASS`.

## Result rules

`PASS` requires all of the following in the same run:

1. clean erase and upload complete on the first attempt, followed by exactly
   one stable diagnostic boot and at least three serial heartbeats;
2. SoftAP start plus a completed receive scan containing at least one nearby
   network;
3. external observation of `ESPClock-RadioTest-xxxx`;
4. BLE advertising start, encrypted/bonded connection from the physical
   iPhone, a valid 12-byte time write, and `time-accepted` acknowledgement.

`FAIL` means at least one attempted requirement failed. `INCOMPLETE` means the
automatic checks passed but an over-the-air observation was skipped. In an RF
quiet or shielded location, `--allow-empty-wifi-scan` changes a zero-result
receive scan from `FAIL` to `INCOMPLETE`; it never turns it into a pass.

AccessorySetupKit error 150 is a connection failure, not successful
onboarding. Record it as `no`. When retesting a board whose flash/bonds were
erased, remove its previous Kids Clock authorization from the iPhone if iOS
keeps trying stale bond material, then rerun the test.

## Useful modes

Run only the automated portion for build benches or CI-like checks:

```sh
uv run --locked tools/diagnose_esp.py --non-interactive
```

That result is necessarily `INCOMPLETE`, because internal logs do not prove RF
transmission. `--no-erase` also forces `INCOMPLETE`; it exists only for quick
diagnostic-firmware development and cannot qualify an incoming board.

Restore a known product profile after the result is saved:

```sh
uv run --locked tools/diagnose_esp.py \
  --restore-env esp32-c3-oled-128x64
```

Without `--restore-env`, the board intentionally remains on the diagnostic
image. Label rejected boards with the JSON report timestamp and do not assemble
them into a clock.

## What this does not prove

The bare-board check does not qualify five-metre range, enclosure detuning,
display-load brownouts, voltage regulation, sensor buses, cold starts, or
long-duration reliability. Continue with the hardware acceptance tests in the
README after a board passes this gate.
