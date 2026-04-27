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

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                Text("Diagnostics").font(.largeTitle).bold()

                let samples = state.metrics.samples
                if samples.isEmpty {
                    ContentUnavailableView("No live data",
                                           systemImage: "waveform",
                                           description: Text("Start a session to see live RTT, jitter, encode/decode timings."))
                        .frame(minHeight: 360)
                } else {
                    let rtt = state.metrics.stats(\.rttMs)
                    let jitter = state.metrics.stats(\.jitterMs)
                    let enc = state.metrics.stats(\.encodeMs)
                    let dec = state.metrics.stats(\.decodeMs)

                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 16) {
                        statCard("RTT", "ms", rtt, samples, \.rttMs, .blue)
                        statCard("Jitter", "ms", jitter, samples, \.jitterMs, .purple)
                        statCard("Encode", "ms", enc, samples, \.encodeMs, .orange)
                        statCard("Decode", "ms", dec, samples, \.decodeMs, .teal)
                    }

                    GroupBox("Loss & throughput") {
                        HStack(spacing: 24) {
                            metricCell("Packet loss",
                                       String(format: "%.2f%%", state.latestMetrics?.packetLossPct ?? 0))
                            metricCell("Bitrate",
                                       String(format: "%.0f Mb/s", state.latestMetrics?.bitrateMbps ?? 0))
                            metricCell("FPS",
                                       String(format: "%.0f", state.latestMetrics?.fps ?? 0))
                            Spacer()
                        }.padding(8)
                    }
                }
                Spacer(minLength: 0)
            }
            .padding(28)
        }
    }

    @ViewBuilder
    private func statCard(_ title: String, _ unit: String, _ s: RollingStats,
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
                    legend("min", String(format: "%.1f", s.min))
                    legend("mean", String(format: "%.1f", s.mean))
                    legend("p95", String(format: "%.1f", s.p95))
                    legend("max", String(format: "%.1f", s.max))
                }.font(.caption).foregroundStyle(.secondary)
            }
            .padding(8)
        }
    }

    private func legend(_ k: String, _ v: String) -> some View {
        HStack(spacing: 3) { Text(k); Text(v).monospacedDigit().foregroundStyle(.primary) }
    }

    private func metricCell(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title3).monospacedDigit()
        }
    }
}
