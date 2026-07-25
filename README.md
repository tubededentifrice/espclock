# ESPClock

A small, child-friendly travel clock: plug it into USB-C and it immediately shows the best time it has, then quietly tries to improve it from an authorized phone or the Internet.

The first solderless prototype uses a **full-size 38-pin ESP32 DevKit**, the
owned **0.96-inch 128x64 I2C OLED**, and an owned **BH1750/GY-302**. A DS3231 is
optional for this bring-up: once synchronized, the ESP keeps time while USB
power remains connected. The compact travel build remains an **ESP32-C3 Super
Mini**, preferably with a larger 1.2-inch TM1637 display and a battery-backed
DS3231 after the behavior has been proven on the bench.

## What it does

1. Reads UTC from a DS3231 when one is fitted and valid; otherwise it starts
   without inventing a time and waits for a synchronization source.
2. Advertises `KidsClock-xxxx` over BLE. The included iPhone companion app writes UTC plus the current UTC offset, reconnects in the background, and answers six-hour sync requests.
3. If BLE has not already synchronized this boot, starts a two-minute no-install Wi-Fi portal named `KidsClock-xxxx` after the two-minute onboarding window. Joining it lets the phone browser transfer its time and offset automatically, including after travel.
4. As a last resort, tries open Wi-Fi access points in signal-strength order, never retrying a failed BSSID in the same boot, and requests UTC from NTP.
5. Keeps time in the ESP while powered, mirrors it to an available RTC, and
   reopens bounded synchronization opportunities every six hours.
6. Samples room light about once per second, filters it over several seconds, and adjusts the display through eight brightness levels.

Radio failures never stop the clock or blank a valid display.

## Important phone/Bluetooth reality

Approving a Bluetooth pairing authorizes a connection; it does not universally make a phone publish its clock or Internet connection.

- Android does not expose the BLE Current Time Service as a universal system service.
- Generic BLE accessories usually need a phone app to initiate their custom GATT exchange.
- The ESP32-C3 has BLE, not Bluetooth Classic PAN, so it cannot consume Bluetooth tethering.
- NTP provides UTC only. It does not reveal the local timezone.

The repository therefore includes an iOS 18+ [companion app](ios/README.md), an encrypted/bonded BLE write service, and a universal no-app captive-portal fallback. The app uses AccessorySetupKit onboarding and a long-lived low-duty connection so the clock can request time about every six hours. The portal remains the reliable no-install path on a new phone. Open Wi-Fi/NTP refreshes UTC but retains the last phone-confirmed UTC offset. Concurrent updates are arbitrated atomically in the order BLE, portal, then NTP, so a lower-trust callback cannot overwrite a pending higher-trust update.

This is an honest limit of phone operating systems and profiles, not an ESP32 coding omission. See [hardware research](docs/hardware-research.md) and the [adversarial design review](docs/design-review.md) for sources and deeper tradeoffs.

## Hardware choice

| Board already available | Wi-Fi | Bluetooth | Size/fit | Decision |
|---|---:|---:|---|---|
| **ESP32 38-pin** | **Yes** | **Classic + BLE** | Breadboard-friendly, usually Micro-USB | **Selected for the first solderless prototype** |
| **ESP32-C3 Super Mini** | **Yes** | **BLE 5** | **About 22.5 × 18 mm, USB-C** | **Selected** |
| S2 Mini | Yes | No | Small | Rejected: no Bluetooth |
| C6 Mini N4 | Yes, including Wi-Fi 6 | BLE 5.3 | Small | Works, but its extra radios add no clock benefit |

The C3 has enough GPIO, flash, RAM, and radio capability without the size of
the 38-pin board, so it remains the travel target. The firmware has distinct,
pin-safe PlatformIO profiles for both board families and for TM1637, SSD1306
128x64, and SSD1306 128x32 displays.

Of the two OLEDs already on hand, start with the **0.96-inch 128x64** unit. The
narrow 0.91-inch module is normally 128x32; with the same 128-pixel width but
half as many rows, its digits are substantially shorter. Neither tiny OLED is
guaranteed readable from two metres, and continuously displayed OLED pixels can
age, so the physical distance/night test decides whether either is suitable for
the final clock. The display backend is modular, so that test does not lock the
firmware to one screen.

## Bill of materials

Prices were checked on **2026-07-25** and are approximate. Search links and a more detailed comparison are in [docs/hardware-research.md](docs/hardware-research.md).

| Qty | Required part | What to buy | Approx. AliExpress | Approx. Amazon.ae |
|---:|---|---|---:|---:|
| 1 | 38-pin ESP32 DevKit | Already owned; first breadboard target | AED 15–30 | AED 25–55 |
| 1 | 0.96-inch SSD1306 OLED, 128x64 | Already owned; first display target, normally `0x3C` | AED 5–12 | AED 15–35 |
| 1 | 0.91-inch SSD1306 OLED, normally 128x32 | Already owned; optional comparison target | AED 4–10 | AED 15–30 |
| 1 | BH1750/GY-302 light sensor | Already owned; default address `0x23` | AED 3–8 | AED 10–30 |
| 1 | ESP32-C3 Super Mini | Already owned; compact travel target | AED 9–18 | AED 20–45 |
| 1 | 1.2-inch red TM1637 clock display | Optional final-display candidate; centre colon | AED 15–40 | AED 35–70 |
| 1 | DS3231 RTC module | Optional for bring-up, required for unplugged retention; verify a non-charging CR2032 arrangement | AED 6–15 | AED 20–45 |
| 1 | Branded CR2032 | Primary, non-rechargeable coin cell | AED 2–5 | AED 5–10 |
| 1 | 5 × 7 cm, 2.54 mm perfboard | Single-sided is sufficient | AED 3–7 | AED 10–20/pack |
| 2 | Female socket strips | Socket the C3 | AED 1–3 | AED 8–15/pack |
| 1 each | 100 µF ≥10 V electrolytic; 100 nF ceramic | Local decoupling | AED 1–2 | Usually in an assortment |
| 1 | USB-C cable and certified 5 V/1 A supply | A known-good phone charger is fine | existing | AED 15–35 |
| — | Solid wire, solder, heat-shrink, smoked red window | Workshop consumables | AED 3–8 allocated | AED 10–25 |

The first breadboard test needs no new electronic module. A compact final build
with a purchased TM1637, RTC, cell, and perfboard is roughly **AED 28–72** from
AliExpress excluding shipping, case, cable, and charger.

Optional only if the exact large display fails its 3.3 V acceptance test: a two-channel BSS138 level-shifter module, approximately AED 2–8.

## Wiring

For the first solderless test, power both owned I2C modules from the full-size
ESP32's regulated **3.3 V** rail:

| 38-pin ESP32 | Connect to | Notes |
|---|---|---|
| `3V3` | OLED `VCC`; BH1750 `VCC`; optional DS3231 `VCC` | Keep every I2C pull-up at 3.3 V |
| `GND` | Every module `GND` | One common ground |
| GPIO21 | OLED `SDA`; BH1750 `SDA`; optional DS3231 `SDA` | Shared I2C data |
| GPIO22 | OLED `SCL`; BH1750 `SCL`; optional DS3231 `SCL` | Shared I2C clock |
| GPIO0 | Existing BOOT button only | Five-second powered-on recovery gesture |

Follow the labels printed on the board. Many 38-pin DevKits occupy nearly the
whole breadboard width; use two joined breadboards or jumper one pin row to a
second breadboard if necessary.

The compact TM1637 travel wiring remains:

| ESP32-C3 pin | Connect to | Notes |
|---|---|---|
| `3V3` | TM1637 `VCC`; BH1750 `VCC`; DS3231 `VCC` | Never connect to 5 V |
| `GND` | Every module `GND` | One common ground |
| GPIO4 | TM1637 `CLK` | Dedicated display bus |
| GPIO3 | TM1637 `DIO` | Dedicated display bus |
| GPIO6 | BH1750 `SDA`; DS3231 `SDA` | Shared I2C data |
| GPIO7 | BH1750 `SCL`; DS3231 `SCL` | Shared I2C clock |

Do not attach modules to C3 strapping pins GPIO2/8/9 or native-USB GPIO18/19.
The 128x64 or 128x32 OLED can replace the TM1637 on the C3 by joining it to the
same GPIO6/7 I2C bus.

![ESPClock perfboard placement and wiring](hardware/protoboard-layout.svg)

The complete net list, schematic, continuity procedure, level-shifter fallback, and mechanical notes are in [hardware/wiring.md](hardware/wiring.md).

### Mandatory RTC safety check

Many cheap blue ZS-042/HW-111 DS3231 modules try to charge the coin cell through a resistor and diode. A CR2032 is **not rechargeable**.

Before installing a CR2032:

1. prove the module has no charging path, or remove its charge resistor/diode;
2. power the RTC at 3.3 V without a cell and inspect/measure the holder;
3. install the cell, re-power, and verify its voltage is not being driven upward;
4. place it behind a screwed enclosure where a child cannot reach it.

Do not build the clock if this check is uncertain.

### Display voltage acceptance

Running a red TM1637 module at 3.3 V is simple and prevents 5 V pull-ups from reaching the ESP. The TM1637 manufacturer's guaranteed electrical table is specified at higher voltage, so the exact purchased 1.2-inch module must pass:

- repeated cold starts;
- all-segments/full-brightness operation during Wi-Fi transmit;
- two-metre daylight readability;
- minimum-brightness dark-room comfort.

If it fails, follow the 5 V plus BSS138 level-shifter alternative in [hardware/wiring.md](hardware/wiring.md). Never connect a 5 V-powered TM1637 module directly to C3 pins.

## Breadboard bring-up

1. Place the full-size ESP32 on a breadboard and connect the 0.96-inch OLED and
   BH1750 using GPIO21/22. Leave the DS3231 disconnected.
2. Flash `esp32-devkit-oled-128x64`, synchronize from the iPhone app or portal,
   and confirm the clock continues while USB stays powered.
3. Power-cycle it without an RTC. It must return to `PAIR`/`----`, then accept a
   new synchronization normally.
4. Compare the 0.91-inch screen using the separate 128x32 firmware profile.
5. Only then add and qualify a DS3231. Leave the CR2032 out until its charging
   path check is complete.
6. Exercise every brightness level and perform the two-metre/daylight and
   dark-bedroom tests before choosing the final display.
7. Bench-test for at least 24 hours before choosing the compact hardware.

## Later travel assembly

1. Socket the C3 on perfboard with its antenna end at or beyond the board edge.
2. Put the BH1750 at a clear front/top aperture, shaded from the display.
3. Fit 100 µF across display `3V3`/`GND` and 100 nF near the I2C modules.
4. Solder the final wiring and add strain relief; do not leave Dupont jumpers
   in the travel unit.
5. Repeat the 24-hour, radio, brightness, and cold-start tests before designing
   the case.

Do not pot the first build. If later reinforcement is useful, use printed clips or electronics-safe neutral-cure silicone. Keep the antenna, USB-C connector, sensor aperture, socketed ESP, and coin cell clear and serviceable.

## Build and flash

Host requirements:

- [uv](https://docs.astral.sh/uv/)
- a data-capable cable matching the selected board (normally Micro-USB for the
  full-size DevKit and USB-C for the C3);
- for classic DevKit clones, the CP210x or CH340 USB-UART driver if macOS does
  not already recognize it; the C3 uses its native USB serial/JTAG interface.

All Python tooling is pinned in `pyproject.toml` and `uv.lock`; do not install PlatformIO globally.

```sh
uv sync
uv run pio test -e native
uv run pio run -e esp32-devkit-oled-128x64
uv run pio run -e esp32-devkit-oled-128x64 --target upload
```

PlatformIO normally detects the serial port. If several boards are connected:

```sh
uv run pio device list
uv run pio run -e esp32-devkit-oled-128x64 --target upload \
  --upload-port /dev/cu.SLAB_USBtoUART
```

Other common classic-board port names are `/dev/cu.usbserial-*` and
`/dev/cu.wchusbserial*`; C3 boards commonly appear as
`/dev/cu.usbmodemXXXX`.

Open the serial monitor:

```sh
uv run pio device monitor --baud 115200
```

If a clone does not enter download mode automatically, hold its `BOOT` button, tap `RESET`, release `BOOT`, and retry the upload. These buttons are not used in normal clock operation. The existing BOOT button also provides the deliberate recovery gesture described below.

The default build is the full-size ESP32 + 128x64 OLED bench profile. Available
profiles are:

| PlatformIO environment | Board and display |
|---|---|
| `esp32-devkit-oled-128x64` | 38-pin ESP32, owned 0.96-inch OLED |
| `esp32-devkit-oled-128x32` | 38-pin ESP32, likely geometry of the 0.91-inch OLED |
| `esp32-devkit-tm1637` | 38-pin ESP32, TM1637 on GPIO25/26 |
| `esp32-c3-oled-128x64` | C3, 128x64 OLED |
| `esp32-c3-oled-128x32` | C3, 128x32 OLED |
| `esp32-c3-super-mini` | C3 travel target, TM1637 on GPIO4/3 |

OLED resolution cannot be identified reliably over I2C. If the 0.91-inch
module is blank or malformed with the 128x32 profile, verify its controller,
address, and listing rather than adding runtime guessing. The default address
is `0x3C`; override `CLOCK_OLED_ADDRESS=0x3D` in a profile if an I2C scan proves
that address.

## Install the iPhone companion

The native Swift companion lives in `ios/` and requires iOS 18 or newer. Open
`ios/ESPClockCompanion.xcodeproj`, select a signing team and your physical
iPhone, then press **Run**. It contains no third-party packages, account,
location permission, network service, advertising, or analytics.

Power-cycle the clock immediately before tapping **Add Kids Clock**. Apple's
accessory picker handles the required phone-level approval. Leave automatic
sync enabled on one family iPhone and do not force-quit the app; the clock will
ask that connected app for fresh time about every six hours. See the
[complete iPhone install, behavior, recovery, and test guide](ios/README.md).

## First use

### RTC already contains valid time

- The clock displays it immediately.
- `PAIR` appears briefly during the two-minute BLE-first/onboarding opportunity.
- BLE advertising remains available for already bonded clients even after the normal time display returns. New, unbonded phones are accepted for two minutes after boot so Apple's accessory picker has time to complete.
- If BLE has not already synchronized, the two-minute `KidsClock-xxxx` portal is offered after two minutes so another phone can update the timezone without an app. Valid time remains visible most of the time.
- If no phone supplies a fresh time, the clock then tries last-resort open Wi-Fi/NTP.

### RTC is new, missing, or lost power

1. The display shows `PAIR` while BLE advertises.
2. After two minutes, it shows `SEt` and starts the open Wi-Fi AP `KidsClock-xxxx`.
3. On a phone, join that network. Its captive page should open and set the clock automatically.
4. If it does not open, browse to [http://192.168.4.1](http://192.168.4.1) and tap **Set time now**.
5. The page sends only Unix UTC and the phone's current UTC offset. It sends no account, Wi-Fi password, location coordinate, or personal data.

The setup AP closes after success or after two minutes. With no RTC, the clock
continues from the ESP system clock only until USB power is removed. A later
power cycle deliberately returns to the no-time flow and resynchronizes; the
persisted UTC offset alone is not treated as a valid instant.

### Recover from a badly wrong but plausible RTC

The first accepted synchronization is allowed to correct an uninitialized clock by any amount. After that, a persistent confirmed-sync marker activates the five-minute anti-replay/corruption limit.

If the RTC is later wrong by more than five minutes, leave the clock powered
and hold the board's existing `BOOT` button for five seconds. The OLED shows
`RESET`; the four-digit TM1637 approximates `rSt`. Release the button when the
message appears. The full-size ESP32 uses GPIO0 and the C3 uses GPIO9. The
firmware clears the confirmed-sync marker, stored offset, and BLE bonds and
restarts. Do **not** reset or apply power while BOOT is held; that enters the
board's download loader. After restart, use the BLE window or portal to set the
correct time. The RTC itself is left intact so the screen can continue to show
its best available value until the correction arrives.

This recessed service gesture is not part of normal use. Make the onboard button reachable through a tool/paperclip pinhole in the eventual case.

### After travel to a different timezone

Keep the clock near its authorized iPhone companion; it writes the new time/offset on reconnect. Otherwise, wait two minutes after boot and join the offered `KidsClock-xxxx` setup portal. An NTP-only result refreshes UTC but cannot determine civil timezone by itself.

The current firmware stores a validated offset in 15-minute-capable minutes, not a whole-hour approximation, so zones such as `+05:30` and `+05:45` work. It does not contain the world's timezone/DST database; a phone sync is required after an offset/DST change when the clock has no richer timezone source.

## Display indications

| Display | Meaning |
|---|---|
| `HH:MM`, blinking colon | Phone-confirmed local offset this boot |
| `HH MM`, brief colon pulse every two seconds | Valid UTC with a retained, not-yet-confirmed timezone offset |
| `PAIR` alternating with time | BLE pairing/sync opportunity |
| `SET` on OLED / `SEt` on TM1637 | No-app phone setup portal is active |
| `WIFI` / seven-segment approximation | Trying an open network/NTP |
| `----` | No trustworthy time is available yet |
| Fast colon pulse | BLE client connected |
| `RESET` on OLED / `rSt` on TM1637 | BOOT recovery button is being held; keep holding for five seconds, then release |

The pairing message yields to a valid time most of the time; network work never leaves a valid clock blank.

## BLE time service

BLE device name: `KidsClock-xxxx`, where the suffix comes from the ESP chip
identifier.

The legacy BLE payload is deliberately split without truncation. The 27-byte
primary advertisement contains general-discoverable/BR-EDR-unsupported flags,
the complete 128-bit service UUID, and preferred connection intervals. Its
16-byte scan response contains the complete `KidsClock-xxxx` local name. Both
remain below the 31-byte limit. Connectability is part of the advertising PDU;
bondability is an SMP security policy, not an advertising-data flag.

| Item | UUID |
|---|---|
| Service | `7f510000-1b15-4dc7-9f3f-19b30a6f6a21` |
| Time read/write/notify | `7f510001-1b15-4dc7-9f3f-19b30a6f6a21` |
| Status read/notify | `7f510002-1b15-4dc7-9f3f-19b30a6f6a21` |

Time writes require an encrypted/bonded BLE connection. A client may send either:

- UTF-8 text: `unix_utc_seconds,utc_offset_minutes`
- 12-byte binary packet:

| Byte | Value |
|---:|---|
| 0 | protocol version `1` |
| 1–8 | signed Unix UTC seconds, little-endian |
| 9–10 | signed UTC offset minutes, little-endian |
| 11 | flags; reserved on write |

Accepted epochs are 2024-01-01 through 2099-12-31. Offsets must be between UTC−14:00 and UTC+14:00. Malformed, stale-range, oversized, and trailing-junk payloads are rejected.

The status characteristic reports `time-needed`, `sync-request`, `time-pending`, `time-accepted`, `time-rejected`, `rate-limited`, or `invalid-time`. The included iPhone app connects during the two-minute new-phone window for its first bond, enables status notifications, writes the phone's current UTC/offset, waits for `time-accepted`, and retains a low-duty connection. The firmware reissues `sync-request` when notifications are subscribed and approximately every six hours thereafter. The time characteristic includes both the base GATT write property and its encrypted-write requirement. Status values are exact UTF-8 bytes without a trailing NUL; either contract violation breaks Core Bluetooth interoperability even if the clock applies the payload.

BLE bonding capacity is finite even though any family phone may take a turn. This build provisions **16 bond and notification records** and evicts the first bond returned by NimBLE when full so a new family phone can recover the clock. A pairing attempt at capacity must make room before authentication, so a cancelled attempt may evict one existing bond; the acceptance checklist covers this recovery edge case. “Any number of phones” means the clock is not owned by one account or hard-coded handset, not infinite simultaneous connections. Firmware is compiled for one simultaneous connection, and only one iPhone should leave automatic synchronization enabled at a time; its persistent connection is the active sync owner until the app toggle is turned off or it leaves range.

After the clock has a persistent confirmed-sync marker, phone, portal, and NTP corrections must be within five minutes of its running time. An uninitialized clock is allowed one arbitrary valid correction; the BOOT recovery gesture deliberately reopens that path. This rejects captured old values and arbitrary large forward/backward jumps during normal operation while retaining a no-computer recovery path. BLE writes are also limited to one accepted update per 30 seconds.

BLE uses encrypted **Just Works** bonding because the clock has no input device. The phone shows/owns the approval, but Just Works has no numeric-comparison MITM protection. New unbonded peers are rejected after the two-minute onboarding window. The setup Wi-Fi AP is intentionally open for cross-platform ease, accepts one update, applies the confirmed-clock freshness rule, then shuts down. These controls fit a low-consequence clock, but they are not equivalent to a per-device cryptographic owner secret.

## Open Wi-Fi/NTP fallback

This is deliberately bounded and contains no credentials:

- only scan results explicitly marked open are considered;
- candidates are ordered by signal strength;
- a failed **BSSID** is remembered for this boot and never retried;
- if the fixed 24-entry failure table fills, open-Wi-Fi fallback is disabled for the rest of that boot;
- association and NTP have fixed timeouts;
- captive portals are not bypassed and terms are not accepted automatically;
- no SSID/password is saved;
- the station MAC is randomized once per boot before joining opportunistic networks;
- no admin interface, telemetry, OTA, or child/family information is exposed;
- successful NTP updates UTC while retaining the last confirmed offset.

Open networks are untrusted and often unusable without a browser. The feature is a best-effort last resort, not the foundation of correct time. It can be disabled in `platformio.ini`:

```ini
-D CLOCK_ENABLE_OPEN_WIFI_FALLBACK=0
```

## Automatic brightness

The BH1750 is sampled every second. An exponential filter gives roughly a
several-second response and 20% hysteresis prevents flicker near level
boundaries. The normalized eight levels map directly to TM1637 brightness and
to a low-end-weighted SSD1306 contrast table. If the BH1750 is absent or returns
five invalid samples, the display remains usable at fallback level 2 and the
sensor is retried every 30 seconds.

Tune the following in `include/AppConfig.h` or with PlatformIO `-D` flags:

- pin assignments and BH1750 address;
- BLE, portal, Wi-Fi, NTP, and resync timeouts;
- light sample interval;
- fallback UTC offset;
- open Wi-Fi enable/disable.

For a red TM1637, use a smoked red acrylic window or neutral-density film if
brightness code 0 still lights the room. Bench-tune the OLED minimum contrast
before enclosure work. Do not use slow visible blinking as a dimming
substitute.

## Firmware layout

| Path | Responsibility |
|---|---|
| `src/main.cpp` | Atomically prioritized time-update integration and visible-state selection |
| `src/TimeKeeper.cpp` | UTC system clock, DS3231, and persisted offset |
| `src/BleTimeService.cpp` | Bonded BLE GATT time/status service |
| `src/NetworkTimeService.cpp` | Captive setup portal, open-Wi-Fi backoff, and NTP |
| `src/DisplayController.cpp` | Display-independent visible-state and BH1750 brightness policy |
| `src/*DisplayBackend.cpp` | Modular TM1637 and SSD1306 rendering backends |
| `src/ClockCore.cpp` | Host-testable payload validation and light filtering |
| `test/` | Native Unity tests |
| `hardware/` | Wiring authority and protoboard placement |
| `docs/` | Research and adversarial review |
| `ios/` | iOS 18+ AccessorySetupKit/Core Bluetooth companion and packet tests |
| `AGENTS.md` | Repository-wide agent instructions |
| `.codex/skills/maintain-espclock/` | Repo-local maintenance workflow for coding agents |

External embedded libraries are version-pinned in `platformio.ini`. The
SSD1306 profiles use the pinned Adafruit SSD1306, GFX, and BusIO libraries; the
custom geometric digits maximize panel height without a large font asset. Host
tooling is locked by uv.

## Acceptance checklist

Do not close or pot the case until every applicable physical item passes.

### Automated

- [x] `uv run pio test -e native` passes (12/12).
- [x] `uv run pio run -e esp32-devkit-oled-128x64` completes.
- [x] All six display/board profiles compile in the final verification matrix.
- [x] No firmware compiler warnings are reported.
- [x] Normal icon-inclusive Swift app and test-bundle builds complete for a
  generic iPhone target.
- [x] All 13 packet and onboarding-lifecycle XCTests pass on the iOS 26.5
  Simulator.

### Power, display, and sensor

- [ ] USB cold-start succeeds 20 times without pressing a button.
- [ ] Both owned OLED profiles render correct, centered four-digit time and all status words.
- [ ] 0.96-inch and 0.91-inch displays are compared at two metres in daylight.
- [ ] Minimum brightness remains readable but does not illuminate a dark bedroom.
- [ ] BH1750 responds smoothly over dark, bedroom, room, and daylight conditions.
- [ ] Display light does not materially feed back into the BH1750.
- [ ] Selected board regulator and wiring remain comfortably below unsafe temperature.

### RTC and safety

- [ ] Missing DS3231 boots, synchronizes, keeps time while powered, and returns
  to no-time/pairing after complete power loss.
- [ ] DS3231 loss-of-power state is detected when the module is later fitted.
- [ ] RTC retains time after one hour and 24 hours unplugged.
- [ ] Powered VBAT measurement proves the CR2032 is not being charged.
- [ ] Coin cell and solder joints are inaccessible without tools.
- [ ] USB cable has strain relief; the connector is not structural.

### Time and recovery

- [ ] Invalid RTC never displays a plausible invented time.
- [ ] Portal sync works on at least one current iPhone and one current Android phone.
- [ ] BLE write works after first bond and after reconnect.
- [ ] A second family phone can also bond/write.
- [ ] All 16 bond/notification slots survive reboot/reconnect; adding or cancelling a 17th pairing follows the documented eviction/recovery behavior.
- [ ] AccessorySetupKit onboarding works on physical iOS 18 and current iOS.
- [ ] An independent BLE scanner shows the complete name/service UUID and a
  connectable advertisement throughout the two-minute `PAIR` window.
- [ ] Locked/background iPhone answers a six-hour `sync-request`.
- [ ] Core Bluetooth restores after system termination, without user force-quit.
- [ ] Two iPhones can hand off using the automatic-sync toggle.
- [ ] Bad epoch, bad offset, oversized payload, and trailing junk are rejected.
- [ ] A plausible RTC that is hours wrong can be recovered with the five-second BOOT gesture and corrected without a computer.
- [ ] UTC−12, UTC+14, `+05:30`, and `+05:45` display correctly.
- [ ] Open Wi-Fi association failure does not retry that BSSID before reboot.
- [ ] Captive/no-Internet open networks time out and return to the clock.
- [ ] With all radios unavailable, valid RTC time remains displayed.
- [ ] Six-hour resync scheduling does not interrupt display service.

### Enclosure/RF

- [ ] BLE and Wi-Fi work at five metres with intended case materials in place.
- [ ] No copper, cell, fastener, epoxy, or metal blocks the C3 antenna.
- [ ] BH1750 aperture, USB-C, and RTC cell remain serviceable.

## Known limitations

- The included app is iPhone-only and requires iOS 18+. iOS background BLE is event-driven and best-effort: force-quitting the app, disabling Bluetooth, leaving range, expired development signing, or some reboot/first-unlock states stop updates until the app can run again. The DS3231 and captive portal are the resilience layers.
- One iPhone owns the persistent BLE sync connection at a time. Other authorized family phones can take over after automatic sync is disabled on the active phone.
- The current phone payload stores the present UTC offset, not a complete IANA timezone/DST rule. Re-sync after travel or DST changes.
- Open Wi-Fi cannot lawfully or technically bypass captive-portal terms, and NTP is not authenticated.
- Cheap C3, DS3231, BH1750, OLED, and large TM1637 modules vary. The 0.91-inch
  unit may even use an SH1106-compatible controller despite its listing.
  Qualify the exact parts before sealing a child-facing build.
- An always-on OLED can suffer uneven pixel aging. The tiny owned OLEDs are
  excellent for solderless bring-up, but the larger TM1637 remains the leading
  final-display candidate until the two-metre and long-duration tests pass.
- A four-digit display provides no seconds or named timezone. A brief colon pulse indicates that the retained offset has not been phone-confirmed this boot; the serial log gives deeper diagnostics.

These limits are why the RTC plus no-app portal are retained: the clock remains recoverable without pretending iOS guarantees continuous execution.

## Development rules

Future agents and contributors must read [AGENTS.md](AGENTS.md) and the repo-local [maintain-espclock skill](.codex/skills/maintain-espclock/SKILL.md). In particular:

- use `uv` for all Python work;
- keep code, pins, BOM, wiring, and visible states synchronized;
- never charge a CR2032;
- never claim generic BLE pairing automatically transfers time;
- run native tests and the C3 build before handoff.
