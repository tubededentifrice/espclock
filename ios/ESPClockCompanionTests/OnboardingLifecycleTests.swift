import XCTest
@testable import ESPClockCompanion

final class OnboardingLifecycleTests: XCTestCase {
    private let identifier = UUID(uuidString: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE")!

    private var clock: AuthorizedClock {
        AuthorizedClock(
            displayName: "Kids Clock",
            bluetoothIdentifier: identifier
        )
    }

    @MainActor
    func testClockSyncManagerFreshInitHasNoCentralManager() {
        let manager = ClockSyncManager()

        XCTAssertFalse(manager.hasCentralManagerForTesting)
    }

    func testFreshInstallDoesNotRequestCentralManager() {
        var lifecycle = OnboardingLifecycle()

        XCTAssertNil(lifecycle.sessionActivated(with: nil))
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Ready to add clock")
    }

    func testPickerPresentationHappensWithoutCentralManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: nil)

        lifecycle.pickerWillPresent()

        XCTAssertTrue(lifecycle.pickerIsActive)
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
        XCTAssertEqual(lifecycle.radioStatusText(for: .unknown), "Finding accessories…")
    }

    func testAccessoryAddedStoresSelectionWithoutInitializingBluetooth() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: nil)
        lifecycle.pickerWillPresent()

        lifecycle.accessoryAdded(clock)

        XCTAssertEqual(lifecycle.selectedClock, clock)
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
    }

    func testPickerDismissalRequestsExactlyOneManagerAndConnection() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: nil)
        lifecycle.pickerWillPresent()
        lifecycle.accessoryAdded(clock)

        XCTAssertEqual(
            lifecycle.pickerDidDismiss(),
            .initializeBluetoothAndConnect(identifier)
        )
        XCTAssertNil(lifecycle.pickerDidDismiss())
        XCTAssertTrue(lifecycle.bluetoothInitializationRequested)
    }

    func testPickerCancellationDoesNotRequestManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: nil)
        lifecycle.pickerWillPresent()

        XCTAssertNil(lifecycle.pickerDidDismiss())
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Ready to add clock")
    }

    func testLaunchWithAuthorizedClockRequestsRestorationAndReconnect() {
        var lifecycle = OnboardingLifecycle()

        XCTAssertEqual(
            lifecycle.sessionActivated(with: clock),
            .initializeBluetoothAndConnect(identifier)
        )
        XCTAssertTrue(lifecycle.bluetoothInitializationRequested)
    }

    func testRadioOffMessagingRequiresPostAuthorizationManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: nil)
        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Ready to add clock")

        lifecycle.pickerWillPresent()
        lifecycle.accessoryAdded(clock)
        _ = lifecycle.pickerDidDismiss()

        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Turn on Bluetooth")
        XCTAssertEqual(
            lifecycle.radioStatusText(for: .unauthorized),
            "Bluetooth access for this clock is not authorized"
        )
        XCTAssertEqual(
            lifecycle.radioStatusText(for: .unsupported),
            "This iPhone does not support Bluetooth LE"
        )
        XCTAssertEqual(
            lifecycle.radioStatusText(for: .resetting),
            "Bluetooth is resetting; waiting…"
        )
        XCTAssertEqual(lifecycle.radioStatusText(for: .unknown), "Checking Bluetooth…")
    }

    func testRemovalAndReaddingReturnToCleanOnboardingState() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: clock)

        XCTAssertEqual(lifecycle.accessoryRemoved(), .tearDownBluetooth)
        XCTAssertNil(lifecycle.selectedClock)
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Ready to add clock")

        lifecycle.pickerWillPresent()
        lifecycle.accessoryAdded(clock)
        XCTAssertEqual(
            lifecycle.pickerDidDismiss(),
            .initializeBluetoothAndConnect(identifier)
        )
    }
}
