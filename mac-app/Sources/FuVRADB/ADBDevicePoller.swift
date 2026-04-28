// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Known VR/Android XR headset USB vendor IDs (decimal).
/// adb reports them in `adb devices -l` as `usb:...` or in `getprop`.
private let knownVRVendors: Set<String> = [
    "2833",  // Oculus / Meta
    "0fce",  // Sony (PSVR2 companion)
    "04e8",  // Samsung
    "2d95",  // XREAL / Nreal
    "1ebf",  // Pico
]

/// A connected Android device, possibly a VR headset.
public struct ADBDevice: Equatable, Sendable {
    public let serial: String
    public let state: String         // "device", "offline", "unauthorized"
    public let model: String?        // from `adb -s X shell getprop ro.product.model`
    public let isLikelyVR: Bool

    public var isReady: Bool { state == "device" }
}

/// Delegate receives state changes on an arbitrary background queue.
/// Implementations must dispatch to Main themselves if they need to
/// update UI.
public protocol ADBDevicePollerDelegate: AnyObject, Sendable {
    func poller(_ poller: ADBDevicePoller, didUpdateDevices devices: [ADBDevice])
}

/// Polls `adb devices` every `interval` seconds and reports changes.
public final class ADBDevicePoller: @unchecked Sendable {

    public weak var delegate: ADBDevicePollerDelegate?
    public let interval: TimeInterval
    private let runner: ADBRunner
    private var timer: DispatchSourceTimer?
    private let queue = DispatchQueue(label: "fuvr.adb.poller", qos: .utility)
    private var lastDevices: [ADBDevice] = []

    public init(runner: ADBRunner = .shared, interval: TimeInterval = 2.0) {
        self.runner = runner
        self.interval = interval
    }

    // MARK: - Lifecycle

    public func start() {
        stop()
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now(), repeating: interval)
        t.setEventHandler { [weak self] in self?.poll() }
        t.resume()
        timer = t
    }

    public func stop() {
        timer?.cancel()
        timer = nil
    }

    // MARK: - Internals

    private func poll() {
        let result = runner.run("devices", "-l")
        FileHandle.standardError.write(Data("[FuVR.poller] adb=\(runner.resolvedPath) exit=\(result.exitCode) stdout=\(result.stdout.prefix(400)) stderr=\(result.stderr.prefix(200))\n".utf8))
        guard result.succeeded || result.exitCode == 0 else { return }
        let devices = Self.parse(result.stdout)
        FileHandle.standardError.write(Data("[FuVR.poller] parsed \(devices.count) device(s): \(devices.map { "\($0.serial)/\($0.state)/ready=\($0.isReady)" }.joined(separator: ", "))\n".utf8))
        if devices != lastDevices {
            lastDevices = devices
            delegate?.poller(self, didUpdateDevices: devices)
        }
    }

    /// Parse `adb devices -l` output.
    static func parse(_ output: String) -> [ADBDevice] {
        var devices: [ADBDevice] = []
        let lines = output.components(separatedBy: .newlines)
        for line in lines {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            guard !trimmed.isEmpty,
                  !trimmed.hasPrefix("List of devices"),
                  !trimmed.hasPrefix("*") else { continue }

            // Format: `<serial><whitespace><state>[<whitespace><k:v>…]`
            // adb separates fields with spaces (not tabs), so split on
            // any run of whitespace and take the first two tokens.
            let tokens = trimmed.split(whereSeparator: { $0 == " " || $0 == "\t" })
                                .map(String.init)
            guard tokens.count >= 2 else { continue }
            let serial = tokens[0]
            let state  = tokens[1]
            let rest   = tokens.dropFirst(2).joined(separator: " ")

            // Heuristic: USB serials with Meta/Oculus look like "1WMHH…" (14 hex chars)
            // or "2833XXXXXXXX". Wi-Fi ADB serials end in ":5555".
            let isLikelyVR = Self.looksLikeVRDevice(serial: serial, qualifiers: rest)

            devices.append(ADBDevice(
                serial: serial,
                state: state,
                model: nil,          // populated lazily by APKInstaller
                isLikelyVR: isLikelyVR
            ))
        }
        return devices
    }

    private static func looksLikeVRDevice(serial: String, qualifiers: String) -> Bool {
        // Meta Quest USB serials are 14-char alphanumeric starting with numbers.
        // Also match `product:eureka` / `model:Quest` in qualifiers.
        let lower = qualifiers.lowercased()
        if lower.contains("quest") || lower.contains("oculus") ||
           lower.contains("pico") || lower.contains("xreal") ||
           lower.contains("nreal") { return true }
        // Fallback: any device that passes (≥8 hex-ish chars serial or ends :5555).
        return serial.count >= 8
    }
}
