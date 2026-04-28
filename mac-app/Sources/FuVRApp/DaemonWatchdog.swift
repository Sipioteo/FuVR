// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Monitors the `fuvrd` daemon process and auto-restarts it if it exits.
///
/// The watchdog is intentionally lightweight: it observes an existing
/// `Process` via `DispatchSourceProcess`. On exit it waits 500 ms
/// (to avoid tight-spin on repeated crash) then restarts.
@MainActor
final class DaemonWatchdog {

    /// Called whenever the daemon is (re)started. Receives the new PID.
    var onRestart: ((pid_t) -> Void)?
    /// Called when the watchdog gives up after `maxRestarts` consecutive failures.
    var onGaveUp: (() -> Void)?

    private let daemonURL: URL
    private let socketPath: String
    private let maxRestarts: Int
    private var restartCount: Int = 0
    private var currentProcess: Process?
    private var source: DispatchSourceProcess?
    private let queue = DispatchQueue(label: "fuvr.daemon.watchdog", qos: .utility)

    init(daemonURL: URL, socketPath: String, maxRestarts: Int = 10) {
        self.daemonURL = daemonURL
        self.socketPath = socketPath
        self.maxRestarts = maxRestarts
    }

    // MARK: - Public

    func start() {
        restartCount = 0
        launch()
    }

    func stop() {
        source?.cancel()
        source = nil
        currentProcess?.terminate()
        currentProcess = nil
    }

    // MARK: - Internal

    private func launch() {
        let process = Process()
        process.executableURL = daemonURL
        process.arguments = ["--socket", socketPath]

        do {
            try process.run()
        } catch {
            scheduleRestart()
            return
        }

        currentProcess = process

        let src = DispatchSource.makeProcessSource(
            identifier: process.processIdentifier,
            eventMask: .exit,
            queue: queue
        )
        src.setEventHandler { [weak self] in
            guard let self else { return }
            src.cancel()
            self.scheduleRestart()
        }
        src.resume()
        source = src

        let pid = process.processIdentifier
        DispatchQueue.main.async { [weak self] in
            self?.onRestart?(pid)
        }
    }

    private func scheduleRestart() {
        restartCount += 1
        guard restartCount <= maxRestarts else {
            DispatchQueue.main.async { [weak self] in self?.onGaveUp?() }
            return
        }
        queue.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            DispatchQueue.main.async { self?.launch() }
        }
    }

    // MARK: - Daemon location helper

    /// Resolves the bundled `fuvrd` binary or falls back to the build output.
    static func resolveDaemonURL() -> URL? {
        if let path = Bundle.main.path(forAuxiliaryExecutable: "fuvrd") {
            return URL(fileURLWithPath: path)
        }
        // Development: walk up from bundle to find build/daemon/fuvrd.
        var url = Bundle.main.bundleURL
        for _ in 0..<10 {
            let candidate = url
                .appendingPathComponent("build/daemon/fuvrd")
            if FileManager.default.fileExists(atPath: candidate.path) {
                return candidate
            }
            url.deleteLastPathComponent()
        }
        return nil
    }
}
