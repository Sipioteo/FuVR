// SPDX-License-Identifier: Apache-2.0
import Foundation
import Network

public final class MockDaemon {
    private let queue = DispatchQueue(label: "fuvr.mock.daemon")
    private var listener: NWListener?
    private var clients: [NWConnection] = []
    private var metricsTimer: DispatchSourceTimer?
    private var running = false
    private var sessionActive = false
    private var lastConfig: SessionConfig?

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
        listener.start(queue: queue)
        self.listener = listener
        self.running = true
        scheduleMetrics()
    }

    public func stop() {
        running = false
        metricsTimer?.cancel()
        metricsTimer = nil
        listener?.cancel()
        listener = nil
        clients.forEach { $0.cancel() }
        clients.removeAll()
        try? FileManager.default.removeItem(atPath: socketPath)
    }

    private func accept(_ conn: NWConnection) {
        clients.append(conn)
        let buffer = Data()
        conn.stateUpdateHandler = { [weak self] state in
            if case .ready = state {
                let caps = DeviceCapabilities(
                    deviceModel: "Quest 3 (mock)",
                    systemVersion: "v74.0",
                    perEyeWidth: 2064, perEyeHeight: 2208,
                    refreshRatesHz: [72, 90, 120],
                    supportedCodecs: [.hevc, .h264],
                    hasHandTracking: true, hasEyeTracking: false
                )
                self?.send(.helloFromQuest(caps), to: conn)
            }
            if case .failed = state { conn.cancel() }
        }
        receive(conn, buffer: buffer)
        conn.start(queue: queue)
    }

    private func receive(_ conn: NWConnection, buffer: Data) {
        var buf = buffer
        conn.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, complete, _ in
            guard let self else { return }
            if let data { buf.append(data) }
            while let nl = buf.firstIndex(of: 0x0A) {
                let line = buf.subdata(in: buf.startIndex..<nl)
                buf.removeSubrange(buf.startIndex...nl)
                if let payload = (try? ControlCodec.decode(line)) ?? nil {
                    self.handle(payload, from: conn)
                }
            }
            if complete { conn.cancel(); return }
            self.receive(conn, buffer: buf)
        }
    }

    private func handle(_ payload: ControlPayload, from conn: NWConnection) {
        switch payload {
        case .helloFromMac(let cfg):
            lastConfig = cfg
            sessionActive = true
            send(.sessionStart, to: conn)
            send(.log(LogLine(timestampMs: now(), level: .info, source: "mock",
                              message: "session start codec=\(cfg.videoCodec.rawValue) \(cfg.refreshRateHz)Hz")), to: conn)
        case .sessionStop:
            sessionActive = false
            send(.log(LogLine(timestampMs: now(), level: .info, source: "mock", message: "session stop")), to: conn)
        default:
            break
        }
    }

    private func scheduleMetrics() {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + .milliseconds(100), repeating: .milliseconds(100))
        timer.setEventHandler { [weak self] in self?.tick() }
        timer.resume()
        metricsTimer = timer
    }

    private func tick() {
        guard sessionActive, !clients.isEmpty else { return }
        let m = MetricsSample(
            timestampMs: now(),
            rttMs: 8 + Double.random(in: -2...4),
            jitterMs: 0.6 + Double.random(in: 0...1.2),
            packetLossPct: max(0, Double.random(in: -0.2...0.4)),
            encodeMs: 4.5 + Double.random(in: -1...2),
            decodeMs: 3.2 + Double.random(in: -0.8...1.5),
            fps: Double(lastConfig?.refreshRateHz ?? 90) + Double.random(in: -0.5...0.5),
            bitrateMbps: 140 + Double.random(in: -8...8)
        )
        for c in clients { send(.metrics(m), to: c) }
    }

    private func send(_ p: ControlPayload, to conn: NWConnection) {
        guard let data = try? ControlCodec.encode(p) else { return }
        conn.send(content: data, completion: .contentProcessed { _ in })
    }

    private func now() -> UInt64 { UInt64(Date().timeIntervalSince1970 * 1000) }
}
