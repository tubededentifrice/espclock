# Hardware assembly and wiring

This is the build authority for the solderless bench prototype and the later
compact travel build. The firmware profiles in `platformio.ini` and defaults in
`include/AppConfig.h` must match this file.

## Solderless first prototype

- full-size 38-pin ESP32 DevKit;
- 0.96-inch 128x64 I2C OLED;
- BH1750/GY-302 ambient-light module;
- no DS3231 required for the first functional test;
- breadboard(s) and female-to-female or male-to-female jumpers as appropriate.

Use the `esp32-devkit-oled-128x64` PlatformIO environment. Both modules share
the bus initialized once by the firmware:

| Net | 38-pin ESP32 | SSD1306 OLED | BH1750 | Optional DS3231 |
|---|---|---|---|---|
| `+3V3` | `3V3` | `VCC` | `VCC` | `VCC` |
| `GND` | `GND` | `GND` | `GND` | `GND` |
| `I2C_SDA` | GPIO21 | `SDA` | `SDA` | `SDA` |
| `I2C_SCL` | GPIO22 | `SCL` | `SCL` | `SCL` |

GPIO0 remains connected only to the DevKit's existing BOOT button for the
powered-on five-second recovery gesture. Do not use classic-ESP32 GPIO6/7:
those pins connect to module flash on normal WROOM DevKits. Follow the printed
pin labels, since physical orientation varies. A 38-pin board often covers the
middle breadboard contacts; use two joined breadboards, a wide breadboard, or
jumper one side to a second board.

Expected addresses do not collide: OLED `0x3C` (occasionally `0x3D`), BH1750
`0x23` (`0x5C` with ADDR high), and DS3231 `0x68`. Do not add external I2C
pull-ups blindly: the breakout pull-ups are connected in parallel. If all three
devices are fitted and communication becomes unreliable, measure SDA/SCL
resistance to 3.3 V with power removed and remove one module's pull-up pair if
the effective resistance is excessively low.

The narrow 0.91-inch display is normally 128x32. Move no wires; flash
`esp32-devkit-oled-128x32`. There is no reliable controller query for display
geometry, so these are intentionally separate builds. A malformed or blank
image calls for checking the listing/controller and I2C address, not runtime
auto-detection.

```text
            full-size ESP32 DevKit
         ┌──────────────────────────┐
   3V3 ──┼── OLED VCC ── BH1750 VCC├── optional DS3231 VCC
   GND ──┼── OLED GND ── BH1750 GND├── optional DS3231 GND
GPIO21 ──┼── OLED SDA ── BH1750 SDA├── optional DS3231 SDA
GPIO22 ──┼── OLED SCL ── BH1750 SCL├── optional DS3231 SCL
  GPIO0  │   onboard BOOT only      │
         └──────────────────────────┘
```

With the RTC absent, a successful phone/portal/NTP sync advances normally for
as long as USB remains powered. Complete power loss erases the running instant,
so the next boot must show pairing/no-time and synchronize again. That is a
required bench test, not an error.

### First-breadboard pre-power check

With USB disconnected:

- verify every module's printed `VCC`, `GND`, `SDA`, and `SCL` labels rather
  than relying on connector order;
- confirm `3V3` is not shorted to `GND`;
- confirm SDA and SCL are not shorted to either rail or each other;
- confirm OLED and BH1750 `VCC` connect to `3V3`, never `5V`;
- leave the DS3231 completely disconnected for the first test.

## Compact travel-build stack

- ESP32-C3 Super Mini or Super Mini Plus, 4 MB, USB-C
- red 1.2-inch, four-digit TM1637 module with centre colon
- BH1750/GY-302 ambient-light module
- DS3231 RTC module with a verified non-charging CR2032 arrangement
- 5 x 7 cm, 2.54 mm soldered perfboard
- 100 µF electrolytic and 100 nF ceramic decoupling capacitors
- certified 5 V, 1 A or greater USB supply

## Compact net list

| Net | ESP32-C3 | TM1637 | BH1750 | DS3231 | Other |
|---|---|---|---|---|---|
| `+3V3` | `3V3` | `VCC` | `VCC` | `VCC` | 100 µF positive; 100 nF |
| `GND` | `GND` | `GND` | `GND` | `GND` | capacitor negatives |
| `TM_CLK` | GPIO4 | `CLK` | — | — | keep short |
| `TM_DIO` | GPIO3 | `DIO` | — | — | keep short |
| `I2C_SDA` | GPIO6 | — | `SDA` | `SDA` | 3.3 V pull-ups only |
| `I2C_SCL` | GPIO7 | — | `SCL` | `SCL` | 3.3 V pull-ups only |

Do not use GPIO2, GPIO8, or GPIO9 for modules; they are ESP32-C3 strapping pins. The board's existing GPIO9 BOOT button is intentionally retained for the five-second, powered-on recovery gesture. Leave GPIO18/GPIO19 alone because the Super Mini uses them for native USB.

The C3 Super Mini Plus has a WS2812 RGB LED on GPIO8. For a Plus board with a
128x64 OLED, flash `esp32-c3-super-mini-plus-oled-128x64`. The profile sends an
all-zero RGB frame after the ROM completes its boot-pin checks. Do not connect
an external module to GPIO8. The power indicator is wired to the power rail and
software cannot turn it off. Remove its LED or series resistor only if you have
the exact board schematic and can do the small surface-mount rework safely.

BH1750 address is normally `0x23`; it becomes `0x5C` if the breakout's `ADDR` input is high. DS3231 is `0x68`. The TM1637 signals are not I2C and must stay on their own two pins.

## Compact electrical schematic

```text
 Certified USB-C 5 V supply
             │
      ┌──────┴──────────── ESP32-C3 Super Mini ──────────────┐
      │                                                       │
      │  on-board 3.3 V regulator                             │
      │        │                                              │
      │       3V3───┬──────────┬────────────┬─────||────┐      │
      │             │          │            │    100 nF │      │
      │          TM1637      BH1750       DS3231         │      │
      │          VCC          VCC           VCC          │      │
      │             │          │            │           GND     │
      │             └──────────┴────────────┴─────|(─────┘      │
      │                                          100 µF         │
      │  GPIO4 ─────────────────────────────── TM1637 CLK       │
      │  GPIO3 ─────────────────────────────── TM1637 DIO       │
      │  GPIO6 ───────────── BH1750 SDA ────── DS3231 SDA       │
      │  GPIO7 ───────────── BH1750 SCL ────── DS3231 SCL       │
      │  GND   ────┬──────── TM1637 GND                         │
      │             ├──────── BH1750 GND                         │
      │             └──────── DS3231 GND                         │
      └─────────────────────────────────────────────────────────┘
```

The electrolytic capacitor's stripe is negative and goes to `GND`. Fit the 100 µF capacitor close to the display connector. Fit the 100 nF ceramic across `3V3`/`GND` near the I2C modules.

## DS3231 cell check — mandatory

Many blue ZS-042/HW-111 RTC boards contain a resistor-and-diode charging path intended for an LIR2032. Never install a primary CR2032 until that charge path has been removed or the module is proven non-charging.

1. Inspect the module and find its schematic/listing.
2. For a charging ZS-042-style module, remove the series charging resistor or diode.
3. With no cell fitted, power the module at 3.3 V and measure the holder.
4. Install a branded CR2032.
5. Re-power the board and verify `VBAT` does not rise above the cell's own resting voltage.
6. Put the cell behind a screwed child-resistant enclosure; never pot it permanently.

If the board or charge path cannot be identified confidently, do not use it.

## TM1637 3.3 V acceptance gate

Powering the complete red module at 3.3 V keeps its signal pull-ups safe for the ESP32-C3 and is the simplest first build. The TM1637 manufacturer's guaranteed electrical table is specified at higher supply voltage, so test the exact purchased module before enclosure work:

1. cold-start it repeatedly from a disconnected USB supply;
2. show all segments at brightness levels 0 through 7;
3. transmit Wi-Fi at maximum display brightness for at least 30 minutes;
4. confirm the C3 does not reset and the display does not corrupt;
5. verify two-metre readability in daylight and minimum brightness in a dark bedroom.

If it fails at 3.3 V, power only the display from USB `5V` and put a two-channel BSS138 bidirectional level-shifter module between GPIO4/GPIO3 and the display's `CLK`/`DIO`. The low side is 3.3 V; the high side is 5 V. Never connect a 5 V-powered module's pull-ups directly to ESP pins.

## Perfboard placement

Use [the placement drawing](protoboard-layout.svg) as a routing guide, not as a manufactured-PCB artwork.

- Put the C3 antenna end at or slightly beyond a perfboard/case edge with no copper, coin cell, display metal, or fastener in front of it.
- Socket the C3 so a failed clone can be replaced.
- Keep the USB-C connector exposed and strain-relieved. Do not use it as a structural support.
- Put the BH1750 at a front/top clear aperture, shaded from direct display light.
- Keep the DS3231 and cell accessible after opening the case.
- Solder all final wires. Dupont jumpers are only for bench bring-up.
- Use printed clips or neutral-cure electronics-safe silicone for strain relief. Do not use acetic-cure household silicone.
- Do not pour epoxy on the first prototype. It makes faults, the RTC cell, USB connector, and optical sensor difficult or impossible to service and can detune the antenna.

## Compact-build pre-enclosure continuity checks

With USB and CR2032 removed:

- `3V3` to `GND` is not shorted.
- `5V` to `3V3` is not shorted.
- GPIO3/4/6/7 are not shorted to either rail.
- SDA/SCL resistance to `3V3` indicates sensible effective pull-ups, roughly 2.2–10 kΩ.
- No module net is connected to GPIO2/8/9/18/19.
- On a Super Mini Plus, the onboard RGB LED on GPIO8 is the only permitted use
  of GPIO8 and the dedicated firmware profile is selected.
- Electrolytic polarity is correct.

Continue with the full acceptance checklist in the project README before closing the case.
