// SPDX-License-Identifier: Apache-2.0
import Foundation

public enum VideoCodec: String, Codable, CaseIterable, Sendable {
    case hevc, h264, av1
}

public enum TransportMode: String, Codable, CaseIterable, Sendable {
    case usb, wifi
}

public struct SessionConfig: Codable, Equatable, Sendable {
    public var perEyeWidth: UInt32
    public var perEyeHeight: UInt32
    public var refreshRateHz: UInt32
    public var videoCodec: VideoCodec
    public var videoBitrateBps: UInt32
    public var audioEnabled: Bool

    public init(
        perEyeWidth: UInt32 = 2064,
        perEyeHeight: UInt32 = 2208,
        refreshRateHz: UInt32 = 90,
        videoCodec: VideoCodec = .hevc,
        videoBitrateBps: UInt32 = 150_000_000,
        audioEnabled: Bool = true
    ) {
        self.perEyeWidth = perEyeWidth
        self.perEyeHeight = perEyeHeight
        self.refreshRateHz = refreshRateHz
        self.videoCodec = videoCodec
        self.videoBitrateBps = videoBitrateBps
        self.audioEnabled = audioEnabled
    }
}

public struct DeviceCapabilities: Codable, Equatable, Sendable {
    public var deviceModel: String
    public var systemVersion: String
    public var perEyeWidth: UInt32
    public var perEyeHeight: UInt32
    public var refreshRatesHz: [UInt32]
    public var supportedCodecs: [VideoCodec]
    public var hasHandTracking: Bool
    public var hasEyeTracking: Bool

    public init(deviceModel: String, systemVersion: String, perEyeWidth: UInt32,
                perEyeHeight: UInt32, refreshRatesHz: [UInt32],
                supportedCodecs: [VideoCodec], hasHandTracking: Bool, hasEyeTracking: Bool) {
        self.deviceModel = deviceModel
        self.systemVersion = systemVersion
        self.perEyeWidth = perEyeWidth
        self.perEyeHeight = perEyeHeight
        self.refreshRatesHz = refreshRatesHz
        self.supportedCodecs = supportedCodecs
        self.hasHandTracking = hasHandTracking
        self.hasEyeTracking = hasEyeTracking
    }
}

public struct MetricsSample: Equatable, Sendable {
    public var timestampMs: UInt64
    public var rttMs: Double
    public var jitterMs: Double
    public var packetLossPct: Double
    public var encodeMs: Double
    public var decodeMs: Double
    public var fps: Double
    public var bitrateMbps: Double

    /// Encoder/decoder breakdown — populated when the daemon emits Cap'n
    /// Proto `Metrics`. Default to the legacy `encodeMs`/`decodeMs`/`fps`
    /// values for callers that only know the older shape.
    public var encoderFps: Double
    public var encoderEncodeMsAvg: Double
    public var encoderEncodeMsP95: Double
    public var decoderFps: Double
    public var decoderDecodeMsP95: Double

    public init(timestampMs: UInt64, rttMs: Double, jitterMs: Double,
                packetLossPct: Double, encodeMs: Double, decodeMs: Double,
                fps: Double, bitrateMbps: Double,
                encoderFps: Double = 0, encoderEncodeMsAvg: Double = 0,
                encoderEncodeMsP95: Double = 0,
                decoderFps: Double = 0, decoderDecodeMsP95: Double = 0) {
        self.timestampMs = timestampMs
        self.rttMs = rttMs
        self.jitterMs = jitterMs
        self.packetLossPct = packetLossPct
        self.encodeMs = encodeMs
        self.decodeMs = decodeMs
        self.fps = fps
        self.bitrateMbps = bitrateMbps
        self.encoderFps = encoderFps == 0 ? fps : encoderFps
        self.encoderEncodeMsAvg = encoderEncodeMsAvg == 0 ? encodeMs : encoderEncodeMsAvg
        self.encoderEncodeMsP95 = encoderEncodeMsP95 == 0 ? encodeMs : encoderEncodeMsP95
        self.decoderFps = decoderFps == 0 ? fps : decoderFps
        self.decoderDecodeMsP95 = decoderDecodeMsP95 == 0 ? decodeMs : decoderDecodeMsP95
    }
}

extension MetricsSample: Codable {
    private enum CodingKeys: String, CodingKey {
        case timestampMs, rttMs, jitterMs, packetLossPct
        case encodeMs, decodeMs, fps, bitrateMbps
        case encoderFps, encoderEncodeMsAvg, encoderEncodeMsP95
        case decoderFps, decoderDecodeMsP95
    }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let ts  = try c.decode(UInt64.self, forKey: .timestampMs)
        let rtt = try c.decode(Double.self, forKey: .rttMs)
        let jit = try c.decode(Double.self, forKey: .jitterMs)
        let los = try c.decode(Double.self, forKey: .packetLossPct)
        let enc = try c.decode(Double.self, forKey: .encodeMs)
        let dec = try c.decode(Double.self, forKey: .decodeMs)
        let fps = try c.decode(Double.self, forKey: .fps)
        let br  = try c.decode(Double.self, forKey: .bitrateMbps)
        let efps = (try? c.decode(Double.self, forKey: .encoderFps)) ?? 0
        let eavg = (try? c.decode(Double.self, forKey: .encoderEncodeMsAvg)) ?? 0
        let ep95 = (try? c.decode(Double.self, forKey: .encoderEncodeMsP95)) ?? 0
        let dfps = (try? c.decode(Double.self, forKey: .decoderFps)) ?? 0
        let dp95 = (try? c.decode(Double.self, forKey: .decoderDecodeMsP95)) ?? 0
        self.init(timestampMs: ts, rttMs: rtt, jitterMs: jit, packetLossPct: los,
                  encodeMs: enc, decodeMs: dec, fps: fps, bitrateMbps: br,
                  encoderFps: efps, encoderEncodeMsAvg: eavg, encoderEncodeMsP95: ep95,
                  decoderFps: dfps, decoderDecodeMsP95: dp95)
    }
    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(timestampMs, forKey: .timestampMs)
        try c.encode(rttMs, forKey: .rttMs)
        try c.encode(jitterMs, forKey: .jitterMs)
        try c.encode(packetLossPct, forKey: .packetLossPct)
        try c.encode(encodeMs, forKey: .encodeMs)
        try c.encode(decodeMs, forKey: .decodeMs)
        try c.encode(fps, forKey: .fps)
        try c.encode(bitrateMbps, forKey: .bitrateMbps)
        try c.encode(encoderFps, forKey: .encoderFps)
        try c.encode(encoderEncodeMsAvg, forKey: .encoderEncodeMsAvg)
        try c.encode(encoderEncodeMsP95, forKey: .encoderEncodeMsP95)
        try c.encode(decoderFps, forKey: .decoderFps)
        try c.encode(decoderDecodeMsP95, forKey: .decoderDecodeMsP95)
    }
}

/// Active-session metadata reported by the daemon via the
/// `startSessionAck` arm of the Cap'n Proto envelope.
public struct SessionInfo: Equatable, Sendable {
    public var sessionId: UInt64
    public var clockOffsetNs: Int64
    public var oneWayDelayNs: UInt64
    public var virtualDisplayId: UInt32
    public var codec: VideoCodec
    public var videoBitrateMbps: UInt32
    public init(sessionId: UInt64, clockOffsetNs: Int64, oneWayDelayNs: UInt64,
                virtualDisplayId: UInt32, codec: VideoCodec, videoBitrateMbps: UInt32) {
        self.sessionId = sessionId
        self.clockOffsetNs = clockOffsetNs
        self.oneWayDelayNs = oneWayDelayNs
        self.virtualDisplayId = virtualDisplayId
        self.codec = codec
        self.videoBitrateMbps = videoBitrateMbps
    }
}

public struct LogLine: Codable, Equatable, Sendable {
    public enum Level: String, Codable, Sendable { case debug, info, warn, error }
    public var timestampMs: UInt64
    public var level: Level
    public var source: String
    public var message: String

    public init(timestampMs: UInt64, level: Level, source: String, message: String) {
        self.timestampMs = timestampMs
        self.level = level
        self.source = source
        self.message = message
    }
}

public enum ControlPayload: Equatable, Sendable {
    case helloFromMac(SessionConfig)
    case helloFromQuest(DeviceCapabilities)
    case sessionStart
    case sessionStarted(SessionInfo)
    case sessionStop
    case metrics(MetricsSample)
    case log(LogLine)
    case error(String)
}

public struct ControlEnvelope: Codable, Equatable, Sendable {
    public let v: Int
    public let type: String
    public let payload: AnyPayload

    public init(_ payload: ControlPayload, version: Int = 1) {
        self.v = version
        switch payload {
        case .helloFromMac(let c):    self.type = "helloFromMac";    self.payload = .helloFromMac(c)
        case .helloFromQuest(let c):  self.type = "helloFromQuest";  self.payload = .helloFromQuest(c)
        case .sessionStart:           self.type = "sessionStart";    self.payload = .empty
        case .sessionStarted:         self.type = "sessionStarted"; self.payload = .empty
        case .sessionStop:            self.type = "sessionStop";     self.payload = .empty
        case .metrics(let m):         self.type = "metrics";         self.payload = .metrics(m)
        case .log(let l):             self.type = "log";             self.payload = .log(l)
        case .error(let s):           self.type = "error";           self.payload = .errorMsg(s)
        }
    }

    public func decoded() -> ControlPayload? {
        switch (type, payload) {
        case ("helloFromMac",   .helloFromMac(let c)):   return .helloFromMac(c)
        case ("helloFromQuest", .helloFromQuest(let c)): return .helloFromQuest(c)
        case ("sessionStart",   _):                      return .sessionStart
        case ("sessionStop",    _):                      return .sessionStop
        case ("metrics",        .metrics(let m)):        return .metrics(m)
        case ("log",            .log(let l)):            return .log(l)
        case ("error",          .errorMsg(let s)):       return .error(s)
        default: return nil
        }
    }

    public enum AnyPayload: Codable, Equatable, Sendable {
        case empty
        case helloFromMac(SessionConfig)
        case helloFromQuest(DeviceCapabilities)
        case metrics(MetricsSample)
        case log(LogLine)
        case errorMsg(String)

        public init(from decoder: Decoder) throws {
            let c = try decoder.singleValueContainer()
            if c.decodeNil() { self = .empty; return }
            if let m = try? c.decode(MetricsSample.self) { self = .metrics(m); return }
            if let l = try? c.decode(LogLine.self) { self = .log(l); return }
            if let s = try? c.decode(SessionConfig.self) { self = .helloFromMac(s); return }
            if let d = try? c.decode(DeviceCapabilities.self) { self = .helloFromQuest(d); return }
            if let e = try? c.decode(String.self) { self = .errorMsg(e); return }
            self = .empty
        }

        public func encode(to encoder: Encoder) throws {
            var c = encoder.singleValueContainer()
            switch self {
            case .empty:                  try c.encodeNil()
            case .helloFromMac(let s):    try c.encode(s)
            case .helloFromQuest(let d):  try c.encode(d)
            case .metrics(let m):         try c.encode(m)
            case .log(let l):             try c.encode(l)
            case .errorMsg(let s):        try c.encode(s)
            }
        }
    }
}

public enum ControlCodec {
    public static func encode(_ payload: ControlPayload) throws -> Data {
        let env = ControlEnvelope(payload)
        var data = try JSONEncoder().encode(env)
        data.append(0x0A)
        return data
    }

    public static func decode(_ line: Data) throws -> ControlPayload? {
        let trimmed = line.last == 0x0A ? line.dropLast() : line[...]
        guard !trimmed.isEmpty else { return nil }
        let env = try JSONDecoder().decode(ControlEnvelope.self, from: Data(trimmed))
        return env.decoded()
    }
}
