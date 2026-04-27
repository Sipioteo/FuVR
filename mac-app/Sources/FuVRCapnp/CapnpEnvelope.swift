// SPDX-License-Identifier: Apache-2.0
import Foundation

// Subset of `proto/fuvrd.capnp` that the mac-app and daemon exchange.
// The Swift types here mirror the schema field-for-field.

public enum CapnpVideoCodec: UInt16, Sendable, Equatable {
    case hevc = 0
    case h264 = 1
}

public struct CapnpStartSessionRequest: Sendable, Equatable {
    public var perEyeWidth: UInt32
    public var perEyeHeight: UInt32
    public var refreshRateHz: UInt32
    public var videoCodec: CapnpVideoCodec
    public var videoBitrateBps: UInt32
    public var forceIdrEveryFrames: UInt32
    public var audioEnabled: Bool
    public var enableVirtualDisplay: Bool

    public init(perEyeWidth: UInt32, perEyeHeight: UInt32, refreshRateHz: UInt32,
                videoCodec: CapnpVideoCodec, videoBitrateBps: UInt32,
                forceIdrEveryFrames: UInt32 = 240, audioEnabled: Bool = false,
                enableVirtualDisplay: Bool = false) {
        self.perEyeWidth = perEyeWidth
        self.perEyeHeight = perEyeHeight
        self.refreshRateHz = refreshRateHz
        self.videoCodec = videoCodec
        self.videoBitrateBps = videoBitrateBps
        self.forceIdrEveryFrames = forceIdrEveryFrames
        self.audioEnabled = audioEnabled
        self.enableVirtualDisplay = enableVirtualDisplay
    }
}

public struct CapnpStartSessionResponse: Sendable, Equatable {
    public var sessionId: UInt64
    public var clockOffsetNs: Int64
    public var oneWayDelayNs: UInt64
    public var virtualDisplayId: UInt32
    public init(sessionId: UInt64, clockOffsetNs: Int64, oneWayDelayNs: UInt64, virtualDisplayId: UInt32) {
        self.sessionId = sessionId
        self.clockOffsetNs = clockOffsetNs
        self.oneWayDelayNs = oneWayDelayNs
        self.virtualDisplayId = virtualDisplayId
    }
}

public struct CapnpStopSessionRequest: Sendable, Equatable {
    public var sessionId: UInt64
    public init(sessionId: UInt64) { self.sessionId = sessionId }
}

public struct CapnpStreamInputsRequest: Sendable, Equatable {
    public var sessionId: UInt64
    public init(sessionId: UInt64) { self.sessionId = sessionId }
}

public struct CapnpMetrics: Sendable, Equatable {
    public var capturedAtNs: UInt64
    public var encoderFps: Float
    public var encoderEncodeMsAvg: Float
    public var encoderEncodeMsP95: Float
    public var transportRttMs: Float
    public var transportLossPct: Float
    public var decoderFps: Float
    public var decoderDecodeMsP95: Float
    public var videoBitrateMbps: Float
    public init(capturedAtNs: UInt64 = 0, encoderFps: Float = 0,
                encoderEncodeMsAvg: Float = 0, encoderEncodeMsP95: Float = 0,
                transportRttMs: Float = 0, transportLossPct: Float = 0,
                decoderFps: Float = 0, decoderDecodeMsP95: Float = 0,
                videoBitrateMbps: Float = 0) {
        self.capturedAtNs = capturedAtNs
        self.encoderFps = encoderFps
        self.encoderEncodeMsAvg = encoderEncodeMsAvg
        self.encoderEncodeMsP95 = encoderEncodeMsP95
        self.transportRttMs = transportRttMs
        self.transportLossPct = transportLossPct
        self.decoderFps = decoderFps
        self.decoderDecodeMsP95 = decoderDecodeMsP95
        self.videoBitrateMbps = videoBitrateMbps
    }
}

public struct CapnpLogLine: Sendable, Equatable {
    public enum Level: UInt8, Sendable, Equatable {
        case trace = 0, debug = 1, info = 2, warn = 3, error = 4
    }
    public var timestampNs: UInt64
    public var level: Level
    public var module: String
    public var message: String
    public init(timestampNs: UInt64, level: Level, module: String, message: String) {
        self.timestampNs = timestampNs
        self.level = level
        self.module = module
        self.message = message
    }
}

public enum CapnpEnvelope: Sendable, Equatable {
    case startSession(CapnpStartSessionRequest)
    case stopSession(CapnpStopSessionRequest)
    case streamMetrics
    case streamLogs
    case streamInputs(CapnpStreamInputsRequest)
    case ping
    case startSessionAck(CapnpStartSessionResponse)
    case metrics(CapnpMetrics)
    case log(CapnpLogLine)
    case pong
    case ok
    case error(String)

    /// Union discriminant for `Envelope.body` from `proto/fuvrd.capnp`. The
    /// discriminant is `ordinal - first_arm_ordinal`. The body union begins
    /// at @2 in the schema, so e.g. `startSession @2` -> 0, `error @16` -> 14.
    public var which: UInt16 {
        switch self {
        case .startSession:      return 0   // @2
        case .stopSession:       return 1   // @3
        // submitFrame @4 -> 2 (not exchanged with mac-app)
        // streamPoses @5 -> 3 (not exchanged)
        case .streamMetrics:     return 4   // @6
        case .streamLogs:        return 5   // @7
        case .ping:              return 6   // @8
        case .startSessionAck:   return 7   // @9
        // encodeStats @10 -> 8 (not exchanged with mac-app)
        // poseSnapshot @11 -> 9 (not exchanged)
        case .metrics:           return 10  // @12
        case .log:               return 11  // @13
        case .pong:              return 12  // @14
        case .ok:                return 13  // @15
        case .error:             return 14  // @16
        case .streamInputs:      return 15  // @17
        // inputSnapshot @18 -> 16, streamEncodeStats @19 -> 17 (reserved)
        }
    }
}

public struct CapnpFramedEnvelope: Sendable, Equatable {
    public var seq: UInt64
    public var streamId: UInt64
    public var body: CapnpEnvelope
    public init(seq: UInt64 = 0, streamId: UInt64 = 0, body: CapnpEnvelope) {
        self.seq = seq
        self.streamId = streamId
        self.body = body
    }
}
