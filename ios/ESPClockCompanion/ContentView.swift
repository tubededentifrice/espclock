import SwiftUI

struct ContentView: View {
    @ObservedObject var syncManager: ClockSyncManager
    @State private var confirmingRemoval = false

    var body: some View {
        NavigationStack {
            Form {
                Section("Clock") {
                    LabeledContent("Accessory") {
                        Text(syncManager.clockName ?? "Not added")
                    }
                    LabeledContent("Status") {
                        Text(syncManager.connectionText)
                            .multilineTextAlignment(.trailing)
                    }
                    LabeledContent("Last synchronized") {
                        if let date = syncManager.lastSyncDate {
                            Text(date, format: .dateTime.day().month().hour().minute())
                        } else {
                            Text("Never")
                        }
                    }
                }

                Section("Automatic synchronization") {
                    Toggle("Keep this clock synchronized", isOn: $syncManager.automaticSyncEnabled)
                        .disabled(!syncManager.hasAuthorizedClock)
                    Text(
                        "The iPhone keeps a low-power Bluetooth connection. The clock asks for fresh time about every six hours."
                    )
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                }

                if let error = syncManager.lastError {
                    Section("Needs attention") {
                        Text(error)
                            .foregroundStyle(.red)
                    }
                }

                Section {
                    if syncManager.hasAuthorizedClock {
                        Button("Sync Now") {
                            syncManager.syncNow()
                        }

                        Button("Remove Clock", role: .destructive) {
                            confirmingRemoval = true
                        }
                    } else {
                        Button("Add Kids Clock") {
                            syncManager.addClock()
                        }
                    }
                }

                Section("Important") {
                    Text(
                        "Keep only one family iPhone's automatic sync enabled at a time. Turn it off here to let another authorized phone take over."
                    )
                    .font(.footnote)
                    Text(
                        "Do not swipe this app away from the app switcher. iOS cannot relaunch a force-quit Bluetooth app. If synchronization stops, open the app once."
                    )
                    .font(.footnote)
                    Text(
                        "The app reads only the iPhone’s time and current time-zone offset. It uses no location, Internet service, account, or analytics."
                    )
                    .font(.footnote)
                }
            }
            .navigationTitle("Kids Clock")
            .confirmationDialog(
                "Remove this clock?",
                isPresented: $confirmingRemoval,
                titleVisibility: .visible
            ) {
                Button("Remove Clock", role: .destructive) {
                    syncManager.forgetClock()
                }
            } message: {
                Text(
                    "You may need the clock's five-second BOOT recovery before adding it again."
                )
            }
        }
    }
}
