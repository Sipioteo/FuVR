// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Spawns and supervises the `fuvrd` streaming daemon as a child process.
/// Provides crash detection (via `DispatchSourceProcess`) and exponential
/// backoff auto-restart. Resilience Rule 1 from the spec.
///
/// Lifecycle:
///   `start()`  → spawns process, watches PID via signal source.
///   On exit    → emits `.crashed(code, signal)` then schedules `restart`
///                with exponential backoff (250ms → 8s, capped).
///   `stop()`   → SIGTERM, cancels watcher, no restart.
public final class DaemonSupervisor: @unchecked Sendable {
    public enum State: Equatable, Sendable {
        case idle
        case running(pid: Int32, startedAt: Date)
        case backoff(attempt: Int, nextRetry: Date, lastError: String?)
        case stopped
    }

    public typealias Listener = @Sendable (State) -> Void

    public struct Config: Sendable {
        public var executableURL: URL
        public var arguments: [String]
        public var environment: [String: String]
        public var workingDirectory: URL?
        public var captureLogs: Bool
        public var maxBackoff: TimeInterval
        public var initialBackoff: TimeInterval

        public init(executableURL: URL,
                    arguments: [String] = [],
                    environment: [String: String] = [:],
                    workingDirectory: URL? = nil,
                    captureLogs: Bool = true,
                    initialBackoff: TimeInterval = 0.25,
                    maxBackoff: TimeInterval = 8.0) {
            self.executableURL = executableURL
            self.arguments = arguments
            self.environment = environment
            self.workingDirectory = workingDirectory
            self.captureLogs = captureLogs
            self.initialBackoff = initialBackoff
            self.maxBackoff = maxBackoff
        }
    }

    private let config: Config
    private let queue = DispatchQueue(label: "com.fuvr.supervisor")
    private let lock = NSLock()
    private var state: State = .idle
    private var listeners: [(UUID, Listener)] = []
    private var process: Process?
    private var watcher: DispatchSourceProcess?
    private var attempt: Int = 0
    private var stopRequested: Bool = false
    /// Last 200 lines emitted by the daemon, for the diagnostics tab.
    private var logBuffer: [String] = []
    private let logBufferCap = 200

    public init(config: Config) { self.config = config }

    public var currentState: State {
        lock.lock(); defer { lock.unlock() }
        return state
    }

    public var recentLogs: [String] {
        lock.lock(); defer { lock.unlock() }
        return logBuffer
    }

    @discardableResult
    public func addListener(_ block: @escaping Listener) -> UUID {
        let id = UUID()
        lock.lock(); listeners.append((id, block))
        let s = state
        lock.unlock()
        block(s)
        return id
    }

    public func removeListener(_ id: UUID) {
        lock.lock(); defer { lock.unlock() }
        listeners.removeAll(where: { $0.0 == id })
    }

    public func start() {
        queue.async { [weak self] in
            guard let self else { return }
            self.lock.lock()
            self.stopRequested = false
            self.attempt = 0
            self.lock.unlock()
            self.spawn()
        }
    }

    public func stop() {
        queue.async { [weak self] in
            guard let self else { return }
            self.lock.lock()
            self.stopRequested = true
            let p = self.process
            self.lock.unlock()
            self.watcher?.cancel()
            self.watcher = nil
            p?.terminate()
            self.transition(.stopped)
        }
    }

    /// Force a restart without changing the user's intent (e.g. settings change).
    public func cycle() {
        queue.async { [weak self] in
            self?.process?.terminate()
        }
    }

    // MARK: - Private

    private func spawn() {
        let p = Process()
        p.executableURL = config.executableURL
        p.arguments = config.arguments
        p.environment = ProcessInfo.processInfo.environment.merging(config.environment) { _, new in new }
        if let cwd = config.workingDirectory { p.currentDirectoryURL = cwd }

        var outPipe: Pipe?
        var errPipe: Pipe?
        if config.captureLogs {
            outPipe = Pipe(); errPipe = Pipe()
            p.standardOutput = outPipe
            p.standardError = errPipe
            attachLogReader(outPipe!.fileHandleForReading, tag: "stdout")
            attachLogReader(errPipe!.fileHandleForReading, tag: "stderr")
        }

        do {
            try p.run()
        } catch {
            scheduleRetry(reason: "spawn failed: \(error.localizedDescription)")
            return
        }

        lock.lock(); process = p; lock.unlock()
        transition(.running(pid: p.processIdentifier, startedAt: Date()))

        let src = DispatchSource.makeProcessSource(identifier: p.processIdentifier,
                                                   eventMask: .exit,
                                                   queue: queue)
        src.setEventHandler { [weak self, weak p] in
            guard let self, let p else { return }
            p.waitUntilExit()
            let code = p.terminationStatus
            let reason: String
            switch p.terminationReason {
            case .uncaughtSignal: reason = "killed by signal (status=\(code))"
            default:              reason = "exited with code \(code)"
            }
            self.append(log: "[supervisor] daemon \(reason)")
            self.lock.lock()
            self.process = nil
            let stopped = self.stopRequested
            self.lock.unlock()
            self.watcher?.cancel(); self.watcher = nil
            if stopped {
                self.transition(.stopped)
            } else {
                self.scheduleRetry(reason: reason)
            }
        }
        src.resume()
        watcher = src
    }

    private func scheduleRetry(reason: String) {
        let next: TimeInterval
        lock.lock()
        attempt += 1
        next = min(config.initialBackoff * pow(2.0, Double(attempt - 1)), config.maxBackoff)
        lock.unlock()
        let when = Date().addingTimeInterval(next)
        transition(.backoff(attempt: attempt, nextRetry: when, lastError: reason))
        queue.asyncAfter(deadline: .now() + next) { [weak self] in
            guard let self else { return }
            self.lock.lock(); let stopped = self.stopRequested; self.lock.unlock()
            if !stopped { self.spawn() }
        }
    }

    private func transition(_ s: State) {
        lock.lock()
        state = s
        let copy = listeners
        lock.unlock()
        for (_, l) in copy { l(s) }
    }

    private func attachLogReader(_ handle: FileHandle, tag: String) {
        handle.readabilityHandler = { [weak self] h in
            let data = h.availableData
            if data.isEmpty { return }
            guard let str = String(data: data, encoding: .utf8) else { return }
            for line in str.split(whereSeparator: { $0.isNewline }) {
                self?.append(log: "[\(tag)] \(line)")
            }
        }
    }

    private func append(log: String) {
        lock.lock(); defer { lock.unlock() }
        logBuffer.append(log)
        if logBuffer.count > logBufferCap {
            logBuffer.removeFirst(logBuffer.count - logBufferCap)
        }
    }
}
