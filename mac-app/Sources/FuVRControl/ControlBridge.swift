// SPDX-License-Identifier: Apache-2.0
import Foundation
import FuVRCapnp

// Bridges between the public Swift control-surface types and the Cap'n Proto
// envelope subset. This is the only place that knows about both schemas.

enum ControlBridge {

    static func capnpCodec(_ codec: VideoCodec) -> CapnpVideoCodec {
        switch codec {
        case .h264: return .h264
        default:    return .hevc
        }
    }

    static func swiftCodec(_ codec: CapnpVideoCodec) -> VideoCodec {
        switch codec {
        case .h264: return .h264
        case .hevc: return .hevc
        }
    }

    static func capnpLevel(_ l: LogLine.Level) -> CapnpLogLine.Level {
        switch l {
        case .debug: return .debug
        case .info:  return .info
        case .warn:  return .warn
        case .error: return .error
        }
    }

    static func swiftLevel(_ l: CapnpLogLine.Level) -> LogLine.Level {
        switch l {
        case .trace, .debug: return .debug
        case .info:          return .info
        case .warn:          return .warn
        case .error:         return .error
        }
    }

    /// Encode an outgoing control payload from the mac-app to the daemon.
    /// `seq` is incremented by the caller.
    static func encodeOutgoing(_ payload: ControlPayload, seq: UInt64) -> CapnpFramedEnvelope? {
        switch payload {
        case .helloFromMac(let cfg):
            // helloFromMac is the legacy JSON name for "start a session".
            let r = CapnpStartSessionRequest(
                perEyeWidth: cfg.perEyeWidth,
                perEyeHeight: cfg.perEyeHeight,
                refreshRateHz: cfg.refreshRateHz,
                videoCodec: capnpCodec(cfg.videoCodec),
                videoBitrateBps: cfg.videoBitrateBps,
                forceIdrEveryFrames: 240,
                audioEnabled: cfg.audioEnabled,
                enableVirtualDisplay: false
            )
            return CapnpFramedEnvelope(seq: seq, body: .startSession(r))
        case .sessionStart:
            // No corresponding outgoing message; daemon emits startSessionAck.
            return nil
        case .sessionStop:
            return CapnpFramedEnvelope(seq: seq, body: .stopSession(.init(sessionId: 0)))
        case .helloFromQuest, .metrics, .log, .error, .sessionStarted:
            // Inbound-only on the mac-app side.
            return nil
        }
    }

    /// Decode a Cap'n Proto envelope received from the daemon into the
    /// mac-app's public control-payload enum. Returns nil if the arm has no
    /// surface mapping (e.g. ping/pong).
    static func decodeIncoming(_ env: CapnpFramedEnvelope, sessionConfig: SessionConfig?) -> ControlPayload? {
        switch env.body {
        case .startSessionAck(let r):
            let info = SessionInfo(
                sessionId: r.sessionId,
                clockOffsetNs: r.clockOffsetNs,
                oneWayDelayNs: r.oneWayDelayNs,
                virtualDisplayId: r.virtualDisplayId,
                codec: sessionConfig?.videoCodec ?? .hevc,
                videoBitrateMbps: (sessionConfig?.videoBitrateBps ?? 0) / 1_000_000
            )
            return .sessionStarted(info)
        case .metrics(let m):
            let cfg = sessionConfig
            let bitrateMbps = m.videoBitrateMbps > 0
                ? Double(m.videoBitrateMbps)
                : Double(cfg?.videoBitrateBps ?? 0) / 1_000_000.0
            let sample = MetricsSample(
                timestampMs: m.capturedAtNs / 1_000_000,
                rttMs: Double(m.transportRttMs),
                jitterMs: 0,
                packetLossPct: Double(m.transportLossPct),
                encodeMs: Double(m.encoderEncodeMsP95),
                decodeMs: Double(m.decoderDecodeMsP95),
                fps: Double(m.encoderFps > 0 ? m.encoderFps : m.decoderFps),
                bitrateMbps: bitrateMbps,
                encoderFps: Double(m.encoderFps),
                encoderEncodeMsAvg: Double(m.encoderEncodeMsAvg),
                encoderEncodeMsP95: Double(m.encoderEncodeMsP95),
                decoderFps: Double(m.decoderFps),
                decoderDecodeMsP95: Double(m.decoderDecodeMsP95)
            )
            return .metrics(sample)
        case .log(let l):
            return .log(LogLine(
                timestampMs: l.timestampNs / 1_000_000,
                level: swiftLevel(l.level),
                source: l.module,
                message: l.message
            ))
        case .error(let s):
            return .error(s)
        case .ok, .pong, .ping, .streamMetrics, .streamLogs, .streamInputs, .startSession, .stopSession:
            return nil
        }
    }
}
