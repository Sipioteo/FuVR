// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import AppKit
import FuVRControl

/// AppDelegate exists for one reason: when launched via `swift run`
/// (i.e. as a bare executable without an .app bundle / Info.plist),
/// AppKit defaults to `.accessory` activation policy — no dock icon,
/// no Apple menu, no ⌘Q. Promoting to `.regular` at the earliest
/// possible moment fixes that without needing to package an .app.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ note: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)

        // Override the generic Terminal/cmd dock icon when running via
        // `swift run` (no .app bundle / Info.plist). Bundle.module resolves
        // to the per-target resource bundle SwiftPM generates for FuVRApp.
        if let url = Bundle.module.url(forResource: "AppIcon", withExtension: "png"),
           let img = NSImage(contentsOf: url) {
            NSApp.applicationIconImage = img
        }
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@main
struct FuVRApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var state = AppState()
    @AppStorage("fuvr.onboarding.shown") private var onboardingShown: Bool = false

    var body: some Scene {
        WindowGroup("FuVR") {
            ContentView()
                .environmentObject(state)
                .frame(minWidth: 880, minHeight: 600)
                .sheet(isPresented: $state.showQuestSetup) {
                    QuestSetupWizard()
                        .environmentObject(state)
                }
                .onAppear {
                    if !onboardingShown {
                        state.showQuestSetup = true
                        onboardingShown = true
                    }
                }
        }
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("About FuVR") { state.showAbout = true }
                Button("Setup wizard…") { state.showQuestSetup = true }
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
