// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Glues together the four building blocks (`AdbController`,
/// `DeviceMonitor`, `DaemonSupervisor`, `SessionMachine`) into the single
/// surface that `AppState` consumes. This is where the spec's workflow lives:
///
///   1. Poll `adb devices`.
///   2. When a Quest appears, ensure the APK is installed (push if absent).
///   3. Launch the Quest app via `am start`.
///   4. Open the adb reverse tunnel.
///   5. Trust the daemon to handshake; on `streamingHandshake` go to STREAMING.
///   6. On disappearance / disconnect, purge and revert to WAITING.
///
/// All side-effecting work runs on a background queue so the SwiftUI main
/// actor never blocks on `Process()`.
public final class SessionOrchestrator: @unchecked Sendable {
    public struct Config: Sendable {
        public var reverseTunnelPort: Int
        public var apkURL: URL?
        public var alwaysReinstall: Bool
        public init(reverseTunnelPort: Int = 9943,
                    apkURL: URL? = nil,
                    alwaysReinstall: Bool = false) {
            self.reverseTunnelPort = reverseTunnelPort
            self.apkURL = apkURL
            self.alwaysReinstall = alwaysReinstall
        }
    }

    public let machine = SessionMachine()
    public let supervisor: DaemonSupervisor?
    private let monitor: DeviceMonitor
    private let adb: AdbController
    private let config: Config
    private let queue = DispatchQueue(label: "com.fuvr.orchestrator", qos: .userInitiated)
    private var monitorToken: UUID?
    private var supervisorToken: UUID?
    private var lastHeadsetSerial: String?
    private var inFlight: Bool = false

    public init(adb: AdbController,
                monitor: DeviceMonitor,
                supervisor: DaemonSupervisor? = nil,
                config: Config = Config()) {
        self.adb = adb
        self.monitor = monitor
        self.supervisor = supervisor
        self.config = config
    }

    public func start() {
        monitorToken = monitor.addListener { [weak self] snap in
            self?.queue.async { self?.handle(snapshot: snap) }
        }
        supervisorToken = supervisor?.addListener { [weak self] state in
            self?.queue.async { self?.handle(supervisorState: state) }
        }
        monitor.start()
        supervisor?.start()
    }

    public func stop() {
        if let t = monitorToken { monitor.removeListener(t) }
        if let t = supervisorToken { supervisor?.removeListener(t) }
        monitor.stop()
        supervisor?.stop()
    }

    /// External signal: the daemon's control client has handed back a session
    /// ack from the Quest. The orchestrator translates that into a state event.
    public func notifyHandshake(sessionId: UInt64?) {
        queue.async { [weak self] in
            guard let self, let s = self.lastHeadsetSerial else { return }
            self.machine.handle(.streamingHandshake(serial: s, sessionId: sessionId))
        }
    }

    public func notifyStreamingEnded() {
        queue.async { [weak self] in self?.machine.handle(.streamingEnded) }
    }

    public func resetToWaiting() {
        queue.async { [weak self] in self?.machine.handle(.userReset) }
    }

    // MARK: - Private

    private func handle(snapshot: DeviceMonitor.Snapshot) {
        if let headset = snapshot.firstReadyHeadset {
            if lastHeadsetSerial != headset.serial {
                lastHeadsetSerial = headset.serial
                machine.handle(.headsetAppeared(serial: headset.serial, model: headset.model))
                attemptInstallAndLaunch(serial: headset.serial)
            }
        } else {
            if lastHeadsetSerial != nil {
                lastHeadsetSerial = nil
                machine.handle(.headsetDisappeared)
            }
        }
    }

    private func handle(supervisorState: DaemonSupervisor.State) {
        switch supervisorState {
        case .backoff(_, _, let err):
            machine.handle(.daemonCrashed(err ?? "unknown"))
        default:
            break
        }
    }

    private func attemptInstallAndLaunch(serial: String) {
        guard !inFlight else { return }
        inFlight = true
        queue.async { [weak self] in
            guard let self else { return }
            defer { self.queue.async { self.inFlight = false } }

            let apk: URL?
            if let provided = self.config.apkURL { apk = provided }
            else { apk = try? BundledTools.questApkURL() }

            // Decide install vs skip.
            let alreadyHave = self.adb.isPackageInstalled(BundledTools.questPackage, serial: serial)
            if alreadyHave && !self.config.alwaysReinstall {
                self.machine.handle(.packageAlreadyPresent(serial: serial))
            } else if let apk {
                self.machine.handle(.installStarted(serial: serial))
                do {
                    try self.adb.install(apk: apk, serial: serial)
                    self.machine.handle(.installCompleted(serial: serial))
                } catch {
                    self.machine.handle(.installFailed(String(describing: error)))
                    return
                }
            } else {
                self.machine.handle(.installFailed("Quest APK not bundled"))
                return
            }

            // Open the USB reverse tunnel before launching so the headset
            // can dial the Mac immediately on startup.
            do {
                try self.adb.removeAllReverses(serial: serial)
                try self.adb.reverse(port: self.config.reverseTunnelPort, serial: serial)
            } catch {
                // Reverse failures aren't fatal in Wi-Fi mode; surface but proceed.
                NSLog("[orchestrator] adb reverse failed: \(error)")
            }

            self.machine.handle(.launchAttempted(serial: serial))
            do {
                try self.adb.launchActivity(package: BundledTools.questPackage,
                                            activity: BundledTools.questActivity,
                                            serial: serial)
            } catch {
                self.machine.handle(.launchFailed(String(describing: error)))
                return
            }
            // We wait for the daemon → control handshake to confirm STREAMING.
        }
    }
}
