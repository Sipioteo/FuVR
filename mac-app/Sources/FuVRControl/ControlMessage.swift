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

public struct MetricsSample: Codable, Equatable, Sendable {
    public var timestampMs: UInt64
    public var rttMs: Double
    public var jitterMs: Double
    public var packetLossPct: Double
    public var encodeMs: Double
    public var decodeMs: Double
    public var fps: Double
    public var bitrateMbps: Double

    public init(timestampMs: UInt64, rttMs: Double, jitterMs: Double,
                packetLossPct: Double, encodeMs: Double, decodeMs: Double,
                fps: Double, bitrateMbps: Double) {
        self.timestampMs = timestampMs
        self.rttMs = rttMs
        self.jitterMs = jitterMs
        self.packetLossPct = packetLossPct
        self.encodeMs = encodeMs
        self.decodeMs = decodeMs
        self.fps = fps
        self.bitrateMbps = bitrateMbps
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

public enum ControlPayload: Codable, Equatable, Sendable {
    case helloFromMac(SessionConfig)
    case helloFromQuest(DeviceCapabilities)
    case sessionStart
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
