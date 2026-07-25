import SwiftUI

@main
struct ESPClockCompanionApp: App {
    @StateObject private var syncManager = ClockSyncManager()

    var body: some Scene {
        WindowGroup {
            ContentView(syncManager: syncManager)
        }
    }
}
