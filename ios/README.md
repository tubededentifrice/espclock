# Kids Clock iPhone companion

This iOS 18+ app gives each added clock regular, timezone-aware synchronization
without an Internet connection. It uses Apple's AccessorySetupKit for one-tap
onboarding. It then keeps one low-duty Bluetooth LE connection to each clock
that has automatic sync enabled, through one Core Bluetooth central manager.
Each ESP clock requests a fresh phone time about every six hours. A status
notification can wake the suspended app so it can answer.

The app has no account, server, advertising, analytics, location permission, or
third-party dependency. It sends only the iPhone's Unix time and the current UTC
offset to the authorized clock.

## Requirements

- an iPhone running iOS 18 or newer;
- Xcode 16 or newer on a Mac;
- a free or paid Apple developer account selected in Xcode;
- the ESPClock firmware from this repository already flashed;
- Bluetooth enabled on the iPhone.

BLE does not work in the iOS Simulator. Final pairing and background behavior
must be tested on a physical iPhone.

## Authorization lifecycle

Fresh launch activates only `ASAccessorySession`. The app does not construct
`CBCentralManager` until AccessorySetupKit has authorized at least one clock:

1. the picker is presented with no central manager in the process;
2. `.accessoryAdded` retains the selected accessory but does not connect;
3. `.pickerDidDismiss` creates one central manager and uses all authorized
   accessories' `bluetoothIdentifier` values to retrieve and connect them;
4. `.activated` creates the manager immediately only when the session already
   contains one or more authorized clocks.

This ordering lets AccessorySetupKit authorize the first accessory without
asking for broad access to nearby Bluetooth devices. The same picker can then
add more clocks without stopping existing clock connections. The app retains
the `bluetooth-central` background mode and its stable, per-install restoration
identifier. On a fresh install, picker cancellation or failure leaves no scan,
connection attempt, or central manager.
The information property list intentionally carries both
`NSAccessorySetupSupports` and `NSAccessorySetupKitSupports`: current
documentation names the former, while a physical iPhone on iOS 26.5.2
runtime-validates the latter and terminates session activation if it is absent.

## Install on an iPhone

1. Open `ios/ESPClockCompanion.xcodeproj` in Xcode.
2. Select the **ESPClockCompanion** target, open **Signing & Capabilities**, and
   choose your development team.
3. If Xcode reports that `com.espclock.KidsClockCompanion` is unavailable,
   change it to a unique bundle identifier such as
   `com.yourname.KidsClockCompanion`.
4. Connect and unlock the iPhone, select it as the run destination, and press
   **Run**.
5. Accept the iPhone's trust/developer-mode prompts if this is the first local
   app you have installed.

A free personal team is sufficient for development-device testing, but its
provisioning expires and the app must periodically be rebuilt. For durable
family installation, use a paid Apple Developer account and distribute through
the App Store, an unlisted App Store listing, or another Apple-approved method.
TestFlight builds also expire.

## Pair the clock

1. Power-cycle the clock that you want to add.
2. Open **Kids Clock** and tap **Add Kids Clock** or **Add Another Clock** while
   that clock accepts new phones—during the first two minutes after boot.
3. Select `KidsClock-xxxx` in Apple's accessory picker and approve Bluetooth
   pairing.
4. Wait until the app says **Connected and synchronized**.
5. Repeat these steps for each additional clock.

Each clock section title uses the complete Bluetooth name, such as
`KidsClock-06E8`. The app reads this name from the advertisement or the standard
GAP Device Name value and saves it for later launches.

The no-app portal starts 10 seconds after boot if BLE has not synchronized. It
stays available until the initial two-minute fallback starts. If the picker
finds nothing, unplug and reconnect the clock, then try once more nearby.

Before the first authorization, the app says **Ready to add clock**. While
Apple's picker is open, it says **Finding accessories…**. **Turn on Bluetooth**
appears only after authorization when the central manager reports
`.poweredOff`; `.unauthorized`, `.unsupported`, `.resetting`, and `.unknown`
have separate messages.

## Normal behavior

- Leave **Keep synchronized** enabled for each clock that this iPhone owns.
- The app has no fixed clock-count limit. It keeps separate connection,
  acknowledgement, retry, and last-sync state for every authorized clock.
- It is safe to leave the app in the background; do not swipe it away.
- The app sends time to each clock immediately after that clock reconnects.
- While connected, each clock asks for another update every six hours.
- After a clock power cycle, iOS automatically reconnects to that clock when
  the phone is in range and the operating system permits it.
- Each clock has its own **Sync Now**, automatic-sync switch, last-sync value,
  status, and remove action.
- A removal confirmation opens from the selected clock's **Remove Clock**
  button and names that clock.
- Each clock's DS3231 keeps time when no phone is nearby, so missed BLE updates
  do not stop a clock.

One iPhone can keep automatic sync enabled for many clocks at the same time.
However, only one iPhone should keep automatic sync enabled for a given clock.
Each ESP clock permits one active synchronization connection. Turn off that
clock's switch on the current phone before another authorized family phone
takes ownership of it.

The app does not set an artificial maximum number of clocks. The practical
number of live Bluetooth connections depends on iOS radio and memory resources,
the radio environment, and whether all clocks are in range.

## What iOS cannot guarantee

The design uses `bluetooth-central`, a stable Core Bluetooth restoration
identifier, AccessorySetupKit authorization, notification subscriptions, and
iOS automatic reconnect. These are the supported mechanisms for background BLE,
but they are event-driven—not a guaranteed six-hour timer.

Background synchronization stops when:

- the user force-quits the app from the app switcher;
- Bluetooth is disabled or permission is revoked;
- the phone and clock are out of range;
- the app is uninstalled or its signing/provisioning expires;
- iOS has not yet allowed apps to run after a phone reboot/first unlock.

Open the app once to recover after those conditions. The clock continues from
its RTC and still offers the no-app captive portal after boot.

Starting with iOS 26, Bluetooth state-restoration relaunch is available only for
accessories originally onboarded through AccessorySetupKit. That is why this app
requires iOS 18+ and does not use a custom in-app scanner for first setup.

## Remove or recover

Each **Remove Clock** action revokes only that clock's iPhone-side
AccessorySetupKit authorization. Other clocks stay connected. The removed ESP
may still retain its BLE bond. If adding it again fails, keep the clock powered
and hold its recessed **BOOT** button for five seconds until `RESET` appears on
OLED or `rSt` on TM1637, then release it. The full-size ESP32 uses its GPIO0
button and the C3 uses GPIO9. The clock clears bonds and restarts; then add it
again within two minutes. This also resets the confirmed timezone and
anti-large-correction marker, so use it only for recovery.

## Protocol and implementation

The app subscribes to the status characteristic before writing a 12-byte,
little-endian packet with response:

| Byte | Value |
|---:|---|
| 0 | protocol version `1` |
| 1–8 | signed Unix UTC seconds |
| 9–10 | signed UTC offset minutes |
| 11 | reserved flags, currently zero |

It accepts `time-accepted` as the application-level acknowledgement and performs
a status read if that notification is delayed. Each clock has an independent
acknowledgement deadline and bounded retry sequence. One central manager owns
all clock peripherals and restores all connected or pending peripherals after
an eligible iOS relaunch. The OS owns long-range pending reconnect behavior.
Firmware status values are exact UTF-8 bytes without a trailing NUL. The time
characteristic advertises the base GATT write capability and separately
requires an encrypted link; both properties are necessary for Core Bluetooth
to permit `.withResponse` writes.

## Reproduce and capture onboarding diagnostics

Use a physical iPhone attached to Xcode. Delete the app first, remove any Kids
Clock entry under **Settings → Privacy & Security → Accessories**, then use the
five-second powered-on BOOT recovery gesture to clear the ESP's bonds. Do not
hold BOOT while applying power.

1. In Xcode, run the rebuilt app on the unlocked iPhone and show the debug
   console.
2. Start the firmware serial monitor with
   `uv run --locked opendle-pio device monitor --baud 115200`.
3. Power-cycle the ESP and confirm its serial log says `[BLE] advertising
   start=ok`; `PAIR` alone is not proof of a successful radio start.
4. Confirm the app initially says **Ready to add clock**. Bluetooth should be
   enabled globally; the app should be managed under the Accessories privacy
   page rather than receiving access to every nearby Bluetooth device.
5. Tap **Add Kids Clock**, exercise setup or cancellation, and save both logs.

The Xcode category `BLEOnboarding` records every `ASAccessorySession` event,
every `CBCentralManager` state transition, picker error domain/code, and whether
a central manager existed when the picker opened or appeared. It deliberately
does not log Bluetooth identifiers. The expected fresh-install sequence is:

```text
Accessory session event=activated centralManagerExists=false
Opening accessory picker; centralManagerExists=false
Accessory session event=pickerDidPresent centralManagerExists=false
Accessory session event=accessoryAdded centralManagerExists=false
Accessory session event=pickerDidDismiss centralManagerExists=false
Creating post-authorization central manager
Central manager state=poweredOn
```

When you add a later clock, the existing central manager and clock connections
remain active while the picker is open. The fresh-install sequence above still
has `centralManagerExists=false`.

The earlier failure symptom to compare against is: system Bluetooth on, the app absent
from broad Bluetooth privacy settings, an empty picker, and a manager created
before authorization reporting `.poweredOff`. That baseline still requires an
actual fresh-install capture; the generic-device build cannot reproduce a
physical radio/privacy state.

Picker failures appear in the UI and log as an error domain and numeric code.
This preserves actionable detail without displaying or logging accessory
identifiers.

## Independently verify the advertisement

Before evaluating the picker, inspect the ESP with an independent BLE scanner
on a second phone or Mac. During the two-minute `PAIR` window confirm:

- complete local name `KidsClock-xxxx`;
- complete service UUID `7F510000-1B15-4DC7-9F3F-19B30A6F6A21`;
- a connectable legacy advertisement that continues throughout the window.

The onboarding advertisement remains at the previously qualified 30–60 ms
interval. When the two-minute window closes, the same name, UUID, scan response,
and connectable PDU continue at an 800–1000 ms interval for lower-duty bonded
reconnects. Confirm both cadences over the air; an API log is not sufficient.

The firmware explicitly emits a 27-byte primary advertisement: 3-byte
general-discoverable/BR-EDR-unsupported flags, 18-byte complete 128-bit service
UUID field, and 6-byte preferred-connection-interval field. The 16-byte scan
response contains the complete 14-byte name plus its field overhead. Both are
below the 31-byte legacy packet limit.

BLE has no general “pairable” advertising-data bit. The connectable PDU can be
confirmed in a scanner; bondability is confirmed when AccessorySetupKit connects
and the firmware log reports `authentication requested` followed by
`authentication complete ... bonded=yes`. The firmware also logs advertising
start/recovery, connections, privacy-safe authentication state, numeric
disconnect reasons, and whether advertising remained active when the pairing
window closed. Repeated failed/rejected connection storms are rate-limited and
report a suppressed-event count. It never logs peer addresses or authentication
material.

The app intentionally retains the legitimate service-UUID-plus-name discovery
descriptor first. If a current physical iOS release still shows an empty picker
after this lifecycle fix and the scanner proves the advertisement is correct,
the next experiment is a compact service-data blob/mask marker using this same
service UUID. Recalculate both packet sizes before that experiment. Do not use
Espressif's or any other organization's Bluetooth Company Identifier. If a
registered company identifier proves empirically mandatory, stop and choose a
properly assigned and registered option before changing firmware or app
filters.

## Build checks

Compile the unsigned app and its test bundle for a generic device:

```sh
xcodebuild \
  -project ios/ESPClockCompanion.xcodeproj \
  -scheme ESPClockCompanion \
  -configuration Debug \
  -sdk iphoneos \
  -destination 'generic/platform=iOS' \
  -derivedDataPath /private/tmp/espclock-ios-derived \
  CODE_SIGNING_ALLOWED=NO \
  build

xcodebuild \
  -project ios/ESPClockCompanion.xcodeproj \
  -scheme ESPClockCompanion \
  -configuration Debug \
  -sdk iphoneos \
  -destination 'generic/platform=iOS' \
  -derivedDataPath /private/tmp/espclock-ios-tests-derived \
  CODE_SIGNING_ALLOWED=NO \
  build-for-testing
```

The XCTest cases validate the firmware packet's byte order and input bounds,
plus fresh-install lifecycle ordering, cancellation, multi-clock authorized
relaunch, later-clock addition, picker failure isolation, radio-state
messaging, and per-clock removal cleanup.
Run them on a compatible iOS Simulator or selected physical device from Xcode.

The normal icon-inclusive app build, test-bundle build, and all 29 Simulator
tests pass with the documented toolchain. These tests include six packet, three
per-clock preference, one identity-presentation, 12 onboarding lifecycle, four
acknowledgement-state, one status-decoding, and two reconnect-state cases. If a Mac
reports a CoreSimulator framework mismatch or `iOS ... is not installed`, run
Xcode once, finish **Xcode → Settings → Components**, or use
`xcodebuild -runFirstLaunch` followed by `xcodebuild -downloadPlatform iOS`.
The platform download can be several gigabytes.

## Physical acceptance tests

Record the actual versions rather than substituting an SDK or Simulator result:

| Test device | iPhone model | iOS version | App build | Result/date |
|---|---|---|---|---|
| Oldest supported | Not yet tested | Not yet tested | local Debug | Pending |
| Current release | iPhone 17 Pro | iOS 26.5.2 | local Debug | Fresh onboarding, assembled-display time, reboot reconnect, remove/re-add, manual sync, `time-accepted`, and `+240` offset passed with a qualified C3 (2026-07-25) |

For each applicable physical iPhone:

- delete the prior app/Accessory authorization and clear ESP bonds with the
  powered-on BOOT gesture;
- verify fresh-install picker discovery during `PAIR`, approval, connection,
  `time-accepted`, correct local civil time, and current UTC offset;
- compare Xcode and firmware logs with the expected lifecycle above;
- test picker cancel/retry and picker failure recovery;
- remove and re-add the clock, including ESP bond recovery;
- test ESP reboot reconnect, walk out of range/back in range, and **Sync Now**;
- add at least three clocks, keep all three connected, and prove that each
  clock receives and acknowledges its own update;
- power-cycle and move each of the three clocks out of range separately; prove
  that one clock's retry and acknowledgement state does not change the others;
- disable automatic sync and remove one clock; prove that the other clocks
  remain connected and continue to synchronize;
- after the two-minute window, confirm the 800–1000 ms low-duty advertisement
  still restores a bonded background connection at five metres;
- test locked/background reconnection and a six-hour `sync-request`;
- test Core Bluetooth restoration after system termination, separately from an
  intentional force-quit;
- test handoff of one clock between two authorized family iPhones by using that
  clock's automatic-sync switch;
- fill all 16 bond/notification slots, cancel one at-capacity pairing, and
  verify recovery;
- verify no location, Internet service, account, analytics, third-party SDK, or
  broad Bluetooth-scanning authorization was introduced;
- test the captive-portal fallback with the app unavailable.

As of this repository update, the unsigned app and test bundle are built with
Xcode 26.6 and the iOS 26.5 SDK. A signed fresh install was launched
successfully on the iPhone 17 Pro above after its development profile was
trusted. That launch caught a physical-device-only iOS 26.5.2 requirement for
the compatibility `NSAccessorySetupKitSupports` key; the app now retains it
alongside `NSAccessorySetupSupports`.

On a qualified bare ESP32-C3 Super Mini, independent Mac scans received the
complete service UUID/name advertisement at -45 dBm and the real
`KidsClock-06E8` SoftAP at -35 dBm. The physical iPhone picker then discovered
and authorized the clock without broad Bluetooth access; firmware recorded a
bonded encrypted connection and accepted the phone time with a `+240` minute
offset, and the app received `time-accepted`. A comparison C3 could receive
nearby Wi-Fi but transmitted neither a minimal BLE advertisement nor a minimal
SoftAP at 5–10 cm, proving that board's RF transmit path was defective rather
than an app filter failure. After installing the qualified C3, the assembled
display showed correct local time, app removal/re-add worked, an ESP reboot
reconnected and synchronized automatically, and manual **Sync Now** was
accepted. Out-of-range return, locked/background restoration, and the six-hour
request remain explicit device gates.

Apple references: [AccessorySetupKit](https://developer.apple.com/documentation/accessorysetupkit/),
[Core Bluetooth background processing](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html),
and [TN3115 state-restoration relaunch rules](https://developer.apple.com/documentation/technotes/tn3115-bluetooth-state-restoration-app-relaunch-rules).
