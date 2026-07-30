import AccessorySetupKit
@preconcurrency import CoreBluetooth
import Foundation
import os
import UIKit

struct ClockViewState: Identifiable, Equatable {
    let id: UUID
    let name: String
    let connectionText: String
    let lastSyncDate: Date?
    let lastError: String?
    let isConnected: Bool
    let automaticSyncEnabled: Bool
}

@MainActor
final class ClockSyncManager: NSObject, ObservableObject {
    static let serviceUUID = CBUUID(string: "7F510000-1B15-4DC7-9F3F-19B30A6F6A21")
    static let timeUUID = CBUUID(string: "7F510001-1B15-4DC7-9F3F-19B30A6F6A21")
    static let statusUUID = CBUUID(string: "7F510002-1B15-4DC7-9F3F-19B30A6F6A21")
    static let gapServiceUUID = CBUUID(string: "1800")
    static let deviceNameUUID = CBUUID(string: "2A00")

    @Published private(set) var clocks: [ClockViewState] = []
    @Published private(set) var setupText = "Starting accessory setup…"
    @Published private(set) var lastError: String?

    private enum Keys {
        static let restorationIdentifier = "centralRestorationIdentifier"
    }

    private final class ClockRuntime {
        let identifier: UUID
        var displayName: String
        var connectionText: String
        var lastSyncDate: Date?
        var lastError: String?
        var isConnected = false
        var automaticSyncEnabled: Bool
        var peripheral: CBPeripheral?
        var timeCharacteristic: CBCharacteristic?
        var statusCharacteristic: CBCharacteristic?
        var connectPending = false
        var sentInitialTimeForConnection = false
        var awaitingClockAcceptance = false
        var manualSyncRequested = false
        var acknowledgementTimeout: DispatchWorkItem?
        var reconnectWorkItem: DispatchWorkItem?
        var reconnectAttempts = 0

        init(
            clock: AuthorizedClock,
            bluetoothName: String?,
            automaticSyncEnabled: Bool,
            lastSyncDate: Date?
        ) {
            identifier = clock.bluetoothIdentifier
            displayName = bluetoothName ?? clock.displayName
            connectionText = automaticSyncEnabled
                ? "Waiting for Bluetooth…"
                : "Automatic sync is off"
            self.automaticSyncEnabled = automaticSyncEnabled
            self.lastSyncDate = lastSyncDate
        }
    }

    private let defaults = UserDefaults.standard
    private let preferences = ClockPreferenceStore()
    private let accessorySession = ASAccessorySession()
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.espclock.KidsClockCompanion",
        category: "BLEOnboarding"
    )
    private var onboardingLifecycle = OnboardingLifecycle()
    private var centralManager: CBCentralManager?
    private var runtimes: [UUID: ClockRuntime] = [:]
    private var scanTimeout: DispatchWorkItem?
    private var scanTargetIdentifiers: Set<UUID> = []
    private var accessorySessionActivated = false

#if DEBUG
    var hasCentralManagerForTesting: Bool {
        centralManager != nil
    }
#endif

    override init() {
        super.init()

        accessorySession.activate(on: .main) { [weak self] event in
            self?.handleAccessoryEvent(event)
        }
        logger.info("Accessory session activation requested; centralManagerExists=false")
    }

    func addClock() {
        guard accessorySessionActivated else {
            lastError = "Accessory setup is still starting. Try again in a moment."
            return
        }
        guard !onboardingLifecycle.pickerIsActive else { return }

        let descriptor = ASDiscoveryDescriptor()
        descriptor.bluetoothServiceUUID = Self.serviceUUID
        descriptor.bluetoothNameSubstring = "KidsClock-"
        descriptor.supportedOptions = [.bluetoothPairingLE]

        guard let image = UIImage(systemName: "clock.fill") else {
            lastError = "The clock setup image could not be created."
            return
        }
        let item = ASPickerDisplayItem(
            name: "Kids Clock",
            productImage: image,
            descriptor: descriptor
        )
        lastError = nil
        onboardingLifecycle.pickerWillPresent()
        setupText = onboardingLifecycle.radioStatusText(for: .unknown)
        logger.info(
            "Opening accessory picker; centralManagerExists=\(self.centralManager != nil, privacy: .public)"
        )
        accessorySession.showPicker(for: [item]) { [weak self] error in
            Task { @MainActor in
                guard let self, let error else { return }
                let nsError = error as NSError
                self.logger.error(
                    "Picker completion failed domain=\(nsError.domain, privacy: .public) code=\(nsError.code, privacy: .public)"
                )
                self.handlePickerFailure(error, prefix: "Clock setup failed")
                if let action = self.onboardingLifecycle.pickerFailed() {
                    self.handleLifecycleAction(action)
                }
                self.updateSetupText()
            }
        }
    }

    func setAutomaticSync(_ enabled: Bool, for identifier: UUID) {
        guard let runtime = runtimes[identifier] else { return }
        runtime.automaticSyncEnabled = enabled
        preferences.setAutomaticSync(enabled, for: identifier)
        runtime.lastError = nil
        if enabled {
            runtime.reconnectAttempts = 0
            runtime.connectionText = "Connecting…"
            connectAuthorizedClocks()
        } else {
            stopConnection(runtime, finalText: "Automatic sync is off")
            updateScanState()
        }
        publishClocks()
    }

    func syncNow(_ identifier: UUID) {
        guard let runtime = runtimes[identifier] else { return }
        runtime.lastError = nil
        runtime.manualSyncRequested = true
        runtime.reconnectAttempts = 0
        guard runtime.isConnected else {
            runtime.connectionText = "Connecting…"
            publishClocks()
            connectAuthorizedClocks()
            return
        }
        sendPhoneTime(to: runtime)
    }

    func forgetClock(_ identifier: UUID) {
        guard let accessory = authorizedAccessories.first(where: {
            $0.bluetoothIdentifier == identifier
        }) else {
            return
        }
        if let runtime = runtimes[identifier] {
            stopConnection(runtime, finalText: "Removing…")
        }
        accessorySession.removeAccessory(accessory) { [weak self] error in
            Task { @MainActor in
                guard let self else { return }
                if let error {
                    if let runtime = self.runtimes[identifier] {
                        runtime.lastError = self.privacySafeError(
                            error,
                            prefix: "Could not remove the clock"
                        )
                    }
                    self.publishClocks()
                    self.connectAuthorizedClocks()
                    return
                }

                self.removeRuntime(identifier)
                let remaining = self.onboardingLifecycle.authorizedClocks.filter {
                    $0.bluetoothIdentifier != identifier
                }
                if let action = self.onboardingLifecycle.replaceAuthorizedClocks(
                    with: remaining
                ) {
                    self.handleLifecycleAction(action)
                }
                self.preferences.removeClock(identifier)
                self.updateSetupText()
            }
        }
    }

    static func validatedClockName(_ candidate: String?) -> String? {
        let prefix = "KidsClock-"
        guard let candidate,
              candidate.hasPrefix(prefix) else {
            return nil
        }
        let suffix = candidate.dropFirst(prefix.count)
        guard suffix.count == 4,
              suffix.allSatisfy(\.isHexDigit) else {
            return nil
        }
        return prefix + suffix.uppercased()
    }

    private func applyClockName(
        _ candidate: String?,
        to runtime: ClockRuntime
    ) {
        guard let name = Self.validatedClockName(candidate) else { return }
        runtime.displayName = name
        preferences.setBluetoothName(name, for: runtime.identifier)
    }

    private var authorizedAccessories: [ASAccessory] {
        accessorySession.accessories.filter {
            $0.state == .authorized && $0.bluetoothIdentifier != nil
        }
    }

    private var currentAuthorizedClocks: [AuthorizedClock] {
        authorizedAccessories.compactMap(authorizedClock(from:))
    }

    private func handleAccessoryEvent(_ event: ASAccessoryEvent) {
        logger.info(
            "Accessory session event=\(self.accessoryEventName(event.eventType), privacy: .public) centralManagerExists=\(self.centralManager != nil, privacy: .public)"
        )
        if let error = event.error {
            let nsError = error as NSError
            logger.error(
                "Accessory session error domain=\(nsError.domain, privacy: .public) code=\(nsError.code, privacy: .public)"
            )
        }

        switch event.eventType {
        case .activated:
            accessorySessionActivated = true
            let authorized = currentAuthorizedClocks
            preferences.migrateLegacyPreferences(for: authorized)
            reconcileAuthorizedClocks(authorized)
            if let action = onboardingLifecycle.sessionActivated(with: authorized) {
                handleLifecycleAction(action)
            }
            updateSetupText()
        case .accessoryAdded:
            if let clock = authorizedClock(from: event.accessory) {
                onboardingLifecycle.accessoryAdded(clock)
                reconcileAuthorizedClocks(onboardingLifecycle.authorizedClocks)
                if !onboardingLifecycle.pickerIsActive {
                    connectAuthorizedClocks()
                }
            } else {
                reconcileWithAccessorySession()
            }
        case .accessoryChanged:
            reconcileWithAccessorySession()
            if !onboardingLifecycle.pickerIsActive {
                connectAuthorizedClocks()
            }
        case .accessoryRemoved:
            reconcileWithAccessorySession()
        case .pickerDidPresent:
            onboardingLifecycle.pickerDidPresent()
            setupText = "Finding accessories…"
            logger.info(
                "Accessory picker presented; centralManagerExists=\(self.centralManager != nil, privacy: .public)"
            )
        case .pickerDidDismiss:
            reconcileWithAccessorySession()
            if let action = onboardingLifecycle.pickerDidDismiss() {
                handleLifecycleAction(action)
            } else {
                connectAuthorizedClocks()
            }
            updateSetupText()
        case .pickerSetupFailed:
            if let error = event.error {
                handlePickerFailure(error, prefix: "The clock could not be paired")
            } else {
                lastError = "The clock could not be paired."
            }
            if let action = onboardingLifecycle.pickerFailed() {
                handleLifecycleAction(action)
            }
            updateSetupText()
        case .invalidated:
            accessorySessionActivated = false
            if let error = event.error {
                lastError = privacySafeError(
                    error,
                    prefix: "Accessory setup became unavailable"
                )
            } else {
                lastError = "Accessory setup became unavailable."
            }
            reconcileAuthorizedClocks([])
            if let action = onboardingLifecycle.sessionInvalidated() {
                handleLifecycleAction(action)
            } else {
                tearDownCoreBluetooth()
            }
            setupText = "Accessory setup is unavailable"
        default:
            break
        }
    }

    private func reconcileWithAccessorySession() {
        let authorized = currentAuthorizedClocks
        reconcileAuthorizedClocks(authorized)
        if let action = onboardingLifecycle.replaceAuthorizedClocks(with: authorized) {
            handleLifecycleAction(action)
        }
        updateSetupText()
    }

    private func reconcileAuthorizedClocks(_ authorized: [AuthorizedClock]) {
        let authorizedIdentifiers = Set(authorized.map(\.bluetoothIdentifier))
        for identifier in Array(runtimes.keys) where
            !authorizedIdentifiers.contains(identifier) {
            removeRuntime(identifier)
        }
        for clock in authorized {
            if let runtime = runtimes[clock.bluetoothIdentifier] {
                if let peripheralName = Self.validatedClockName(
                    runtime.peripheral?.name
                ) {
                    runtime.displayName = peripheralName
                    preferences.setBluetoothName(
                        peripheralName,
                        for: clock.bluetoothIdentifier
                    )
                } else if let savedName = preferences.bluetoothName(
                    for: clock.bluetoothIdentifier
                ) {
                    runtime.displayName = savedName
                } else {
                    runtime.displayName = clock.displayName
                }
            } else {
                runtimes[clock.bluetoothIdentifier] = ClockRuntime(
                    clock: clock,
                    bluetoothName: preferences.bluetoothName(
                        for: clock.bluetoothIdentifier
                    ),
                    automaticSyncEnabled: preferences.automaticSync(
                        for: clock.bluetoothIdentifier
                    ),
                    lastSyncDate: preferences.lastSync(
                        for: clock.bluetoothIdentifier
                    )
                )
            }
        }
        publishClocks()
    }

    private func removeRuntime(_ identifier: UUID) {
        guard let runtime = runtimes[identifier] else { return }
        stopConnection(runtime, finalText: nil)
        runtimes.removeValue(forKey: identifier)
        updateScanState()
        publishClocks()
    }

    private func authorizedClock(from accessory: ASAccessory?) -> AuthorizedClock? {
        guard let accessory,
              accessory.state == .authorized,
              let identifier = accessory.bluetoothIdentifier else {
            return nil
        }
        return AuthorizedClock(
            displayName: accessory.displayName,
            bluetoothIdentifier: identifier
        )
    }

    private func handleLifecycleAction(_ action: OnboardingLifecycleAction) {
        switch action {
        case .initializeBluetoothAndConnect:
            initializeCoreBluetooth()
        case .tearDownBluetooth:
            tearDownCoreBluetooth()
        }
    }

    private func initializeCoreBluetooth() {
        guard centralManager == nil,
              accessorySessionActivated,
              !onboardingLifecycle.authorizedClocks.isEmpty else {
            connectAuthorizedClocks()
            return
        }
        let restorationIdentifier: String
        if let saved = defaults.string(forKey: Keys.restorationIdentifier) {
            restorationIdentifier = saved
        } else {
            restorationIdentifier = "com.espclock.companion.central.\(UUID().uuidString)"
            defaults.set(restorationIdentifier, forKey: Keys.restorationIdentifier)
        }
        setupText = onboardingLifecycle.radioStatusText(for: .unknown)
        logger.info("Creating post-authorization central manager")
        centralManager = CBCentralManager(
            delegate: self,
            queue: .main,
            options: [CBCentralManagerOptionRestoreIdentifierKey: restorationIdentifier]
        )
    }

    private func tearDownCoreBluetooth() {
        scanTimeout?.cancel()
        scanTimeout = nil
        scanTargetIdentifiers = []
        centralManager?.stopScan()
        for runtime in runtimes.values {
            stopConnection(runtime, finalText: nil)
        }
        centralManager?.delegate = nil
        centralManager = nil
        publishClocks()
    }

    private func handlePickerFailure(_ error: Error, prefix: String) {
        lastError = privacySafeError(error, prefix: prefix)
    }

    private func privacySafeError(_ error: Error, prefix: String) -> String {
        let nsError = error as NSError
        return "\(prefix) (\(nsError.domain) code \(nsError.code))."
    }

    private func accessoryEventName(_ type: ASAccessoryEventType) -> String {
        switch type {
        case .unknown: "unknown"
        case .activated: "activated"
        case .invalidated: "invalidated"
        case .migrationComplete: "migrationComplete"
        case .accessoryAdded: "accessoryAdded"
        case .accessoryRemoved: "accessoryRemoved"
        case .accessoryChanged: "accessoryChanged"
        case .accessoryDiscovered: "accessoryDiscovered"
        case .pickerDidPresent: "pickerDidPresent"
        case .pickerDidDismiss: "pickerDidDismiss"
        case .pickerSetupBridging: "pickerSetupBridging"
        case .pickerSetupFailed: "pickerSetupFailed"
        case .pickerSetupPairing: "pickerSetupPairing"
        case .pickerSetupRename: "pickerSetupRename"
        @unknown default: "future(\(type.rawValue))"
        }
    }

    private func radioState(for state: CBManagerState) -> BluetoothRadioState {
        switch state {
        case .poweredOn: .poweredOn
        case .poweredOff: .poweredOff
        case .unauthorized: .unauthorized
        case .unsupported: .unsupported
        case .resetting: .resetting
        case .unknown: .unknown
        @unknown default: .unknown
        }
    }

    private func centralStateName(_ state: CBManagerState) -> String {
        switch state {
        case .poweredOn: "poweredOn"
        case .poweredOff: "poweredOff"
        case .unauthorized: "unauthorized"
        case .unsupported: "unsupported"
        case .resetting: "resetting"
        case .unknown: "unknown"
        @unknown default: "future(\(state.rawValue))"
        }
    }

    private func updateSetupText() {
        if onboardingLifecycle.pickerIsActive {
            setupText = "Finding accessories…"
        } else if clocks.isEmpty {
            setupText = accessorySessionActivated
                ? "Ready to add clock"
                : "Starting accessory setup…"
        } else {
            setupText = clocks.count == 1
                ? "Managing 1 clock"
                : "Managing \(clocks.count) clocks"
        }
    }

    private func publishClocks() {
        clocks = runtimes.values.map {
            ClockViewState(
                id: $0.identifier,
                name: $0.displayName,
                connectionText: $0.connectionText,
                lastSyncDate: $0.lastSyncDate,
                lastError: $0.lastError,
                isConnected: $0.isConnected,
                automaticSyncEnabled: $0.automaticSyncEnabled
            )
        }
        .sorted {
            let nameOrder = $0.name.localizedCaseInsensitiveCompare($1.name)
            if nameOrder == .orderedSame {
                return $0.id.uuidString < $1.id.uuidString
            }
            return nameOrder == .orderedAscending
        }
        updateSetupText()
    }

    private func wantsConnection(_ runtime: ClockRuntime) -> Bool {
        runtime.automaticSyncEnabled || runtime.manualSyncRequested
    }

    private func connectAuthorizedClocks() {
        guard accessorySessionActivated,
              let centralManager,
              centralManager.state == .poweredOn else {
            return
        }

        let desired = runtimes.values.filter(wantsConnection)
        let unresolvedIdentifiers = desired.compactMap {
            $0.peripheral == nil ? $0.identifier : nil
        }
        if !unresolvedIdentifiers.isEmpty {
            for peripheral in centralManager.retrievePeripherals(
                withIdentifiers: unresolvedIdentifiers
            ) {
                adopt(peripheral)
            }
        }

        for runtime in desired {
            guard let peripheral = runtime.peripheral else {
                runtime.connectionText =
                    "Clock authorized; waiting for it to advertise…"
                continue
            }
            switch peripheral.state {
            case .connected:
                runtime.isConnected = true
                discoverClockServices(for: runtime)
            case .disconnected:
                requestConnection(for: runtime)
            case .connecting:
                runtime.connectPending = true
                runtime.connectionText = "Connecting…"
            case .disconnecting:
                runtime.connectionText = "Disconnecting; waiting to reconnect…"
            @unknown default:
                runtime.connectionText = "Waiting for Bluetooth…"
            }
        }
        updateScanState()
        publishClocks()
    }

    private func adopt(_ peripheral: CBPeripheral) {
        guard let runtime = runtimes[peripheral.identifier] else { return }
        if runtime.peripheral !== peripheral {
            runtime.timeCharacteristic = nil
            runtime.statusCharacteristic = nil
            runtime.sentInitialTimeForConnection = false
            runtime.awaitingClockAcceptance = false
        }
        runtime.peripheral = peripheral
        applyClockName(peripheral.name, to: runtime)
        peripheral.delegate = self
    }

    private func requestConnection(for runtime: ClockRuntime) {
        guard let centralManager,
              let peripheral = runtime.peripheral,
              wantsConnection(runtime),
              !runtime.connectPending,
              peripheral.state == .disconnected else {
            return
        }
        runtime.reconnectWorkItem?.cancel()
        runtime.reconnectWorkItem = nil
        runtime.connectPending = true
        runtime.connectionText = "Connecting to \(runtime.displayName)…"
        centralManager.connect(
            peripheral,
            options: [
                CBConnectPeripheralOptionEnableAutoReconnect: true,
                CBConnectPeripheralOptionNotifyOnConnectionKey: false,
                CBConnectPeripheralOptionNotifyOnDisconnectionKey: false,
                CBConnectPeripheralOptionNotifyOnNotificationKey: false,
            ]
        )
    }

    private func scheduleReconnect(for runtime: ClockRuntime) {
        guard runtime.automaticSyncEnabled else { return }
        runtime.reconnectWorkItem?.cancel()
        let delays: [TimeInterval] = [3, 15, 60]
        guard runtime.reconnectAttempts < delays.count else {
            runtime.connectionText =
                "Automatic reconnect paused; open the app to retry"
            publishClocks()
            return
        }
        let delay = delays[runtime.reconnectAttempts]
        runtime.reconnectAttempts += 1
        let identifier = runtime.identifier
        let work = DispatchWorkItem { [weak self] in
            guard let self,
                  let current = self.runtimes[identifier],
                  current.automaticSyncEnabled else {
                return
            }
            self.requestConnection(for: current)
            self.publishClocks()
        }
        runtime.reconnectWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func updateScanState() {
        guard let centralManager, centralManager.state == .poweredOn else { return }
        let unresolved = runtimes.values.filter {
            wantsConnection($0) && $0.peripheral == nil
        }
        let unresolvedIdentifiers = Set(unresolved.map(\.identifier))
        guard !unresolved.isEmpty else {
            centralManager.stopScan()
            scanTimeout?.cancel()
            scanTimeout = nil
            scanTargetIdentifiers = []
            return
        }
        let hasNewTarget =
            !unresolvedIdentifiers.isSubset(of: scanTargetIdentifiers)
        scanTargetIdentifiers = unresolvedIdentifiers
        if !centralManager.isScanning {
            centralManager.scanForPeripherals(
                withServices: [Self.serviceUUID],
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
            )
            scheduleScanTimeout()
        } else if hasNewTarget {
            scheduleScanTimeout()
        }
    }

    private func scheduleScanTimeout() {
        scanTimeout?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.centralManager?.stopScan()
            self.scanTimeout = nil
            self.scanTargetIdentifiers = []
            for runtime in self.runtimes.values where
                self.wantsConnection(runtime) && runtime.peripheral == nil {
                runtime.connectionText =
                    "Clock not found; power it on and tap Sync Now"
                self.finishManualSyncIfNeeded(
                    for: runtime,
                    finalText: "Clock not found; automatic sync is off"
                )
            }
            self.publishClocks()
        }
        scanTimeout = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 20, execute: work)
    }

    private func discoverClockServices(for runtime: ClockRuntime) {
        guard let peripheral = runtime.peripheral,
              peripheral.state == .connected else {
            return
        }
        peripheral.discoverServices([
            Self.serviceUUID,
            Self.gapServiceUUID,
        ])
    }

    private func stopConnection(
        _ runtime: ClockRuntime,
        finalText: String?
    ) {
        runtime.acknowledgementTimeout?.cancel()
        runtime.acknowledgementTimeout = nil
        runtime.reconnectWorkItem?.cancel()
        runtime.reconnectWorkItem = nil
        runtime.reconnectAttempts = 0
        runtime.awaitingClockAcceptance = false
        runtime.manualSyncRequested = false
        runtime.connectPending = false
        if let peripheral = runtime.peripheral {
            centralManager?.cancelPeripheralConnection(peripheral)
        }
        runtime.peripheral = nil
        runtime.timeCharacteristic = nil
        runtime.statusCharacteristic = nil
        runtime.sentInitialTimeForConnection = false
        runtime.isConnected = false
        if let finalText {
            runtime.connectionText = finalText
        }
    }

    private func sendPhoneTime(to runtime: ClockRuntime) {
        guard !runtime.awaitingClockAcceptance,
              let peripheral = runtime.peripheral,
              peripheral.state == .connected,
              let timeCharacteristic = runtime.timeCharacteristic else {
            return
        }

        do {
            let packet = try TimePacketEncoder.encode(date: Date())
            runtime.awaitingClockAcceptance = true
            runtime.connectionText = "Sending iPhone time…"
            peripheral.writeValue(packet, for: timeCharacteristic, type: .withResponse)
            scheduleAcknowledgementCheck(for: runtime)
            publishClocks()
        } catch {
            runtime.lastError = error.localizedDescription
            publishClocks()
        }
    }

    private func finishManualSyncIfNeeded(
        for runtime: ClockRuntime,
        finalText: String? = nil
    ) {
        guard runtime.manualSyncRequested else { return }
        runtime.manualSyncRequested = false
        guard !runtime.automaticSyncEnabled else { return }
        stopConnection(runtime, finalText: finalText ?? "Automatic sync is off")
        updateScanState()
    }

    private func scheduleAcknowledgementCheck(for runtime: ClockRuntime) {
        runtime.acknowledgementTimeout?.cancel()
        let identifier = runtime.identifier
        let readWork = DispatchWorkItem { [weak self] in
            guard let self,
                  let current = self.runtimes[identifier],
                  current.awaitingClockAcceptance else {
                return
            }
            if let peripheral = current.peripheral,
               let status = current.statusCharacteristic {
                peripheral.readValue(for: status)
            }
            let failWork = DispatchWorkItem { [weak self] in
                guard let self,
                      let current = self.runtimes[identifier],
                      current.awaitingClockAcceptance else {
                    return
                }
                current.lastError =
                    "The clock did not acknowledge the update. Keep it nearby and tap Sync Now."
                current.awaitingClockAcceptance = false
                self.finishManualSyncIfNeeded(for: current)
                self.publishClocks()
            }
            current.acknowledgementTimeout = failWork
            DispatchQueue.main.asyncAfter(deadline: .now() + 2, execute: failWork)
        }
        runtime.acknowledgementTimeout = readWork
        DispatchQueue.main.asyncAfter(deadline: .now() + 6, execute: readWork)
    }

    private func handleStatus(_ data: Data?, for runtime: ClockRuntime) {
        guard let data, let status = String(data: data, encoding: .utf8) else {
            return
        }
        switch status {
        case "sync-request", "time-needed":
            sendPhoneTime(to: runtime)
        case "time-accepted":
            runtime.acknowledgementTimeout?.cancel()
            runtime.acknowledgementTimeout = nil
            runtime.awaitingClockAcceptance = false
            runtime.lastError = nil
            let now = Date()
            runtime.lastSyncDate = now
            preferences.setLastSync(now, for: runtime.identifier)
            runtime.connectionText = "Connected and synchronized"
            finishManualSyncIfNeeded(
                for: runtime,
                finalText: "Synchronized; automatic sync is off"
            )
        case "time-pending":
            runtime.connectionText = "Clock is applying the time…"
        case "rate-limited":
            runtime.acknowledgementTimeout?.cancel()
            runtime.acknowledgementTimeout = nil
            runtime.awaitingClockAcceptance = false
            runtime.connectionText = "Connected; clock recently synchronized"
            finishManualSyncIfNeeded(
                for: runtime,
                finalText: "Clock recently synchronized; automatic sync is off"
            )
        case "time-rejected", "invalid-time":
            runtime.acknowledgementTimeout?.cancel()
            runtime.acknowledgementTimeout = nil
            runtime.awaitingClockAcceptance = false
            runtime.lastError =
                "The clock rejected the phone time. Check the iPhone date and time."
            finishManualSyncIfNeeded(for: runtime)
        default:
            break
        }
        publishClocks()
    }
}

extension ClockSyncManager: @preconcurrency CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard centralManager === central else { return }
        logger.info(
            "Central manager state=\(self.centralStateName(central.state), privacy: .public)"
        )
        setupText = onboardingLifecycle.radioStatusText(
            for: radioState(for: central.state)
        )
        switch central.state {
        case .poweredOn:
            connectAuthorizedClocks()
        case .poweredOff, .unauthorized:
            scanTimeout?.cancel()
            scanTimeout = nil
            scanTargetIdentifiers = []
            central.stopScan()
            for runtime in runtimes.values {
                runtime.reconnectWorkItem?.cancel()
                runtime.reconnectWorkItem = nil
                runtime.connectPending = false
                runtime.isConnected = false
                runtime.connectionText = wantsConnection(runtime)
                    ? onboardingLifecycle.radioStatusText(
                        for: radioState(for: central.state)
                    )
                    : "Automatic sync is off"
            }
        case .unsupported, .resetting, .unknown:
            scanTimeout?.cancel()
            scanTimeout = nil
            scanTargetIdentifiers = []
            central.stopScan()
            for runtime in runtimes.values {
                runtime.connectPending = false
                runtime.connectionText = wantsConnection(runtime)
                    ? onboardingLifecycle.radioStatusText(
                        for: radioState(for: central.state)
                    )
                    : "Automatic sync is off"
            }
        @unknown default:
            for runtime in runtimes.values {
                runtime.connectPending = false
            }
        }
        publishClocks()
    }

    func centralManager(
        _ central: CBCentralManager,
        willRestoreState dict: [String: Any]
    ) {
        guard centralManager === central,
              let peripherals =
                dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral]
        else {
            return
        }
        for peripheral in peripherals where runtimes[peripheral.identifier] != nil {
            adopt(peripheral)
            guard let runtime = runtimes[peripheral.identifier] else { continue }
            if peripheral.state == .connected {
                runtime.isConnected = true
                discoverClockServices(for: runtime)
            } else if peripheral.state == .connecting {
                runtime.connectPending = true
            }
        }
        publishClocks()
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard centralManager === central,
              let runtime = runtimes[peripheral.identifier],
              wantsConnection(runtime) else {
            return
        }
        adopt(peripheral)
        applyClockName(
            advertisementData[CBAdvertisementDataLocalNameKey] as? String,
            to: runtime
        )
        requestConnection(for: runtime)
        updateScanState()
        publishClocks()
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard centralManager === central,
              let runtime = runtimes[peripheral.identifier],
              wantsConnection(runtime) else {
            central.cancelPeripheralConnection(peripheral)
            return
        }
        runtime.reconnectWorkItem?.cancel()
        runtime.reconnectWorkItem = nil
        runtime.reconnectAttempts = 0
        runtime.connectPending = false
        runtime.isConnected = true
        runtime.lastError = nil
        runtime.connectionText = "Connected; discovering clock service…"
        adopt(peripheral)
        discoverClockServices(for: runtime)
        publishClocks()
    }

    func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        guard centralManager === central,
              let runtime = runtimes[peripheral.identifier] else {
            return
        }
        runtime.connectPending = false
        runtime.isConnected = false
        runtime.lastError = error.map {
            "Connection failed: \($0.localizedDescription)"
        }
        finishManualSyncIfNeeded(for: runtime)
        scheduleReconnect(for: runtime)
        publishClocks()
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        timestamp: CFAbsoluteTime,
        isReconnecting: Bool,
        error: Error?
    ) {
        guard centralManager === central,
              let runtime = runtimes[peripheral.identifier] else {
            return
        }
        runtime.connectPending = isReconnecting
        runtime.isConnected = false
        runtime.timeCharacteristic = nil
        runtime.statusCharacteristic = nil
        runtime.sentInitialTimeForConnection = false
        runtime.awaitingClockAcceptance = false
        runtime.connectionText = isReconnecting
            ? "Clock out of range; reconnecting…"
            : "Disconnected"
        if !runtime.automaticSyncEnabled {
            if runtime.manualSyncRequested {
                finishManualSyncIfNeeded(for: runtime)
            } else {
                runtime.connectionText = "Automatic sync is off"
            }
        } else if !isReconnecting {
            scheduleReconnect(for: runtime)
        }
        publishClocks()
    }
}

extension ClockSyncManager: @preconcurrency CBPeripheralDelegate {
    func peripheralDidUpdateName(_ peripheral: CBPeripheral) {
        guard let runtime = runtimes[peripheral.identifier] else { return }
        applyClockName(peripheral.name, to: runtime)
        publishClocks()
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let runtime = runtimes[peripheral.identifier] else { return }
        if let error {
            runtime.lastError =
                "Service discovery failed: \(error.localizedDescription)"
            publishClocks()
            return
        }
        let services = peripheral.services ?? []
        if let gapService = services.first(where: {
            $0.uuid == Self.gapServiceUUID
        }) {
            peripheral.discoverCharacteristics(
                [Self.deviceNameUUID],
                for: gapService
            )
        }
        guard let clockService = services.first(where: {
            $0.uuid == Self.serviceUUID
        }) else {
            runtime.lastError =
                "This accessory does not expose the Kids Clock service."
            publishClocks()
            return
        }
        peripheral.discoverCharacteristics(
            [Self.timeUUID, Self.statusUUID],
            for: clockService
        )
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        guard let runtime = runtimes[peripheral.identifier] else { return }
        if let error {
            if service.uuid == Self.gapServiceUUID {
                return
            }
            runtime.lastError =
                "Characteristic discovery failed: \(error.localizedDescription)"
            publishClocks()
            return
        }
        if service.uuid == Self.gapServiceUUID {
            if let deviceName = service.characteristics?.first(where: {
                $0.uuid == Self.deviceNameUUID
            }) {
                peripheral.readValue(for: deviceName)
            }
            return
        }
        guard service.uuid == Self.serviceUUID else { return }
        for characteristic in service.characteristics ?? [] {
            switch characteristic.uuid {
            case Self.timeUUID:
                runtime.timeCharacteristic = characteristic
            case Self.statusUUID:
                runtime.statusCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            default:
                break
            }
        }
        guard runtime.timeCharacteristic != nil,
              runtime.statusCharacteristic != nil else {
            runtime.lastError =
                "The clock service is incomplete. Update its firmware."
            publishClocks()
            return
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard let runtime = runtimes[peripheral.identifier] else { return }
        if let error {
            runtime.lastError =
                "Background notifications could not be enabled: \(error.localizedDescription)"
        } else if characteristic.uuid == Self.statusUUID,
                  characteristic.isNotifying {
            peripheral.readValue(for: characteristic)
            if !runtime.sentInitialTimeForConnection {
                runtime.sentInitialTimeForConnection = true
                sendPhoneTime(to: runtime)
            }
        }
        publishClocks()
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard let runtime = runtimes[peripheral.identifier] else { return }
        if characteristic.uuid == Self.deviceNameUUID {
            guard error == nil,
                  let value = characteristic.value,
                  let candidate = String(data: value, encoding: .utf8) else {
                return
            }
            applyClockName(candidate, to: runtime)
            publishClocks()
            return
        }
        if let error {
            runtime.lastError =
                "Clock status could not be read: \(error.localizedDescription)"
            publishClocks()
            return
        }
        if characteristic.uuid == Self.statusUUID {
            handleStatus(characteristic.value, for: runtime)
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard characteristic.uuid == Self.timeUUID,
              let runtime = runtimes[peripheral.identifier] else {
            return
        }
        if let error {
            runtime.acknowledgementTimeout?.cancel()
            runtime.acknowledgementTimeout = nil
            runtime.awaitingClockAcceptance = false
            runtime.lastError = "Time transfer failed: \(error.localizedDescription)"
            finishManualSyncIfNeeded(for: runtime)
            publishClocks()
        }
    }
}
