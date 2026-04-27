// SPDX-License-Identifier: Apache-2.0
import XCTest
@testable import FuVRControl

final class MetricsTests: XCTestCase {
    func testEmptyStats() {
        XCTAssertEqual(MetricsBuffer.computeStats([]), .empty)
    }

    func testStatsBasic() {
        let s = MetricsBuffer.computeStats([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
        XCTAssertEqual(s.count, 10)
        XCTAssertEqual(s.min, 1)
        XCTAssertEqual(s.max, 10)
        XCTAssertEqual(s.mean, 5.5, accuracy: 0.0001)
        XCTAssertEqual(s.p95, 10, accuracy: 0.0001)
    }

    func testRollingCapacity() {
        let buf = MetricsBuffer(capacity: 5)
        for i in 0..<10 {
            buf.append(MetricsSample(timestampMs: UInt64(i), rttMs: Double(i),
                                     jitterMs: 0, packetLossPct: 0,
                                     encodeMs: 0, decodeMs: 0, fps: 0, bitrateMbps: 0))
        }
        XCTAssertEqual(buf.samples.count, 5)
        XCTAssertEqual(buf.samples.first?.rttMs, 5)
        XCTAssertEqual(buf.samples.last?.rttMs, 9)
    }

    func testStatsKeyPath() {
        let buf = MetricsBuffer(capacity: 100)
        for i in 1...4 {
            buf.append(MetricsSample(timestampMs: 0, rttMs: Double(i * 10),
                                     jitterMs: 0, packetLossPct: 0,
                                     encodeMs: 0, decodeMs: 0, fps: 0, bitrateMbps: 0))
        }
        let s = buf.stats(\.rttMs)
        XCTAssertEqual(s.min, 10)
        XCTAssertEqual(s.max, 40)
        XCTAssertEqual(s.mean, 25, accuracy: 0.0001)
    }
}
