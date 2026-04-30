// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

struct StatusPill: View {
    let text: String
    let color: Color

    var body: some View {
        HStack(spacing: 8) {
            Circle().fill(color).frame(width: 10, height: 10)
            Text(text).font(.headline).monospacedDigit()
        }
        .padding(.vertical, 10).padding(.horizontal, 16)
        .background(.thinMaterial, in: Capsule())
        .overlay(Capsule().strokeBorder(color.opacity(0.4), lineWidth: 1))
    }
}

struct SessionView: View {
    @EnvironmentObject var state: AppState

    @AppStorage(SettingsKey.socketPath)    private var socketPath: String = DefaultSocketPath.resolve()
    @AppStorage(SettingsKey.useMockDaemon) private var useMock: Bool = true
    @AppStorage(SettingsKey.perEyeWidth)   private var perEyeWidth: Int = 2064
    @AppStorage(SettingsKey.perEyeHeight)  private var perEyeHeight: Int = 2208
    @AppStorage(SettingsKey.refreshRate)   private var refreshRate: Int = 90
    @AppStorage(SettingsKey.codec)         private var codec: String = VideoCodec.hevc.rawValue
    @AppStorage(SettingsKey.bitrateMbps)   private var bitrateMbps: Int = 150
    @AppStorage(SettingsKey.audioEnabled)  private var audioEnabled: Bool = true

    @State private var showTetheringWizard: Bool = false
    @State private var rndisLink: RndisInterfaceMonitor.Snapshot?
    @State private var linkMonitor = RndisInterfaceMonitor()

    private var pillColor: Color {
        switch state.statusColorName {
        case "green":  return .green
        case "blue":   return .blue
        case "yellow": return .yellow
        case "red":    return .red
        default:       return .secondary
        }
    }

    private var connected: Bool {
        if case .connected = state.connectionState { return true }
        return false
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                HStack {
                    Text("Session").font(.largeTitle).bold()
                    Spacer()
                    StatusPill(text: state.statusText, color: pillColor)
                }

                GroupBox("Daemon") {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Text("Socket").frame(width: 90, alignment: .leading)
                            TextField("path", text: $socketPath).textFieldStyle(.roundedBorder)
                        }
                        Toggle("Run mock daemon in-process", isOn: $useMock)
                        HStack {
                            if connected {
                                Button("Disconnect") { state.disconnect() }
                                    .keyboardShortcut("d", modifiers: [.command])
                            } else {
                                Button("Connect") { state.connect(socketPath: socketPath, useMock: useMock) }
                                    .keyboardShortcut(.return, modifiers: [.command])
                                    .buttonStyle(.borderedProminent)
                            }
                            Spacer()
                            if let caps = state.capabilities {
                                Text("\(caps.deviceModel) · \(caps.systemVersion)")
                                    .foregroundStyle(.secondary).font(.callout)
                            }
                        }
                    }.padding(8)
                }

                GroupBox("USB Link") {
                    HStack(spacing: 12) {
                        if let link = rndisLink {
                            Image(systemName: "bolt.horizontal.circle.fill")
                                .foregroundStyle(.green)
                            VStack(alignment: .leading, spacing: 2) {
                                Text("High-Speed Link Active").font(.headline)
                                Text("Host \(link.ipv4) on \(link.interfaceName) → Quest 192.168.42.129:59000")
                                    .font(.caption).foregroundStyle(.secondary).monospaced()
                            }
                        } else {
                            Image(systemName: "cable.connector.horizontal")
                                .foregroundStyle(.orange)
                            VStack(alignment: .leading, spacing: 2) {
                                Text("USB Tethering not active").font(.headline)
                                Text("Quest will fall back to legacy TCP. Run the wizard for the lower-latency UDP path.")
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                        }
                        Spacer()
                        Button(rndisLink == nil ? "Set up…" : "Re-run wizard") {
                            showTetheringWizard = true
                        }
                    }.padding(8)
                }

                GroupBox("Stream") {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack(spacing: 24) {
                            metric("Codec", codec.uppercased())
                            metric("Resolution", "\(perEyeWidth)×\(perEyeHeight)")
                            metric("Refresh", "\(refreshRate) Hz")
                            metric("Bitrate", "\(bitrateMbps) Mb/s")
                        }
                        HStack {
                            if state.sessionActive {
                                Button("Stop session") { state.stopSession() }
                                    .buttonStyle(.bordered)
                            } else {
                                Button("Start session") {
                                    let cfg = SessionConfig(
                                        perEyeWidth: UInt32(perEyeWidth),
                                        perEyeHeight: UInt32(perEyeHeight),
                                        refreshRateHz: UInt32(refreshRate),
                                        videoCodec: VideoCodec(rawValue: codec) ?? .hevc,
                                        videoBitrateBps: UInt32(bitrateMbps) * 1_000_000,
                                        audioEnabled: audioEnabled
                                    )
                                    state.startSession(cfg)
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!connected)
                            }
                            Spacer()
                            if let m = state.latestMetrics {
                                liveBadge("RTT", String(format: "%.1f ms", m.rttMs))
                                liveBadge("FPS", String(format: "%.0f", m.fps))
                                liveBadge("Loss", String(format: "%.2f%%", m.packetLossPct))
                            }
                        }
                    }.padding(8)
                }

                if let err = state.lastError {
                    Label(err, systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(.red)
                }
                Spacer(minLength: 0)
            }
            .padding(28)
        }
        .sheet(isPresented: $showTetheringWizard) {
            TetheringWizardView(
                serial: nil,
                onLinkActive: { snap in rndisLink = snap },
                onDismiss: { showTetheringWizard = false }
            )
        }
        .onAppear {
            rndisLink = RndisInterfaceMonitor.currentSnapshot()
            linkMonitor.onChange = { snap in
                Task { @MainActor in rndisLink = snap }
            }
            linkMonitor.start()
        }
        .onDisappear { linkMonitor.stop() }
    }

    private func metric(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title3).monospacedDigit()
        }
    }

    private func liveBadge(_ label: String, _ value: String) -> some View {
        HStack(spacing: 4) {
            Text(label).font(.caption2).foregroundStyle(.secondary)
            Text(value).font(.caption).monospacedDigit()
        }
        .padding(.horizontal, 8).padding(.vertical, 4)
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 6))
    }
}
