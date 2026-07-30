import Foundation

struct ClockPreferenceStore {
    private enum Keys {
        static let legacyAutomaticSync = "automaticSyncEnabled"
        static let legacyLastSync = "lastSyncDate"
        static let automaticSyncPrefix = "automaticSyncEnabled."
        static let lastSyncPrefix = "lastSyncDate."
        static let bluetoothNamePrefix = "bluetoothName."
        static let migrationComplete = "perClockPreferencesMigrated"
    }

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    func automaticSync(for identifier: UUID) -> Bool {
        let key = automaticSyncKey(for: identifier)
        if let saved = defaults.object(forKey: key) as? Bool {
            return saved
        }
        defaults.set(true, forKey: key)
        return true
    }

    func setAutomaticSync(_ enabled: Bool, for identifier: UUID) {
        defaults.set(enabled, forKey: automaticSyncKey(for: identifier))
    }

    func lastSync(for identifier: UUID) -> Date? {
        defaults.object(forKey: lastSyncKey(for: identifier)) as? Date
    }

    func setLastSync(_ date: Date, for identifier: UUID) {
        defaults.set(date, forKey: lastSyncKey(for: identifier))
    }

    func bluetoothName(for identifier: UUID) -> String? {
        defaults.string(forKey: bluetoothNameKey(for: identifier))
    }

    func setBluetoothName(_ name: String, for identifier: UUID) {
        defaults.set(name, forKey: bluetoothNameKey(for: identifier))
    }

    func removeClock(_ identifier: UUID) {
        defaults.removeObject(forKey: automaticSyncKey(for: identifier))
        defaults.removeObject(forKey: lastSyncKey(for: identifier))
        defaults.removeObject(forKey: bluetoothNameKey(for: identifier))
    }

    func migrateLegacyPreferences(for clocks: [AuthorizedClock]) {
        guard !defaults.bool(forKey: Keys.migrationComplete) else { return }

        let legacyAutomatic =
            defaults.object(forKey: Keys.legacyAutomaticSync) as? Bool
        let legacyLastSync =
            defaults.object(forKey: Keys.legacyLastSync) as? Date
        for clock in clocks {
            let identifier = clock.bluetoothIdentifier
            let automaticKey = automaticSyncKey(for: identifier)
            if defaults.object(forKey: automaticKey) == nil,
               let legacyAutomatic {
                defaults.set(legacyAutomatic, forKey: automaticKey)
            }
            let syncKey = lastSyncKey(for: identifier)
            if defaults.object(forKey: syncKey) == nil,
               let legacyLastSync {
                defaults.set(legacyLastSync, forKey: syncKey)
            }
        }
        defaults.set(true, forKey: Keys.migrationComplete)
    }

    private func automaticSyncKey(for identifier: UUID) -> String {
        Keys.automaticSyncPrefix + identifier.uuidString
    }

    private func lastSyncKey(for identifier: UUID) -> String {
        Keys.lastSyncPrefix + identifier.uuidString
    }

    private func bluetoothNameKey(for identifier: UUID) -> String {
        Keys.bluetoothNamePrefix + identifier.uuidString
    }
}
