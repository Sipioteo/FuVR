// SPDX-License-Identifier: Apache-2.0
import Foundation
import Combine
import FuVRControl

@MainActor
final class AppState: ObservableObject {
    @Published var connectionState: ControlClientState = .idle
    @Published var sessionActive: Bool = false
    @Published var capabilities: DeviceCapabilities?
    @Published var latestMetrics: MetricsSample?
    @Published var logs: [LogLine] = []
    @Published var showAbout: Bool = false
    @Published var lastError: String?

    let metrics = MetricsBuffer(capacity: 600)

    private let client = ControlClient()
    private var bridge: ClientBridge!
    private var mock: MockDaemon?

    init() {
        bridge = ClientBridge(owner: self)
        client.delegate = bridge
    }

    var statusText: String {
        switch connectionState {
        case .idle:        return sessionActive ? "Active" : "Disconnected"
        case .connecting:  return "Connecting…"
        case .connected:   return sessionActive ? "Streaming" : "Connected"
        case .failed(let m): return "Failed: \(m)"
        }
    }

    var statusColorName: String {
        switch connectionState {
        case .connected:      return sessionActive ? "green" : "blue"
        case .connecting:     return "yellow"
        case .failed:         return "red"
        case .idle:           return "gray"
        }
    }

    func connect(socketPath: String, useMock: Bool) {
        if useMock {
            let m = MockDaemon(socketPath: socketPath)
            do { try m.start(); self.mock = m }
            catch { self.lastError = "mock start: \(error.localizedDescription)" }
        }
        client.connect(socketPath: socketPath)
    }

    func disconnect() {
        if sessionActive { client.send(.sessionStop) }
        client.disconnect()
        sessionActive = false
        mock?.stop()
        mock = nil
    }

    func startSession(_ config: SessionConfig) {
        client.send(.helloFromMac(config))
    }

    func stopSession() {
        client.send(.sessionStop)
        sessionActive = false
    }

    fileprivate func handle(_ payload: ControlPayload) {
        switch payload {
        case .helloFromQuest(let caps): capabilities = caps
        case .sessionStart:             sessionActive = true
        case .sessionStop:              sessionActive = false
        case .metrics(let m):
            latestMetrics = m
            metrics.append(m)
        case .log(let l):
            logs.append(l)
            if logs.count > 1000 { logs.removeFirst(logs.count - 1000) }
        case .error(let s):             lastError = s
        case .helloFromMac:             break
        }
    }

    fileprivate func setConnectionState(_ s: ControlClientState) {
        connectionState = s
        if case .idle = s { sessionActive = false }
    }
}

private final class ClientBridge: ControlClientDelegate {
    weak var owner: AppState?
    init(owner: AppState) { self.owner = owner }

    func controlClient(_ client: ControlClient, didChangeState state: ControlClientState) {
        Task { @MainActor in self.owner?.setConnectionState(state) }
    }

    func controlClient(_ client: ControlClient, didReceive payload: ControlPayload) {
        Task { @MainActor in self.owner?.handle(payload) }
    }
}
