import AccessorySetupKit
@preconcurrency import CoreBluetooth
import Foundation
import os
import UIKit

@MainActor
final class ClockSyncManager: NSObject, ObservableObject {
    static let serviceUUID = CBUUID(string: "7F510000-1B15-4DC7-9F3F-19B30A6F6A21")
    static let timeUUID = CBUUID(string: "7F510001-1B15-4DC7-9F3F-19B30A6F6A21")
    static let statusUUID = CBUUID(string: "7F510002-1B15-4DC7-9F3F-19B30A6F6A21")

    @Published private(set) var connectionText = "Ready to add clock"
    @Published private(set) var clockName: String?
    @Published private(set) var lastSyncDate: Date?
    @Published private(set) var lastError: String?
    @Published private(set) var isConnected = false
    @Published private(set) var hasAuthorizedClock = false
    @Published var automaticSyncEnabled: Bool {
        didSet {
            defaults.set(automaticSyncEnabled, forKey: Keys.automaticSync)
            if automaticSyncEnabled {
                reconnectAttempts = 0
                connectAuthorizedClock()
            } else {
                stopConnection()
            }
        }
    }

    private enum Keys {
        static let automaticSync = "automaticSyncEnabled"
        static let lastSync = "lastSyncDate"
        static let restorationIdentifier = "centralRestorationIdentifier"
    }

    private let defaults = UserDefaults.standard
    private let accessorySession = ASAccessorySession()
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.espclock.KidsClockCompanion",
        category: "BLEOnboarding"
    )
    private var onboardingLifecycle = OnboardingLifecycle()
    private var centralManager: CBCentralManager?
    private var activePeripheral: CBPeripheral?
    private var timeCharacteristic: CBCharacteristic?
    private var statusCharacteristic: CBCharacteristic?
    private var connectPending = false
    private var sentInitialTimeForConnection = false
    private var awaitingClockAcceptance = false
    private var manualSyncRequested = false
    private var acknowledgementTimeout: DispatchWorkItem?
    private var reconnectWorkItem: DispatchWorkItem?
    private var scanTimeout: DispatchWorkItem?
    private var reconnectAttempts = 0
    private var accessorySessionActivated = false

#if DEBUG
    var hasCentralManagerForTesting: Bool {
        centralManager != nil
    }
#endif

    override init() {
        automaticSyncEnabled =
            UserDefaults.standard.object(forKey: Keys.automaticSync) as? Bool ?? true
        lastSyncDate = UserDefaults.standard.object(forKey: Keys.lastSync) as? Date
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

        if centralManager != nil {
            logger.error("Discarding unexpected central manager before picker presentation")
            tearDownCoreBluetooth()
        }

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
        connectionText = onboardingLifecycle.radioStatusText(for: .unknown)
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
                } else {
                    self.tearDownCoreBluetooth()
                }
                self.connectionText = "Ready to add clock"
            }
        }
    }

    func syncNow() {
        lastError = nil
        manualSyncRequested = true
        reconnectAttempts = 0
        guard isConnected else {
            connectionText = "Connecting…"
            connectAuthorizedClock(allowWhenAutomaticOff: true)
            return
        }
        sendPhoneTime()
    }

    func forgetClock() {
        guard let identifier = onboardingLifecycle.selectedClock?.bluetoothIdentifier,
              let accessory = authorizedAccessories.first(where: {
                  $0.bluetoothIdentifier == identifier
              }) else {
            return
        }
        stopConnection()
        accessorySession.removeAccessory(accessory) { [weak self] error in
            Task { @MainActor in
                guard let self else { return }
                if let error {
                    self.lastError = self.privacySafeError(
                        error,
                        prefix: "Could not remove the clock"
                    )
                } else {
                    if let action = self.onboardingLifecycle.accessoryRemoved() {
                        self.handleLifecycleAction(action)
                    } else {
                        self.tearDownCoreBluetooth()
                    }
                    self.hasAuthorizedClock = false
                    self.clockName = nil
                    self.connectionText = "Ready to add clock"
                }
            }
        }
    }

    private var authorizedAccessories: [ASAccessory] {
        accessorySession.accessories.filter {
            $0.state == .authorized && $0.bluetoothIdentifier != nil
        }
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
            refreshAuthorizedAccessoryState()
            if let action = onboardingLifecycle.sessionActivated(
                with: currentAuthorizedClock()
            ) {
                handleLifecycleAction(action)
            }
        case .accessoryAdded:
            if let clock = authorizedClock(from: event.accessory) ?? currentAuthorizedClock() {
                onboardingLifecycle.accessoryAdded(clock)
                hasAuthorizedClock = true
                clockName = clock.displayName
            } else {
                refreshAuthorizedAccessoryState()
            }
        case .accessoryChanged:
            refreshAuthorizedAccessoryState()
            if let clock = currentAuthorizedClock() {
                onboardingLifecycle.accessoryAdded(clock)
                if !onboardingLifecycle.pickerIsActive, centralManager == nil,
                   let action = onboardingLifecycle.sessionActivated(with: clock) {
                    handleLifecycleAction(action)
                }
            } else {
                if let action = onboardingLifecycle.accessoryRemoved() {
                    handleLifecycleAction(action)
                } else {
                    tearDownCoreBluetooth()
                }
                connectionText = "Ready to add clock"
            }
        case .accessoryRemoved:
            if let action = onboardingLifecycle.accessoryRemoved() {
                handleLifecycleAction(action)
            } else {
                tearDownCoreBluetooth()
            }
            refreshAuthorizedAccessoryState()
            connectionText = "Ready to add clock"
        case .pickerDidPresent:
            onboardingLifecycle.pickerDidPresent()
            connectionText = "Finding accessories…"
            logger.info(
                "Accessory picker presented; centralManagerExists=\(self.centralManager != nil, privacy: .public)"
            )
        case .pickerDidDismiss:
            if onboardingLifecycle.selectedClock == nil,
               let clock = currentAuthorizedClock() {
                onboardingLifecycle.accessoryAdded(clock)
            }
            if let action = onboardingLifecycle.pickerDidDismiss() {
                manualSyncRequested = !automaticSyncEnabled
                handleLifecycleAction(action)
            } else if onboardingLifecycle.selectedClock == nil {
                tearDownCoreBluetooth()
                connectionText = "Ready to add clock"
            }
        case .pickerSetupFailed:
            if let error = event.error {
                handlePickerFailure(error, prefix: "The clock could not be paired")
            } else {
                lastError = "The clock could not be paired."
            }
            if let action = onboardingLifecycle.pickerFailed() {
                handleLifecycleAction(action)
            } else {
                tearDownCoreBluetooth()
            }
            connectionText = "Ready to add clock"
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
            if let action = onboardingLifecycle.accessoryRemoved() {
                handleLifecycleAction(action)
            } else {
                tearDownCoreBluetooth()
            }
        default:
            break
        }
    }

    private func refreshAuthorizedAccessoryState() {
        let clock = currentAuthorizedClock()
        hasAuthorizedClock = clock != nil
        clockName = clock?.displayName
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

    private func currentAuthorizedClock() -> AuthorizedClock? {
        authorizedClock(from: authorizedAccessories.first)
    }

    private func handleLifecycleAction(_ action: OnboardingLifecycleAction) {
        switch action {
        case .initializeBluetoothAndConnect(let identifier):
            initializeCoreBluetooth(authorizedIdentifier: identifier)
        case .tearDownBluetooth:
            tearDownCoreBluetooth()
        }
    }

    private func initializeCoreBluetooth(authorizedIdentifier: UUID) {
        guard centralManager == nil,
              accessorySessionActivated,
              onboardingLifecycle.selectedClock?.bluetoothIdentifier ==
                  authorizedIdentifier else {
            return
        }
        let restorationIdentifier: String
        if let saved = defaults.string(forKey: Keys.restorationIdentifier) {
            restorationIdentifier = saved
        } else {
            restorationIdentifier = "com.espclock.companion.central.\(UUID().uuidString)"
            defaults.set(restorationIdentifier, forKey: Keys.restorationIdentifier)
        }
        connectionText = onboardingLifecycle.radioStatusText(for: .unknown)
        logger.info("Creating post-authorization central manager")
        centralManager = CBCentralManager(
            delegate: self,
            queue: .main,
            options: [CBCentralManagerOptionRestoreIdentifierKey: restorationIdentifier]
        )
    }

    private func tearDownCoreBluetooth() {
        stopConnection()
        centralManager?.delegate = nil
        centralManager = nil
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

    private func connectAuthorizedClock(allowWhenAutomaticOff: Bool = false) {
        guard automaticSyncEnabled || allowWhenAutomaticOff,
              accessorySessionActivated,
              let centralManager,
              centralManager.state == .poweredOn,
              let identifier = onboardingLifecycle.selectedClock?.bluetoothIdentifier else {
            return
        }

        if activePeripheral?.identifier == identifier {
            if activePeripheral?.state == .connected {
                discoverClockServices()
            } else if let activePeripheral,
                      activePeripheral.state == .disconnected,
                      !connectPending {
                requestConnection(to: activePeripheral)
            }
            return
        }

        guard let peripheral =
            centralManager.retrievePeripherals(withIdentifiers: [identifier]).first else {
            connectionText = "Clock authorized; waiting for it to advertise…"
            centralManager.scanForPeripherals(
                withServices: [Self.serviceUUID],
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
            )
            scanTimeout?.cancel()
            let work = DispatchWorkItem { [weak self] in
                guard let self else { return }
                self.centralManager?.stopScan()
                self.connectionText = "Clock not found; power it on and tap Sync Now"
                self.finishManualSyncIfNeeded(
                    connectionText: "Clock not found; automatic sync is off"
                )
            }
            scanTimeout = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 20, execute: work)
            return
        }
        adopt(peripheral)
        requestConnection(to: peripheral)
    }

    private func adopt(_ peripheral: CBPeripheral) {
        activePeripheral = peripheral
        peripheral.delegate = self
        timeCharacteristic = nil
        statusCharacteristic = nil
        sentInitialTimeForConnection = false
        awaitingClockAcceptance = false
    }

    private func requestConnection(to peripheral: CBPeripheral) {
        guard let centralManager,
              !connectPending,
              peripheral.state == .disconnected else {
            return
        }
        reconnectWorkItem?.cancel()
        reconnectWorkItem = nil
        scanTimeout?.cancel()
        scanTimeout = nil
        connectPending = true
        connectionText = "Connecting to \(clockName ?? "Kids Clock")…"
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

    private func scheduleReconnect(to peripheral: CBPeripheral) {
        guard automaticSyncEnabled else { return }
        reconnectWorkItem?.cancel()
        let delays: [TimeInterval] = [3, 15, 60]
        guard reconnectAttempts < delays.count else {
            connectionText = "Automatic reconnect paused; open the app to retry"
            return
        }
        let delay = delays[reconnectAttempts]
        reconnectAttempts += 1
        let work = DispatchWorkItem { [weak self, weak peripheral] in
            guard let self,
                  let peripheral,
                  self.automaticSyncEnabled,
                  self.activePeripheral?.identifier == peripheral.identifier else {
                return
            }
            self.requestConnection(to: peripheral)
        }
        reconnectWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func discoverClockServices() {
        guard let peripheral = activePeripheral, peripheral.state == .connected else { return }
        peripheral.discoverServices([Self.serviceUUID])
    }

    private func stopConnection() {
        acknowledgementTimeout?.cancel()
        acknowledgementTimeout = nil
        reconnectWorkItem?.cancel()
        reconnectWorkItem = nil
        scanTimeout?.cancel()
        scanTimeout = nil
        reconnectAttempts = 0
        awaitingClockAcceptance = false
        manualSyncRequested = false
        connectPending = false
        centralManager?.stopScan()
        if let peripheral = activePeripheral {
            centralManager?.cancelPeripheralConnection(peripheral)
        }
        activePeripheral = nil
        timeCharacteristic = nil
        statusCharacteristic = nil
        isConnected = false
        if hasAuthorizedClock {
            connectionText = "Automatic sync is off"
        }
    }

    private func sendPhoneTime() {
        guard !awaitingClockAcceptance,
              let peripheral = activePeripheral,
              peripheral.state == .connected,
              let timeCharacteristic else {
            return
        }

        do {
            let packet = try TimePacketEncoder.encode(date: Date())
            awaitingClockAcceptance = true
            connectionText = "Sending iPhone time…"
            peripheral.writeValue(packet, for: timeCharacteristic, type: .withResponse)
            scheduleAcknowledgementCheck()
        } catch {
            lastError = error.localizedDescription
        }
    }

    private func finishManualSyncIfNeeded(connectionText finalText: String? = nil) {
        guard manualSyncRequested else { return }
        manualSyncRequested = false
        guard !automaticSyncEnabled else { return }
        stopConnection()
        if let finalText {
            connectionText = finalText
        }
    }

    private func scheduleAcknowledgementCheck() {
        acknowledgementTimeout?.cancel()
        let readWork = DispatchWorkItem { [weak self] in
            guard let self, self.awaitingClockAcceptance else { return }
            if let peripheral = self.activePeripheral,
               let status = self.statusCharacteristic {
                peripheral.readValue(for: status)
            }
            let failWork = DispatchWorkItem { [weak self] in
                guard let self, self.awaitingClockAcceptance else { return }
                self.lastError =
                    "The clock did not acknowledge the update. Keep it nearby and tap Sync Now."
                self.awaitingClockAcceptance = false
                self.finishManualSyncIfNeeded()
            }
            self.acknowledgementTimeout = failWork
            DispatchQueue.main.asyncAfter(deadline: .now() + 2, execute: failWork)
        }
        acknowledgementTimeout = readWork
        DispatchQueue.main.asyncAfter(deadline: .now() + 6, execute: readWork)
    }

    private func handleStatus(_ data: Data?) {
        guard let data, let status = String(data: data, encoding: .utf8) else { return }
        switch status {
        case "sync-request", "time-needed":
            sendPhoneTime()
        case "time-accepted":
            acknowledgementTimeout?.cancel()
            acknowledgementTimeout = nil
            awaitingClockAcceptance = false
            lastError = nil
            let now = Date()
            lastSyncDate = now
            defaults.set(now, forKey: Keys.lastSync)
            connectionText = "Connected and synchronized"
            finishManualSyncIfNeeded(connectionText: "Synchronized; automatic sync is off")
        case "time-pending":
            connectionText = "Clock is applying the time…"
        case "rate-limited":
            acknowledgementTimeout?.cancel()
            acknowledgementTimeout = nil
            awaitingClockAcceptance = false
            connectionText = "Connected; clock recently synchronized"
            finishManualSyncIfNeeded(connectionText: "Clock recently synchronized; automatic sync is off")
        case "time-rejected", "invalid-time":
            acknowledgementTimeout?.cancel()
            acknowledgementTimeout = nil
            awaitingClockAcceptance = false
            lastError = "The clock rejected the phone time. Check the iPhone date and time."
            finishManualSyncIfNeeded()
        default:
            break
        }
    }
}

extension ClockSyncManager: @preconcurrency CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard centralManager === central else { return }
        logger.info(
            "Central manager state=\(self.centralStateName(central.state), privacy: .public)"
        )
        connectionText = onboardingLifecycle.radioStatusText(
            for: radioState(for: central.state)
        )
        switch central.state {
        case .poweredOn:
            connectAuthorizedClock(allowWhenAutomaticOff: manualSyncRequested)
        case .poweredOff:
            scanTimeout?.cancel()
            scanTimeout = nil
            reconnectWorkItem?.cancel()
            reconnectWorkItem = nil
            connectPending = false
            central.stopScan()
            isConnected = false
        case .unauthorized:
            scanTimeout?.cancel()
            scanTimeout = nil
            reconnectWorkItem?.cancel()
            reconnectWorkItem = nil
            connectPending = false
            central.stopScan()
            isConnected = false
        case .unsupported:
            connectPending = false
        case .resetting, .unknown:
            connectPending = false
        @unknown default:
            connectPending = false
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        willRestoreState dict: [String: Any]
    ) {
        guard centralManager === central else { return }
        guard let peripherals =
            dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
              let identifier =
                onboardingLifecycle.selectedClock?.bluetoothIdentifier,
              let peripheral = peripherals.first(where: {
                  $0.identifier == identifier
              }) else {
            return
        }
        adopt(peripheral)
        if peripheral.state == .connected {
            isConnected = true
            discoverClockServices()
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard centralManager === central else { return }
        guard automaticSyncEnabled || manualSyncRequested,
              onboardingLifecycle.selectedClock?.bluetoothIdentifier ==
                  peripheral.identifier else {
            return
        }
        central.stopScan()
        scanTimeout?.cancel()
        scanTimeout = nil
        adopt(peripheral)
        requestConnection(to: peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard centralManager === central,
              onboardingLifecycle.selectedClock?.bluetoothIdentifier ==
                  peripheral.identifier else {
            central.cancelPeripheralConnection(peripheral)
            return
        }
        reconnectWorkItem?.cancel()
        reconnectWorkItem = nil
        reconnectAttempts = 0
        connectPending = false
        isConnected = true
        lastError = nil
        connectionText = "Connected; discovering clock service…"
        adopt(peripheral)
        discoverClockServices()
    }

    func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        guard centralManager === central else { return }
        connectPending = false
        isConnected = false
        lastError = error.map { "Connection failed: \($0.localizedDescription)" }
        finishManualSyncIfNeeded()
        scheduleReconnect(to: peripheral)
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        timestamp: CFAbsoluteTime,
        isReconnecting: Bool,
        error: Error?
    ) {
        guard centralManager === central else { return }
        connectPending = isReconnecting
        isConnected = false
        timeCharacteristic = nil
        statusCharacteristic = nil
        sentInitialTimeForConnection = false
        awaitingClockAcceptance = false
        connectionText = isReconnecting ? "Clock out of range; reconnecting…" : "Disconnected"
        if !automaticSyncEnabled {
            finishManualSyncIfNeeded()
        } else if !isReconnecting {
            scheduleReconnect(to: peripheral)
        }
    }
}

extension ClockSyncManager: @preconcurrency CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            lastError = "Service discovery failed: \(error.localizedDescription)"
            return
        }
        guard let service = peripheral.services?.first(where: {
            $0.uuid == Self.serviceUUID
        }) else {
            lastError = "This accessory does not expose the Kids Clock service."
            return
        }
        peripheral.discoverCharacteristics(
            [Self.timeUUID, Self.statusUUID],
            for: service
        )
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        if let error {
            lastError = "Characteristic discovery failed: \(error.localizedDescription)"
            return
        }
        for characteristic in service.characteristics ?? [] {
            switch characteristic.uuid {
            case Self.timeUUID:
                timeCharacteristic = characteristic
            case Self.statusUUID:
                statusCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            default:
                break
            }
        }
        guard timeCharacteristic != nil, statusCharacteristic != nil else {
            lastError = "The clock service is incomplete. Update its firmware."
            return
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            lastError = "Background notifications could not be enabled: \(error.localizedDescription)"
        } else if characteristic.uuid == Self.statusUUID,
                  characteristic.isNotifying {
            peripheral.readValue(for: characteristic)
            if !sentInitialTimeForConnection {
                sentInitialTimeForConnection = true
                sendPhoneTime()
            }
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            lastError = "Clock status could not be read: \(error.localizedDescription)"
            return
        }
        if characteristic.uuid == Self.statusUUID {
            handleStatus(characteristic.value)
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard characteristic.uuid == Self.timeUUID else { return }
        if let error {
            acknowledgementTimeout?.cancel()
            acknowledgementTimeout = nil
            awaitingClockAcceptance = false
            lastError = "Time transfer failed: \(error.localizedDescription)"
            finishManualSyncIfNeeded()
        }
    }
}
