// SPDX-License-Identifier: Apache-2.0
import Foundation
import FuVRControl

/// Result of a single APK install + launch attempt.
public enum APKInstallResult: Sendable {
    case installed
    case alreadyInstalled
    case failed(String)
}

/// Orchestrates `adb install -r` followed by `adb shell am start`.
///
/// Package / activity are hard-wired to the FuVR Quest app. If you need
/// to retarget, change the two constants below.
public final class APKInstaller: @unchecked Sendable {

    public static let packageName  = "com.fuvr.quest"
    public static let activityName = ".MainActivity"
    public static let fullComponent = "\(packageName)/\(packageName)\(activityName)"

    private let runner: ADBRunner

    public init(runner: ADBRunner = .shared) {
        self.runner = runner
    }

    // MARK: - APK path resolution

    /// Returns the path to the bundled APK.
    /// Priority: FuVRControl module bundle → repo build output (dev fallback).
    public static func bundledAPKPath() -> String? {
        // 1. Module bundle (populated by `scripts/install-quest.sh --copy-apk`).
        if let path = FuVRControlBundle.apkPath() { return path }
        // 2. Repo build output — works without running the copy script locally.
        let devPath = repoRoot()
            .appendingPathComponent("quest-app/app/build/outputs/apk/debug/app-debug.apk")
            .path
        if FileManager.default.fileExists(atPath: devPath) { return devPath }
        return nil
    }

    // MARK: - Install + Launch

    /// Checks whether the package is already installed on `serial`.
    public func isInstalled(serial: String) -> Bool {
        let r = runner.run(args: ["-s", serial, "shell",
                                  "pm", "list", "packages", APKInstaller.packageName])
        return r.stdout.contains(APKInstaller.packageName)
    }

    /// Fetches `ro.product.model` from the device.
    public func deviceModel(serial: String) -> String? {
        let r = runner.run(args: ["-s", serial, "shell", "getprop", "ro.product.model"])
        let v = r.stdout.trimmingCharacters(in: .whitespacesAndNewlines)
        return v.isEmpty ? nil : v
    }

    /// Installs the bundled APK (or re-installs with `-r`) on `serial`.
    /// Calls `progress` with values 0.0 → 1.0 from a background thread.
    public func install(
        serial: String,
        progress: @escaping @Sendable (Double) -> Void,
        completion: @escaping @Sendable (APKInstallResult) -> Void
    ) {
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            guard let apk = APKInstaller.bundledAPKPath() else {
                completion(.failed("APK not found in bundle. Build quest-app first."))
                return
            }

            progress(0.1)

            // `adb install -r` handles both fresh install and re-install.
            let result = runner.run(args: ["-s", serial, "install", "-r", apk])
            progress(0.9)

            if result.succeeded && result.stdout.contains("Success") {
                completion(.installed)
            } else if result.stderr.contains("INSTALL_FAILED_ALREADY_EXISTS") ||
                      result.stdout.contains("INSTALL_FAILED_ALREADY_EXISTS") {
                completion(.alreadyInstalled)
            } else {
                let msg = result.stderr.isEmpty ? result.stdout : result.stderr
                completion(.failed(msg.trimmingCharacters(in: .whitespacesAndNewlines).prefix(300).description))
            }
            progress(1.0)
        }
    }

    /// Launches the FuVR Quest activity on `serial`.
    public func launch(serial: String, completion: @escaping @Sendable (Bool, String) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            let r = runner.run(args: [
                "-s", serial,
                "shell", "am", "start",
                "-n", APKInstaller.fullComponent
            ])
            let ok = r.succeeded && !r.stdout.contains("Error")
            let msg = ok ? "Activity started." :
                (r.stderr.isEmpty ? r.stdout : r.stderr).trimmingCharacters(in: .whitespacesAndNewlines)
            completion(ok, msg)
        }
    }

    // MARK: - Helpers

    private static func repoRoot() -> URL {
        var url = Bundle.main.bundleURL
        for _ in 0..<10 {
            if FileManager.default.fileExists(
                atPath: url.appendingPathComponent("quest-app").path) {
                return url
            }
            url.deleteLastPathComponent()
        }
        return Bundle.main.bundleURL
    }
}
