// SPDX-License-Identifier: Apache-2.0
import Foundation

public enum SettingsKey {
    public static let perEyeWidth     = "fuvr.perEyeWidth"
    public static let perEyeHeight    = "fuvr.perEyeHeight"
    public static let refreshRate     = "fuvr.refreshRateHz"
    public static let codec           = "fuvr.videoCodec"
    public static let bitrateMbps     = "fuvr.bitrateMbps"
    public static let audioEnabled    = "fuvr.audioEnabled"
    public static let transportMode   = "fuvr.transport"
    public static let wifiHost        = "fuvr.wifiHost"
    public static let wifiPort        = "fuvr.wifiPort"
    public static let socketPath      = "fuvr.socketPath"
    public static let useMockDaemon   = "fuvr.useMockDaemon"
}

public enum DefaultSocketPath {
    public static func resolve() -> String {
        if let cache = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first {
            let dir = cache.appendingPathComponent("fuvr", isDirectory: true)
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            return dir.appendingPathComponent("control.sock").path
        }
        return "/tmp/fuvr.sock"
    }
}
