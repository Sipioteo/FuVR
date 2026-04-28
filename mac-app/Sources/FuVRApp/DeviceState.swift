// SPDX-License-Identifier: Apache-2.0
import Foundation

/// The top-level state machine that drives the FuVR companion app UX.
///
/// Transitions (happy path):
///   waiting → deviceFound → installing → launching → streaming
///
/// Any state can drop back to `waiting` on disconnect or error.
public enum DeviceState: Equatable, Sendable {

    /// No VR headset detected. Polled `adb devices` returns nothing useful.
    case waiting

    /// A headset with the given serial was found via USB ADB.
    /// The app checks whether the APK is already installed.
    case deviceFound(serial: String, model: String)

    /// APK installation in progress. `progress` is 0.0 … 1.0.
    case installing(serial: String, model: String, progress: Double)

    /// APK installed; `adb shell am start` has been issued.
    case launching(serial: String, model: String)

    /// The Quest app has connected back to the streaming daemon.
    case streaming(serial: String, model: String)

    /// Something went wrong. The error string is user-readable.
    case failed(String)

    // MARK: - Convenience

    var serial: String? {
        switch self {
        case .deviceFound(let s, _),
             .installing(let s, _, _),
             .launching(let s, _),
             .streaming(let s, _): return s
        default: return nil
        }
    }

    var model: String? {
        switch self {
        case .deviceFound(_, let m),
             .installing(_, let m, _),
             .launching(_, let m),
             .streaming(_, let m): return m
        default: return nil
        }
    }

    var isActive: Bool {
        switch self {
        case .streaming: return true
        default: return false
        }
    }

    var humanLabel: String {
        switch self {
        case .waiting:               return "Waiting for headset…"
        case .deviceFound(_, let m): return "Found \(m)"
        case .installing(_, let m, _): return "Installing on \(m)…"
        case .launching(_, let m):   return "Launching on \(m)…"
        case .streaming(_, let m):   return "Streaming to \(m)"
        case .failed(let e):         return "Error: \(e)"
        }
    }

    var systemImage: String {
        switch self {
        case .waiting:      return "cable.connector"
        case .deviceFound:  return "checkmark.circle"
        case .installing:   return "arrow.down.circle"
        case .launching:    return "play.circle"
        case .streaming:    return "waveform.path.ecg.rectangle"
        case .failed:       return "exclamationmark.triangle"
        }
    }

    /// Accent colour name for state-specific theming.
    var colorName: String {
        switch self {
        case .waiting:    return "gray"
        case .deviceFound: return "blue"
        case .installing: return "yellow"
        case .launching:  return "orange"
        case .streaming:  return "green"
        case .failed:     return "red"
        }
    }
}
