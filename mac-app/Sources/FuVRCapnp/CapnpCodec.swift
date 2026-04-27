// SPDX-License-Identifier: Apache-2.0
import Foundation

// Encoder/decoder for the Cap'n Proto subset of `proto/fuvrd.capnp` we care
// about. Output layout matches libcapnp's slot-allocation rules for the
// schema as committed.
//
// Each `encodeXyz` writes the raw struct body (data words first, then
// pointer words, then any out-of-line storage referenced by those pointers
// — Text payloads in our subset). The result is a flat array of words ready
// to be embedded as a struct landing in a parent's pointer section.
//
// For top-level encoding (`encodeFramedEnvelope`) we lay out a single-segment
// message: [root pointer][envelope words][child structs / out-of-line data...].

// MARK: - Helpers

private func writeUInt64(_ buf: inout [UInt8], _ off: Int, _ v: UInt64) {
    var le = v.littleEndian
    withUnsafeBytes(of: &le) { src in
        for i in 0..<8 { buf[off + i] = src[i] }
    }
}

private func writeInt64(_ buf: inout [UInt8], _ off: Int, _ v: Int64) {
    writeUInt64(&buf, off, UInt64(bitPattern: v))
}

private func writeUInt32(_ buf: inout [UInt8], _ off: Int, _ v: UInt32) {
    var le = v.littleEndian
    withUnsafeBytes(of: &le) { src in
        for i in 0..<4 { buf[off + i] = src[i] }
    }
}

private func writeUInt16(_ buf: inout [UInt8], _ off: Int, _ v: UInt16) {
    var le = v.littleEndian
    withUnsafeBytes(of: &le) { src in
        for i in 0..<2 { buf[off + i] = src[i] }
    }
}

private func writeFloat32(_ buf: inout [UInt8], _ off: Int, _ v: Float) {
    writeUInt32(&buf, off, v.bitPattern)
}

private func bytesToWords(_ bytes: [UInt8]) -> [UInt64] {
    precondition(bytes.count % 8 == 0)
    var words: [UInt64] = []
    words.reserveCapacity(bytes.count / 8)
    for i in stride(from: 0, to: bytes.count, by: 8) {
        var w: UInt64 = 0
        for j in 0..<8 { w |= UInt64(bytes[i + j]) << (8 * j) }
        words.append(w)
    }
    return words
}

private func readUInt64(_ words: [UInt64], _ off: Int, _ wordIndex: Int) -> UInt64 {
    return words[wordIndex + off / 8]
}

private func readUInt32(_ words: [UInt64], _ wordIndex: Int, byteOffset: Int) -> UInt32 {
    let w = words[wordIndex + byteOffset / 8]
    let shift = (byteOffset % 8) * 8
    return UInt32((w >> shift) & 0xFFFF_FFFF)
}

private func readUInt16(_ words: [UInt64], _ wordIndex: Int, byteOffset: Int) -> UInt16 {
    let w = words[wordIndex + byteOffset / 8]
    let shift = (byteOffset % 8) * 8
    return UInt16((w >> shift) & 0xFFFF)
}

private func readUInt8(_ words: [UInt64], _ wordIndex: Int, byteOffset: Int) -> UInt8 {
    let w = words[wordIndex + byteOffset / 8]
    let shift = (byteOffset % 8) * 8
    return UInt8((w >> shift) & 0xFF)
}

private func readFloat32(_ words: [UInt64], _ wordIndex: Int, byteOffset: Int) -> Float {
    return Float(bitPattern: readUInt32(words, wordIndex, byteOffset: byteOffset))
}

// MARK: - Per-struct encoders
//
// Each function returns `(words, pointerCount)` where `words` is data words
// followed by pointer words followed by any out-of-line referenced bytes
// (e.g. Text). The struct's data-word count and pointer-word count are
// implicit constants per type.

struct EncodedStruct {
    var dataWords: UInt16
    var pointerWords: UInt16
    /// data words + pointer words + trailing data referenced by pointers
    var words: [UInt64]
}

private func encodeStartSessionRequest(_ r: CapnpStartSessionRequest) -> EncodedStruct {
    // 3 data words, 0 pointers.
    var bytes = [UInt8](repeating: 0, count: 24)
    writeUInt32(&bytes, 0, r.perEyeWidth)
    writeUInt32(&bytes, 4, r.perEyeHeight)
    writeUInt32(&bytes, 8, r.refreshRateHz)
    writeUInt16(&bytes, 12, r.videoCodec.rawValue)
    // hole at bytes 14..15 holds bool fields
    var holeByte: UInt8 = 0
    if r.audioEnabled         { holeByte |= 0x01 }
    if r.enableVirtualDisplay { holeByte |= 0x02 }
    bytes[14] = holeByte
    writeUInt32(&bytes, 16, r.videoBitrateBps)
    writeUInt32(&bytes, 20, r.forceIdrEveryFrames)
    return EncodedStruct(dataWords: 3, pointerWords: 0, words: bytesToWords(bytes))
}

private func decodeStartSessionRequest(_ words: [UInt64], at base: Int, dataWords: Int) -> CapnpStartSessionRequest {
    var bytes = [UInt8](repeating: 0, count: dataWords * 8)
    for i in 0..<dataWords {
        let w = words[base + i]
        for j in 0..<8 { bytes[i * 8 + j] = UInt8((w >> (8 * j)) & 0xFF) }
    }
    let read32 = { (off: Int) -> UInt32 in
        bytes.count > off + 3 ? UInt32(bytes[off]) | (UInt32(bytes[off+1]) << 8) | (UInt32(bytes[off+2]) << 16) | (UInt32(bytes[off+3]) << 24) : 0
    }
    let read16 = { (off: Int) -> UInt16 in
        bytes.count > off + 1 ? UInt16(bytes[off]) | (UInt16(bytes[off+1]) << 8) : 0
    }
    let read8 = { (off: Int) -> UInt8 in
        bytes.count > off ? bytes[off] : 0
    }
    let codecRaw = read16(12)
    return CapnpStartSessionRequest(
        perEyeWidth: read32(0),
        perEyeHeight: read32(4),
        refreshRateHz: read32(8),
        videoCodec: CapnpVideoCodec(rawValue: codecRaw) ?? .hevc,
        videoBitrateBps: read32(16),
        forceIdrEveryFrames: read32(20),
        audioEnabled: (read8(14) & 0x01) != 0,
        enableVirtualDisplay: (read8(14) & 0x02) != 0
    )
}

private func encodeStartSessionResponse(_ r: CapnpStartSessionResponse) -> EncodedStruct {
    // 4 data words, 0 pointers.
    var bytes = [UInt8](repeating: 0, count: 32)
    writeUInt64(&bytes, 0, r.sessionId)
    writeInt64(&bytes, 8, r.clockOffsetNs)
    writeUInt64(&bytes, 16, r.oneWayDelayNs)
    writeUInt32(&bytes, 24, r.virtualDisplayId)
    return EncodedStruct(dataWords: 4, pointerWords: 0, words: bytesToWords(bytes))
}

private func decodeStartSessionResponse(_ words: [UInt64], at base: Int, dataWords: Int) -> CapnpStartSessionResponse {
    let pad = max(4, dataWords)
    var ws = [UInt64](repeating: 0, count: pad)
    for i in 0..<min(dataWords, pad) { ws[i] = words[base + i] }
    let session = ws[0]
    let offset = Int64(bitPattern: ws[1])
    let delay = ws[2]
    let vid = UInt32(ws[3] & 0xFFFF_FFFF)
    return CapnpStartSessionResponse(sessionId: session, clockOffsetNs: offset,
                                     oneWayDelayNs: delay, virtualDisplayId: vid)
}

private func encodeStopSessionRequest(_ r: CapnpStopSessionRequest) -> EncodedStruct {
    var bytes = [UInt8](repeating: 0, count: 8)
    writeUInt64(&bytes, 0, r.sessionId)
    return EncodedStruct(dataWords: 1, pointerWords: 0, words: bytesToWords(bytes))
}

private func decodeStopSessionRequest(_ words: [UInt64], at base: Int, dataWords: Int) -> CapnpStopSessionRequest {
    return CapnpStopSessionRequest(sessionId: dataWords >= 1 ? words[base] : 0)
}

private func encodeStreamInputsRequest(_ r: CapnpStreamInputsRequest) -> EncodedStruct {
    var bytes = [UInt8](repeating: 0, count: 8)
    writeUInt64(&bytes, 0, r.sessionId)
    return EncodedStruct(dataWords: 1, pointerWords: 0, words: bytesToWords(bytes))
}

private func decodeStreamInputsRequest(_ words: [UInt64], at base: Int, dataWords: Int) -> CapnpStreamInputsRequest {
    return CapnpStreamInputsRequest(sessionId: dataWords >= 1 ? words[base] : 0)
}

private func encodeMetrics(_ m: CapnpMetrics) -> EncodedStruct {
    // 5 data words, 0 pointers.
    var bytes = [UInt8](repeating: 0, count: 40)
    writeUInt64(&bytes, 0, m.capturedAtNs)
    writeFloat32(&bytes, 8, m.encoderFps)
    writeFloat32(&bytes, 12, m.encoderEncodeMsAvg)
    writeFloat32(&bytes, 16, m.encoderEncodeMsP95)
    writeFloat32(&bytes, 20, m.transportRttMs)
    writeFloat32(&bytes, 24, m.transportLossPct)
    writeFloat32(&bytes, 28, m.decoderFps)
    writeFloat32(&bytes, 32, m.decoderDecodeMsP95)
    writeFloat32(&bytes, 36, m.videoBitrateMbps)
    return EncodedStruct(dataWords: 5, pointerWords: 0, words: bytesToWords(bytes))
}

private func decodeMetrics(_ words: [UInt64], at base: Int, dataWords: Int) -> CapnpMetrics {
    let pad = max(5, dataWords)
    var ws = [UInt64](repeating: 0, count: pad)
    for i in 0..<min(dataWords, pad) { ws[i] = words[base + i] }
    return CapnpMetrics(
        capturedAtNs: ws[0],
        encoderFps: readFloat32(ws, 0, byteOffset: 8),
        encoderEncodeMsAvg: readFloat32(ws, 0, byteOffset: 12),
        encoderEncodeMsP95: readFloat32(ws, 0, byteOffset: 16),
        transportRttMs: readFloat32(ws, 0, byteOffset: 20),
        transportLossPct: readFloat32(ws, 0, byteOffset: 24),
        decoderFps: readFloat32(ws, 0, byteOffset: 28),
        decoderDecodeMsP95: readFloat32(ws, 0, byteOffset: 32),
        videoBitrateMbps: readFloat32(ws, 0, byteOffset: 36)
    )
}

// MARK: - Text helpers
//
// A Text in Cap'n Proto is a list-of-UInt8, NUL-terminated, with the list
// length including the NUL. Encoded as a list pointer (elementSize=2 -> byte).

private func encodeTextWords(_ s: String) -> (pointerOffsetWords: Int, words: [UInt64], elementCount: UInt32) {
    // We don't yet know the offset; caller fills it. We return the payload
    // words and the list element count.
    var bytes = Array(s.utf8)
    bytes.append(0) // NUL terminator
    let count = bytes.count
    let padded = (count + 7) / 8 * 8
    while bytes.count < padded { bytes.append(0) }
    return (0, bytesToWords(bytes), UInt32(count))
}

private func encodeLogLine(_ l: CapnpLogLine) -> EncodedStruct {
    // 2 data words, 2 pointer words.
    // data: [0..7]=timestampNs, [8]=level, [9..15]=padding
    var dataBytes = [UInt8](repeating: 0, count: 16)
    writeUInt64(&dataBytes, 0, l.timestampNs)
    dataBytes[8] = l.level.rawValue
    var data = bytesToWords(dataBytes)

    // Build out-of-line text payloads.
    var pointers: [UInt64] = [0, 0]
    var trailing: [UInt64] = []

    // module pointer (slot 0). offset is relative to the word *after* the pointer.
    // Layout: [data 2w][ptr0][ptr1][module text words][message text words]
    // For pointer at index 0 (third position from struct start), the "next word"
    // is index 1 of the pointer section. Offset to start of trailing storage
    // in words from the next word after pointer 0: 1 (pointer 1) + 0 = 1.
    // Wait — Cap'n Proto pointer offsets are measured from the word *after*
    // the pointer to the target word. For ptr0 located at struct word 2
    // (data 0,1; pointer 0 at index 2; pointer 1 at index 3), the "next"
    // word is index 3. Trailing data starts at index 4. Offset = 4 - 3 = 1.
    let (_, modBytes, modCount) = encodeTextWords(l.module)
    let (_, msgBytes, msgCount) = encodeTextWords(l.message)

    let ptr0Index = 2  // pointer slot 0 lives at struct word index 2
    let ptr1Index = 3
    let modStartIndex = 4
    let msgStartIndex = modStartIndex + modBytes.count

    let off0 = Int32(modStartIndex - (ptr0Index + 1))
    let off1 = Int32(msgStartIndex - (ptr1Index + 1))
    pointers[0] = CapnpPointer.makeList(offsetWords: off0, elementSize: 2, elementCount: modCount)
    pointers[1] = CapnpPointer.makeList(offsetWords: off1, elementSize: 2, elementCount: msgCount)

    trailing.append(contentsOf: modBytes)
    trailing.append(contentsOf: msgBytes)

    data.append(contentsOf: pointers)
    data.append(contentsOf: trailing)
    return EncodedStruct(dataWords: 2, pointerWords: 2, words: data)
}

private func decodeLogLine(_ words: [UInt64], at base: Int, dataWords: Int, pointerWords: Int) throws -> CapnpLogLine {
    let timestamp = dataWords >= 1 ? words[base] : 0
    let level = dataWords >= 2 ? UInt8(words[base + 1] & 0xFF) : 0
    let ptrBase = base + dataWords
    func readText(_ ptrIndex: Int) throws -> String {
        guard ptrIndex < pointerWords else { return "" }
        let ptr = words[ptrBase + ptrIndex]
        if ptr == 0 { return "" }
        let kind = ptr & 0x3
        guard kind == 1 else { throw CapnpError.malformed("text not list") }
        let lo = UInt32(ptr & 0xFFFF_FFFF)
        let offRaw = lo >> 2
        let offset: Int32 = {
            // sign-extend 30 bits
            let m: UInt32 = 0x2000_0000
            if (offRaw & m) != 0 {
                return Int32(bitPattern: offRaw | 0xC000_0000)
            }
            return Int32(offRaw)
        }()
        let hi = UInt32(ptr >> 32)
        let elementSize = hi & 0x7
        let count = hi >> 3
        guard elementSize == 2 else { throw CapnpError.malformed("text element size") }
        let target = ptrBase + ptrIndex + 1 + Int(offset)
        let n = Int(count)
        guard n > 0 else { return "" }
        var bytes = [UInt8]()
        bytes.reserveCapacity(n - 1)
        for i in 0..<(n - 1) {
            let w = words[target + i / 8]
            let shift = (i % 8) * 8
            bytes.append(UInt8((w >> shift) & 0xFF))
        }
        return String(decoding: bytes, as: UTF8.self)
    }
    let module = try readText(0)
    let message = try readText(1)
    return CapnpLogLine(
        timestampNs: timestamp,
        level: CapnpLogLine.Level(rawValue: level) ?? .info,
        module: module,
        message: message
    )
}

// MARK: - Envelope codec

public enum CapnpCodec {
    /// Envelope layout:
    ///   data: 3 words (24 bytes)
    ///     bytes 0..7  : seq UInt64
    ///     bytes 8..15 : streamId UInt64
    ///     bytes 16..17: union discriminant UInt16
    ///     bytes 18..23: padding
    ///   pointers: 12 slots
    ///     slot 0  : startSession
    ///     slot 1  : stopSession
    ///     slot 2  : submitFrame      (unused here)
    ///     slot 3  : streamPoses      (unused here)
    ///     slot 4  : startSessionAck
    ///     slot 5  : encodeStats      (unused here)
    ///     slot 6  : poseSnapshot     (unused here)
    ///     slot 7  : metrics
    ///     slot 8  : log
    ///     slot 9  : error (Text)
    ///     slot 10 : streamInputs
    ///     slot 11 : inputSnapshot    (unused here)
    public static let envelopeDataWords: UInt16 = 3
    public static let envelopePointerWords: UInt16 = 12

    public static func encode(_ env: CapnpFramedEnvelope) -> Data {
        // Build envelope body.
        var dataBytes = [UInt8](repeating: 0, count: 24)
        writeUInt64(&dataBytes, 0, env.seq)
        writeUInt64(&dataBytes, 8, env.streamId)
        writeUInt16(&dataBytes, 16, env.body.which)
        var envWords = bytesToWords(dataBytes)
        var pointers = [UInt64](repeating: 0, count: 12)
        var trailing: [UInt64] = []

        // Helper to attach a struct in pointer slot `slot` if non-Void/Text.
        func attachStruct(_ slot: Int, _ enc: EncodedStruct) {
            // The "next word" after this pointer slot is at envelope-word index
            //   3 (data) + slot + 1
            // Trailing storage we've appended so far starts at:
            //   3 (data) + 12 (pointers) + trailing.count
            let nextWord = 3 + slot + 1
            let target = 3 + 12 + trailing.count
            let offset = Int32(target - nextWord)
            pointers[slot] = CapnpPointer.makeStruct(
                offsetWords: offset,
                dataWords: enc.dataWords,
                pointerWords: enc.pointerWords
            )
            trailing.append(contentsOf: enc.words)
        }

        func attachText(_ slot: Int, _ s: String) {
            let nextWord = 3 + slot + 1
            let target = 3 + 12 + trailing.count
            let offset = Int32(target - nextWord)
            let (_, textWords, count) = encodeTextWords(s)
            pointers[slot] = CapnpPointer.makeList(offsetWords: offset, elementSize: 2, elementCount: count)
            trailing.append(contentsOf: textWords)
        }

        switch env.body {
        case .startSession(let r):    attachStruct(0,  encodeStartSessionRequest(r))
        case .stopSession(let r):     attachStruct(1,  encodeStopSessionRequest(r))
        case .startSessionAck(let r): attachStruct(4,  encodeStartSessionResponse(r))
        case .metrics(let m):         attachStruct(7,  encodeMetrics(m))
        case .log(let l):             attachStruct(8,  encodeLogLine(l))
        case .error(let s):           attachText(9,    s)
        case .streamInputs(let r):    attachStruct(10, encodeStreamInputsRequest(r))
        case .streamMetrics, .streamLogs, .ping, .pong, .ok:
            break
        }

        envWords.append(contentsOf: pointers)
        envWords.append(contentsOf: trailing)

        // Prefix root pointer (struct, offset 0, dataWords=3, pointerWords=12).
        let root = CapnpPointer.makeStruct(offsetWords: 0,
                                            dataWords: envelopeDataWords,
                                            pointerWords: envelopePointerWords)
        var allWords = [root]
        allWords.append(contentsOf: envWords)

        // Single-segment framing.
        let segment = CapnpFraming.encodeSegments([allWords])
        // Pack and length-prefix.
        let packed = CapnpPacked.pack(segment)
        return CapnpFrame.encode(packed)
    }

    public static func decode(_ frame: Data) throws -> CapnpFramedEnvelope {
        let unpacked = try CapnpPacked.unpack(frame)
        let segments = try CapnpFraming.decodeSegments(unpacked)
        guard let words = segments.first else {
            throw CapnpError.malformed("no segments")
        }
        guard words.count >= 4 else { throw CapnpError.truncated }
        // Root pointer at index 0.
        let root = words[0]
        guard (root & 0x3) == 0 else {
            throw CapnpError.malformed("root pointer not struct")
        }
        let lo = UInt32(root & 0xFFFF_FFFF)
        let offRaw = lo >> 2
        let offset: Int32 = {
            let m: UInt32 = 0x2000_0000
            if (offRaw & m) != 0 { return Int32(bitPattern: offRaw | 0xC000_0000) }
            return Int32(offRaw)
        }()
        let hi = UInt32(root >> 32)
        let dataWords = Int(hi & 0xFFFF)
        let pointerWords = Int((hi >> 16) & 0xFFFF)
        // root pointer at word 0; its "next word" is word 1; target is 1 + offset
        let envBase = 1 + Int(offset)

        guard envBase + dataWords + pointerWords <= words.count else {
            throw CapnpError.truncated
        }

        // Parse envelope data section.
        let seq = dataWords >= 1 ? words[envBase] : 0
        let streamId = dataWords >= 2 ? words[envBase + 1] : 0
        let which: UInt16 = {
            guard dataWords >= 3 else { return 0 }
            let w = words[envBase + 2]
            return UInt16(w & 0xFFFF)
        }()
        let ptrBase = envBase + dataWords

        func readStructPointer(_ slot: Int) throws -> (base: Int, dataWords: Int, pointerWords: Int)? {
            guard slot < pointerWords else { return nil }
            let p = words[ptrBase + slot]
            if p == 0 { return nil }
            guard (p & 0x3) == 0 else { throw CapnpError.malformed("expected struct pointer") }
            let plo = UInt32(p & 0xFFFF_FFFF)
            let oRaw = plo >> 2
            let off: Int32 = {
                let m: UInt32 = 0x2000_0000
                if (oRaw & m) != 0 { return Int32(bitPattern: oRaw | 0xC000_0000) }
                return Int32(oRaw)
            }()
            let phi = UInt32(p >> 32)
            let dw = Int(phi & 0xFFFF)
            let pw = Int((phi >> 16) & 0xFFFF)
            let target = ptrBase + slot + 1 + Int(off)
            return (target, dw, pw)
        }

        func readTextPointer(_ slot: Int) throws -> String {
            guard slot < pointerWords else { return "" }
            let p = words[ptrBase + slot]
            if p == 0 { return "" }
            guard (p & 0x3) == 1 else { throw CapnpError.malformed("expected list pointer") }
            let plo = UInt32(p & 0xFFFF_FFFF)
            let oRaw = plo >> 2
            let off: Int32 = {
                let m: UInt32 = 0x2000_0000
                if (oRaw & m) != 0 { return Int32(bitPattern: oRaw | 0xC000_0000) }
                return Int32(oRaw)
            }()
            let phi = UInt32(p >> 32)
            let elementSize = phi & 0x7
            let count = Int(phi >> 3)
            guard elementSize == 2 else { throw CapnpError.malformed("text element size") }
            let target = ptrBase + slot + 1 + Int(off)
            guard count > 0 else { return "" }
            var bytes = [UInt8]()
            bytes.reserveCapacity(count - 1)
            for i in 0..<(count - 1) {
                let w = words[target + i / 8]
                let shift = (i % 8) * 8
                bytes.append(UInt8((w >> shift) & 0xFF))
            }
            return String(decoding: bytes, as: UTF8.self)
        }

        let body: CapnpEnvelope
        switch which {
        case 0: // startSession
            guard let (b, dw, _) = try readStructPointer(0) else {
                throw CapnpError.malformed("startSession: nil pointer")
            }
            body = .startSession(decodeStartSessionRequest(words, at: b, dataWords: dw))
        case 1: // stopSession
            guard let (b, dw, _) = try readStructPointer(1) else {
                throw CapnpError.malformed("stopSession: nil pointer")
            }
            body = .stopSession(decodeStopSessionRequest(words, at: b, dataWords: dw))
        case 4: body = .streamMetrics
        case 5: body = .streamLogs
        case 6: body = .ping
        case 7: // startSessionAck
            guard let (b, dw, _) = try readStructPointer(4) else {
                throw CapnpError.malformed("startSessionAck: nil pointer")
            }
            body = .startSessionAck(decodeStartSessionResponse(words, at: b, dataWords: dw))
        case 10: // metrics
            guard let (b, dw, _) = try readStructPointer(7) else {
                throw CapnpError.malformed("metrics: nil pointer")
            }
            body = .metrics(decodeMetrics(words, at: b, dataWords: dw))
        case 11: // log
            guard let (b, dw, pw) = try readStructPointer(8) else {
                throw CapnpError.malformed("log: nil pointer")
            }
            body = .log(try decodeLogLine(words, at: b, dataWords: dw, pointerWords: pw))
        case 12: body = .pong
        case 13: body = .ok
        case 14: // error
            body = .error(try readTextPointer(9))
        case 15: // streamInputs
            guard let (b, dw, _) = try readStructPointer(10) else {
                throw CapnpError.malformed("streamInputs: nil pointer")
            }
            body = .streamInputs(decodeStreamInputsRequest(words, at: b, dataWords: dw))
        default:
            throw CapnpError.unsupported("unknown discriminant \(which)")
        }
        return CapnpFramedEnvelope(seq: seq, streamId: streamId, body: body)
    }
}
