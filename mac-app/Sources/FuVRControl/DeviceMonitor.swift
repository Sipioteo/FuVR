// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Polls `adb devices` on a background queue and publishes the currently
/// attached headsets. Detection is purely heuristic: the Quest 2/3/Pro
/// expose model strings that start with "Quest"; we also accept any device
/// whose model contains "Quest" (case-insensitive). Non-headset Android
/// devices are still surfaced — the UI presents them so the user can pick.
public final class DeviceMonitor: @unchecked Sendable {
    public struct Snapshot: Sendable, Equatable {
        public let devices: [AdbController.Device]
        public let timestamp: Date
        public let error: String?

        public var headsets: [AdbController.Device] {
            devices.filter { Self.looksLikeHeadset($0) }
        }
        public var firstReadyHeadset: AdbController.Device? {
            headsets.first(where: { $0.isReady })
        }
        public var hasUnauthorized: Bool {
            devices.contains(where: { $0.isUnauthorized })
        }

        public static func looksLikeHeadset(_ d: AdbController.Device) -> Bool {
            let model = d.model?.lowercased() ?? ""
            let product = d.product?.lowercased() ?? ""
            // Meta Quest: model strings include "Quest_3", "Quest_Pro", etc.
            // Some firmware reports product "hollywood" (Quest 3), "eureka" (Q3S),
            // "seacliff" (Quest Pro), "hollywood1" (Q3 dev). Be permissive.
            return model.contains("quest")
                || product.contains("hollywood")
                || product.contains("eureka")
                || product.contains("seacliff")
                || product.contains("monterey")  // Quest 2
        }
    }

    public typealias Listener = @Sendable (Snapshot) -> Void

    private let adb: AdbController
    private let interval: TimeInterval
    private let queue = DispatchQueue(label: "com.fuvr.devicemonitor", qos: .utility)
    private let lock = NSLock()
    private var listeners: [(UUID, Listener)] = []
    private var timer: DispatchSourceTimer?
    private var lastSnapshot: Snapshot?

    public init(adb: AdbController, pollInterval: TimeInterval = 1.5) {
        self.adb = adb
        self.interval = pollInterval
    }

    deinit { stop() }

    /// Attach a listener. Returns a token to remove it later.
    @discardableResult
    public func addListener(_ block: @escaping Listener) -> UUID {
        let id = UUID()
        lock.lock(); listeners.append((id, block))
        let snap = lastSnapshot
        lock.unlock()
        if let snap { block(snap) }
        return id
    }

    public func removeListener(_ id: UUID) {
        lock.lock(); defer { lock.unlock() }
        listeners.removeAll(where: { $0.0 == id })
    }

    public func start() {
        queue.async { [weak self] in self?._start() }
    }

    private func _start() {
        if timer != nil { return }
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now(), repeating: interval)
        t.setEventHandler { [weak self] in self?.tick() }
        timer = t
        t.resume()
    }

    public func stop() {
        queue.sync {
            timer?.cancel()
            timer = nil
        }
    }

    /// Force a poll immediately (e.g. after the user clicks "Refresh").
    public func pokeNow() {
        queue.async { [weak self] in self?.tick() }
    }

    private func tick() {
        let snap: Snapshot
        do {
            let devices = try adb.listDevices()
            snap = Snapshot(devices: devices, timestamp: Date(), error: nil)
        } catch {
            snap = Snapshot(devices: [], timestamp: Date(), error: String(describing: error))
        }
        lock.lock()
        let changed = lastSnapshot != snap
        lastSnapshot = snap
        let copy = listeners
        lock.unlock()
        if changed {
            for (_, l) in copy { l(snap) }
        }
    }
}
