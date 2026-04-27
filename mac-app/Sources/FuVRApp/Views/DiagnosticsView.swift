// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import Charts
import FuVRControl

private struct Point: Identifiable {
    let id: Int
    let x: Double
    let y: Double
}

struct DiagnosticsView: View {
    @EnvironmentObject var state: AppState

    /// Rolling window: 30 seconds at the daemon's current 10 Hz cadence.
    private let windowSamples = 300

    private var windowed: [MetricsSample] {
        let s = state.metrics.samples
        if s.count <= windowSamples { return s }
        return Array(s.suffix(windowSamples))
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                Text("Diagnostics").font(.largeTitle).bold()

                if let info = state.sessionInfo {
                    activeSessionCard(info)
                }

                let samples = windowed
                if samples.isEmpty {
                    ContentUnavailableView("No live data",
                                           systemImage: "waveform",
                                           description: Text("Start a session to see live encoder, decoder, and transport metrics."))
                        .frame(minHeight: 360)
                } else {
                    encoderRow(samples)
                    decoderRow(samples)
                }
                Spacer(minLength: 0)
            }
            .padding(28)
        }
    }

    // MARK: Encoder row

    @ViewBuilder
    private func encoderRow(_ samples: [MetricsSample]) -> some View {
        let fps   = MetricsBuffer.computeStats(samples.map(\.encoderFps))
        let p95   = MetricsBuffer.computeStats(samples.map(\.encoderEncodeMsP95))
        let rtt   = MetricsBuffer.computeStats(samples.map(\.rttMs))
        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 16) {
            sparkCard("Encoder fps", "fps", fps, samples, \.encoderFps, .green)
            sparkCard("Encode p95", "ms", p95, samples, \.encoderEncodeMsP95, .orange)
            sparkCard("RTT", "ms", rtt, samples, \.rttMs, .blue)
        }
    }

    @ViewBuilder
    private func decoderRow(_ samples: [MetricsSample]) -> some View {
        let dfps  = MetricsBuffer.computeStats(samples.map(\.decoderFps))
        let dp95  = MetricsBuffer.computeStats(samples.map(\.decoderDecodeMsP95))
        let loss  = MetricsBuffer.computeStats(samples.map(\.packetLossPct))
        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 16) {
            sparkCard("Decoder fps", "fps", dfps, samples, \.decoderFps, .teal)
            sparkCard("Decode p95", "ms", dp95, samples, \.decoderDecodeMsP95, .purple)
            sparkCard("Packet loss", "%", loss, samples, \.packetLossPct, .red)
        }
    }

    // MARK: Active session card

    @ViewBuilder
    private func activeSessionCard(_ info: SessionInfo) -> some View {
        GroupBox {
            HStack(spacing: 24) {
                fact("Session", String(format: "0x%llx", info.sessionId))
                fact("Codec", info.codec.rawValue.uppercased())
                fact("Bitrate", "\(info.videoBitrateMbps) Mb/s")
                fact("Display id", info.virtualDisplayId == 0 ? "—" : "\(info.virtualDisplayId)")
                fact("Clock offset", String(format: "%+.3f ms", Double(info.clockOffsetNs) / 1_000_000.0))
                Spacer(minLength: 0)
            }
            .padding(8)
        } label: {
            Label("Active session", systemImage: "dot.radiowaves.left.and.right")
        }
    }

    private func fact(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.callout).monospacedDigit()
        }
    }

    // MARK: Sparkline cards

    @ViewBuilder
    private func sparkCard(_ title: String, _ unit: String, _ s: RollingStats,
                           _ samples: [MetricsSample],
                           _ key: KeyPath<MetricsSample, Double>,
                           _ color: Color) -> some View {
        let points = samples.enumerated().map { Point(id: $0.offset, x: Double($0.offset), y: $0.element[keyPath: key]) }
        GroupBox {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text(title).font(.headline)
                    Spacer()
                    Text(String(format: "%.2f \(unit)", samples.last?[keyPath: key] ?? 0))
                        .monospacedDigit().foregroundStyle(.secondary)
                }
                Chart(points) { p in
                    LineMark(x: .value("t", p.x), y: .value("v", p.y))
                        .foregroundStyle(color)
                        .interpolationMethod(.monotone)
                    AreaMark(x: .value("t", p.x), y: .value("v", p.y))
                        .foregroundStyle(color.opacity(0.15))
                        .interpolationMethod(.monotone)
                }
                .chartYAxis(.hidden)
                .chartXAxis(.hidden)
                .frame(height: 80)
                HStack(spacing: 14) {
                    badge("min", String(format: "%.1f", s.min))
                    badge("avg", String(format: "%.1f", s.mean))
                    badge("max", String(format: "%.1f", s.max))
                }.font(.caption).foregroundStyle(.secondary)
            }
            .padding(8)
        }
    }

    private func badge(_ k: String, _ v: String) -> some View {
        HStack(spacing: 3) { Text(k); Text(v).monospacedDigit().foregroundStyle(.primary) }
    }
}
