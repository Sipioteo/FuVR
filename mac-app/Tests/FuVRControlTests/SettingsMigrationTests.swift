// SPDX-License-Identifier: Apache-2.0
import XCTest
@testable import FuVRControl

final class SettingsMigrationTests: XCTestCase {

    private func freshDefaults() -> UserDefaults {
        let suite = "fuvr.test.\(UUID().uuidString)"
        let d = UserDefaults(suiteName: suite)!
        d.removePersistentDomain(forName: suite)
        return d
    }

    func testLoadDefaultsWhenEmpty() {
        let d = freshDefaults()
        let b = SettingsMigration.load(from: d)
        XCTAssertEqual(b.version, SettingsBundle.currentVersion)
        XCTAssertEqual(b.perEyeWidth, SettingsBundle.defaults.perEyeWidth)
        // Persisted to v2 key after first load.
        XCTAssertNotNil(d.data(forKey: SettingsBundle.storageKey))
    }

    func testMigratesLegacyKeys() {
        let d = freshDefaults()
        d.set(3072, forKey: SettingsKey.perEyeWidth)
        d.set(3216, forKey: SettingsKey.perEyeHeight)
        d.set(120, forKey: SettingsKey.refreshRate)
        d.set("h264", forKey: SettingsKey.codec)
        d.set(220, forKey: SettingsKey.bitrateMbps)
        d.set(false, forKey: SettingsKey.audioEnabled)
        d.set("wifi", forKey: SettingsKey.transportMode)
        d.set("10.0.0.5", forKey: SettingsKey.wifiHost)
        d.set(9999, forKey: SettingsKey.wifiPort)
        d.set("/tmp/sock", forKey: SettingsKey.socketPath)
        d.set(false, forKey: SettingsKey.useMockDaemon)

        let b = SettingsMigration.load(from: d)
        XCTAssertEqual(b.perEyeWidth, 3072)
        XCTAssertEqual(b.perEyeHeight, 3216)
        XCTAssertEqual(b.refreshRateHz, 120)
        XCTAssertEqual(b.videoCodec, "h264")
        XCTAssertEqual(b.bitrateMbps, 220)
        XCTAssertEqual(b.audioEnabled, false)
        XCTAssertEqual(b.transportMode, "wifi")
        XCTAssertEqual(b.wifiHost, "10.0.0.5")
        XCTAssertEqual(b.wifiPort, 9999)
        XCTAssertEqual(b.socketPath, "/tmp/sock")
        XCTAssertEqual(b.useMockDaemon, false)
        XCTAssertEqual(b.version, SettingsBundle.currentVersion)
    }

    func testReturnsExistingV2BlobAsIs() {
        let d = freshDefaults()
        var seed = SettingsBundle.defaults
        seed.bitrateMbps = 333
        seed.audioEnabled = false
        SettingsMigration.save(seed, to: d)

        let b = SettingsMigration.load(from: d)
        XCTAssertEqual(b.bitrateMbps, 333)
        XCTAssertEqual(b.audioEnabled, false)
    }

    func testRoundTripPreservesAllFields() throws {
        let d = freshDefaults()
        var seed = SettingsBundle.defaults
        seed.wifiHost = "192.168.42.7"
        seed.useMockDaemon = false
        SettingsMigration.save(seed, to: d)
        let loaded = SettingsMigration.load(from: d)
        XCTAssertEqual(loaded, seed)
    }
}
