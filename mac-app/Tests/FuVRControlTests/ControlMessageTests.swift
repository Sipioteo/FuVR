// SPDX-License-Identifier: Apache-2.0
import XCTest
@testable import FuVRControl

final class ControlMessageTests: XCTestCase {
    private func roundtrip(_ p: ControlPayload) throws -> ControlPayload? {
        let data = try ControlCodec.encode(p)
        XCTAssertEqual(data.last, 0x0A, "envelope must be newline-delimited")
        return try ControlCodec.decode(data)
    }

    func testHelloFromMacRoundtrip() throws {
        let cfg = SessionConfig(perEyeWidth: 2064, perEyeHeight: 2208,
                                refreshRateHz: 90, videoCodec: .hevc,
                                videoBitrateBps: 150_000_000, audioEnabled: true)
        guard case .helloFromMac(let out) = try roundtrip(.helloFromMac(cfg)) else {
            return XCTFail("decoded payload type mismatch")
        }
        XCTAssertEqual(out, cfg)
    }

    func testMetricsRoundtrip() throws {
        let m = MetricsSample(timestampMs: 12345, rttMs: 8.3, jitterMs: 0.7,
                              packetLossPct: 0.05, encodeMs: 4.2, decodeMs: 3.1,
                              fps: 90, bitrateMbps: 142.0)
        guard case .metrics(let out) = try roundtrip(.metrics(m)) else {
            return XCTFail("decoded payload type mismatch")
        }
        XCTAssertEqual(out, m)
    }

    func testSessionStartRoundtrip() throws {
        guard case .sessionStart = try roundtrip(.sessionStart) else {
            return XCTFail("expected sessionStart")
        }
    }

    func testErrorRoundtrip() throws {
        guard case .error(let s) = try roundtrip(.error("nope")) else {
            return XCTFail("expected error")
        }
        XCTAssertEqual(s, "nope")
    }

    func testLogRoundtrip() throws {
        let l = LogLine(timestampMs: 1, level: .warn, source: "encoder", message: "frame drop")
        guard case .log(let out) = try roundtrip(.log(l)) else {
            return XCTFail("expected log")
        }
        XCTAssertEqual(out, l)
    }

    func testEnvelopeVersionField() throws {
        let data = try ControlCodec.encode(.sessionStop)
        let str = String(data: data, encoding: .utf8) ?? ""
        XCTAssertTrue(str.contains("\"v\":1"), "envelope must carry version: \(str)")
        XCTAssertTrue(str.contains("\"type\":\"sessionStop\""))
    }
}
