// SPDX-License-Identifier: Apache-2.0
import XCTest
@testable import FuVRControl
import FuVRCapnp

/// End-to-end check: encode an outgoing payload via ControlBridge, run it
/// through the Cap'n Proto codec, and decode it back as the daemon would.
/// This exercises the full wire pipeline (ControlBridge → CapnpCodec → frame
/// → unpack → CapnpCodec.decode → ControlBridge) without depending on a
/// runloop or NWListener-backed Unix socket.
final class MockDaemonRoundtripTests: XCTestCase {

    func testStartSessionPipelineRoundtrip() throws {
        let cfg = SessionConfig(perEyeWidth: 2064, perEyeHeight: 2208,
                                refreshRateHz: 90, videoCodec: .hevc,
                                videoBitrateBps: 150_000_000, audioEnabled: true)
        guard let outgoing = ControlBridge.encodeOutgoing(.helloFromMac(cfg), seq: 1) else {
            return XCTFail("encodeOutgoing returned nil")
        }
        let frame = CapnpCodec.encode(outgoing)
        // Strip 4-byte length prefix.
        let len = Int(frame.prefix(4).withUnsafeBytes { $0.load(as: UInt32.self) }.littleEndian)
        XCTAssertEqual(frame.count, 4 + len)
        let packed = frame.subdata(in: 4..<frame.count)

        // Daemon-side decode.
        let env = try CapnpCodec.decode(packed)
        guard case .startSession(let r) = env.body else {
            return XCTFail("expected startSession arm, got \(env.body)")
        }
        XCTAssertEqual(r.perEyeWidth, 2064)
        XCTAssertEqual(r.perEyeHeight, 2208)
        XCTAssertEqual(r.refreshRateHz, 90)
        XCTAssertEqual(r.videoCodec, .hevc)
        XCTAssertEqual(r.videoBitrateBps, 150_000_000)
        XCTAssertTrue(r.audioEnabled)
    }

    func testMetricsPipelineRoundtrip() throws {
        let m = CapnpMetrics(
            capturedAtNs: 1_700_000_000_000_000_000,
            encoderFps: 89.7, encoderEncodeMsAvg: 4.4, encoderEncodeMsP95: 8.2,
            transportRttMs: 7.9, transportLossPct: 0.04,
            decoderFps: 89.4, decoderDecodeMsP95: 6.0,
            videoBitrateMbps: 142.0
        )
        let env = CapnpFramedEnvelope(seq: 5, body: .metrics(m))
        let frame = CapnpCodec.encode(env)
        let packed = frame.subdata(in: 4..<frame.count)
        let decoded = try CapnpCodec.decode(packed)

        let cfg = SessionConfig()
        guard let payload = ControlBridge.decodeIncoming(decoded, sessionConfig: cfg) else {
            return XCTFail("bridge returned nil for metrics")
        }
        guard case .metrics(let s) = payload else {
            return XCTFail("expected metrics arm, got \(payload)")
        }
        XCTAssertEqual(s.encoderFps, 89.7, accuracy: 0.01)
        XCTAssertEqual(s.encoderEncodeMsP95, 8.2, accuracy: 0.01)
        XCTAssertEqual(s.decoderDecodeMsP95, 6.0, accuracy: 0.01)
        XCTAssertEqual(s.bitrateMbps, 142.0, accuracy: 0.01)
        XCTAssertEqual(s.rttMs, 7.9, accuracy: 0.01)
    }

    func testStartSessionAckBridgeBridgesToSessionStarted() throws {
        let ack = CapnpStartSessionResponse(sessionId: 0xCAFE, clockOffsetNs: -123_456,
                                            oneWayDelayNs: 5_000_000, virtualDisplayId: 7)
        let env = CapnpFramedEnvelope(seq: 2, body: .startSessionAck(ack))
        let cfg = SessionConfig(videoCodec: .h264, videoBitrateBps: 100_000_000)
        guard let payload = ControlBridge.decodeIncoming(env, sessionConfig: cfg) else {
            return XCTFail("nil payload")
        }
        guard case .sessionStarted(let info) = payload else {
            return XCTFail("expected sessionStarted, got \(payload)")
        }
        XCTAssertEqual(info.sessionId, 0xCAFE)
        XCTAssertEqual(info.clockOffsetNs, -123_456)
        XCTAssertEqual(info.oneWayDelayNs, 5_000_000)
        XCTAssertEqual(info.virtualDisplayId, 7)
        XCTAssertEqual(info.codec, .h264)
        XCTAssertEqual(info.videoBitrateMbps, 100)
    }

    func testLogPipelineRoundtrip() throws {
        let l = CapnpLogLine(timestampNs: 1_700_000_000_123_000_000, level: .warn,
                             module: "encoder", message: "queue full")
        let env = CapnpFramedEnvelope(seq: 3, body: .log(l))
        let frame = CapnpCodec.encode(env)
        let decoded = try CapnpCodec.decode(frame.subdata(in: 4..<frame.count))
        guard let payload = ControlBridge.decodeIncoming(decoded, sessionConfig: nil) else {
            return XCTFail("nil")
        }
        guard case .log(let line) = payload else { return XCTFail("expected log") }
        XCTAssertEqual(line.level, .warn)
        XCTAssertEqual(line.source, "encoder")
        XCTAssertEqual(line.message, "queue full")
    }
}
