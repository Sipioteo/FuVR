// SPDX-License-Identifier: Apache-2.0
import Foundation
import IOKit
import IOKit.usb

/// Detect the presence of a Meta/Oculus USB device via IOKit.
///
/// Meta's USB vendor ID is `0x2833`. We use this rather than `adb devices`
/// because the latter only sees devices that have already authorised the
/// host's RSA key — IOKit sees the cable the moment it's plugged in,
/// which lets us prompt the user to unlock the headset and tap "Allow".
public enum UsbVendorDetector {
    public static let metaVendorID: Int = 0x2833

    public struct Match: Equatable, Sendable {
        public let vendorID: Int
        public let productID: Int
        public let productName: String?
    }

    /// Returns the first matching Meta USB device, or `nil` if none is
    /// connected. Cheap to call (single IOServiceGetMatchingServices pass).
    public static func findMetaDevice() -> Match? {
        // Match dictionary: kIOUSBDeviceClassName + idVendor.
        guard let classMatch = IOServiceMatching(kIOUSBDeviceClassName) else { return nil }
        let dict = classMatch as NSMutableDictionary
        dict["idVendor"] = NSNumber(value: metaVendorID)

        var iter: io_iterator_t = 0
        let kr = IOServiceGetMatchingServices(kIOMainPortDefault, dict, &iter)
        guard kr == KERN_SUCCESS, iter != 0 else { return nil }
        defer { IOObjectRelease(iter) }

        var first: Match?
        while case let svc = IOIteratorNext(iter), svc != 0 {
            if first == nil {
                let pidNum = (IORegistryEntryCreateCFProperty(
                    svc, "idProduct" as CFString, kCFAllocatorDefault, 0
                )?.takeRetainedValue() as? NSNumber)?.intValue ?? 0
                let name = IORegistryEntryCreateCFProperty(
                    svc, "USB Product Name" as CFString, kCFAllocatorDefault, 0
                )?.takeRetainedValue() as? String
                first = Match(vendorID: metaVendorID, productID: pidNum, productName: name)
            }
            IOObjectRelease(svc)
        }
        return first
    }
}
