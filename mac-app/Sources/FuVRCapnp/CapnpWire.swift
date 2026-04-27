// SPDX-License-Identifier: Apache-2.0
import Foundation

// Minimal hand-rolled Cap'n Proto encoder/decoder for the subset of
// `proto/fuvrd.capnp` arms exchanged between the mac-app and the daemon.
//
// Wire format references:
//   https://capnproto.org/encoding.html
//
// We implement a single-segment Cap'n Proto message and the "packed" framing
// described at https://capnproto.org/encoding.html#packing. Length-prefixed
// packed messages are then framed for the UDS RPC socket.
//
// The encoder lays out messages in a fixed layout so that wire bytes are
// deterministic; that lets us unit-test hand-computed reference bytes for
// sentinel messages.

public enum CapnpError: Error, Equatable {
    case truncated
    case malformed(String)
    case unsupported(String)
}

// MARK: - Word-level buffer

public struct CapnpBuffer: Equatable {
    public private(set) var words: [UInt64]
    public init() { self.words = [] }
    public init(words: [UInt64]) { self.words = words }

    public mutating func append(_ word: UInt64) { words.append(word) }
    public mutating func append(zeros count: Int) {
        for _ in 0..<count { words.append(0) }
    }

    public var byteCount: Int { words.count * 8 }
    public var wordCount: Int { words.count }

    public func bytes() -> Data {
        var d = Data(capacity: words.count * 8)
        for w in words {
            var le = w.littleEndian
            withUnsafeBytes(of: &le) { d.append(contentsOf: $0) }
        }
        return d
    }
}

// MARK: - Pointer encoding

public enum CapnpPointer {
    // Struct pointer: B=0, offset (30b signed words), dataSize (16b words),
    // pointerSize (16b words). Offset is from the word *immediately after*
    // the pointer to the first word of the struct.
    public static func makeStruct(offsetWords: Int32, dataWords: UInt16, pointerWords: UInt16) -> UInt64 {
        // 0..1   = type (0 = struct)
        // 2..31  = offset (30 bits signed)
        // 32..47 = data size (16 bits)
        // 48..63 = pointer size (16 bits)
        let masked = UInt32(bitPattern: offsetWords) & 0x3FFF_FFFF
        let lo = UInt32(0) | (masked << 2)
        let hi = UInt32(dataWords) | (UInt32(pointerWords) << 16)
        return UInt64(lo) | (UInt64(hi) << 32)
    }

    // List pointer: B=1, offset (30b signed words), elementSize (3b),
    // elementCount or wordCount (29b).
    public static func makeList(offsetWords: Int32, elementSize: UInt8, elementCount: UInt32) -> UInt64 {
        let masked = UInt32(bitPattern: offsetWords) & 0x3FFF_FFFF
        let lo = UInt32(1) | (masked << 2)
        let hi = UInt32(elementSize & 0x7) | (elementCount << 3)
        return UInt64(lo) | (UInt64(hi) << 32)
    }

    public static let null: UInt64 = 0
}

// MARK: - Segment framing
//
// Cap'n Proto segment table: 4-byte little-endian segment count - 1, then
// 4-byte little-endian segment word counts (one per segment), then padding
// to an 8-byte boundary, then segment data.

public enum CapnpFraming {
    public static func encodeSegments(_ segments: [[UInt64]]) -> Data {
        precondition(!segments.isEmpty)
        var d = Data()
        let count = UInt32(segments.count - 1).littleEndian
        withUnsafeBytes(of: count) { d.append(contentsOf: $0) }
        for seg in segments {
            let n = UInt32(seg.count).littleEndian
            withUnsafeBytes(of: n) { d.append(contentsOf: $0) }
        }
        if (segments.count + 1) % 2 == 1 {
            // pad to 8 bytes
            d.append(contentsOf: [0, 0, 0, 0])
        }
        for seg in segments {
            for w in seg {
                var le = w.littleEndian
                withUnsafeBytes(of: &le) { d.append(contentsOf: $0) }
            }
        }
        return d
    }

    public static func decodeSegments(_ data: Data) throws -> [[UInt64]] {
        guard data.count >= 4 else { throw CapnpError.truncated }
        let segMinus1 = data.withUnsafeBytes { $0.load(as: UInt32.self) }.littleEndian
        let segCount = Int(segMinus1) + 1
        let headerWordsRaw = 1 + segCount   // in 4-byte units
        let headerWords = headerWordsRaw + (headerWordsRaw % 2)  // round up
        let headerBytes = headerWords * 4
        guard data.count >= headerBytes else { throw CapnpError.truncated }
        var sizes: [Int] = []
        for i in 0..<segCount {
            let off = 4 + i * 4
            let s = data.subdata(in: off..<(off + 4)).withUnsafeBytes { $0.load(as: UInt32.self) }.littleEndian
            sizes.append(Int(s))
        }
        var off = headerBytes
        var segs: [[UInt64]] = []
        for sz in sizes {
            let bytes = sz * 8
            guard data.count >= off + bytes else { throw CapnpError.truncated }
            var words: [UInt64] = []
            words.reserveCapacity(sz)
            for w in 0..<sz {
                let p = off + w * 8
                let val = data.subdata(in: p..<(p + 8)).withUnsafeBytes { $0.load(as: UInt64.self) }.littleEndian
                words.append(val)
            }
            segs.append(words)
            off += bytes
        }
        return segs
    }
}

// MARK: - Packed encoding (https://capnproto.org/encoding.html#packing)

public enum CapnpPacked {
    public static func pack(_ data: Data) -> Data {
        precondition(data.count % 8 == 0)
        var out = Data()
        var i = 0
        let n = data.count
        while i < n {
            // collect 8 bytes
            var word = [UInt8](repeating: 0, count: 8)
            for j in 0..<8 { word[j] = data[i + j] }
            // tag: bit set per nonzero byte
            var tag: UInt8 = 0
            for j in 0..<8 where word[j] != 0 { tag |= UInt8(1 << j) }
            out.append(tag)
            for j in 0..<8 where word[j] != 0 { out.append(word[j]) }

            i += 8
            if tag == 0x00 {
                // count following all-zero words (max 255)
                var run: UInt8 = 0
                while i < n && run < 255 {
                    var allZero = true
                    for j in 0..<8 where data[i + j] != 0 { allZero = false; break }
                    if !allZero { break }
                    run += 1
                    i += 8
                }
                out.append(run)
            } else if tag == 0xFF {
                // count following non-trivial words (heuristic: words with <= 1 zero byte)
                var startScan = i
                var run: UInt8 = 0
                while startScan < n && run < 255 {
                    var zeroCount = 0
                    for j in 0..<8 where data[startScan + j] == 0 { zeroCount += 1 }
                    if zeroCount > 1 { break }
                    run += 1
                    startScan += 8
                }
                out.append(run)
                // copy `run` words verbatim
                for _ in 0..<Int(run) {
                    for j in 0..<8 { out.append(data[i + j]) }
                    i += 8
                }
            }
        }
        return out
    }

    public static func unpack(_ packed: Data) throws -> Data {
        var out = Data()
        var i = 0
        let n = packed.count
        while i < n {
            guard i < n else { throw CapnpError.truncated }
            let tag = packed[i]
            i += 1
            var word = [UInt8](repeating: 0, count: 8)
            for j in 0..<8 {
                if (tag & UInt8(1 << j)) != 0 {
                    guard i < n else { throw CapnpError.truncated }
                    word[j] = packed[i]
                    i += 1
                }
            }
            out.append(contentsOf: word)
            if tag == 0x00 {
                guard i < n else { throw CapnpError.truncated }
                let run = Int(packed[i]); i += 1
                for _ in 0..<run {
                    out.append(contentsOf: [0, 0, 0, 0, 0, 0, 0, 0])
                }
            } else if tag == 0xFF {
                guard i < n else { throw CapnpError.truncated }
                let run = Int(packed[i]); i += 1
                for _ in 0..<run {
                    guard i + 8 <= n else { throw CapnpError.truncated }
                    for j in 0..<8 { out.append(packed[i + j]) }
                    i += 8
                }
            }
        }
        return out
    }
}

// MARK: - Frame (length-prefixed packed message) for the daemon RPC socket.
//
// Frame: 4-byte little-endian payload length, then `length` packed bytes.

public enum CapnpFrame {
    public static func encode(_ packed: Data) -> Data {
        var d = Data()
        let len = UInt32(packed.count).littleEndian
        withUnsafeBytes(of: len) { d.append(contentsOf: $0) }
        d.append(packed)
        return d
    }

    /// Pulls one frame off the front of `buffer`. Returns the inner packed
    /// payload and removes the consumed bytes; returns nil if a full frame
    /// is not yet available.
    public static func extract(from buffer: inout Data) -> Data? {
        guard buffer.count >= 4 else { return nil }
        let len = Int(buffer.prefix(4).withUnsafeBytes { $0.load(as: UInt32.self) }.littleEndian)
        guard buffer.count >= 4 + len else { return nil }
        let payload = buffer.subdata(in: (buffer.startIndex + 4)..<(buffer.startIndex + 4 + len))
        buffer.removeSubrange(buffer.startIndex..<(buffer.startIndex + 4 + len))
        return payload
    }
}
