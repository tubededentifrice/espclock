# Hardware research and recommendation

Research and prices checked 2026-07-25. This regional snapshot uses UAE prices
in AED for a consistent comparison; it does not assume contributors are in the
UAE. Marketplace prices move constantly and shipping can cost more than the
parts, so the purchase links below are primarily stable search links and the
prices are realistic ranges rather than quotes.

## Executive recommendation

Prototype without solder on a **38-pin ESP32 DevKit**,
**0.96-inch 128x64 SSD1306 OLED**, and **BH1750/GY-302**. Build the travelling
version around an **ESP32-C3 Super Mini**, a **red 1.2-inch four-digit TM1637
module**, the same ambient-light module, and a
**battery-backed DS3231 RTC**. Assemble it on 2.54 mm perfboard with socket
headers so the ESP and sensors remain replaceable. Power the display, sensor and
RTC from the C3's regulated 3.3 V rail so no ESP pin is exposed to 5 V.

The C3 is the smallest, least expensive suitable controller in the compared
set while retaining both 2.4 GHz Wi-Fi and BLE. The 1.2-inch display sets a
roughly 130 x 50 mm front face: still a small travel-alarm-clock form, but large
enough to meet the stated two-metre viewing distance without depending on
perfect eyesight. The core travel-build hardware is approximately **AED
40–98**, excluding shipping, enclosure, cable, and power supply.

There is one important product limitation: **a standards-only Bluetooth
connection cannot automatically obtain local time from every Android and iOS
phone without phone software or user interaction**. BLE Current Time Service is
useful on compatible iPhones, but is not universal. The implemented initial source order is:

1. retain the last valid UTC time in the DS3231;
2. accept UTC and current offset from an authorized phone through the custom
   encrypted BLE write service;
3. expose a no-installed-app SoftAP captive page that automatically transfers
   the phone browser's epoch and current offset after the user joins the AP;
4. try each truly open Wi-Fi BSSID once per boot and use NTP for UTC while
   retaining the last phone-confirmed offset.

The captive-page path needs one phone action ("join `KidsClock-xxxx`"), but no form,
account, app, Wi-Fi password, or manual time entry. It is the most painless
cross-platform fallback technically available.

BLE runs alone for 10 seconds on a cleared clock, then coexists with the setup
portal until the two-minute mark before NTP fallback starts. The first
successful route is persisted: BLE refreshes every six hours, while portal and
NTP routes refresh every 24 hours after a 90-second opportunity for BLE to
take over. A later accepted BLE update permanently promotes either Wi-Fi route
and stops Wi-Fi activity. This avoids powering all discovery paths forever
while retaining BLE as the highest-priority upgrade.

## ESP board comparison

| Board | Radio capability | Typical board size / USB | Useful observations | Verdict |
|---|---|---|---|---|
| Classic ESP32, 38-pin | 2.4 GHz 802.11b/g/n, Bluetooth Classic and BLE 4.2 | about 54.4 x 27.9 mm, usually Micro-USB | Dual-core 240 MHz and many GPIOs, but much larger; Classic Bluetooth adds no useful phone-time standard | Excellent bench prototype, unnecessarily large final board |
| ESP32-C3 Super Mini | 2.4 GHz 802.11b/g/n and BLE 5; no Classic Bluetooth | about 22.5 x 18 mm, USB-C | 160 MHz RISC-V, 4 MB flash on common boards, 11 exposed GPIOs; enough for two display pins and one I2C bus | **Recommended** |
| LOLIN/Wemos S2 Mini | 2.4 GHz 802.11b/g/n; **no Bluetooth radio** | 34.3 x 25.4 mm, USB-C | Plenty of GPIO and native USB, but cannot satisfy the primary BLE requirement | Reject |
| ESP32-C6 Mini N4 | 2.4 GHz 802.11b/g/n/ax, BLE 5.3, 802.15.4 Thread/Zigbee | roughly 22.5 x 18 mm for the generic Super Mini, USB-C | 160 MHz RISC-V, 512 KB SRAM, 4 MB flash. Better/newer radio feature set, but Thread, Zigbee and Wi-Fi 6 do not help this clock; board costs roughly twice a C3 and its hobby-board ecosystem is younger | Good drop-in alternative, not enough benefit here |

Authoritative radio references:

- Espressif's [classic ESP32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
  describes it as a Wi-Fi + Bluetooth + BLE SoC; its
  [DevKitC guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html)
  includes board schematics and dimensions.
- Espressif describes the
  [ESP32-C3](https://www.espressif.com/en/products/socs/esp32-c3) as a
  Wi-Fi and Bluetooth 5 LE device; the
  [current datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.pdf)
  is the electrical authority.
- Espressif's [ESP32-S2 product page](https://www.espressif.com/en/products/socs/esp32-s2)
  and [datasheet](https://documentation.espressif.com/esp32-s2_datasheet_en.html)
  specify Wi-Fi only. Wemos specifies the
  [S2 Mini as 34.3 x 25.4 mm](https://docs.wemos.cc/en/latest/s2/s2_mini.html).
- Espressif's [ESP32-C6 product page](https://www.espressif.com/en/products/socs/esp32-c6)
  and [current datasheet](https://documentation.espressif.com/esp32-c6_datasheet_en.pdf)
  specify Wi-Fi 6, BLE 5.3 and 802.15.4.

The C3 Super Mini is a generic design, not one tightly controlled product.
Inspect the received board for the expected ESP32-C3 marking and test Wi-Fi/BLE
range before encapsulating it. Some very cheap clones have mediocre ceramic
antenna layout. Do not put copper, perfboard ground plane, a coin cell, epoxy
filled with conductive pigment, or metal case hardware immediately above or in
front of the antenna end. If a particular C3 has poor RF range, substitute a C6
Mini N4 before redesigning anything.

Physical comparison on 2026-07-25 demonstrated why this qualification is a
hard gate. One bare Super Mini received ten nearby Wi-Fi networks (strongest
-67 dBm) but emitted neither a minimal service-bearing BLE advertisement nor a
minimal open SoftAP at 5–10 cm, before or after a full RF-calibration/NVS erase.
A second bare Super Mini on the same cable and identical pinned toolchain
emitted the service advertisement at -48 to -45 dBm and a channel-1 SoftAP at
-34 to -35 dBm. That isolates the failed board's shared RF transmit
path/matching hardware; an API reporting “advertising active” or “SoftAP
started” is not an over-the-air acceptance result.

## Display choice

### Recommended: red 1.2-inch TM1637 clock module

Search for exactly **`1.2 inch 4 digit TM1637 red clock display module`**:

- [AliExpress search](https://www.aliexpress.com/wholesale?SearchText=1.2+inch+4+digit+TM1637+red+clock+display)
- [Amazon.ae search](https://www.amazon.ae/s?k=1.2+inch+4+digit+TM1637+red+clock+display)

Expected price is AED 15–40; Amazon.ae stock is intermittent. Confirm the listing
actually says **TM1637**, **four digits**, **clock colon**, and **red** rather
than buying a visually similar bare display. Red remains legible at much lower
perceived room illumination than blue or white and is kinder in a dark bedroom.
The common assembled board is roughly 120–127 x 40–50 mm with approximately
30 mm digits. It is markedly more comfortable at two metres than the common
14 mm hobby clock modules.

The TM1637 provides eight brightness commands, with a lowest on-duty of 1/16
([TM1637 datasheet](https://datasheet.lcsc.com/szlcsc/Shenzhen-Titan-Micro-Elec-TM1637_C20001.pdf)).
At night, combine the lowest setting with a dark red/smoked acrylic window or
neutral-density film. The filter is important because some displays remain too
bright at their minimum code. Avoid slow software blinking as a dimming method;
visible flicker is unpleasant beside a bed.

Power the complete module from 3.3 V so its CLK/DIO pull-ups cannot drive an ESP
pin above 3.3 V. This is a common and deliberately simple ESP32 hookup, and red
LEDs have enough forward-voltage headroom, but the TM1637 manufacturer's
published electrical-characteristic table is measured at 4.5–5.5 V. Therefore
3.3 V brightness and cold-start operation on the **exact purchased 1.2-inch
module are hardware acceptance gates**, not assumptions. If it does not pass,
the robust remedy is 5 V display power plus a two-channel BSS138 bidirectional
level shifter on CLK/DIO; never connect a 5 V-powered module directly to ESP
GPIO. Also watch the C3 board's 3.3 V rail during Wi-Fi transmit at full display
brightness: generic Super Mini regulators vary. If it falls below 3.1 V or
resets, give the display its own adequately-rated 3.3 V regulator from USB VBUS,
with common ground and the same 3.3 V CLK/DIO logic.

### Compact alternative

A **red 0.56-inch four-digit TM1637 module** costs only AED 4–10 and shrinks the
front face to roughly 55 x 30 mm, but its 14 mm digits are borderline at the
specified two metres and it is rejected for the first build. An HT16K33
1.2-inch backpack is readable but usually costs more and its 5 V I2C pull-ups
require level translation, with no benefit to this four-digit clock. A
continuously-lit OLED is still a weaker final-build choice because it has a
smaller useful digit height and is susceptible to long-term burn-in. However,
the two supported OLED sizes are excellent solderless bring-up displays. The
0.96-inch 128x64 unit is the first test target. The narrow 0.91-inch module is
normally 128x32, giving roughly half the available digit height. Firmware
profiles support both; two-metre readability remains a physical acceptance
gate.

## Ambient-light sensor

Use a **BH1750FVI GY-302 I2C ambient-light breakout**, powered at 3.3 V. Search
**`GY-302 BH1750 light sensor module`**:

- [AliExpress search](https://www.aliexpress.com/wholesale?SearchText=GY-302+BH1750+light+sensor+module)
- [Amazon.ae search](https://www.amazon.ae/s?k=GY-302+BH1750+light+sensor)

Expected cost is AED 3–8 from AliExpress, usually more in an Amazon multipack.
ROHM specifies a digital 16-bit I2C ambient-light sensor with an approximately
1–65,535 lux range, visible-light response designed to reduce IR influence, and
0.5 lux resolution in high-resolution mode 2; see the
[BH1750FVI datasheet](https://www.robotpark.com/image/data/PRO/91421/bh1750fvi-e.pdf).
The IC supply range is 2.4–3.6 V, so powering a GY-302 breakout from 3.3 V keeps
both power and I2C levels ESP-safe. The lowest readings are coarse compared with
more expensive sensors, but the clock only needs to select a minimum night
level, not measure scientific sub-lux differences.

Put the sensor behind its own clear or lightly frosted aperture on the front or
top of the enclosure. Shield it from direct light leaking from the seven-segment
display, otherwise the brightness loop can oscillate. Use a 5–10 second filtered
average plus hysteresis, and map lux logarithmically to the eight display
levels. One-shot high-resolution measurements provide the same one-second
brightness input while allowing the sensor to return to power-down between
conversions.

## RTC and backup power

A DS3231 is not needed to count time while USB remains powered; the ESP system
clock can do that. It **is needed for the promised unplug, travel, and plug-in
experience**: the ESP's clock does not survive complete removal of power, and a
phone or open network may not be available at the next boot.

The DS3231 maintains time from its battery input when main power disappears and
is specified to ±2 ppm from 0–40 °C by Analog Devices
([product page and datasheet](https://www.analog.com/en/products/ds3231.html)).
Store UTC in the DS3231 and retain the last timezone/offset separately in ESP
NVS. An RTC preserves an already-known instant; it cannot discover the new
timezone after travel.

Search **`DS3231 RTC module CR2032`**:

- [AliExpress search](https://www.aliexpress.com/wholesale?SearchText=DS3231+RTC+module+CR2032)
- [Amazon.ae search](https://www.amazon.ae/s?k=DS3231+RTC+module)

Budget AED 6–15 for the module and AED 2–5 for a branded CR2032.

### Coin-cell hazard

The DS3231 IC itself correctly supports a primary coin cell, but many large blue
**ZS-042/HW-111 modules add a diode and roughly 200-ohm resistor intended to
charge an LIR2032**. A normal CR2032 is not rechargeable. Do not put a CR2032 in
such a board until the charge path has been disabled by removing the series
resistor or diode and continuity/voltage has been checked. This module teardown
shows the issue and modification:
[DS3231 module charging circuit](https://electronics.stackexchange.com/questions/134725/ds3231-module-circuit-teardown).

Prefer a breakout explicitly documented for a non-rechargeable CR2032 and with
no charge path. Whichever module is used, power its VCC from 3.3 V, verify with a
multimeter that powered VBAT never rises above the cell's own voltage, and do
this test before potting or giving the clock to a child. Do not substitute a
cheap LIR2032: the crude module circuit is not a good lithium-ion charger.
Button cells are a severe ingestion hazard, so the finished case must retain the
cell behind screws; a friction lid, exposed holder, or tool-free battery door is
not acceptable.

## BLE phone-time feasibility

The Bluetooth SIG
[Current Time Service (CTS), UUID 0x1805](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/CTS_v1.0/out/en/index-en.html)
defines a GATT **server** that exposes local date/time and optionally local UTC
offset/DST information to a GATT **client**. It does not require every phone OS
to host that server.

### iOS

Apple's accessory guidance has documented the iOS GATT Current Time Service for
connected accessories, so an ESP acting as the GATT client can read it on
compatible iPhones. However, iOS system services are not guaranteed to be
available immediately or in every connection state; use Service Changed and
discovery rather than assuming a fixed handle. See Apple's current
[Accessory Design Guidelines](https://developer.apple.com/accessories/Accessory-Design-Guidelines.pdf)
and the analogous service-availability warning in Apple's
[Apple Media Service reference](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Specification/Specification.html).

This still does not make a generic ESP accessory universally "pair from
Settings and forget." Modern Apple setup APIs such as
[AccessorySetupKit](https://developer.apple.com/documentation/accessorysetupkit/setting-up-and-authorizing-a-bluetooth-accessory)
are app-driven. Generic GATT devices are normally found and connected by a
CoreBluetooth app; the Bluetooth Settings UI is reliable for OS-supported
accessory profiles, not arbitrary custom GATT products. Masquerading as a
keyboard, audio device, or other unrelated profile merely to appear in Settings
would be confusing and is not recommended.

### Android

Android does not expose CTS as a universal system service. The Android Open
Source Project explicitly says
["Android doesn't support this out of box"](https://source.android.com/docs/automotive/time/time_zone_options)
and that phone CTS support covers too little of the market to rely upon.
Android's BLE documentation also describes an
[application acting as the GATT client](https://developer.android.com/develop/connectivity/bluetooth/ble/connect-gatt-server);
pairing alone does not cause Android to write its clock into an accessory.

Android Settings can pair supported accessories, but generic BLE behavior
varies by vendor/version and newer systems can require an app to connect to the
GATT service. Therefore the firmware may advertise and bond with multiple
phones, but a bond must not be interpreted as a time source. "Any number" also
cannot literally mean infinite stored bonds: flash is finite. The build permits
up to 16 stored bonds and 16 persisted notification records, allows new pairing
only during the visible boot window, and evicts the first bond enumerated by
NimBLE rather than claiming a true LRU policy. Only one BLE connection is
compiled in because the tiny protocol has one global synchronization
transaction. The clock remains ownerless; any compatible family phone can take
a turn sequentially.

The current custom service is advertised in a connectable legacy PDU with an
explicit, non-truncated split: flags, complete 128-bit service UUID, and
preferred connection intervals consume 27 primary-advertisement bytes; the
complete 14-character `KidsClock-xxxx` name consumes 16 scan-response bytes.
Bondability is negotiated after connection and is not represented by a generic
advertising-data “pairable” flag. The two-minute new-phone window retains the
30–60 ms discovery cadence already proven with AccessorySetupKit; afterward the
same connectable payload uses an 800–1000 ms cadence for lower-duty bonded
reconnects. A physical BLE scanner and iPhone remain the authority for radio,
timing, background reconnect, and AccessorySetupKit discovery acceptance.

### Practical no-app fallback

When automatic sources fail, start a WPA2-free local AP named
`KidsClock-xxxx` and a captive portal. The page needs no controls: JavaScript
reads `Date.now()` plus the browser's current UTC offset, sends both to the ESP,
shows the confirmed result, and disconnects. This works in normal browsers on iOS and Android
without an installed app. It still requires the user to join that Wi-Fi network;
print a short instruction and QR code on the case. A Web Bluetooth page is not a
cross-platform replacement because Safari/iOS does not provide the same general
Web Bluetooth workflow as Chromium Android.

## Open-Wi-Fi limitations

Only attempt networks whose scan result explicitly reports no link-layer
authentication. Never try guessed passwords and never treat a similarly-named
network as the same network: track failures by BSSID for the current boot.
After association, require DHCP; a successful bounded NTP response is the
implemented Internet/time proof. Mark that BSSID failed on association timeout,
DHCP timeout, NTP failure, or total-window exhaustion, and do not retry it until
the next boot. Captive portals normally fail this proof and are not bypassed.

NTP returns UTC, not the local timezone. Automatic local display on open Wi-Fi
therefore also needs an IP-geolocation/timezone HTTPS service. That adds an
external-service dependency and exposes the network's public IP to the chosen
provider. Captive portals commonly block NTP and HTTPS until terms are accepted,
and random open networks may be untrusted or unlawful to use. Open Wi-Fi should
remain a bounded best-effort fallback, never the foundation of correct time.

## Bill of materials

| Qty | Part | Selection notes | AliExpress estimate | Amazon.ae estimate |
|---:|---|---|---:|---:|
| 1 | 38-pin ESP32 DevKit | Solderless reference target | AED 15–30 | AED 25–55 |
| 1 | 0.96-inch SSD1306 128x64 OLED | Default bench display profile | AED 5–12 | AED 15–35 |
| 1 | 0.91-inch SSD1306 OLED, likely 128x32 | Optional comparison profile | AED 4–10 | AED 15–30 |
| 1 | ESP32-C3 Super Mini, 4 MB, USB-C | Travel target; verify radio and pinout | AED 9–18 | AED 20–45 |
| 1 | TM1637 1.2-inch red 4-digit module with centre colon | Buy an assembled module, not bare digits | AED 15–40 | AED 35–70 |
| 1 | BH1750FVI GY-302 I2C breakout | Power at 3.3 V; ADDR tied low/default | AED 3–8 | AED 10–30 |
| 1 | DS3231 RTC breakout | Cheap units are clone-risk; qualify drift and verify no cell charger | AED 6–15 | AED 20–45 |
| 1 | Branded CR2032 | Only after charge-path check | AED 2–5 | AED 5–10 |
| 1 | 5 x 7 cm single-sided 2.54 mm perfboard | Cut down after test | AED 3–7 | AED 10–20/pack |
| 2 | 8-pin 2.54 mm female socket strips | Socket the C3; cut longer strip to size | AED 1–3 | AED 8–15/pack |
| 1 | 100 µF, ≥10 V electrolytic plus 100 nF ceramic | Display bulk/rail decoupling | AED 1–2 | usually from assortment |
| — | 24–28 AWG solid wire, solder, heat-shrink, red smoked window | Workshop consumables | AED 3–8 allocated | AED 10–25 |

The core travel configuration (C3, TM1637, BH1750, RTC/cell, perfboard, sockets,
and decoupling) is approximately **AED 40–98 from AliExpress** before shipping,
enclosure, cable, and power supply. Including the listed consumables allowance
brings it to approximately **AED 43–106**. The default bench configuration
(38-pin ESP32, 128x64 OLED, and BH1750) is approximately **AED 23–50**.
Amazon.ae commonly requires multipacks, making the checkout total higher even
though spare parts remain.

## Prototype-board wiring

Recommended C3 Super Mini pin assignment (confirm silkscreen against the actual
board before soldering):

| C3 pin | Connection | Notes |
|---|---|---|
| 3V3 | BH1750/GY-302 VCC; DS3231 VCC; TM1637 VCC | Never connect this rail to 5 V |
| GND | Every module GND | One common ground |
| GPIO3 | TM1637 DIO | Bidirectional display data |
| GPIO4 | TM1637 CLK | Bit-banged display clock |
| GPIO6 | I2C SDA to BH1750 SDA and DS3231 SDA | Shared 3.3 V bus |
| GPIO7 | I2C SCL to BH1750 SCL and DS3231 SCL | Shared 3.3 V bus |
| GPIO8 | On-board LED/strapping pin on some clones | Leave untouched; the build does not depend on its varying polarity or wiring |
| GPIO9 | Existing BOOT button | Hold five seconds after normal boot, then release, to clear sync trust/offset/bonds and restart; never require it in daily use |

I2C addresses are compatible: BH1750 is `0x23` with ADDR low/default (`0x5C`
with ADDR high) and DS3231 is `0x68`. The TM1637 bus only resembles I2C and
remains on its own GPIO4/GPIO3 pair. Most I2C breakout boards already contain
pull-ups. With power disconnected, measure resistance from SDA/SCL to 3.3 V; do
not blindly parallel extra pull-ups. A combined effective value around 2.2–10
kΩ is appropriate for these short, 100 kHz wires.

```text
                              USB-C
                                |
                         C3 Super Mini
                                |
       3V3 -----+----------------+----------------+------ TM1637 VCC
                |                |                |         + 100 uF
            BH1750/GY-302     DS3231              |             |
              0x23             0x68               |        common GND
 GPIO6 SDA ----SDA--------------SDA               |
 GPIO7 SCL ----SCL--------------SCL               |
 GPIO4 CLK --------------------------------------- TM1637 CLK
 GPIO3 DIO --------------------------------------- TM1637 DIO
 GND ----------------------------------------------------- all grounds
```

Layout the display at the front, BH1750 at a shaded top/front aperture, and
the C3 antenna at a case edge with clear space. Place the 100 µF capacitor
between display 3.3 V and ground close to the display. Route the I2C wires
together and keep all signal wires short. Use socket strips for the ESP and
connectors or strain relief for the display/sensor wires. After all tests pass,
trim the perfboard and secure heavy modules with neutral-cure electronics-safe
silicone or printed clips.

Do **not** pour ordinary hardware-store acetic-cure silicone over electronics;
its cure chemistry can corrode copper. Do not pot the first prototype in epoxy:
potting makes the RTC cell, USB connector, and failed modules unserviceable and
can detune the antenna. A screwed case with internal strain relief is the better
first build. If later conformal coating or potting is desired, leave the antenna,
USB-C connector, BH1750 optical window, socketed ESP, and coin cell accessible.

## Hardware acceptance tests before enclosure

1. Flash over USB-C repeatedly and verify automatic boot without pressing BOOT.
2. Run a 24-hour display test at all brightness codes; confirm the minimum plus
   selected filter is visible but does not illuminate a dark room.
3. Compare BH1750 readings in full dark, bedroom light, room light, and sunlit
   room; ensure display light does not materially change the reading.
4. Unplug USB for at least one hour, then verify DS3231 continuity and drift.
5. With USB powered, measure VBAT and prove the CR2032 is not being charged.
6. Exercise BLE and Wi-Fi at 5 m with the intended case materials temporarily in
   place; reject a poor-antenna C3 before assembly.
7. Let Wi-Fi transmit at maximum display brightness for an hour from a marginal
   5 V USB supply; there must be no brownout, flicker, spontaneous reset, hot
   regulator, or 3.3 V rail below 3.1 V.
8. Test the complete sync UX on at least one current iPhone and two unrelated
   Android brands. Verify the custom bonded GATT write with compatible clients
   and verify captive-portal time transfer on both phone families.
