import XCTest
@testable import ESPClockCompanion

final class OnboardingLifecycleTests: XCTestCase {
    private let firstIdentifier =
        UUID(uuidString: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE")!
    private let secondIdentifier =
        UUID(uuidString: "11111111-2222-3333-4444-555555555555")!

    private var firstClock: AuthorizedClock {
        AuthorizedClock(
            displayName: "Bedroom Clock",
            bluetoothIdentifier: firstIdentifier
        )
    }

    private var secondClock: AuthorizedClock {
        AuthorizedClock(
            displayName: "Travel Clock",
            bluetoothIdentifier: secondIdentifier
        )
    }

    @MainActor
    func testClockSyncManagerFreshInitHasNoCentralManager() {
        let manager = ClockSyncManager()

        XCTAssertFalse(manager.hasCentralManagerForTesting)
    }

    func testFreshInstallDoesNotRequestCentralManager() {
        var lifecycle = OnboardingLifecycle()

        XCTAssertNil(lifecycle.sessionActivated(with: []))
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Ready to add clock")
    }

    func testPickerPresentationHappensWithoutCentralManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [])

        lifecycle.pickerWillPresent()

        XCTAssertTrue(lifecycle.pickerIsActive)
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
        XCTAssertEqual(lifecycle.radioStatusText(for: .unknown), "Finding accessories…")
    }

    func testAccessoryAddedStoresClockWithoutInitializingBluetooth() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [])
        lifecycle.pickerWillPresent()

        lifecycle.accessoryAdded(firstClock)

        XCTAssertEqual(lifecycle.authorizedClocks, [firstClock])
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
    }

    func testPickerDismissalRequestsExactlyOneManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [])
        lifecycle.pickerWillPresent()
        lifecycle.accessoryAdded(firstClock)

        XCTAssertEqual(
            lifecycle.pickerDidDismiss(),
            .initializeBluetoothAndConnect
        )
        XCTAssertNil(lifecycle.pickerDidDismiss())
        XCTAssertTrue(lifecycle.bluetoothInitializationRequested)
    }

    func testPickerCancellationDoesNotRequestManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [])
        lifecycle.pickerWillPresent()

        XCTAssertNil(lifecycle.pickerDidDismiss())
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Ready to add clock")
    }

    func testLaunchWithMultipleAuthorizedClocksRequestsOneManager() {
        var lifecycle = OnboardingLifecycle()

        XCTAssertEqual(
            lifecycle.sessionActivated(with: [secondClock, firstClock]),
            .initializeBluetoothAndConnect
        )
        XCTAssertEqual(lifecycle.authorizedClocks, [firstClock, secondClock])
        XCTAssertTrue(lifecycle.bluetoothInitializationRequested)
    }

    func testAddingSecondClockKeepsExistingBluetoothManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [firstClock])
        lifecycle.pickerWillPresent()
        lifecycle.accessoryAdded(secondClock)

        XCTAssertNil(lifecycle.pickerDidDismiss())
        XCTAssertEqual(lifecycle.authorizedClocks, [firstClock, secondClock])
        XCTAssertTrue(lifecycle.bluetoothInitializationRequested)
    }

    func testPickerFailureKeepsExistingClocksAndManager() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [firstClock])
        lifecycle.pickerWillPresent()

        XCTAssertNil(lifecycle.pickerFailed())
        XCTAssertEqual(lifecycle.authorizedClocks, [firstClock])
        XCTAssertTrue(lifecycle.bluetoothInitializationRequested)
    }

    func testRemovingOneClockKeepsManagerUntilLastClockIsRemoved() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [firstClock, secondClock])

        XCTAssertNil(
            lifecycle.replaceAuthorizedClocks(with: [secondClock])
        )
        XCTAssertEqual(lifecycle.authorizedClocks, [secondClock])
        XCTAssertTrue(lifecycle.bluetoothInitializationRequested)

        XCTAssertEqual(
            lifecycle.replaceAuthorizedClocks(with: []),
            .tearDownBluetooth
        )
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)
    }

    func testRadioOffMessagingRequiresAuthorizedClock() {
        var lifecycle = OnboardingLifecycle()
        _ = lifecycle.sessionActivated(with: [])
        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Ready to add clock")

        lifecycle.pickerWillPresent()
        lifecycle.accessoryAdded(firstClock)
        _ = lifecycle.pickerDidDismiss()

        XCTAssertEqual(lifecycle.radioStatusText(for: .poweredOff), "Turn on Bluetooth")
        XCTAssertEqual(
            lifecycle.radioStatusText(for: .unauthorized),
            "Bluetooth access for these clocks is not authorized"
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
        _ = lifecycle.sessionActivated(with: [firstClock])

        XCTAssertEqual(
            lifecycle.replaceAuthorizedClocks(with: []),
            .tearDownBluetooth
        )
        XCTAssertTrue(lifecycle.authorizedClocks.isEmpty)
        XCTAssertFalse(lifecycle.bluetoothInitializationRequested)

        lifecycle.pickerWillPresent()
        lifecycle.accessoryAdded(firstClock)
        XCTAssertEqual(
            lifecycle.pickerDidDismiss(),
            .initializeBluetoothAndConnect
        )
    }
}

final class ClockPreferenceStoreTests: XCTestCase {
    private var suiteName = ""
    private var defaults: UserDefaults!
    private var store: ClockPreferenceStore!
    private let firstIdentifier =
        UUID(uuidString: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE")!
    private let secondIdentifier =
        UUID(uuidString: "11111111-2222-3333-4444-555555555555")!

    override func setUp() {
        super.setUp()
        suiteName = "ClockPreferenceStoreTests.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
        store = ClockPreferenceStore(defaults: defaults)
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suiteName)
        store = nil
        defaults = nil
        super.tearDown()
    }

    func testAutomaticSyncSettingsAreIndependent() {
        store.setAutomaticSync(false, for: firstIdentifier)

        XCTAssertFalse(store.automaticSync(for: firstIdentifier))
        XCTAssertTrue(store.automaticSync(for: secondIdentifier))
    }

    func testLastSyncAndRemovalAreIndependent() {
        let firstDate = Date(timeIntervalSince1970: 1_735_689_600)
        let secondDate = Date(timeIntervalSince1970: 1_735_776_000)
        store.setLastSync(firstDate, for: firstIdentifier)
        store.setLastSync(secondDate, for: secondIdentifier)
        store.setBluetoothName("KidsClock-1234", for: firstIdentifier)
        store.setBluetoothName("KidsClock-5678", for: secondIdentifier)

        store.removeClock(firstIdentifier)

        XCTAssertNil(store.lastSync(for: firstIdentifier))
        XCTAssertEqual(store.lastSync(for: secondIdentifier), secondDate)
        XCTAssertNil(store.bluetoothName(for: firstIdentifier))
        XCTAssertEqual(
            store.bluetoothName(for: secondIdentifier),
            "KidsClock-5678"
        )
    }

    func testLegacySettingsMigrateOnlyToExistingClocks() {
        let legacyDate = Date(timeIntervalSince1970: 1_735_689_600)
        defaults.set(false, forKey: "automaticSyncEnabled")
        defaults.set(legacyDate, forKey: "lastSyncDate")
        let firstClock = AuthorizedClock(
            displayName: "Bedroom Clock",
            bluetoothIdentifier: firstIdentifier
        )

        store.migrateLegacyPreferences(for: [firstClock])

        XCTAssertFalse(store.automaticSync(for: firstIdentifier))
        XCTAssertEqual(store.lastSync(for: firstIdentifier), legacyDate)
        XCTAssertTrue(store.automaticSync(for: secondIdentifier))
        XCTAssertNil(store.lastSync(for: secondIdentifier))
    }
}

final class ClockViewStateTests: XCTestCase {
    @MainActor
    func testClockNameAcceptsAndNormalizesTheFirmwareIdentifier() {
        XCTAssertEqual(
            ClockSyncManager.validatedClockName("KidsClock-a1b2"),
            "KidsClock-A1B2"
        )
        XCTAssertNil(
            ClockSyncManager.validatedClockName("KidsClock-not-a-clock")
        )
        XCTAssertNil(
            ClockSyncManager.validatedClockName("OtherClock-A1B2")
        )
    }
}
