import Foundation

struct AuthorizedClock: Equatable {
    let displayName: String
    let bluetoothIdentifier: UUID
}

enum OnboardingLifecycleAction: Equatable {
    case initializeBluetoothAndConnect
    case tearDownBluetooth
}

struct OnboardingLifecycle {
    private(set) var authorizedClocks: [AuthorizedClock] = []
    private(set) var pickerIsActive = false
    private(set) var bluetoothInitializationRequested = false

    mutating func sessionActivated(
        with clocks: [AuthorizedClock]
    ) -> OnboardingLifecycleAction? {
        authorizedClocks = Self.normalized(clocks)
        guard !authorizedClocks.isEmpty else {
            bluetoothInitializationRequested = false
            return nil
        }
        return requestBluetoothInitialization()
    }

    mutating func pickerWillPresent() {
        pickerIsActive = true
    }

    mutating func pickerDidPresent() {
        pickerIsActive = true
    }

    mutating func accessoryAdded(_ clock: AuthorizedClock) {
        authorizedClocks.removeAll {
            $0.bluetoothIdentifier == clock.bluetoothIdentifier
        }
        authorizedClocks.append(clock)
        authorizedClocks = Self.normalized(authorizedClocks)
    }

    mutating func replaceAuthorizedClocks(
        with clocks: [AuthorizedClock]
    ) -> OnboardingLifecycleAction? {
        authorizedClocks = Self.normalized(clocks)
        guard authorizedClocks.isEmpty else {
            return requestBluetoothInitialization()
        }
        guard bluetoothInitializationRequested else { return nil }
        bluetoothInitializationRequested = false
        return .tearDownBluetooth
    }

    mutating func pickerDidDismiss() -> OnboardingLifecycleAction? {
        pickerIsActive = false
        guard !authorizedClocks.isEmpty else { return nil }
        return requestBluetoothInitialization()
    }

    mutating func pickerFailed() -> OnboardingLifecycleAction? {
        pickerIsActive = false
        guard authorizedClocks.isEmpty, bluetoothInitializationRequested else {
            return nil
        }
        bluetoothInitializationRequested = false
        return .tearDownBluetooth
    }

    mutating func sessionInvalidated() -> OnboardingLifecycleAction? {
        pickerIsActive = false
        authorizedClocks = []
        guard bluetoothInitializationRequested else { return nil }
        bluetoothInitializationRequested = false
        return .tearDownBluetooth
    }

    func radioStatusText(for state: BluetoothRadioState) -> String {
        guard !authorizedClocks.isEmpty, bluetoothInitializationRequested else {
            return pickerIsActive ? "Finding accessories…" : "Ready to add clock"
        }
        switch state {
        case .poweredOn:
            return "Ready to connect"
        case .poweredOff:
            return "Turn on Bluetooth"
        case .unauthorized:
            return "Bluetooth access for these clocks is not authorized"
        case .unsupported:
            return "This iPhone does not support Bluetooth LE"
        case .resetting:
            return "Bluetooth is resetting; waiting…"
        case .unknown:
            return "Checking Bluetooth…"
        }
    }

    private mutating func requestBluetoothInitialization()
        -> OnboardingLifecycleAction? {
        guard !pickerIsActive, !bluetoothInitializationRequested else {
            return nil
        }
        bluetoothInitializationRequested = true
        return .initializeBluetoothAndConnect
    }

    private static func normalized(
        _ clocks: [AuthorizedClock]
    ) -> [AuthorizedClock] {
        Dictionary(
            clocks.map { ($0.bluetoothIdentifier, $0) },
            uniquingKeysWith: { _, newest in newest }
        )
        .values
        .sorted {
            let nameOrder = $0.displayName.localizedCaseInsensitiveCompare(
                $1.displayName
            )
            if nameOrder == .orderedSame {
                return $0.bluetoothIdentifier.uuidString
                    < $1.bluetoothIdentifier.uuidString
            }
            return nameOrder == .orderedAscending
        }
    }
}

enum BluetoothRadioState: Equatable {
    case poweredOn
    case poweredOff
    case unauthorized
    case unsupported
    case resetting
    case unknown
}
