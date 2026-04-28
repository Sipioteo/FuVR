// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

@main
struct FuVRApp: App {
    @StateObject private var state = AppState()
    @AppStorage("fuvr.onboarding.shown") private var onboardingShown: Bool = false

    var body: some Scene {
        WindowGroup("FuVR") {
            ContentView()
                .environmentObject(state)
                .frame(minWidth: 880, minHeight: 600)
                .sheet(isPresented: $state.showOnboarding) {
                    OnboardingView()
                        .environmentObject(state)
                }
                .sheet(isPresented: $state.showQuestSetup) {
                    QuestSetupWizard()
                        .environmentObject(state)
                }
                .onAppear {
                    if !onboardingShown {
                        state.showOnboarding = true
                        onboardingShown = true
                    }
                }
        }
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("About FuVR") { state.showAbout = true }
                Button("Setup wizard…") { state.showOnboarding = true }
                Button("Set up Quest headset…") { state.showQuestSetup = true }
                    .keyboardShortcut("h", modifiers: [.command, .shift])
            }
        }

        Settings {
            SettingsRoot()
                .environmentObject(state)
                .frame(width: 480, height: 380)
        }
    }
}
