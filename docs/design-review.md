# Adversarial design review

> **Review record, not build authority.** This document deliberately preserves
> provisional alternatives and worst-case requirements raised during design.
> Conflicts were resolved in favour of the current stacks in `README.md` and
> `hardware/wiring.md`: a full-size ESP32 with the owned 128x64 OLED for
> solderless bring-up, then a 3.3 V red 1.2-inch TM1637 with a tested BSS138/5 V
> fallback for the compact build. Follow those two files when wiring.

Status: **conditional go, product dependency resolved for iPhone**. The
repository now includes the recommended iOS 18+ AccessorySetupKit/Core Bluetooth
companion, while retaining the no-app captive portal and RTC fallbacks. A phone
still does not automatically connect to an arbitrary BLE peripheral and
volunteer its clock merely because it was paired; the app and initial
phone-level approval are required. Fresh onboarding, encrypted synchronization,
remove/re-add, and reboot reconnect passed on an iPhone 17 Pro running iOS
26.5.2. Oldest-supported-iOS coverage, out-of-range return, restoration while
locked/backgrounded, and a six-hour request remain release gates.

The companion's fresh-install lifecycle is authorization-first: it activates
only `ASAccessorySession`, presents the picker without a central manager,
retains `.accessoryAdded`, and constructs Core Bluetooth only after
`.pickerDidDismiss`. An already-authorized relaunch constructs the stable
restoration manager from `.activated`. This ordering is automated-test covered.
Physical picker discovery and authorization without broad Bluetooth access
passed on the current-release device above; the remaining device/version gates
are recorded in `ios/README.md`.

This review treats a wrong-but-plausible displayed time as the primary failure,
followed by a clock that is hard for a child or parent to recover.

## Product contract that must be made explicit

1. **"Zero configuration" can only mean zero configuration after onboarding.**
   A companion phone app can discover the clock, obtain Bluetooth permission,
   perform a one-tap association, and then update it on later boots. Android's
   official BLE flow has an app scan for, connect to, and exchange data with the
   peripheral; its companion-device API likewise does not create the connection
   itself. iOS provides Core Bluetooth/AccessorySetupKit to an app for the same
   purpose. There is no portable OS feature that lets an unknown accessory pull
   local time and timezone from any nearby phone without user authorization.
   See the official [Android BLE
   overview](https://developer.android.com/develop/connectivity/bluetooth/ble/ble-overview),
   [Android companion pairing
   documentation](https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing),
   and [Apple AccessorySetupKit
   documentation](https://developer.apple.com/documentation/accessorysetupkit/).
2. **BLE Current Time Service is a wire format, not automatic handset
   behavior.** The Bluetooth SIG specification says the CTS *server* exposes
   local time to a client. It does not require a phone to advertise that service,
   connect to this product, or reveal location. A conventional ESP peripheral is
   itself the GATT server, the opposite role from the desired pull operation.
   See the [Bluetooth Current Time Service
   specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/CTS_v1.0/out/en/index-en.html).
3. **NTP provides UTC, not location or civil-time rules.** SNTP can set the
   epoch, while timezone conversion is a separate setting in ESP-IDF. See the
   [NTP specification](https://www.rfc-editor.org/info/rfc5905/) and
   [ESP-IDF system-time
   documentation](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/system_time.html).
   A phone/app must provide the current offset and DST rules, or an Internet
   geolocation/timezone service must be added. IP geolocation is imperfect
   around VPNs, carrier gateways, borders, and satellite/aircraft networks.
4. **"Any number of phones" cannot mean unlimited stored bonds or simultaneous
   connections.** Persistent bond storage is finite; ESP-NimBLE's documented
   default maximum is three. The practical requirement should be “any family
   phone may update it sequentially, with a documented finite trusted-device
   cache and deterministic eviction.” See
   [`CONFIG_BT_NIMBLE_MAX_BONDS`](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/kconfig-reference.html#config-bt-nimble-max-bonds).

### Ideal future source priority

Use one explicit arbitration policy, never “last packet wins”:

1. authenticated companion-app write containing UTC epoch, UTC offset, timezone
   identity/rules, uncertainty, and source age;
2. HTTPS timezone/time service over a network that has proven Internet access;
3. SNTP for UTC while retaining the last known timezone;
4. battery-backed RTC plus last known timezone.

Store the RTC in UTC. Store civil-time metadata separately with a checksum and
version. A BLE payload should carry at least: protocol version, Unix time,
offset in minutes, phone-observed timezone ID, the next DST transition and
post-transition offset (or an equivalent POSIX rule), source age, and an
authenticator. Quarter-hour zones, half-hour zones, DST, and International Date
Line changes must not be approximated as whole hours.

If no fresh timezone source exists after travel, the clock must continue with
the previous timezone and show a subtle “stale/not location-confirmed” state.
Silently presenting old-zone time as synchronized is unacceptable.

## Prioritized risks and required actions

### P0 — resolve before claiming the requirements are met

#### 1. Phone synchronization has an unstated app dependency

**Failure:** the ESP advertises forever, but nearby iOS/Android phones do
nothing. Pairing in the Bluetooth Settings screen alone does not implement a
custom time transfer.

**Required action:** choose and document one of these honest product definitions:

- **Recommended:** a tiny Android/iOS companion app with one-tap first use,
  background reconnect where the OS permits, and a visible “open the app”
  recovery path. Subsequent normal use can approach plug-and-play, but background
  delivery is best effort: iOS may suspend/terminate an app and Android imposes
  background-work constraints. Apple's [background BLE
  documentation](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html)
  explicitly notes that connections can be lost if the process is terminated;
  Android documents comparable [background BLE
  constraints](https://developer.android.com/develop/connectivity/bluetooth/ble/background).
- **Reduced scope:** no app; BLE exposes a service usable by a manual GATT tool,
  while the real automatic source is Internet plus a timezone API. This is not
  “automatic from any phone.”

Do not make firmware completion imply that the end-to-end phone feature exists.

#### 2. Timezone acquisition is not designed

**Failure:** NTP succeeds in Dubai after a flight from Paris, but the RTC remains
Paris civil time; or the current offset works until the next DST transition.

**Required action:** make timezone an independently validated result. A sync is
“fully synchronized” only when both UTC and current civil-time rule/offset are
fresh. The app should derive timezone using phone OS APIs. An Internet fallback
needs a named HTTPS provider, certificate validation, bounded response parsing,
timeouts, and a documented privacy/availability dependency. If only SNTP
succeeds, mark UTC fresh but preserve and visibly flag the stale timezone.

#### 3. Headless BLE security conflicts with painless multi-phone use

**Failure:** a nearby stranger writes a plausible but wrong time, or an attacker
pairs first. With no keyboard/confirmation input, BLE Secure Connections “Just
Works” is still unauthenticated and has no MITM protection; this is explicit in
the [Bluetooth Security Manager
specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html).

**High-assurance option:** use a per-device random secret and app-layer authentication.
The lowest-friction robust onboarding is a QR code on/inside the case that the
companion app scans once. Derive per-message authentication from that secret and
include a nonce/replay counter. BLE encryption is still worthwhile, but is not
the trust decision. If the project intentionally accepts unauthenticated time
writes, call that out as a conscious low-consequence tradeoff and restrict them
to a short, visibly indicated boot pairing window.

Define conflict behavior: accept the first authenticated, sane, fresh source in
a sync window; reject older/replayed sources; permit a materially newer source
to correct it; rate-limit updates. Never average civil times from multiple
phones.

**Selected v1 disposition:** this clock deliberately accepts the lower-assurance
tradeoff appropriate to a non-safety-critical display. Writes require a bonded,
encrypted connection; new bonds are admitted only during a visibly indicated
two-minute boot window; accepted values are range/plausibility checked and
rate-limited; and one simultaneous BLE connection prevents conflicting writers.
There is no QR secret, numeric comparison, or full replay counter. The persistent
five-minute correction bound eventually rejects captured old values but is not
cryptographic replay protection. Sixteen bonds and CCCD records are provisioned;
when full, the first bond enumerated by NimBLE is evicted, not a claimed LRU.

The implemented recovery keeps the on-board BOOT button reachable through a
pinhole: hold it for five seconds while firmware is already running, then
release it to clear sync trust, offset, and bonds and restart. Holding BOOT while
applying power would enter the ROM downloader and is intentionally not the
recovery gesture.

#### 4. Opportunistic open Wi-Fi is a risky, weak fallback

**Failure:** the unit joins a hostile look-alike AP, gets trapped behind a portal,
leaks a stable identifier, accepts spoofed unauthenticated NTP, or repeatedly
burns time trying the same failing network. Terms of use may require a human
acceptance that the clock cannot give.

Captive portals are specifically networks that restrict access until user
conditions are fulfilled, typically through a browser. The IETF architecture
does not provide a headless device a universal way to negotiate access; see
[RFC 8952](https://www.rfc-editor.org/info/rfc8952/).

**Required action:** treat this as optional, last-resort, compile-time-enabled
behavior—not as a reliable requirement:

- attempt only truly open APs; never guess passwords or automate portal/TOS
  acceptance;
- key the “attempted this boot” set by **BSSID**, not only SSID, and cap the set;
- sort candidates deterministically, enforce short association/DHCP/Internet
  timeouts, try each BSSID once per boot, cap the total number and total elapsed
  time, then turn Wi-Fi off;
- prove Internet access before calling it success; distinguish associated,
  DHCP-complete, captive, DNS-failed, and Internet-reachable states;
- do not expose admin servers, OTA, secrets, or local-network discovery while on
  these networks; retain no AP beyond the current boot;
- use TLS with normal certificate/hostname validation for timezone APIs. A
  battery RTC supplies the approximate time needed for certificate validation.
  SNTP itself is not authenticated and must be treated as lower trust;
- randomize the station identity where supported and never send child/family
  identifiers or telemetry.

The owner should explicitly decide whether opportunistic joining is enabled in
the normal build. My recommendation is **disabled by default** until its legal,
privacy, and spoofing tradeoff is accepted.

#### 5. Power loss destroys time without an external RTC

**Failure:** after a flight with the unit unplugged, neither phone nor network is
available and the device starts at an invalid epoch.

**Required action:** fit a DS3231-class battery-backed RTC and store UTC. The
DS3231 is specified for 3.3 V operation, battery backup, and ±2 ppm at 0–40 °C;
see the [manufacturer's
datasheet](https://www.analog.com/en/products/ds3231.html). Check and surface its
oscillator-stop/invalid-time condition at boot.

Use a **non-charging** holder/module with a primary CR2032. Many cheap DS3231
boards contain a charging path intended for a rechargeable cell; do not charge a
CR2032. The coin cell must be captive behind a screwed enclosure because button
cells are a severe ingestion hazard. Do not permanently pot a replaceable cell.

### P1 — hardware and usability risks

#### Platform choice

Use **ESP32-C3 for the travel build**. Use the owned 38-pin ESP32 DevKit for
solderless bring-up; its GPIO21/22 I2C pins make the OLED and BH1750 easy to
test. The C3 has 2.4 GHz Wi-Fi and Bluetooth LE 5, is much smaller than the
original board, and is ample for display, RTC, sensor, BLE, and
occasional Wi-Fi. ESP32-S2 is disqualified because it has no Bluetooth; ESP32-C6
adds protocols this project does not use. Espressif's [chip
comparison](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s2/hw-reference/chip-series-comparison.html)
and [Bluetooth support
table](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32/api-guides/bluetooth.html)
support this choice.

For first bring-up, an official ESP32-C3-DevKitM-1 is the lower-risk reference.
The inexpensive “C3 Super Mini” is a third-party family, not one controlled
board design: qualify the exact batch for USB enumeration, reset/boot behavior,
Wi-Fi/BLE RSSI, 5 V pin behavior, regulator temperature, and continuous
operation before freezing the travel build. Do not use GPIO18/19 for peripherals
on a native-USB C3 design; Espressif assigns them to USB D−/D+.

#### Display selection and logic levels

Test the owned **0.96-inch 128x64 OLED first** through the modular SSD1306
backend. It should yield larger digits than the narrow 0.91-inch unit, normally
128x32. Both require a two-metre test and have an always-on pixel-aging risk.
For the travel build, use a **red 1.2-inch four-digit seven-segment display**
unless an OLED passes those gates. A 0.56-inch module is a
needless readability risk at two metres, while an OLED/TFT adds backlight glow,
cost, and viewing-angle concerns. Red is preferable at low night brightness.
Verify the actual module through a two-metre day/night viewing test before case
design.

The following HT16K33 discussion records a rejected alternative. The selected
TM1637 design and its acceptance gate are documented in `hardware/wiring.md`.
An HT16K33 backpack is attractive but is not automatically a 3.3 V part. Holtek
specifies HT16K33A operation at 4.5–5.5 V and 16 global brightness levels in its
[display-driver
guide](https://www.holtek.com/webapi/148997/guide.pdf). Generic 5 V boards may
pull SDA/SCL to 5 V, which is unsafe for ESP32 pins. Use either:

- a backpack explicitly providing separate 5 V LED power and 3.3 V logic I/O;
  or
- a proper bidirectional I²C level shifter, with pull-ups to 3.3 V on the ESP
  side and 5 V on the display side.

Do not rely on “3.3 V usually reads as high at 5 V.” Verify low-level night
brightness; 1/16 may still be too bright. A dark red neutral-density window and,
if needed, a driver-enable/supply-dimming scheme must not introduce visible
flicker.

#### Ambient-light control

Use a digital lux sensor such as BH1750-class on the 3.3 V I²C bus. Place it
behind a clear/tinted aperture where the display cannot illuminate it. Apply a
several-second low-pass filter, hysteresis, rate limiting, and a hand-tuned
piecewise/logarithmic lux-to-brightness curve. Define safe behavior for sensor
failure: conservative low brightness at night is more important than maximum
day visibility.

Pairing and stale-status animations must obey the same brightness ceiling. No
full-brightness boot flash is acceptable in a dark bedroom.

#### Power and prototype construction

- The compact travel build powers its qualified TM1637 module from 3.3 V. If that
  exact module fails the documented electrical/readability test, use the
  documented 5 V plus BSS138 fallback; never expose the C3 pins to 5 V.
- Use one common ground. Add 100 nF local decoupling at every module and
  100–470 µF near the display/5 V entry to absorb multiplexing/radio transients.
  Validate worst case with all segments at maximum brightness while Wi-Fi
  transmits.
- Keep the 3.3 V I²C section on one side of the level shifter and the 5 V
  display on the other. Use one effective pull-up pair per side. Keep wiring
  short and use soldered perfboard, not removable Dupont jumpers, in the final
  unit.
- Provide strain relief for USB-C and every inter-board wire. The USB connector
  must not be the structural mounting point. Make reset/BOOT recoverable through
  recessed holes.
- Keep copper, wiring, display metal, epoxy, and the enclosure away from the C3
  antenna keep-out. Measure radio range in the finished enclosure.
- Potting is not a substitute for mechanical support. It impedes repair and can
  stress connectors/sensors or detune the antenna. If used, use electronics-safe
  neutral-cure silicone or a characterized low-exotherm compound; never use
  acetic-cure household silicone, cover the antenna/sensor aperture, or encapsulate
  the coin cell/USB connector.

### P2 — state-machine and maintenance quality

Use explicit non-blocking states with deadlines:

`BOOT_SELF_TEST → RTC_DISPLAY → BLE_SYNC_WINDOW → OPTIONAL_WIFI_FALLBACK →
NORMAL_DISPLAY`, with periodic transitions back to a bounded sync window.
Display valid RTC time immediately; networking must never leave the screen blank.

Recommended initial policy for testing is a 60–120 second BLE opportunity,
bounded Wi-Fi fallback after that, and a resync opportunity every six hours plus
on every boot. The exact values are product tuning, not correctness assumptions.
Always retain the last good time while a source is being tried. Use watchdogs
and ensure all failure paths free BLE/Wi-Fi scan results and return to display
service.

The four-digit display needs an unambiguous but quiet state vocabulary documented
in the README, for example:

- normal colon cadence: valid local time;
- brief slow colon pulse: looking for sync;
- distinct dot/colon pattern: pairing available;
- persistent subtle dot: UTC/RTC valid but timezone stale;
- unmistakable startup pattern: RTC invalid.

Do not alternate away from the time continuously. A parent should be able to
diagnose the state without a serial cable, while a sleeping child should not see
a light show.

## Acceptance test checklist

No item below should be replaced by “works on my phone.”

### Time and timezone

- [ ] Cold boot with valid RTC shows time within two seconds, regardless of
      radio availability.
- [ ] Cold boot with invalid/missing RTC never shows a plausible invented time;
      it shows the invalid/pairing state.
- [ ] BLE synchronization is demonstrated on iOS 18 and current iOS, including
      first AccessorySetupKit authorization, backgrounded app, system
      termination/restoration, clock power-cycle reconnect, Bluetooth off, and
      permission denied. A user force-quit is verified to require reopening the
      app. The no-app portal is tested on current Android and iOS.
- [ ] Onboarding and recovery steps match the documented “painless” claim and
      require no serial console.
- [ ] Test vectors cover UTC−12, UTC+14, +05:30, +05:45, a DST spring gap, a DST
      fall fold, year/month rollover, leap day, and a west/east International
      Date Line flight.
- [ ] With only SNTP available, UTC becomes fresh while the old timezone remains
      visibly stale; the firmware does not infer a timezone from UTC.
- [ ] RTC remains within the stated tolerance after 24 hours and seven days
      unpowered; missing/flat battery and oscillator-stop flag are detected.
- [ ] A backward time correction, large forward correction, corrupt payload,
      out-of-range date, stale payload, and replayed payload are handled
      deterministically and cannot crash or wrap the display.

### BLE trust and multiple phones

- [ ] An unbonded phone cannot change time outside the two-minute onboarding
      window, and a connected session cannot write until bonding completes.
- [ ] Captured old values outside the five-minute correction window are
      rejected; tests and documentation do not claim cryptographic replay
      protection.
- [ ] Firmware accepts only one simultaneous BLE connection; two authorized
      iPhones hand off using the automatic-sync toggle without oscillation.
- [ ] Fill the trusted/bond cache, add one more phone, reboot, and verify the
      documented first-enumerated eviction behavior. Also cancel a pairing at
      capacity and verify the documented possible eviction/recovery path.
- [ ] Five-second BOOT hold while running, followed by release, clears sync
      trust/offset/bonds, preserves firmware, visibly shows `rSt`, and restarts.
- [ ] Pairing indication is visible at two metres by day and non-disruptive in a
      dark room.

### Open Wi-Fi fallback

- [ ] Test open AP success, association rejection, no DHCP, DNS failure, no
      route, captive portal, TLS interception/bad certificate, UDP/123 blocked,
      and AP disappearance mid-sync.
- [ ] Two APs with the same SSID/different BSSID are tracked separately; one
      BSSID is never retried in the same boot after terminal failure.
- [ ] Candidate count, per-stage timeout, total fallback duration, memory use,
      and reboot-reset of the attempt set meet documented bounds.
- [ ] No open AP is persisted; no listener/admin/OTA service is reachable; no
      secret or stable child/family identifier is transmitted.
- [ ] A spoofed NTP answer cannot override a fresher authenticated phone source,
      and the display/source-quality state reflects lower trust.
- [ ] Captive-portal terms are never auto-accepted or bypassed.

### Display and light sensing

- [ ] All four digits and status indicators are readable at two metres in bright
      room light across the intended viewing angles.
- [ ] In a dark bedroom after settling, the display remains readable without
      noticeably lighting walls/ceiling; evaluate with the actual red window and
      enclosure, not a bare bench module.
- [ ] Sweep from darkness through normal indoor light to bright daylight:
      transitions are monotonic, take several seconds, do not hunt, and show no
      visible flicker.
- [ ] Cover/uncover the sensor, shine the display toward it, disconnect it, and
      inject bad readings; feedback and failure do not cause bright flashes.
- [ ] Boot, pairing, error, and resync indications all respect the current
      brightness ceiling.

### Electrical, thermal, and mechanical

- [ ] Verify SDA/SCL voltage on both sides of the level shifter; no ESP GPIO ever
      sees 5 V.
- [ ] Test minimum and maximum display brightness with all segments on while
      Wi-Fi and BLE transmit; there are no brownouts, I²C errors, or RTC resets.
- [ ] Test several USB-C supplies/cables, rapid unplug/replug, slow supply ramp,
      and repeated brownouts. Recovery never needs a computer.
- [ ] Run a 72-hour soak with periodic synchronization and forced failures;
      watchdog resets, heap growth, missed display refreshes, and drift are zero
      or within explicitly documented limits.
- [ ] Check regulator, display driver, wiring, and enclosure temperature at
      maximum brightness/radio activity.
- [ ] Perform cable/connector tug, one-metre drop in the final enclosure, luggage
      vibration, and at least 100 USB insertion cycles; no perfboard wire or USB
      connector carries enclosure load.
- [ ] Compare BLE/Wi-Fi RSSI and sync success before and after final enclosure or
      potting; antenna performance has not materially regressed.
- [ ] The coin cell is inaccessible without a tool, is clearly identified as
      non-rechargeable, and is replaceable without damaging the unit.

### Reproducibility

- [ ] A clean checkout builds with pinned toolchain/library versions.
- [ ] The documented board target flashes over its USB-C connector and still
      exposes a recovery/download path after bad application firmware.
- [ ] README schematic, pin table, module voltages, I²C addresses, level shifting,
      exact BOM variants, assembly photos/diagrams, and visual-state legend match
      the physical reference build.
- [ ] Automated host-side tests cover time conversion, DST payloads, source
      arbitration, retry/backoff bookkeeping, corrupt persistent storage, and
      brightness mapping.

## Release gate

The hardware/firmware can be called ready to flash when the electrical and
host-side acceptance items pass. The **product** can be called plug-and-play only
after the companion-app path (or an explicitly reduced requirement) is selected
and end-to-end tested. Open-Wi-Fi fallback must remain visibly “best effort”; it
cannot substitute for a phone onboarding decision or promise location-correct
time.
