// SPDX-License-Identifier: Apache-2.0
import SwiftUI

@main
struct FuVRApp: App {
    @StateObject private var state = AppState()

    var body: some Scene {
        WindowGroup("FuVR") {
            ContentView()
                .environmentObject(state)
                .frame(minWidth: 880, minHeight: 600)
        }
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("About FuVR") { state.showAbout = true }
            }
        }

        Settings {
            SettingsRoot()
                .environmentObject(state)
                .frame(width: 480, height: 380)
        }
    }
}
