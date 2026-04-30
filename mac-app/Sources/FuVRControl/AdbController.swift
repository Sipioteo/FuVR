// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Thin wrapper over the bundled `adb` binary. Each method spawns a single
/// `Process()`, captures stdout/stderr, and returns either parsed output or a
/// structured error. Methods are blocking; callers should run them off the
/// main actor (use `async` wrappers below).
///
/// The controller intentionally does **not** keep `adb server` state: it
/// shells out for every call. `adb` will start its own server on the first
/// invocation and reuse it across calls. Killing the server is offered for
/// clean-shutdown via ``killServer()``.
public final class AdbController: @unchecked Sendable {
    public struct Device: Equatable, Sendable {
        public let serial: String
        public let state: String        // "device", "unauthorized", "offline", …
        public let model: String?       // from `adb devices -l` (may be nil)
        public let product: String?

        public var isReady: Bool { state == "device" }
        public var isUnauthorized: Bool { state == "unauthorized" }

        public init(serial: String, state: String, model: String? = nil, product: String? = nil) {
            self.serial = serial; self.state = state; self.model = model; self.product = product
        }
    }

    public enum AdbError: Error, CustomStringConvertible {
        case binaryUnavailable(String)
        case nonZeroExit(code: Int32, stderr: String)
        case timeout(seconds: Double)

        public var description: String {
            switch self {
            case .binaryUnavailable(let s): return "adb unavailable: \(s)"
            case .nonZeroExit(let c, let e): return "adb exited \(c): \(e.trimmingCharacters(in: .whitespacesAndNewlines))"
            case .timeout(let s):           return "adb timed out after \(s)s"
            }
        }
    }

    private let adbURL: URL

    /// Initialize from the bundled binary. Throws if adb is missing.
    public init() throws {
        self.adbURL = try BundledTools.adbURL()
    }

    /// Test injection point (unit tests pass a fake binary).
    public init(adbURL: URL) {
        self.adbURL = adbURL
    }

    public var binaryPath: String { adbURL.path }

    // MARK: - Public commands

    /// Run `adb devices -l` and parse the table.
    @discardableResult
    public func listDevices(timeout: TimeInterval = 5) throws -> [Device] {
        let result = try run(["devices", "-l"], timeout: timeout)
        return Self.parseDevices(result.stdout)
    }

    /// Install (or reinstall) an APK on the given device.
    public func install(apk: URL, serial: String, timeout: TimeInterval = 120) throws {
        _ = try run(["-s", serial, "install", "-r", "-g", apk.path], timeout: timeout)
    }

    /// `adb shell am start -n package/activity` on the target device.
    public func launchActivity(package: String, activity: String, serial: String, timeout: TimeInterval = 10) throws {
        let component = "\(package)/\(activity)"
        _ = try run(["-s", serial, "shell", "am", "start", "-n", component], timeout: timeout)
    }

    /// `adb shell am force-stop <package>`. Used when ending a session cleanly.
    public func forceStop(package: String, serial: String, timeout: TimeInterval = 5) throws {
        _ = try run(["-s", serial, "shell", "am", "force-stop", package], timeout: timeout)
    }

    /// `adb reverse tcp:<port> tcp:<port>` — exposes a Mac-side TCP port to the
    /// headset over USB. Used for the streaming control channel.
    public func reverse(port: Int, serial: String, timeout: TimeInterval = 5) throws {
        _ = try run(["-s", serial, "reverse", "tcp:\(port)", "tcp:\(port)"], timeout: timeout)
    }

    /// Tear down all reverse tunnels for a serial.
    public func removeAllReverses(serial: String, timeout: TimeInterval = 5) throws {
        _ = try run(["-s", serial, "reverse", "--remove-all"], timeout: timeout)
    }

    /// Open the hidden Android Tether settings activity on the Quest. Used
    /// once per connection cycle to land the user on the toggle screen for
    /// USB Tethering — Meta's launcher hides this menu so we have to deep-link.
    public func openTetherSettings(serial: String, timeout: TimeInterval = 5) throws {
        _ = try run([
            "-s", serial,
            "shell", "am", "start",
            "-n", "com.android.settings/.TetherSettings",
        ], timeout: timeout)
    }

    /// Probe whether a package is already installed on a device.
    public func isPackageInstalled(_ package: String, serial: String) -> Bool {
        guard let result = try? run(["-s", serial, "shell", "pm", "list", "packages", package], timeout: 5) else {
            return false
        }
        return result.stdout
            .split(separator: "\n")
            .contains(where: { $0.trimmingCharacters(in: .whitespaces) == "package:\(package)" })
    }

    /// `adb kill-server` — useful on clean shutdown to release USB.
    public func killServer() {
        _ = try? run(["kill-server"], timeout: 5)
    }

    // MARK: - Internals

    @discardableResult
    func run(_ args: [String], timeout: TimeInterval) throws -> (stdout: String, stderr: String) {
        let process = Process()
        process.executableURL = adbURL
        process.arguments = args
        let outPipe = Pipe()
        let errPipe = Pipe()
        process.standardOutput = outPipe
        process.standardError = errPipe
        // Detach from any controlling terminal — adb otherwise tries to print
        // colour escapes that confuse parsing.
        process.environment = (ProcessInfo.processInfo.environment).merging([
            "ADB_TRACE": "",
            "TERM": "dumb",
        ]) { _, new in new }

        try process.run()

        let deadline = Date().addingTimeInterval(timeout)
        while process.isRunning {
            if Date() > deadline {
                process.terminate()
                throw AdbError.timeout(seconds: timeout)
            }
            Thread.sleep(forTimeInterval: 0.05)
        }

        let outData = outPipe.fileHandleForReading.readDataToEndOfFile()
        let errData = errPipe.fileHandleForReading.readDataToEndOfFile()
        let stdout = String(data: outData, encoding: .utf8) ?? ""
        let stderr = String(data: errData, encoding: .utf8) ?? ""

        if process.terminationStatus != 0 {
            throw AdbError.nonZeroExit(code: process.terminationStatus, stderr: stderr.isEmpty ? stdout : stderr)
        }
        return (stdout, stderr)
    }

    /// Parse the output of `adb devices -l`. Public for testability.
    public static func parseDevices(_ output: String) -> [Device] {
        var out: [Device] = []
        for raw in output.split(whereSeparator: { $0.isNewline }) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty { continue }
            // Skip header line `List of devices attached`
            if line.lowercased().hasPrefix("list of devices") { continue }
            // Skip daemon banner lines.
            if line.hasPrefix("*") { continue }
            // Format: <serial><WS><state>[<WS>key:value]...
            let parts = line.split(whereSeparator: { $0 == " " || $0 == "\t" }).map(String.init)
            guard parts.count >= 2 else { continue }
            let serial = parts[0]
            let state = parts[1]
            var model: String?
            var product: String?
            for kv in parts.dropFirst(2) {
                guard let colon = kv.firstIndex(of: ":") else { continue }
                let key = String(kv[..<colon])
                let value = String(kv[kv.index(after: colon)...])
                switch key {
                case "model":   model = value
                case "product": product = value
                default: break
                }
            }
            out.append(Device(serial: serial, state: state, model: model, product: product))
        }
        return out
    }
}
