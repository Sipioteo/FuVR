// SPDX-License-Identifier: Apache-2.0
import Foundation

public struct RollingStats: Sendable, Equatable {
    public let count: Int
    public let mean: Double
    public let min: Double
    public let max: Double
    public let p95: Double

    public static let empty = RollingStats(count: 0, mean: 0, min: 0, max: 0, p95: 0)
}

public final class MetricsBuffer {
    public let capacity: Int
    private(set) public var samples: [MetricsSample] = []

    public init(capacity: Int = 600) {
        self.capacity = capacity
        self.samples.reserveCapacity(capacity)
    }

    public func append(_ s: MetricsSample) {
        samples.append(s)
        if samples.count > capacity {
            samples.removeFirst(samples.count - capacity)
        }
    }

    public func clear() { samples.removeAll(keepingCapacity: true) }

    public func stats(_ key: KeyPath<MetricsSample, Double>) -> RollingStats {
        let values = samples.map { $0[keyPath: key] }
        return Self.computeStats(values)
    }

    public static func computeStats(_ values: [Double]) -> RollingStats {
        guard !values.isEmpty else { return .empty }
        let sorted = values.sorted()
        let sum = values.reduce(0, +)
        let mean = sum / Double(values.count)
        let p95Index = max(0, min(sorted.count - 1, Int((Double(sorted.count) * 0.95).rounded(.down))))
        return RollingStats(
            count: values.count,
            mean: mean,
            min: sorted.first ?? 0,
            max: sorted.last ?? 0,
            p95: sorted[p95Index]
        )
    }
}
