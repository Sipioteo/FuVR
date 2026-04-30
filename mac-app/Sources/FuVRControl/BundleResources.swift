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

    /// Path to the bundled mock `libopenvr_api.dylib` (universal binary).
    /// Returns `nil` when the `openvr_api` CMake target has not yet been
    /// built — its POST_BUILD step stages the dylib here.
    ///
    /// SwiftPM lays out `.copy` resources differently between dev builds
    /// (`<bundle>/openvr/libopenvr_api.dylib`) and shipped .app bundles
    /// (`<bundle>/Contents/Resources/openvr/libopenvr_api.dylib`). We
    /// prefer `Bundle.module.url(forResource:...)` which abstracts the
    /// difference, and fall back to manually probing both layouts.
    public static func openvrShimPath() -> String? {
        let fm = FileManager.default
        if let url = Bundle.module.url(forResource: "libopenvr_api",
                                       withExtension: "dylib",
                                       subdirectory: "openvr") {
            return url.path
        }
        let candidates = [
            Bundle.module.bundleURL.appendingPathComponent("openvr/libopenvr_api.dylib"),
            Bundle.module.bundleURL.appendingPathComponent("Contents/Resources/openvr/libopenvr_api.dylib"),
        ]
        for c in candidates where fm.fileExists(atPath: c.path) { return c.path }
        return nil
    }

    /// URL of the bundled `fuvr-mod.jar` (our forked Vivecraft mod), or `nil`
    /// if the jar has not yet been staged into the package resources.
    /// The build pipeline drops it at `Resources/fuvr-mod.jar`; until then
    /// callers should treat the absence as "no mod install available" rather
    /// than a hard error.
    public static var fuvrModJarURL: URL? {
        if let url = Bundle.module.url(forResource: "fuvr-mod",
                                       withExtension: "jar") {
            return url
        }
        let fm = FileManager.default
        let candidates = [
            Bundle.module.bundleURL.appendingPathComponent("fuvr-mod.jar"),
            Bundle.module.bundleURL.appendingPathComponent("Contents/Resources/fuvr-mod.jar"),
        ]
        for c in candidates where fm.fileExists(atPath: c.path) { return c }
        return nil
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
