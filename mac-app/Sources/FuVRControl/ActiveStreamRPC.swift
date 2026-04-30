// SPDX-License-Identifier: Apache-2.0
import Foundation

extension ControlClient {

    /// Fetch a snapshot of the daemon's currently bound OpenVR application.
    ///
    /// TODO(daemon-rpc): wire to the real `GetActiveStream` RPC once the
    /// Cap'n Proto schema in `proto/fuvrd.capnp` is regenerated with that
    /// method id. The wire reply layout is tracked in
    /// `ActiveStream.swift`.
    ///
    /// Until then, this method honours the `FUVR_FAKE_ACTIVE_STREAM` env var:
    ///   - `1` / `true` → returns a synthetic Vivecraft stream so the Stream
    ///     tab can be visually iterated on without daemon support.
    ///   - unset / `0`  → returns `ActiveStream(connected: false, …)`.
    public func getActiveStream() async throws -> ActiveStream {
        if Self.fakeStreamEnabled {
            // Animate the fake FPS / frame counter a tiny bit so the UI
            // doesn't look frozen during development.
            let t = Date().timeIntervalSince1970
            let fps = Float(89.0 + sin(t * 1.7) * 0.6)
            let frames = UInt64(t.truncatingRemainder(dividingBy: 1_000_000) * 90)
            return ActiveStream.from(
                connected: true,
                appKey: "org.jrbudda.vivecraft.steamvrinput",
                perEyeWidth: 2064,
                perEyeHeight: 2208,
                refreshRateHz: 90,
                currentFps: fps,
                framesSubmitted: frames
            )
        }
        return ActiveStream.from(
            connected: false,
            appKey: "",
            perEyeWidth: 0,
            perEyeHeight: 0,
            refreshRateHz: 0,
            currentFps: 0,
            framesSubmitted: 0
        )
    }

    private static var fakeStreamEnabled: Bool {
        guard let v = ProcessInfo.processInfo.environment["FUVR_FAKE_ACTIVE_STREAM"] else {
            return false
        }
        return v == "1" || v.lowercased() == "true" || v.lowercased() == "yes"
    }
}
