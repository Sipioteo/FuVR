// SPDX-License-Identifier: Apache-2.0
import Foundation
import FuVRControl

/// Wraps `Process()` to run `adb` commands.
///
/// Binary resolution order:
///   1. Bundled binary from FuVRControl module bundle (populated by `scripts/fetch-adb.sh`)
///   2. System PATH (Homebrew / Android Studio install)
///   3. Well-known Homebrew paths
public final class ADBRunner: Sendable {

    /// Shared instance. Safe to call from any thread.
    public static let shared = ADBRunner()

    private let adbPath: String

    public init() {
        // 1. Bundled binary.
        if let bundled = FuVRControlBundle.adbPath() {
            self.adbPath = bundled
            return
        }

        // 2. System PATH.
        let paths = ProcessInfo.processInfo.environment["PATH"] ?? ""
        for dir in paths.split(separator: ":") {
            let candidate = "\(dir)/adb"
            if FileManager.default.isExecutableFile(atPath: candidate) {
                self.adbPath = candidate
                return
            }
        }

        // 3. Well-known Homebrew locations.
        for path in ["/opt/homebrew/bin/adb", "/usr/local/bin/adb"] {
            if FileManager.default.isExecutableFile(atPath: path) {
                self.adbPath = path
                return
            }
        }

        self.adbPath = "adb"  // will produce a clear ProcessError on first use
    }

    // MARK: - API

    public struct RunResult: Sendable {
        public let exitCode: Int32
        public let stdout: String
        public let stderr: String
        public var succeeded: Bool { exitCode == 0 }
    }

    /// Run `adb [args…]` synchronously. Must be called off the main thread.
    public func run(_ args: String...) -> RunResult { run(args: args) }

    public func run(args: [String]) -> RunResult {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: adbPath)
        process.arguments = args
        let outPipe = Pipe(); let errPipe = Pipe()
        process.standardOutput = outPipe
        process.standardError = errPipe
        do {
            try process.run()
        } catch {
            return RunResult(exitCode: -1, stdout: "",
                             stderr: "adb launch failed (\(adbPath)): \(error.localizedDescription)")
        }
        process.waitUntilExit()
        let out = String(data: outPipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        let err = String(data: errPipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        return RunResult(exitCode: process.terminationStatus, stdout: out, stderr: err)
    }

    /// Asynchronous variant — delivers result on `queue` (default main).
    public func runAsync(
        args: [String],
        queue: DispatchQueue = .main,
        completion: @escaping @Sendable (RunResult) -> Void
    ) {
        DispatchQueue.global(qos: .utility).async { [self] in
            let r = run(args: args)
            queue.async { completion(r) }
        }
    }

    // MARK: - Info

    public var resolvedPath: String { adbPath }
}
