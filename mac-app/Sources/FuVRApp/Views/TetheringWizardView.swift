// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

/// Guides the user through enabling USB Tethering inside the Quest. Meta's
/// launcher hides the standard Android tether menu, so we deep-link the user
/// straight to the activity via `adb shell am start ... TetherSettings` and
/// then watch for an IPv4 address on the host's `192.168.42.0/24` to confirm
/// the toggle landed.
///
/// State machine:
///   .idle         → no Quest seen on USB yet. Spinner.
///   .deviceFound  → Meta USB device present. "Enable High-Speed Link" button.
///   .waitingToggle→ tether intent fired; watching getifaddrs.
///   .linkActive   → `192.168.42.x` assigned to host. Green checkmark, dismiss.
public struct TetheringWizardView: View {
    public enum Phase: Equatable {
        case idle
        case deviceFound(productName: String?)
        case waitingToggle
        case linkActive(interface: String, ipv4: String)
        case error(message: String)
    }

    @State private var phase: Phase = .idle
    @State private var monitor = RndisInterfaceMonitor()
    @State private var deviceTimer: Timer?

    /// Optional ADB serial for the connected Quest. If `nil`, the
    /// "Enable High-Speed Link" button falls back to the first device
    /// `adb devices -l` reports.
    public let serial: String?
    public let onLinkActive: (RndisInterfaceMonitor.Snapshot) -> Void
    public let onDismiss: () -> Void

    public init(
        serial: String?,
        onLinkActive: @escaping (RndisInterfaceMonitor.Snapshot) -> Void,
        onDismiss: @escaping () -> Void = {}
    ) {
        self.serial = serial
        self.onLinkActive = onLinkActive
        self.onDismiss = onDismiss
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                Image(systemName: "cable.connector.horizontal")
                    .font(.title2)
                    .foregroundStyle(.tint)
                Text("High-Speed USB Link").font(.title2).bold()
                Spacer()
                Button("Close") { onDismiss() }.buttonStyle(.plain)
            }

            Divider()

            phaseContent

            Spacer(minLength: 0)
        }
        .padding(24)
        .frame(minWidth: 480, minHeight: 320)
        .onAppear { start() }
        .onDisappear { stop() }
    }

    @ViewBuilder
    private var phaseContent: some View {
        switch phase {
        case .idle:
            row(
                icon: "questmark.circle",
                title: "Waiting for Quest via USB…",
                detail: "Plug the Quest into the Mac with a USB-C cable. Make sure the headset is on and unlocked."
            ) {
                ProgressView().controlSize(.small)
            }

        case .deviceFound(let name):
            row(
                icon: "checkmark.circle.fill",
                tint: .green,
                title: name.map { "Detected: \($0)" } ?? "Quest detected",
                detail: "Tap the button below to open the hidden Android tether menu inside the headset."
            ) {
                Button("Enable High-Speed Link") { fireTetherIntent() }
                    .keyboardShortcut(.return, modifiers: [.command])
                    .buttonStyle(.borderedProminent)
            }

        case .waitingToggle:
            row(
                icon: "eye.fill",
                tint: .orange,
                title: "Look inside your headset",
                detail: """
                A hidden Android menu has appeared. Toggle "USB Tethering" to ON. \
                The Mac will detect the link automatically.
                """
            ) {
                wizardIllustrationStub
                ProgressView().controlSize(.small)
            }

        case .linkActive(let iface, let ip):
            row(
                icon: "bolt.horizontal.circle.fill",
                tint: .green,
                title: "Link Active: UDP 150 Mbps Available",
                detail: "Host: \(ip) on \(iface) · Quest: 192.168.42.129:59000"
            ) {
                Button("Done") { onDismiss() }
                    .buttonStyle(.borderedProminent)
            }

        case .error(let msg):
            row(
                icon: "exclamationmark.triangle.fill",
                tint: .red,
                title: "Couldn't enable the link",
                detail: msg
            ) {
                Button("Retry") { fireTetherIntent() }
            }
        }
    }

    /// Placeholder illustration — replace with a labelled screenshot of the
    /// Quest's TetherSettings activity when assets are ready.
    private var wizardIllustrationStub: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(.thinMaterial)
                .frame(height: 140)
            VStack(spacing: 8) {
                Image(systemName: "visionpro").font(.system(size: 40))
                    .foregroundStyle(.secondary)
                Text("Look for: Settings ▸ Tethering ▸ USB tethering")
                    .font(.callout).foregroundStyle(.secondary)
            }
        }
    }

    private func row<Trailing: View>(
        icon: String,
        tint: Color = .accentColor,
        title: String,
        detail: String,
        @ViewBuilder trailing: () -> Trailing
    ) -> some View {
        HStack(alignment: .top, spacing: 16) {
            Image(systemName: icon)
                .font(.system(size: 28))
                .foregroundStyle(tint)
                .frame(width: 36)
            VStack(alignment: .leading, spacing: 8) {
                Text(title).font(.headline)
                Text(detail).font(.callout).foregroundStyle(.secondary)
                trailing()
            }
            Spacer(minLength: 0)
        }
    }

    // MARK: - Lifecycle

    private func start() {
        // 1. Watch for the Quest USB device (cheap IOKit poll, 1 Hz).
        deviceTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in
            tickDevicePresence()
        }
        tickDevicePresence()

        // 2. Watch for the host's IP assignment on 192.168.42.0/24.
        monitor.onChange = { [self] snap in
            Task { @MainActor in
                if let s = snap {
                    phase = .linkActive(interface: s.interfaceName, ipv4: s.ipv4)
                    onLinkActive(s)
                } else if case .linkActive = phase {
                    phase = .waitingToggle
                }
            }
        }
        monitor.start()
    }

    private func stop() {
        deviceTimer?.invalidate()
        deviceTimer = nil
        monitor.stop()
    }

    private func tickDevicePresence() {
        // If we're already past device-detection, don't regress.
        if case .waitingToggle = phase { return }
        if case .linkActive    = phase { return }

        if let match = UsbVendorDetector.findMetaDevice() {
            phase = .deviceFound(productName: match.productName)
        } else {
            phase = .idle
        }
    }

    private func fireTetherIntent() {
        phase = .waitingToggle
        Task.detached(priority: .userInitiated) {
            do {
                let adb = try AdbController()
                let target: String
                if let serial { target = serial }
                else if let first = try adb.listDevices().first(where: { $0.isReady }) {
                    target = first.serial
                } else {
                    await MainActor.run { phase = .error(message: "No authorised Quest on adb. Approve the USB-debug prompt inside the headset.") }
                    return
                }
                try adb.openTetherSettings(serial: target)
            } catch {
                await MainActor.run { phase = .error(message: String(describing: error)) }
            }
        }
    }
}
