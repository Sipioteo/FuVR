// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Exposes paths to resources bundled inside the FuVRControl module bundle.
///
/// Resources are placed by `scripts/fetch-adb.sh` and `scripts/install-quest.sh`:
///   adb/arm64/adb       ← Apple Silicon platform-tools binary
///   adb/x86_64/adb      ← Intel platform-tools binary
///   quest/fuvr-quest.apk
public enum FuVRControlBundle {

    /// Path to the `adb` binary for the current host architecture, or `nil`
    /// if the binary has not been staged yet (run `scripts/fetch-adb.sh`).
    public static func adbPath() -> String? {
        let arch = currentArch()
        let candidate = Bundle.module.bundleURL
            .appendingPathComponent("Contents/Resources/adb/\(arch)/adb")
            .path
        guard FileManager.default.fileExists(atPath: candidate) else { return nil }
        // Ensure it's executable (gets stripped by git or unzip occasionally).
        if !FileManager.default.isExecutableFile(atPath: candidate) {
            try? FileManager.default.setAttributes(
                [.posixPermissions: 0o755], ofItemAtPath: candidate)
        }
        return candidate
    }

    /// Path to the bundled Quest APK, or `nil` if not yet staged.
    public static func apkPath() -> String? {
        let candidate = Bundle.module.bundleURL
            .appendingPathComponent("Contents/Resources/quest/fuvr-quest.apk")
            .path
        return FileManager.default.fileExists(atPath: candidate) ? candidate : nil
    }

    private static func currentArch() -> String {
        var sysinfo = utsname()
        uname(&sysinfo)
        return withUnsafePointer(to: &sysinfo.machine) {
            $0.withMemoryRebound(to: CChar.self, capacity: Int(_SYS_NAMELEN)) {
                String(cString: $0)
            }
        }.contains("arm") ? "arm64" : "x86_64"
    }
}
