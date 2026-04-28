// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Top-level state machine governing the Mac → Quest streaming session.
///
/// States (mirrors the spec, with two transient sub-states for clarity):
///   - `waiting`      : no headset attached. Daemon may or may not be running.
///   - `connected`    : headset attached, deciding whether to install/launch.
///   - `installing`   : pushing APK over adb.
///   - `launching`    : `adb shell am start` issued, waiting for handshake.
///   - `streaming`    : daemon reports an active session with the Quest.
///   - `error(msg)`   : transient error; auto-falls back to `waiting`.
///
/// The machine itself does not perform side effects — it consumes events
/// (`headsetAppeared`, `headsetDisappeared`, `installCompleted`, …) and emits
/// the next state. ``SessionOrchestrator`` is the integrator that wires it
/// to ``DeviceMonitor``, ``AdbController`` and ``DaemonSupervisor``.
public enum SessionState: Equatable, Sendable {
    case waiting
    case connected(serial: String, model: String?)
    case installing(serial: String, progress: Double)   // 0…1, NaN if unknown
    case launching(serial: String)
    case streaming(serial: String, sessionId: UInt64?)
    case error(String)

    public var serial: String? {
        switch self {
        case .connected(let s, _),
             .installing(let s, _),
             .launching(let s),
             .streaming(let s, _):
            return s
        case .waiting, .error: return nil
        }
    }

    public var headline: String {
        switch self {
        case .waiting:                return "Waiting for headset"
        case .connected(_, let m):    return "Connected · \(m ?? "Quest")"
        case .installing:             return "Installing FuVR client…"
        case .launching:              return "Launching on headset…"
        case .streaming:              return "Streaming"
        case .error(let m):           return "Error · \(m)"
        }
    }

    public var symbolName: String {
        switch self {
        case .waiting:    return "antenna.radiowaves.left.and.right.slash"
        case .connected:  return "headphones.circle"
        case .installing: return "arrow.down.circle"
        case .launching:  return "play.circle"
        case .streaming:  return "dot.radiowaves.left.and.right"
        case .error:      return "exclamationmark.triangle.fill"
        }
    }
}

public enum SessionEvent: Sendable {
    case headsetAppeared(serial: String, model: String?)
    case headsetDisappeared
    case packageAlreadyPresent(serial: String)
    case installStarted(serial: String)
    case installProgress(Double)
    case installCompleted(serial: String)
    case installFailed(String)
    case launchAttempted(serial: String)
    case launchFailed(String)
    case streamingHandshake(serial: String, sessionId: UInt64?)
    case streamingEnded
    case daemonCrashed(String)
    case userReset
}

/// Thread-safe — uses an internal lock. All transitions are pure.
public final class SessionMachine: @unchecked Sendable {
    public typealias Listener = @Sendable (SessionState) -> Void

    private let lock = NSLock()
    private var _state: SessionState = .waiting
    private var listeners: [(UUID, Listener)] = []

    public init() {}

    public var state: SessionState {
        lock.lock(); defer { lock.unlock() }
        return _state
    }

    @discardableResult
    public func addListener(_ block: @escaping Listener) -> UUID {
        let id = UUID()
        lock.lock(); listeners.append((id, block))
        let s = _state
        lock.unlock()
        block(s)
        return id
    }

    public func removeListener(_ id: UUID) {
        lock.lock(); defer { lock.unlock() }
        listeners.removeAll(where: { $0.0 == id })
    }

    public func handle(_ event: SessionEvent) {
        let next = Self.transition(from: state, event: event)
        guard next != state else { return }
        lock.lock()
        _state = next
        let copy = listeners
        lock.unlock()
        for (_, l) in copy { l(next) }
    }

    /// Pure transition function — exposed for tests.
    public static func transition(from current: SessionState, event: SessionEvent) -> SessionState {
        // `userReset` and `headsetDisappeared` and `daemonCrashed` are
        // global resets: spec Resilience Rule 2 says we must purge the
        // session and revert to WAITING from any state.
        switch event {
        case .headsetDisappeared, .userReset:
            return .waiting
        case .daemonCrashed(let msg):
            // Daemon is restarted by the supervisor; the user-visible state
            // reverts to .connected if a headset is still attached, else
            // .waiting. Without device knowledge here, fall back to .error
            // which the orchestrator will follow with a fresh appearance.
            if let s = current.serial {
                return .error("daemon crashed: \(msg) (serial \(s))")
            }
            return .waiting
        default: break
        }

        switch (current, event) {
        case (.waiting, .headsetAppeared(let s, let m)):
            return .connected(serial: s, model: m)
        case (.error, .headsetAppeared(let s, let m)):
            return .connected(serial: s, model: m)
        case (.connected(let s, _), .packageAlreadyPresent(let s2)) where s == s2:
            return .launching(serial: s)
        case (.connected(let s, _), .installStarted(let s2)) where s == s2:
            return .installing(serial: s, progress: 0)
        case (.installing(let s, _), .installProgress(let p)):
            return .installing(serial: s, progress: max(0, min(1, p)))
        case (.installing(let s, _), .installCompleted(let s2)) where s == s2:
            return .launching(serial: s)
        case (.installing, .installFailed(let msg)):
            return .error(msg)
        case (.launching(let s), .launchAttempted(let s2)) where s == s2:
            return .launching(serial: s)
        case (.launching, .launchFailed(let msg)):
            return .error(msg)
        case (.launching(let s), .streamingHandshake(let s2, let sess)) where s == s2:
            return .streaming(serial: s, sessionId: sess)
        case (.connected(let s, _), .streamingHandshake(let s2, let sess)) where s == s2:
            return .streaming(serial: s, sessionId: sess)
        case (.streaming, .streamingEnded):
            // Headset still attached: drop back to .connected so we can
            // re-launch. Without serial we just go to waiting.
            if let s = current.serial { return .connected(serial: s, model: nil) }
            return .waiting
        default:
            // Ignore non-applicable events rather than silently swallowing —
            // log via an error if it's surprising; for now keep current state.
            return current
        }
    }
}
