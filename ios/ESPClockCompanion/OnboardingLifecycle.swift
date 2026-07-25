import Foundation

struct AuthorizedClock: Equatable {
    let displayName: String
    let bluetoothIdentifier: UUID
}

enum OnboardingLifecycleAction: Equatable {
    case initializeBluetoothAndConnect(UUID)
    case tearDownBluetooth
}

struct OnboardingLifecycle {
    private(set) var selectedClock: AuthorizedClock?
    private(set) var pickerIsActive = false
    private(set) var bluetoothInitializationRequested = false

    mutating func sessionActivated(
        with authorizedClock: AuthorizedClock?
    ) -> OnboardingLifecycleAction? {
        selectedClock = authorizedClock
        guard let authorizedClock else {
            bluetoothInitializationRequested = false
            return nil
        }
        return requestBluetoothInitialization(for: authorizedClock.bluetoothIdentifier)
    }

    mutating func pickerWillPresent() {
        pickerIsActive = true
    }

    mutating func pickerDidPresent() {
        pickerIsActive = true
    }

    mutating func accessoryAdded(_ clock: AuthorizedClock) {
        selectedClock = clock
    }

    mutating func pickerDidDismiss() -> OnboardingLifecycleAction? {
        pickerIsActive = false
        guard let selectedClock else { return nil }
        return requestBluetoothInitialization(for: selectedClock.bluetoothIdentifier)
    }

    mutating func pickerFailed() -> OnboardingLifecycleAction? {
        pickerIsActive = false
        selectedClock = nil
        guard bluetoothInitializationRequested else { return nil }
        bluetoothInitializationRequested = false
        return .tearDownBluetooth
    }

    mutating func accessoryRemoved() -> OnboardingLifecycleAction? {
        pickerIsActive = false
        selectedClock = nil
        guard bluetoothInitializationRequested else { return nil }
        bluetoothInitializationRequested = false
        return .tearDownBluetooth
    }

    func radioStatusText(for state: BluetoothRadioState) -> String {
        guard selectedClock != nil, bluetoothInitializationRequested else {
            return pickerIsActive ? "Finding accessories…" : "Ready to add clock"
        }
        switch state {
        case .poweredOn:
            return "Ready to connect"
        case .poweredOff:
            return "Turn on Bluetooth"
        case .unauthorized:
            return "Bluetooth access for this clock is not authorized"
        case .unsupported:
            return "This iPhone does not support Bluetooth LE"
        case .resetting:
            return "Bluetooth is resetting; waiting…"
        case .unknown:
            return "Checking Bluetooth…"
        }
    }

    private mutating func requestBluetoothInitialization(
        for identifier: UUID
    ) -> OnboardingLifecycleAction? {
        guard !pickerIsActive, !bluetoothInitializationRequested else { return nil }
        bluetoothInitializationRequested = true
        return .initializeBluetoothAndConnect(identifier)
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
