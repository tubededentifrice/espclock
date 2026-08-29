import SwiftUI

struct ContentView: View {
    @ObservedObject var syncManager: ClockSyncManager
    @State private var clockPendingRemoval: ClockViewState?

    var body: some View {
        NavigationStack {
            Form {
                Section("Clock setup") {
                    LabeledContent("Status") {
                        Text(syncManager.setupText)
                            .multilineTextAlignment(.trailing)
                    }

                    Button(
                        syncManager.clocks.isEmpty
                            ? "Add ESPClock"
                            : "Add Another ESPClock"
                    ) {
                        syncManager.addClock()
                    }
                }

                if syncManager.clocks.isEmpty {
                    Section("Clocks") {
                        Text("No clocks are connected to this app.")
                            .foregroundStyle(.secondary)
                    }
                } else {
                    ForEach(syncManager.clocks) { clock in
                        Section(clock.name) {
                            LabeledContent("Status") {
                                Text(clock.connectionText)
                                    .multilineTextAlignment(.trailing)
                            }
                            LabeledContent("Last synchronized") {
                                if let date = clock.lastSyncDate {
                                    Text(
                                        date,
                                        format: .dateTime
                                            .day()
                                            .month()
                                            .hour()
                                            .minute()
                                    )
                                } else {
                                    Text("Never")
                                }
                            }

                            Toggle(
                                "Keep synchronized",
                                isOn: Binding(
                                    get: { clock.automaticSyncEnabled },
                                    set: {
                                        syncManager.setAutomaticSync(
                                            $0,
                                            for: clock.id
                                        )
                                    }
                                )
                            )

                            if let error = clock.lastError {
                                Text(error)
                                    .foregroundStyle(.red)
                            }

                            Button("Sync Now") {
                                syncManager.syncNow(clock.id)
                            }

                            Button("Remove Clock", role: .destructive) {
                                clockPendingRemoval = clock
                            }
                            .confirmationDialog(
                                "Remove \(clock.name)?",
                                isPresented: Binding(
                                    get: {
                                        clockPendingRemoval?.id == clock.id
                                    },
                                    set: {
                                        if !$0,
                                           clockPendingRemoval?.id == clock.id {
                                            clockPendingRemoval = nil
                                        }
                                    }
                                ),
                                titleVisibility: .visible
                            ) {
                                Button(
                                    "Remove \(clock.name)",
                                    role: .destructive
                                ) {
                                    syncManager.forgetClock(clock.id)
                                    clockPendingRemoval = nil
                                }
                            } message: {
                                Text(
                                    "You may need the clock's five-second BOOT recovery before you add it again."
                                )
                            }
                        }
                    }
                }

                if let error = syncManager.lastError {
                    Section("Needs attention") {
                        Text(error)
                            .foregroundStyle(.red)
                    }
                }

                Section("Important") {
                    Text(
                        "This iPhone can keep a separate Bluetooth connection to each added clock. Each clock still accepts only one connected phone at a time."
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
            .navigationTitle("ESPClock")
        }
    }
}
