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
    @Published var showQuestSetup: Bool = false

    /// Snapshot of the currently bound OpenVR application, refreshed every
    /// 500 ms while the Stream tab is visible. `nil` means "never polled".
    @Published var activeStream: ActiveStream?

    private var streamPollTask: Task<Void, Never>?

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
        Self.exportOpenXrRuntimeEnv()
        // Publish current Encoder settings to the GUI env at startup so
        // the FIRST Blender launch already inherits them. Subsequent
        // edits in EncoderSettingsView re-run this via .onChange.
        let d = UserDefaults.standard
        let bitrate = d.object(forKey: SettingsKey.bitrateMbps) as? Int ?? 150
        let codec   = d.object(forKey: SettingsKey.codec)       as? String ?? "hevc"
        let refresh = d.object(forKey: SettingsKey.refreshRate) as? Int ?? 90
        Self.publishEncoderEnv(bitrateMbps: bitrate, codec: codec, refreshHz: refresh)
    }

    /// Push the user's Encoder-page settings into the GUI session env
    /// (via `launchctl setenv`). The runtime-macos read these at session
    /// creation; Blender (re)launched from Finder afterwards inherits.
    /// Idempotent. Driven from `init()` and from EncoderSettingsView's
    /// `.onChange` handlers so changing the slider takes effect on the
    /// NEXT Blender launch.
    static func publishEncoderEnv(bitrateMbps: Int, codec: String, refreshHz: Int) {
        let runArgs: (String, [String]) -> Void = { exe, args in
            let p = Process()
            p.executableURL = URL(fileURLWithPath: exe)
            p.arguments = args
            try? p.run()
            p.waitUntilExit()
        }
        let bps = String(max(1, bitrateMbps) * 1_000_000)
        runArgs("/bin/launchctl", ["setenv", "FUVR_RT_BITRATE_BPS", bps])
        runArgs("/bin/launchctl", ["setenv", "FUVR_RT_CODEC",
                                   codec.lowercased().contains("h264") ? "h264" : "hevc"])
        runArgs("/bin/launchctl", ["setenv", "FUVR_RT_REFRESH_HZ", String(refreshHz)])
    }

    /// Run `adb reverse tcp:9943 tcp:9943` for the given serial so the
    /// Quest app's loopback connect tunnels back to the Mac daemon.
    /// Best-effort and silent on failure — the recv loop on the Quest
    /// side will keep retrying anyway.
    nonisolated private static func setupAdbReverse(serial: String) {
        guard let adb = try? AdbController() else { return }
        do {
            try adb.reverse(port: 9943, serial: serial)
        } catch {
            NSLog("[fuvr] adb reverse failed: \(error)")
        }
    }

    /// Publish `XR_RUNTIME_JSON` to the GUI session so user-launched apps
    /// (Blender, Unity-based games, anything that uses the Khronos OpenXR
    /// loader) pick up FuVR's runtime manifest without needing the user
    /// to set env vars manually. Idempotent and cheap; we always run it
    /// at app startup so a stale or missing setenv self-heals.
    private static func exportOpenXrRuntimeEnv() {
        let home = FileManager.default.homeDirectoryForCurrentUser
        let manifest = home
            .appendingPathComponent(".config/openxr/1/active_runtime.json")
            .path
        guard FileManager.default.fileExists(atPath: manifest) else {
            // No manifest yet — the install-daemon step writes it; running
            // launchctl setenv with a non-existent file would just point
            // Blender at a dead path.
            return
        }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/bin/launchctl")
        p.arguments = ["setenv", "XR_RUNTIME_JSON", manifest]
        try? p.run()
        p.waitUntilExit()
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
        adbPollerPaused = false
    }

    /// Whether the global ADB poller is currently suspended. Exposed so the
    /// wizard can show "ADB paused" hints when the tether toggle is active.
    @Published private(set) var adbPollerPaused: Bool = false

    /// Stop the 2 Hz `adb devices -l` poll. Used during the USB-Tethering
    /// toggle window: when Android is mid-way through reconfiguring the
    /// USB function set to add RNDIS, any concurrent `adb` traffic from
    /// the host re-enumerates the device and the tether toggle silently
    /// rolls back to OFF. Pausing the poller for the duration of the
    /// toggle prevents that race.
    public func pauseAdbPolling() {
        guard !adbPollerPaused else { return }
        poller.stop()
        adbPollerPaused = true
    }

    /// Re-arm the poller after a tether toggle is settled (or aborted).
    public func resumeAdbPolling() {
        guard adbPollerPaused else { return }
        poller.start()
        adbPollerPaused = false
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
                    // Why here: the daemon's `UsbServer` listens on
                    // `127.0.0.1:9943` (Mac side), and the Quest's
                    // TransportClient dials its own `127.0.0.1:9943`.
                    // Without `adb reverse tcp:9943 tcp:9943` the Quest's
                    // localhost is its OWN host — frames go nowhere.
                    // Setting it up here means: by the time the user (or
                    // Blender) clicks "Start session", the tunnel is
                    // already alive. Idempotent: adb returns success even
                    // if the rule already exists.
                    Self.setupAdbReverse(serial: serial)

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

    // MARK: - Active-stream polling

    /// Begin polling the daemon for `GetActiveStream` snapshots at 2 Hz.
    /// Reads are coalesced — if a request takes longer than the tick
    /// interval the next tick is skipped rather than queued. Idempotent.
    func startActiveStreamPolling(intervalMs: UInt64 = 500) {
        guard streamPollTask == nil else { return }
        let nanos = intervalMs * 1_000_000
        streamPollTask = Task { [weak self] in
            while !Task.isCancelled {
                let started = DispatchTime.now().uptimeNanoseconds
                if let snap = try? await self?.client.getActiveStream() {
                    await MainActor.run { self?.activeStream = snap }
                }
                let elapsed = DispatchTime.now().uptimeNanoseconds - started
                if elapsed < nanos {
                    try? await Task.sleep(nanoseconds: nanos - elapsed)
                }
                // If `elapsed >= nanos` we skip the sleep — that's the
                // coalescing behaviour: never queue a backlog of pings.
            }
        }
    }

    func stopActiveStreamPolling() {
        streamPollTask?.cancel()
        streamPollTask = nil
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
