// SPDX-License-Identifier: Apache-2.0
import Foundation

/// A versioned bundle of all FuVR user settings. Replaces the v0/v1
/// per-key `@AppStorage` layout with a single Codable JSON blob persisted
/// under `fuvr.settings.v2`.
///
/// Migration policy:
///   - On launch, `SettingsMigration.load(from:)` looks for `fuvr.settings.v2`.
///     If present, decode and return it.
///   - Otherwise, read each pre-existing `fuvr.*` key, populate a v2 bundle
///     from those values (using defaults for missing keys), persist it under
///     the v2 key, and return it.
///   - Older keys are NOT deleted on migration; they remain readable so a
///     downgrade still works. A future `purgeLegacy(:)` helper can delete
///     them once v2 is ratified.
public struct SettingsBundle: Codable, Equatable, Sendable {
    public static let currentVersion: Int = 2
    public static let storageKey = "fuvr.settings.v2"

    public var version: Int
    public var perEyeWidth: Int
    public var perEyeHeight: Int
    public var refreshRateHz: Int
    public var videoCodec: String
    public var bitrateMbps: Int
    public var audioEnabled: Bool
    public var transportMode: String
    public var wifiHost: String
    public var wifiPort: Int
    public var socketPath: String
    public var useMockDaemon: Bool

    public static let defaults = SettingsBundle(
        version: currentVersion,
        perEyeWidth: 2064,
        perEyeHeight: 2208,
        refreshRateHz: 90,
        videoCodec: VideoCodec.hevc.rawValue,
        bitrateMbps: 150,
        audioEnabled: true,
        transportMode: TransportMode.usb.rawValue,
        wifiHost: "192.168.1.10",
        wifiPort: 9943,
        socketPath: DefaultSocketPath.resolve(),
        useMockDaemon: true
    )
}

public enum SettingsMigration {
    /// Returns the current settings bundle, performing a one-time migration
    /// from legacy `@AppStorage` keys if the v2 blob isn't already present.
    public static func load(from defaults: UserDefaults = .standard) -> SettingsBundle {
        if let data = defaults.data(forKey: SettingsBundle.storageKey),
           let bundle = try? JSONDecoder().decode(SettingsBundle.self, from: data),
           bundle.version >= 2 {
            return bundle
        }
        // No v2 blob yet — gather legacy keys (if any) and build a v2 record.
        var b = SettingsBundle.defaults
        if defaults.object(forKey: SettingsKey.perEyeWidth) != nil {
            b.perEyeWidth = defaults.integer(forKey: SettingsKey.perEyeWidth)
        }
        if defaults.object(forKey: SettingsKey.perEyeHeight) != nil {
            b.perEyeHeight = defaults.integer(forKey: SettingsKey.perEyeHeight)
        }
        if defaults.object(forKey: SettingsKey.refreshRate) != nil {
            b.refreshRateHz = defaults.integer(forKey: SettingsKey.refreshRate)
        }
        if let s = defaults.string(forKey: SettingsKey.codec) { b.videoCodec = s }
        if defaults.object(forKey: SettingsKey.bitrateMbps) != nil {
            b.bitrateMbps = defaults.integer(forKey: SettingsKey.bitrateMbps)
        }
        if defaults.object(forKey: SettingsKey.audioEnabled) != nil {
            b.audioEnabled = defaults.bool(forKey: SettingsKey.audioEnabled)
        }
        if let s = defaults.string(forKey: SettingsKey.transportMode) { b.transportMode = s }
        if let s = defaults.string(forKey: SettingsKey.wifiHost) { b.wifiHost = s }
        if defaults.object(forKey: SettingsKey.wifiPort) != nil {
            b.wifiPort = defaults.integer(forKey: SettingsKey.wifiPort)
        }
        if let s = defaults.string(forKey: SettingsKey.socketPath) { b.socketPath = s }
        if defaults.object(forKey: SettingsKey.useMockDaemon) != nil {
            b.useMockDaemon = defaults.bool(forKey: SettingsKey.useMockDaemon)
        }
        b.version = SettingsBundle.currentVersion
        save(b, to: defaults)
        return b
    }

    public static func save(_ bundle: SettingsBundle, to defaults: UserDefaults = .standard) {
        if let data = try? JSONEncoder().encode(bundle) {
            defaults.set(data, forKey: SettingsBundle.storageKey)
        }
    }

    /// Test-only: clear the v2 blob (does not touch legacy keys).
    public static func resetV2(_ defaults: UserDefaults = .standard) {
        defaults.removeObject(forKey: SettingsBundle.storageKey)
    }
}
