// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import Combine
import FuVRControl
import FuVRADB

@MainActor
final class AppState: ObservableObject {

    // MARK: - Daemon / control-socket state

    @Published var connectionState: ControlClientState = .idle
    @Published var sessionActive: Bool = false
    @Published var capabilities: DeviceCapabilities?
    @Published var latestMetrics: MetricsSample?
    @Published var logs: [LogLine] = []
    @Published var showAbout: Bool = false
    @Published var lastError: String?
    @Published var sessionInfo: SessionInfo?
    @Published var showOnboarding: Bool = false
    @Published var showQuestSetup: Bool = false

    let metrics = MetricsBuffer(capacity: 600)

    // MARK: - ADB / device state

    @Published var deviceState: DeviceState = .waiting

    // MARK: - Private

    private let client = ControlClient()
    private var bridge: ClientBridge!
    private var mock: MockDaemon?

    private let poller = ADBDevicePoller(interval: 2.0)
    private let installer = APKInstaller()
    private var watchdog: DaemonWatchdog?
    private var activeSerial: String?
    /// MUST be retained — `ADBDevicePoller.delegate` is `weak`, so a local
    /// bridge would deallocate immediately after `startADBPoller()` returns
    /// and the poller would silently fire into a nil delegate.
    private var pollerBridge: PollerBridge?

    init() {
        _ = SettingsMigration.load()
        bridge = ClientBridge(owner: self)
        client.delegate = bridge
        startADBPoller()
    }

    // MARK: - Control-socket API (unchanged)

    var statusText: String {
        switch connectionState {
        case .idle:          return sessionActive ? "Active" : "Disconnected"
        case .connecting:    return "Connecting…"
        case .connected:     return sessionActive ? "Streaming" : "Connected"
        case .failed(let m): return "Failed: \(m)"
        }
    }

    var statusColorName: String {
        switch connectionState {
        case .connected:     return sessionActive ? "green" : "blue"
        case .connecting:    return "yellow"
        case .failed:        return "red"
        case .idle:          return "gray"
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

    // MARK: - Watchdog API

    func startWatchdog(socketPath: String) {
        guard let url = DaemonWatchdog.resolveDaemonURL() else {
            lastError = "fuvrd binary not found. Install the daemon first."
            return
        }
        let wd = DaemonWatchdog(daemonURL: url, socketPath: socketPath)
        wd.onRestart = { [weak self] _ in
            self?.connect(socketPath: socketPath, useMock: false)
        }
        wd.onGaveUp = { [weak self] in
            self?.lastError = "Daemon crashed repeatedly — check Logs tab."
        }
        wd.start()
        watchdog = wd
    }

    func stopWatchdog() {
        watchdog?.stop()
        watchdog = nil
    }

    // MARK: - ADB pipeline

    private func startADBPoller() {
        let del = PollerBridge(owner: self)
        pollerBridge = del              // retain — delegate is weak
        poller.delegate = del
        poller.start()
    }

    fileprivate func handleDevices(_ devices: [ADBDevice]) {
        guard let device = devices.first(where: { $0.isReady }) else {
            if devices.isEmpty { purgeSession() }
            return
        }
        if device.serial == activeSerial { return }
        let serial = device.serial
        activeSerial = serial

        Task.detached(priority: .utility) { [weak self] in
            guard let self else { return }
            let model = self.installer.deviceModel(serial: serial) ?? "VR Headset"
            await MainActor.run {
                withAnimation(.spring(duration: 0.5)) {
                    self.deviceState = .deviceFound(serial: serial, model: model)
                }
                self.beginInstallFlow(serial: serial, model: model)
            }
        }
    }

    private func beginInstallFlow(serial: String, model: String) {
        withAnimation(.spring(duration: 0.4)) {
            deviceState = .installing(serial: serial, model: model, progress: 0.0)
        }

        installer.install(serial: serial, progress: { [weak self] pct in
            DispatchQueue.main.async {
                guard let self else { return }
                if case .installing(let s, let m, _) = self.deviceState, s == serial {
                    withAnimation { self.deviceState = .installing(serial: s, model: m, progress: pct) }
                }
            }
        }, completion: { [weak self] result in
            DispatchQueue.main.async {
                guard let self else { return }
                switch result {
                case .installed, .alreadyInstalled:
                    withAnimation(.spring(duration: 0.4)) {
                        self.deviceState = .launching(serial: serial, model: model)
                    }
                    self.launchApp(serial: serial, model: model)
                case .failed(let msg):
                    self.activeSerial = nil
                    withAnimation(.spring(duration: 0.3)) {
                        self.deviceState = .failed(msg)
                    }
                }
            }
        })
    }

    private func launchApp(serial: String, model: String) {
        installer.launch(serial: serial) { [weak self] ok, msg in
            DispatchQueue.main.async {
                guard let self else { return }
                if ok {
                    withAnimation(.spring(duration: 0.5)) {
                        self.deviceState = .streaming(serial: serial, model: model)
                    }
                } else {
                    self.activeSerial = nil
                    self.deviceState = .failed("Launch failed: \(msg)")
                }
            }
        }
    }

    private func purgeSession() {
        guard activeSerial != nil else { return }
        activeSerial = nil
        if sessionActive { client.send(.sessionStop) }
        withAnimation(.spring(duration: 0.4)) { deviceState = .waiting }
    }

    // MARK: - Control message handler

    fileprivate func handle(_ payload: ControlPayload) {
        switch payload {
        case .helloFromQuest(let caps): capabilities = caps
        case .sessionStart:             sessionActive = true
        case .sessionStarted(let info):
            sessionActive = true
            sessionInfo = info
        case .sessionStop:
            sessionActive = false
            sessionInfo = nil
            purgeSession()
        case .metrics(let m):
            latestMetrics = m
            metrics.append(m)
        case .log(let l):
            logs.append(l)
            if logs.count > 1000 { logs.removeFirst(logs.count - 1000) }
        case .error(let s): lastError = s
        case .helloFromMac: break
        }
    }

    fileprivate func setConnectionState(_ s: ControlClientState) {
        connectionState = s
        if case .idle = s { sessionActive = false }
    }
}

// MARK: - Private delegate bridges

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

private final class PollerBridge: ADBDevicePollerDelegate {
    weak var owner: AppState?
    init(owner: AppState) { self.owner = owner }

    func poller(_ poller: ADBDevicePoller, didUpdateDevices devices: [ADBDevice]) {
        Task { @MainActor in self.owner?.handleDevices(devices) }
    }
}
