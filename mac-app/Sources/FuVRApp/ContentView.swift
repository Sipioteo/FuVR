// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

enum Tab: String, CaseIterable, Hashable {
    case session, encoder, transport, diagnostics, log, about
    var label: String {
        switch self {
        case .session: return "Session"
        case .encoder: return "Encoder"
        case .transport: return "Transport"
        case .diagnostics: return "Diagnostics"
        case .log: return "Log"
        case .about: return "About"
        }
    }
    var systemImage: String {
        switch self {
        case .session: return "play.circle"
        case .encoder: return "cpu"
        case .transport: return "cable.connector"
        case .diagnostics: return "waveform.path.ecg"
        case .log: return "text.alignleft"
        case .about: return "info.circle"
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var state: AppState
    @State private var tab: Tab = .session

    var body: some View {
        NavigationSplitView {
            List(Tab.allCases, id: \.self, selection: $tab) { t in
                Label(t.label, systemImage: t.systemImage).tag(t)
            }
            .listStyle(.sidebar)
            .navigationTitle("FuVR")
            .frame(minWidth: 180)
        } detail: {
            Group {
                switch tab {
                case .session:     SessionView()
                case .encoder:     EncoderSettingsView()
                case .transport:   TransportSettingsView()
                case .diagnostics: DiagnosticsView()
                case .log:         LogView()
                case .about:       AboutView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(.regularMaterial)
        }
    }
}

struct SettingsRoot: View {
    var body: some View {
        TabView {
            EncoderSettingsView().tabItem { Label("Encoder", systemImage: "cpu") }
            TransportSettingsView().tabItem { Label("Transport", systemImage: "cable.connector") }
        }
        .padding()
    }
}
