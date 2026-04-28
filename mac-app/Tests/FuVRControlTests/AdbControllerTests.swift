// SPDX-License-Identifier: Apache-2.0
import XCTest
@testable import FuVRControl

final class AdbControllerTests: XCTestCase {
    func testParseDevicesEmpty() {
        let out = """
        List of devices attached

        """
        XCTAssertEqual(AdbController.parseDevices(out), [])
    }

    func testParseDevicesSingleQuest() {
        let out = """
        List of devices attached
        1WMHHA8XF12345         device usb:1-1 product:hollywood model:Quest_3 device:hollywood transport_id:1
        """
        let devices = AdbController.parseDevices(out)
        XCTAssertEqual(devices.count, 1)
        XCTAssertEqual(devices[0].serial, "1WMHHA8XF12345")
        XCTAssertEqual(devices[0].state, "device")
        XCTAssertEqual(devices[0].model, "Quest_3")
        XCTAssertEqual(devices[0].product, "hollywood")
        XCTAssertTrue(devices[0].isReady)
        XCTAssertFalse(devices[0].isUnauthorized)
    }

    func testParseDevicesUnauthorized() {
        let out = """
        List of devices attached
        ABC123 unauthorized
        """
        let devices = AdbController.parseDevices(out)
        XCTAssertEqual(devices.count, 1)
        XCTAssertTrue(devices[0].isUnauthorized)
        XCTAssertFalse(devices[0].isReady)
    }

    func testParseDevicesMultipleAndDaemonBanner() {
        let out = """
        * daemon not running; starting now at tcp:5037
        * daemon started successfully
        List of devices attached
        emulator-5554 device product:sdk_gphone64_arm64
        AAAA1111 unauthorized
        BBBB2222 device product:hollywood model:Quest_3
        """
        let devices = AdbController.parseDevices(out)
        XCTAssertEqual(devices.count, 3)
        XCTAssertEqual(devices.map(\.serial), ["emulator-5554", "AAAA1111", "BBBB2222"])
        XCTAssertEqual(devices.compactMap(\.model), ["Quest_3"])
    }

    func testParseDevicesIgnoresWhitespace() {
        let out = "\n\n   \nList of devices attached\n   \n"
        XCTAssertEqual(AdbController.parseDevices(out), [])
    }

    func testHostArchIsKnown() {
        let arch = BundledTools.hostArch
        XCTAssertTrue(["arm64", "x86_64"].contains(arch),
                      "Unexpected host arch: \(arch)")
    }
}
