// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Maps OpenVR application keys (the `<appkey>` value advertised by SteamVR
/// applications via `IVRApplications`) to user-facing display names and icon
/// hints. This keeps the daemon's RPC payload small (just the appKey string)
/// while letting the GUI render something recognizable.
///
/// Easy to extend: add a new `case` entry. Don't ship third-party logos —
/// stick to SF Symbols or first-party assets we can legally bundle.
public enum AppKeyRegistry {

    public static func displayName(for appKey: String) -> String {
        switch appKey {
        case "org.jrbudda.vivecraft.steamvrinput":
            return "Minecraft (Vivecraft)"
        default:
            return appKey.isEmpty ? "Unknown" : appKey
        }
    }

    /// Returns an SF Symbol name that approximates the app's identity.
    /// We deliberately avoid bundling third-party logos (Mojang, Valve, etc.)
    /// — `cube.fill` is the placeholder for blocky/voxel apps. Update the
    /// switch as more apps get first-class recognition.
    public static func iconSymbolName(for appKey: String) -> String {
        switch appKey {
        case "org.jrbudda.vivecraft.steamvrinput":
            return "cube.fill"
        default:
            return "visionpro"
        }
    }

    /// Optional bundled image asset name (resolved via `Bundle.main` in the
    /// app target). Returns nil when no bundled image exists and the SF
    /// Symbol fallback should be used. Currently always nil — we ship no
    /// proprietary art — but the hook is here for future first-party icons.
    public static func bundledImageName(for appKey: String) -> String? {
        return nil
    }
}
