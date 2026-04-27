// SPDX-License-Identifier: Apache-2.0
import Foundation
import Network
import FuVRCapnp

/// In-process mock daemon that speaks the same packed Cap'n Proto envelope
/// protocol as `fuvrd`. Used for SwiftUI previews and end-to-end tests.
public final class MockDaemon {
    private let queue = DispatchQueue(label: "fuvr.mock.daemon")
    private var listener: NWListener?
    private var clients: [NWConnection] = []
    private var clientBuffers: [ObjectIdentifier: Data] = [:]
    private var metricsTimer: DispatchSourceTimer?
    private var logTimer: DispatchSourceTimer?
    private var running = false
    private var sessionActive = false
    private var sessionId: UInt64 = 0
    private var lastConfig: CapnpStartSessionRequest?
    private var seq: UInt64 = 0

    public let socketPath: String

    public init(socketPath: String) {
        self.socketPath = socketPath
    }

    public func start() throws {
        guard !running else { return }
        try? FileManager.default.removeItem(atPath: socketPath)
        let endpoint = NWEndpoint.unix(path: socketPath)
        let params = NWParameters.tcp
        let listener = try NWListener(using: params)
        listener.parameters.requiredLocalEndpoint = endpoint
        listener.newConnectionHandler = { [weak self] conn in
            self?.accept(conn)
        }
        listener.stateUpdateHandler = { state in
            if case .failed(let err) = state {
                NSLog("MockDaemon listener failed: \(err)")
            }
        }
        listener.start(queue: queue)
        self.listener = listener
        self.running = true
        scheduleStreams()
    }

    public func stop() {
        running = false
        metricsTimer?.cancel()
        metricsTimer = nil
        logTimer?.cancel()
        logTimer = nil
        listener?.cancel()
        listener = nil
        clients.forEach { $0.cancel() }
        clients.removeAll()
        clientBuffers.removeAll()
        try? FileManager.default.removeItem(atPath: socketPath)
    }

    private func accept(_ conn: NWConnection) {
        clients.append(conn)
        clientBuffers[ObjectIdentifier(conn)] = Data()
        conn.stateUpdateHandler = { [weak self] state in
            if case .failed = state { conn.cancel() }
            if case .cancelled = state {
                self?.clientBuffers.removeValue(forKey: ObjectIdentifier(conn))
                self?.clients.removeAll { $0 === conn }
            }
        }
        receive(conn)
        conn.start(queue: queue)
    }

    private func receive(_ conn: NWConnection) {
        conn.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, complete, _ in
            guard let self else { return }
            if let data, !data.isEmpty {
                var buf = self.clientBuffers[ObjectIdentifier(conn)] ?? Data()
                buf.append(data)
                while let packed = CapnpFrame.extract(from: &buf) {
                    if let env = try? CapnpCodec.decode(packed) {
                        self.handle(env, from: conn)
                    }
                }
                self.clientBuffers[ObjectIdentifier(conn)] = buf
            }
            if complete { conn.cancel(); return }
            self.receive(conn)
        }
    }

    private func handle(_ env: CapnpFramedEnvelope, from conn: NWConnection) {
        switch env.body {
        case .startSession(let r):
            lastConfig = r
            sessionActive = true
            sessionId &+= 1
            send(.startSessionAck(.init(
                sessionId: sessionId,
                clockOffsetNs: 250_000,
                oneWayDelayNs: 5_000_000,
                virtualDisplayId: 0
            )), to: conn)
            send(.log(.init(
                timestampNs: nowNs(),
                level: .info,
                module: "mock",
                message: "session start codec=\(r.videoCodec == .h264 ? "h264" : "hevc") \(r.refreshRateHz)Hz"
            )), to: conn)
        case .stopSession:
            sessionActive = false
            send(.log(.init(timestampNs: nowNs(), level: .info,
                            module: "mock", message: "session stop")), to: conn)
            send(.ok, to: conn)
        case .ping:
            send(.pong, to: conn)
        case .streamMetrics, .streamLogs, .streamInputs:
            send(.ok, to: conn)
        default:
            break
        }
    }

    private func scheduleStreams() {
        let mt = DispatchSource.makeTimerSource(queue: queue)
        mt.schedule(deadline: .now() + .milliseconds(100), repeating: .milliseconds(100))
        mt.setEventHandler { [weak self] in self?.tickMetrics() }
        mt.resume()
        metricsTimer = mt

        let lt = DispatchSource.makeTimerSource(queue: queue)
        lt.schedule(deadline: .now() + .seconds(2), repeating: .seconds(3))
        lt.setEventHandler { [weak self] in self?.tickLog() }
        lt.resume()
        logTimer = lt
    }

    private func tickMetrics() {
        guard sessionActive, !clients.isEmpty else { return }
        let cfg = lastConfig
        let m = CapnpMetrics(
            capturedAtNs: nowNs(),
            encoderFps: Float(cfg?.refreshRateHz ?? 90) + Float.random(in: -0.5...0.5),
            encoderEncodeMsAvg: 4.5 + Float.random(in: -0.6...1.2),
            encoderEncodeMsP95: 8.2 + Float.random(in: -0.5...2.0),
            transportRttMs: 8.3 + Float.random(in: -1.5...3.0),
            transportLossPct: max(0, Float.random(in: -0.2...0.4)),
            decoderFps: Float(cfg?.refreshRateHz ?? 90) + Float.random(in: -0.7...0.4),
            decoderDecodeMsP95: 6.0 + Float.random(in: -0.8...1.5),
            videoBitrateMbps: Float((cfg?.videoBitrateBps ?? 150_000_000)) / 1_000_000.0
        )
        for c in clients { send(.metrics(m), to: c) }
    }

    private func tickLog() {
        guard sessionActive, !clients.isEmpty else { return }
        let l = CapnpLogLine(timestampNs: nowNs(), level: .info,
                             module: "mock-encoder",
                             message: "frame \(Int.random(in: 1000...9999)) keyframe")
        for c in clients { send(.log(l), to: c) }
    }

    private func send(_ body: CapnpEnvelope, to conn: NWConnection) {
        seq &+= 1
        let env = CapnpFramedEnvelope(seq: seq, body: body)
        let data = CapnpCodec.encode(env)
        conn.send(content: data, completion: .contentProcessed { _ in })
    }

    private func nowNs() -> UInt64 {
        UInt64(Date().timeIntervalSince1970 * 1_000_000_000)
    }
}
