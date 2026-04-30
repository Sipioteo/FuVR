// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

/// Live dashboard for the OpenVR application currently bound to the daemon.
///
/// Polls `GetActiveStream` (currently stubbed — see `ActiveStreamRPC.swift`)
/// at 2 Hz while the tab is visible. The card layout follows Apple HIG:
/// generous spacing, system materials, monospaced digits for live counters.
///
/// Icon policy: we never bundle third-party logos. Recognized apps fall
/// back to an SF Symbol picked by `AppKeyRegistry.iconSymbolName(for:)` —
/// e.g. `cube.fill` for Vivecraft. Unknown apps get a generic headset
/// glyph. Replace with first-party art if/when we ship our own.
struct StreamView: View {
    @EnvironmentObject var state: AppState

    private var stream: ActiveStream? { state.activeStream }
    private var isLive: Bool { stream?.connected == true }
    private var daemonConnected: Bool {
        if case .connected = state.connectionState { return true }
        return false
    }

    private var statusColor: Color {
        if !daemonConnected { return .red }
        return isLive ? .green : .orange
    }

    private var statusText: String {
        if !daemonConnected { return "Disconnected" }
        return isLive ? "Live" : "Idle"
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 28) {
                header
                if isLive, let s = stream {
                    activeAppCard(s)
                    statsRow(s)
                } else {
                    idleState
                }
                Spacer(minLength: 0)
            }
            .padding(28)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .onAppear  { state.startActiveStreamPolling() }
        .onDisappear { state.stopActiveStreamPolling() }
    }

    // MARK: - Header

    private var header: some View {
        HStack(alignment: .center, spacing: 16) {
            VStack(alignment: .leading, spacing: 6) {
                Text("Stream")
                    .font(.largeTitle).bold()
                if let model = state.deviceState.model {
                    Text("Bound to \(model)")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            Spacer()
            HStack(spacing: 10) {
                Circle()
                    .fill(statusColor)
                    .frame(width: 12, height: 12)
                    .shadow(color: statusColor.opacity(0.6), radius: isLive ? 6 : 0)
                    .animation(.easeInOut(duration: 0.4), value: statusColor)
                Text(statusText)
                    .font(.system(.title3, design: .rounded).weight(.semibold))
            }
            .padding(.vertical, 10).padding(.horizontal, 18)
            .background(.thinMaterial, in: Capsule())
            .overlay(Capsule().strokeBorder(statusColor.opacity(0.35), lineWidth: 1))
        }
    }

    // MARK: - Active app card

    private func activeAppCard(_ s: ActiveStream) -> some View {
        HStack(alignment: .center, spacing: 24) {
            appIcon(for: s.appKey)
                .frame(width: 256, height: 256)
            VStack(alignment: .leading, spacing: 10) {
                Text(s.displayName)
                    .font(.system(.largeTitle, design: .rounded).weight(.bold))
                Text(s.appKey)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }
            Spacer(minLength: 0)
        }
        .padding(24)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 18))
        .overlay(
            RoundedRectangle(cornerRadius: 18)
                .strokeBorder(Color.primary.opacity(0.06), lineWidth: 1)
        )
    }

    @ViewBuilder
    private func appIcon(for appKey: String) -> some View {
        if let asset = AppKeyRegistry.bundledImageName(for: appKey),
           let nsimg = NSImage(named: asset) {
            Image(nsImage: nsimg)
                .resizable()
                .interpolation(.high)
                .aspectRatio(contentMode: .fit)
        } else {
            ZStack {
                RoundedRectangle(cornerRadius: 36)
                    .fill(LinearGradient(
                        colors: [.green.opacity(0.65), .brown.opacity(0.55)],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing))
                Image(systemName: AppKeyRegistry.iconSymbolName(for: appKey))
                    .resizable()
                    .scaledToFit()
                    .padding(56)
                    .foregroundStyle(.white)
                    .shadow(radius: 4, y: 2)
            }
        }
    }

    // MARK: - Stats row

    private func statsRow(_ s: ActiveStream) -> some View {
        HStack(spacing: 16) {
            statTile("Resolution",
                     "\(s.perEyeWidth) × \(s.perEyeHeight)",
                     subtitle: "per eye")
            statTile("Refresh",
                     "\(s.refreshRateHz) Hz")
            statTile("Live FPS",
                     String(format: "%.1f", s.currentFps),
                     emphasised: true)
            statTile("Frames",
                     formatFrames(s.framesSubmitted))
        }
    }

    private func statTile(_ label: String,
                          _ value: String,
                          subtitle: String? = nil,
                          emphasised: Bool = false) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label.uppercased())
                .font(.caption2.weight(.semibold))
                .foregroundStyle(.secondary)
                .tracking(0.5)
            Text(value)
                .font(.system(emphasised ? .title : .title2,
                              design: .rounded).weight(.semibold))
                .monospacedDigit()
                .contentTransition(.numericText())
                .animation(.easeInOut(duration: 0.25), value: value)
            if let subtitle {
                Text(subtitle)
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.vertical, 14).padding(.horizontal, 18)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12))
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .strokeBorder(Color.primary.opacity(0.06), lineWidth: 1)
        )
    }

    private func formatFrames(_ n: UInt64) -> String {
        let f = NumberFormatter()
        f.numberStyle = .decimal
        f.groupingSeparator = ","
        return f.string(from: NSNumber(value: n)) ?? String(n)
    }

    // MARK: - Idle state

    private var idleState: some View {
        VStack(spacing: 18) {
            Image(systemName: "visionpro")
                .resizable()
                .scaledToFit()
                .frame(width: 200, height: 200)
                .foregroundStyle(.secondary)
                .opacity(0.35)
            Text(daemonConnected
                 ? "No application connected."
                 : "Daemon disconnected.")
                .font(.system(.title2, design: .rounded).weight(.semibold))
            Text(daemonConnected
                 ? "Launch a VR-aware app to start streaming."
                 : "Connect to the daemon from the Session tab.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 60)
    }
}
