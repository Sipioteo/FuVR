// SPDX-License-Identifier: Apache-2.0
import Foundation
import Darwin

/// Watches `getifaddrs(3)` for a host-side IPv4 address inside Android's
/// USB-tethering subnet `192.168.42.0/24`. When the user toggles USB
/// Tethering inside the Quest, the Mac receives an `enX` interface assigned
/// an address in this range; the Quest sits at `192.168.42.129`.
///
/// This is the gate that unlocks the "Start VR" button: until an interface
/// in the subnet is up, the daemon's UDP-RNDIS transport will refuse to bind.
public final class RndisInterfaceMonitor: @unchecked Sendable {
    public struct Snapshot: Equatable, Sendable {
        public let interfaceName: String
        public let ipv4: String
    }

    private let pollInterval: TimeInterval
    private let queue = DispatchQueue(label: "com.fuvr.rndis-monitor", qos: .userInitiated)
    private var timer: DispatchSourceTimer?
    private var lastSnapshot: Snapshot?

    /// Called on the main thread when the host-side tether interface
    /// appears, disappears, or changes IP.
    public var onChange: (@Sendable (Snapshot?) -> Void)?

    public init(pollInterval: TimeInterval = 0.75) {
        self.pollInterval = pollInterval
    }

    public func start() {
        stop()
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now(), repeating: pollInterval, leeway: .milliseconds(100))
        t.setEventHandler { [weak self] in self?.tick() }
        timer = t
        t.resume()
    }

    public func stop() {
        timer?.cancel()
        timer = nil
    }

    /// One-shot lookup. Returns `nil` if no interface in the tether subnet.
    public static func currentSnapshot() -> Snapshot? {
        Self.scan()
    }

    private func tick() {
        let snap = Self.scan()
        if snap != lastSnapshot {
            lastSnapshot = snap
            DispatchQueue.main.async { [snap] in self.onChange?(snap) }
        }
    }

    private static func scan() -> Snapshot? {
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0, let first = head else { return nil }
        defer { freeifaddrs(head) }

        var ptr: UnsafeMutablePointer<ifaddrs>? = first
        while let p = ptr {
            defer { ptr = p.pointee.ifa_next }
            guard let addr = p.pointee.ifa_addr else { continue }
            if addr.pointee.sa_family != UInt8(AF_INET) { continue }

            // Skip loopback and inactive interfaces.
            let flags = Int32(p.pointee.ifa_flags)
            if (flags & IFF_LOOPBACK) != 0 { continue }
            if (flags & IFF_UP) == 0 { continue }

            let sin = addr.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { $0.pointee }
            let raw = UInt32(bigEndian: sin.sin_addr.s_addr)
            let octets = [
                UInt8((raw >> 24) & 0xff),
                UInt8((raw >> 16) & 0xff),
                UInt8((raw >>  8) & 0xff),
                UInt8( raw        & 0xff),
            ]
            if octets[0] == 192 && octets[1] == 168 && octets[2] == 42 {
                let name = String(cString: p.pointee.ifa_name)
                let ip = "\(octets[0]).\(octets[1]).\(octets[2]).\(octets[3])"
                return Snapshot(interfaceName: name, ipv4: ip)
            }
        }
        return nil
    }
}
