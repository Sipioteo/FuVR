// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Snapshot of the current OpenVR application bound to the daemon.
/// Mirrors the wire payload of the daemon's `GetActiveStream` RPC:
///
///   bool     connected
///   uint32_t perEyeWidth, perEyeHeight, refreshRateHz
///   float    currentFps
///   uint64_t framesSubmitted
///   char     appKey[128]
///
/// `displayName` is derived host-side from `appKey` via `AppKeyRegistry`.
public struct ActiveStream: Equatable, Sendable {
    public var connected: Bool
    public var appKey: String
    public var displayName: String
    public var perEyeWidth: UInt32
    public var perEyeHeight: UInt32
    public var refreshRateHz: UInt32
    public var currentFps: Float
    public var framesSubmitted: UInt64

    public init(connected: Bool,
                appKey: String,
                displayName: String,
                perEyeWidth: UInt32,
                perEyeHeight: UInt32,
                refreshRateHz: UInt32,
                currentFps: Float,
                framesSubmitted: UInt64) {
        self.connected = connected
        self.appKey = appKey
        self.displayName = displayName
        self.perEyeWidth = perEyeWidth
        self.perEyeHeight = perEyeHeight
        self.refreshRateHz = refreshRateHz
        self.currentFps = currentFps
        self.framesSubmitted = framesSubmitted
    }

    /// Builds a populated value from the raw wire fields, looking up the
    /// human-friendly display name from the app-key registry.
    public static func from(connected: Bool,
                            appKey: String,
                            perEyeWidth: UInt32,
                            perEyeHeight: UInt32,
                            refreshRateHz: UInt32,
                            currentFps: Float,
                            framesSubmitted: UInt64) -> ActiveStream {
        ActiveStream(
            connected: connected,
            appKey: appKey,
            displayName: AppKeyRegistry.displayName(for: appKey),
            perEyeWidth: perEyeWidth,
            perEyeHeight: perEyeHeight,
            refreshRateHz: refreshRateHz,
            currentFps: currentFps,
            framesSubmitted: framesSubmitted
        )
    }
}
