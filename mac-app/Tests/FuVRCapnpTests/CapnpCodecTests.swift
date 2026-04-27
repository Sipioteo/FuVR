// SPDX-License-Identifier: Apache-2.0
import XCTest
@testable import FuVRCapnp

final class CapnpCodecTests: XCTestCase {

    private func roundtrip(_ env: CapnpFramedEnvelope) throws -> CapnpFramedEnvelope {
        let frame = CapnpCodec.encode(env)
        // Strip 4-byte length prefix.
        let len = Int(frame.prefix(4).withUnsafeBytes { $0.load(as: UInt32.self) }.littleEndian)
        XCTAssertEqual(frame.count, 4 + len)
        let packed = frame.subdata(in: 4..<frame.count)
        // Decode end-to-end.
        return try CapnpCodec.decode(packed)
    }

    func testStartSessionRoundtrip() throws {
        let r = CapnpStartSessionRequest(
            perEyeWidth: 2064, perEyeHeight: 2208, refreshRateHz: 90,
            videoCodec: .hevc, videoBitrateBps: 150_000_000,
            forceIdrEveryFrames: 240, audioEnabled: true,
            enableVirtualDisplay: false
        )
        let env = CapnpFramedEnvelope(seq: 7, streamId: 0, body: .startSession(r))
        let out = try roundtrip(env)
        XCTAssertEqual(out.seq, 7)
        guard case .startSession(let got) = out.body else { return XCTFail() }
        XCTAssertEqual(got, r)
    }

    func testStopSessionRoundtrip() throws {
        let env = CapnpFramedEnvelope(seq: 12, body: .stopSession(.init(sessionId: 0xDEAD_BEEF)))
        let out = try roundtrip(env)
        guard case .stopSession(let got) = out.body else { return XCTFail() }
        XCTAssertEqual(got.sessionId, 0xDEAD_BEEF)
    }

    func testStartSessionAckRoundtrip() throws {
        let r = CapnpStartSessionResponse(sessionId: 42, clockOffsetNs: -1_000_000,
                                          oneWayDelayNs: 5_000_000, virtualDisplayId: 7)
        let env = CapnpFramedEnvelope(seq: 1, body: .startSessionAck(r))
        let out = try roundtrip(env)
        guard case .startSessionAck(let got) = out.body else { return XCTFail() }
        XCTAssertEqual(got, r)
    }

    func testMetricsRoundtrip() throws {
        let m = CapnpMetrics(capturedAtNs: 1_700_000_000_000_000_000,
                             encoderFps: 89.5, encoderEncodeMsAvg: 4.2,
                             encoderEncodeMsP95: 9.0, transportRttMs: 8.3,
                             transportLossPct: 0.03, decoderFps: 89.0,
                             decoderDecodeMsP95: 6.5, videoBitrateMbps: 142.0)
        let env = CapnpFramedEnvelope(seq: 99, body: .metrics(m))
        let out = try roundtrip(env)
        guard case .metrics(let got) = out.body else { return XCTFail() }
        XCTAssertEqual(got, m)
    }

    func testLogRoundtrip() throws {
        let l = CapnpLogLine(timestampNs: 12345, level: .warn,
                             module: "encoder", message: "frame drop")
        let env = CapnpFramedEnvelope(seq: 4, body: .log(l))
        let out = try roundtrip(env)
        guard case .log(let got) = out.body else { return XCTFail() }
        XCTAssertEqual(got, l)
    }

    func testErrorTextRoundtrip() throws {
        let env = CapnpFramedEnvelope(seq: 4, body: .error("boom: something failed"))
        let out = try roundtrip(env)
        guard case .error(let got) = out.body else { return XCTFail() }
        XCTAssertEqual(got, "boom: something failed")
    }

    func testEmptyArmsRoundtrip() throws {
        for body in [CapnpEnvelope.streamMetrics, .streamLogs, .ping, .pong, .ok] {
            let env = CapnpFramedEnvelope(seq: 1, body: body)
            let out = try roundtrip(env)
            XCTAssertEqual(out.body, body)
        }
    }

    func testStreamInputsRoundtrip() throws {
        let env = CapnpFramedEnvelope(seq: 2, body: .streamInputs(.init(sessionId: 9)))
        let out = try roundtrip(env)
        guard case .streamInputs(let got) = out.body else { return XCTFail() }
        XCTAssertEqual(got.sessionId, 9)
    }

    func testFrameLengthPrefix() throws {
        let env = CapnpFramedEnvelope(seq: 0, body: .ping)
        let frame = CapnpCodec.encode(env)
        XCTAssertGreaterThan(frame.count, 4)
        let len = Int(frame.prefix(4).withUnsafeBytes { $0.load(as: UInt32.self) }.littleEndian)
        XCTAssertEqual(frame.count - 4, len)
    }

    /// Sentinel: hand-computed reference bytes for a `ping` envelope with
    /// seq=1, streamId=0. Locks the wire layout; if libcapnp's slot
    /// allocator output diverges from this, the test fails loudly.
    func testPingReferenceBytes() throws {
        let env = CapnpFramedEnvelope(seq: 1, body: .ping)
        let frame = CapnpCodec.encode(env)
        let packed = frame.subdata(in: 4..<frame.count)
        let segment = try CapnpPacked.unpack(packed)

        // Expected unpacked layout:
        //   word 0: segment table = [segCount-1=0, segWords=16]
        //     -> 0x00000010_00000000 little-endian
        //   word 1: root pointer (struct, off=0, dw=3, pw=12)
        //     -> 0x000C0003_00000000
        //   word 2: seq=1
        //   word 3: streamId=0
        //   word 4: discriminant=6 in first 16 bits, rest 0
        //     -> 0x0000000000000006
        //   words 5..16: 12 zero pointer slots
        //
        // Total: 1 (header) + 16 (1 root + 3 data + 12 ptrs) = 17 words = 136 bytes.
        XCTAssertEqual(segment.count, 17 * 8, "unpacked size mismatch")

        // Read words from segment.
        func word(_ i: Int) -> UInt64 {
            let off = i * 8
            return segment.subdata(in: off..<(off + 8))
                .withUnsafeBytes { $0.load(as: UInt64.self) }
                .littleEndian
        }

        // Header: segCount-1 = 0 (lo32), segWords = 16 (hi32)
        let header = word(0)
        XCTAssertEqual(UInt32(header & 0xFFFF_FFFF), 0)
        XCTAssertEqual(UInt32(header >> 32), 16)

        // Root pointer
        let root = word(1)
        XCTAssertEqual(root & 0x3, 0, "root must be struct pointer")
        XCTAssertEqual(UInt32(root >> 32) & 0xFFFF, 3, "envelope dataWords")
        XCTAssertEqual((UInt32(root >> 32) >> 16) & 0xFFFF, 12, "envelope pointerWords")

        // Envelope words.
        XCTAssertEqual(word(2), 1, "seq")
        XCTAssertEqual(word(3), 0, "streamId")
        XCTAssertEqual(word(4) & 0xFFFF, 6, "ping discriminant")
        for i in 5..<17 {
            XCTAssertEqual(word(i), 0, "ptr slot \(i - 5) must be zero for ping")
        }
    }

    func testPackedRoundtrip() throws {
        let original: [UInt8] = [
            0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00,
            0x19, 0x00, 0x00, 0x00, 0x82, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        ]
        let data = Data(original)
        let packed = CapnpPacked.pack(data)
        let unpacked = try CapnpPacked.unpack(packed)
        XCTAssertEqual(Data(original), unpacked)
    }
}
