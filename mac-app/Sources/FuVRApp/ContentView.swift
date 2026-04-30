// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

enum Tab: String, CaseIterable, Hashable {
    case stream, device, session, encoder, transport, diagnostics, log, about

    var label: String {
        switch self {
        case .stream:      return "Stream"
        case .device:      return "Device"
        case .session:     return "Session"
        case .encoder:     return "Encoder"
        case .transport:   return "Transport"
        case .diagnostics: return "Diagnostics"
        case .log:         return "Log"
        case .about:       return "About"
        }
    }

    var systemImage: String {
        switch self {
        case .stream:      return "dot.radiowaves.left.and.right"
        case .device:      return "goggles"
        case .session:     return "play.circle"
        case .encoder:     return "cpu"
        case .transport:   return "cable.connector"
        case .diagnostics: return "waveform.path.ecg"
        case .log:         return "text.alignleft"
        case .about:       return "info.circle"
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var state: AppState
    @State private var tab: Tab = .stream

    var body: some View {
        NavigationSplitView {
            List(Tab.allCases, id: \.self, selection: $tab) { t in
                if t == .device {
                    deviceRow(t)
                } else {
                    Label(t.label, systemImage: t.systemImage).tag(t)
                }
            }
            .listStyle(.sidebar)
            .navigationTitle("FuVR")
            .frame(minWidth: 190)
        } detail: {
            Group {
                switch tab {
                case .stream:      StreamView()
                case .device:      DeviceDashboardView()
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

    /// Device row shows a live status dot alongside the label.
    private func deviceRow(_ t: Tab) -> some View {
        HStack(spacing: 8) {
            Image(systemName: t.systemImage)
            Text(t.label)
            Spacer()
            Circle()
                .fill(deviceDotColor)
                .frame(width: 8, height: 8)
                .animation(.easeInOut(duration: 0.4), value: state.deviceState)
        }
        .tag(t)
    }

    private var deviceDotColor: Color {
        switch state.deviceState.colorName {
        case "green":  return .green
        case "blue":   return .blue
        case "yellow": return .yellow
        case "orange": return .orange
        case "red":    return .red
        default:       return Color(.systemGray)
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
