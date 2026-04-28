// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRADB

// MARK: - Main dashboard

struct DeviceDashboardView: View {
    @EnvironmentObject var state: AppState

    @State private var glowPulse = false
    @State private var iconBounce = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 28) {
                Text("Device").font(.largeTitle).bold()

                heroCard
                    .frame(maxWidth: .infinity)

                if case .streaming = state.deviceState {
                    streamMetricsRow
                }

                adbInfoCard

                Spacer(minLength: 0)
            }
            .padding(28)
        }
        .onChange(of: state.deviceState) { _ in triggerEntrance() }
        .onAppear { triggerEntrance() }
    }

    // MARK: Hero card

    private var heroCard: some View {
        ZStack {
            // Animated glow behind card
            RoundedRectangle(cornerRadius: 28, style: .continuous)
                .fill(accentColor.opacity(glowPulse ? 0.18 : 0.08))
                .blur(radius: 24)
                .scaleEffect(glowPulse ? 1.04 : 0.98)
                .animation(
                    .easeInOut(duration: 2.2).repeatForever(autoreverses: true),
                    value: glowPulse
                )

            // Card
            RoundedRectangle(cornerRadius: 22, style: .continuous)
                .fill(.ultraThinMaterial)
                .overlay(
                    RoundedRectangle(cornerRadius: 22, style: .continuous)
                        .strokeBorder(accentColor.opacity(0.3), lineWidth: 1)
                )
                .shadow(color: accentColor.opacity(0.2), radius: 18, y: 6)

            VStack(spacing: 22) {
                // State icon
                ZStack {
                    Circle()
                        .fill(accentColor.opacity(0.12))
                        .frame(width: 90, height: 90)
                    Image(systemName: state.deviceState.systemImage)
                        .font(.system(size: 40, weight: .semibold))
                        .foregroundStyle(accentColor)
                        .symbolEffect(.bounce.down, value: iconBounce)
                        .contentTransition(.symbolEffect(.replace))
                }

                // Label
                VStack(spacing: 6) {
                    Text(state.deviceState.humanLabel)
                        .font(.title2.weight(.semibold))
                        .multilineTextAlignment(.center)
                        .contentTransition(.numericText())

                    if case .installing(_, _, let pct) = state.deviceState {
                        ProgressView(value: pct)
                            .tint(accentColor)
                            .frame(maxWidth: 260)
                            .animation(.easeInOut(duration: 0.3), value: pct)
                    }

                    if case .failed(let msg) = state.deviceState {
                        Text(msg)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 24)
                    }
                }

                // Supplementary info chips
                if let model = state.deviceState.model,
                   let serial = state.deviceState.serial {
                    HStack(spacing: 10) {
                        chip(systemImage: "goggles", text: model)
                        chip(systemImage: "number", text: String(serial.suffix(8)))
                    }
                }
            }
            .padding(36)
        }
        .animation(.spring(duration: 0.55), value: state.deviceState)
    }

    // MARK: Stream metrics row (shown only while streaming)

    private var streamMetricsRow: some View {
        HStack(spacing: 14) {
            if let m = state.latestMetrics {
                metricTile("RTT",    String(format: "%.1f ms",  m.rttMs),        .blue)
                metricTile("FPS",    String(format: "%.0f",     m.encoderFps),   .green)
                metricTile("Encode", String(format: "%.1f ms",  m.encodeMs),     .orange)
                metricTile("Loss",   String(format: "%.2f%%",   m.packetLossPct),.red)
            } else {
                metricTile("RTT",    "— ms",  .blue)
                metricTile("FPS",    "—",     .green)
                metricTile("Encode", "— ms",  .orange)
                metricTile("Loss",   "—%%",   .red)
            }
        }
    }

    // MARK: ADB info card

    private var adbInfoCard: some View {
        GroupBox {
            HStack(spacing: 16) {
                Image(systemName: "cable.connector.horizontal")
                    .foregroundStyle(.secondary)
                    .font(.title3)
                VStack(alignment: .leading, spacing: 3) {
                    Text("Android Debug Bridge")
                        .font(.callout.weight(.medium))
                    Text(ADBRunner.shared.resolvedPath)
                        .font(.caption.monospaced())
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                Spacer()
            }
            .padding(4)
        } label: {
            Label("adb", systemImage: "terminal")
        }
    }

    // MARK: Helpers

    private var accentColor: Color {
        switch state.deviceState.colorName {
        case "green":  return .green
        case "blue":   return .blue
        case "yellow": return .yellow
        case "orange": return .orange
        case "red":    return .red
        default:       return .secondary
        }
    }

    private func chip(systemImage: String, text: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: systemImage).font(.caption)
            Text(text).font(.caption).lineLimit(1)
        }
        .padding(.horizontal, 10).padding(.vertical, 5)
        .background(.thinMaterial, in: Capsule())
        .overlay(Capsule().strokeBorder(.secondary.opacity(0.2), lineWidth: 0.5))
    }

    private func metricTile(_ label: String, _ value: String, _ color: Color) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label).font(.caption2).foregroundStyle(.secondary)
            Text(value).font(.title3.monospacedDigit()).foregroundStyle(color)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(color.opacity(0.25), lineWidth: 1)
        )
    }

    private func triggerEntrance() {
        glowPulse = true
        iconBounce.toggle()
    }
}
