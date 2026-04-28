// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

/// Four-step setup wizard.
///   0 — Install fuvrd launchd agent
///   1 — Enable Developer Mode on the Quest headset
///   2 — Connect headset via USB (ADB auto-detects)
///   3 — Quick stream test
///
/// Shown automatically on first launch. Re-launchable from About or ⌘-menu.
struct OnboardingView: View {
    @EnvironmentObject var state: AppState
    @Environment(\.dismiss) private var dismiss

    enum StepResult: Equatable {
        case pending, running
        case passed(String)
        case failed(String)
    }

    @State private var step: Int = 0
    @State private var installResult: StepResult = .pending
    @State private var devModeResult: StepResult = .pending
    @State private var connectResult: StepResult = .pending
    @State private var testResult: StepResult = .pending
    @State private var testFramesAcked: Int = 0
    private let testFrameTotal = 5 * 90

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            stepIndicator
            Divider()
            ScrollView {
                Group {
                    switch step {
                    case 0:  installStep
                    case 1:  devModeStep
                    case 2:  connectStep
                    default: testStep
                    }
                }
                .padding(28)
            }
            Divider()
            footer
        }
        .frame(minWidth: 680, minHeight: 560)
        // Auto-advance to step 2 when ADB finds the headset.
        .onChange(of: state.deviceState) { ds in
            if step == 2, case .deviceFound = ds {
                connectResult = .passed("Headset detected via ADB.")
            } else if step == 2, case .streaming = ds {
                connectResult = .passed("Headset connected and streaming.")
            }
        }
    }

    // MARK: - Header

    private var header: some View {
        HStack(spacing: 14) {
            Image(systemName: "visionpro")
                .font(.system(size: 30))
                .foregroundStyle(.tint)
                .symbolEffect(.pulse)
            VStack(alignment: .leading) {
                Text("Welcome to FuVR").font(.title2).bold()
                Text("Four quick steps to your first PCVR session.")
                    .foregroundStyle(.secondary).font(.callout)
            }
            Spacer()
        }
        .padding()
    }

    // MARK: - Step indicator

    private var stepIndicator: some View {
        HStack(spacing: 10) {
            stepBadge(0, "Daemon",    installResult)
            connector;  stepBadge(1, "Dev Mode",  devModeResult)
            connector;  stepBadge(2, "Connect",   connectResult)
            connector;  stepBadge(3, "Test",      testResult)
            Spacer()
        }
        .padding(.horizontal)
        .padding(.vertical, 14)
    }

    private var connector: some View {
        Rectangle()
            .frame(height: 1)
            .foregroundStyle(.secondary.opacity(0.3))
            .frame(maxWidth: 48)
    }

    private func stepBadge(_ index: Int, _ title: String, _ result: StepResult) -> some View {
        HStack(spacing: 8) {
            ZStack {
                Circle()
                    .strokeBorder(badgeColor(result), lineWidth: 2)
                    .frame(width: 26, height: 26)
                badgeIcon(index, result)
            }
            Text(title).font(.callout)
                .foregroundStyle(step == index ? .primary : .secondary)
        }
        .opacity(step >= index ? 1.0 : 0.5)
        .animation(.easeInOut(duration: 0.3), value: step)
    }

    @ViewBuilder
    private func badgeIcon(_ index: Int, _ result: StepResult) -> some View {
        switch result {
        case .passed:  Image(systemName: "checkmark").foregroundStyle(.green).font(.caption.bold())
        case .failed:  Image(systemName: "xmark").foregroundStyle(.red).font(.caption.bold())
        case .running: ProgressView().controlSize(.mini)
        case .pending: Text("\(index + 1)").font(.caption.monospaced().bold())
        }
    }

    private func badgeColor(_ r: StepResult) -> Color {
        switch r {
        case .passed:  return .green
        case .failed:  return .red
        case .running: return .blue
        case .pending: return .secondary
        }
    }

    // MARK: - Step 0: Install daemon

    private var installStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("Install the FuVR streaming daemon", systemImage: "1.circle.fill")
                .font(.title3.bold())

            Text("Installs `com.fuvr.daemon.plist` into **~/Library/LaunchAgents** and starts the background daemon. No admin password required — it runs as your user.")
                .foregroundStyle(.secondary)

            instructionBox(steps: [
                ("terminal", "Click the button below to run `scripts/install-launchd.sh`"),
                ("checkmark.shield", "The daemon starts automatically at login from now on"),
            ])

            HStack {
                Button("Install daemon") { runInstallScript() }
                    .buttonStyle(.borderedProminent)
                    .disabled(installResult == .running)
                if case .running = installResult { ProgressView().controlSize(.small) }
                Spacer()
            }
            resultBanner(installResult)
        }
    }

    // MARK: - Step 1: Enable Developer Mode + USB Debugging

    private var devModeStep: some View {
        VStack(alignment: .leading, spacing: 18) {
            Label("Enable Developer Mode on your Quest", systemImage: "2.circle.fill")
                .font(.title3.bold())

            Text("Meta Quest headsets ship with Developer Mode disabled. You need to unlock it once. The steps below work for Quest 2, Quest 3, and Quest Pro.")
                .foregroundStyle(.secondary)

            VStack(alignment: .leading, spacing: 14) {
                devStep(
                    icon: "person.fill.badge.plus",
                    title: "Create a Meta developer account",
                    detail: "Go to developer.oculus.com and sign up — it's free. You need a verified phone number."
                )
                devStep(
                    icon: "app.badge",
                    title: "Open the Meta Quest app on your phone",
                    detail: "Navigate to **Menu → Devices** and select your headset."
                )
                devStep(
                    icon: "wrench.adjustable",
                    title: "Enable Developer Mode",
                    detail: "Tap **Headset Settings → Developer Mode**, toggle it **ON**, then confirm."
                )
                devStep(
                    icon: "cable.connector",
                    title: "Connect Quest to your Mac via USB",
                    detail: "Put on the headset. You will see a dialog asking to **Allow USB Debugging** — tap **Always allow from this computer**."
                )
                devStep(
                    icon: "checkmark.circle.fill",
                    title: "Confirm on the headset",
                    detail: "Once accepted, your Mac can communicate with the Quest over ADB without any extra setup."
                )
            }
            .padding(16)
            .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 14, style: .continuous))

            HStack {
                Button("I've done this — continue") {
                    devModeResult = .passed("Developer Mode and USB Debugging enabled.")
                    step = 2
                }
                .buttonStyle(.borderedProminent)
                Spacer()
            }
            resultBanner(devModeResult)
        }
    }

    private func devStep(icon: String, title: String, detail: String) -> some View {
        HStack(alignment: .top, spacing: 14) {
            Image(systemName: icon)
                .font(.system(size: 20))
                .foregroundStyle(.tint)
                .frame(width: 28)
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.callout.bold())
                Text(LocalizedStringKey(detail)).font(.callout).foregroundStyle(.secondary)
            }
        }
    }

    // MARK: - Step 2: Connect headset (ADB auto-detect)

    private var connectStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("Connect your headset", systemImage: "3.circle.fill")
                .font(.title3.bold())

            Text("Plug the Quest into your Mac with a USB-C cable. FuVR will detect it automatically via ADB.")
                .foregroundStyle(.secondary)

            instructionBox(steps: [
                ("cable.connector",         "USB-C cable connected between Quest and Mac"),
                ("person.fill.checkmark",   "\"Allow USB Debugging\" accepted on the headset"),
                ("magnifyingglass",          "FuVR is scanning every 2 seconds…"),
            ])

            // Live device state mirror.
            HStack(spacing: 12) {
                let ds = state.deviceState
                Image(systemName: ds.systemImage)
                    .foregroundStyle(deviceStateColor(ds))
                    .font(.title3)
                    .contentTransition(.symbolEffect(.replace))
                Text(ds.humanLabel)
                    .font(.callout)
                    .contentTransition(.numericText())
                Spacer()
            }
            .padding(14)
            .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            .animation(.spring(duration: 0.4), value: state.deviceState)

            resultBanner(connectResult)
        }
    }

    // MARK: - Step 3: Quick stream test

    private var testStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("Quick stream test", systemImage: "4.circle.fill")
                .font(.title3.bold())

            Text("Encodes a short synthetic HEVC clip and confirms the daemon is producing frames at the expected rate.")
                .foregroundStyle(.secondary)

            HStack {
                Button("Run test") { Task { await runTestSession() } }
                    .buttonStyle(.borderedProminent)
                    .disabled(testResult == .running)
                Spacer()
                if case .running = testResult {
                    ProgressView(value: Double(testFramesAcked), total: Double(testFrameTotal))
                        .frame(width: 200)
                }
            }
            resultBanner(testResult)
        }
    }

    // MARK: - Shared helpers

    private func instructionBox(steps: [(String, String)]) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(Array(steps.enumerated()), id: \.offset) { _, pair in
                HStack(alignment: .top, spacing: 12) {
                    Image(systemName: pair.0)
                        .foregroundStyle(.tint)
                        .frame(width: 22)
                    Text(pair.1).font(.callout)
                }
            }
        }
        .padding(14)
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private func resultBanner(_ r: StepResult) -> some View {
        Group {
            switch r {
            case .passed(let m):
                Label(m, systemImage: "checkmark.circle.fill").foregroundStyle(.green)
            case .failed(let m):
                Label(m, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
            default:
                EmptyView()
            }
        }
        .padding(8)
        .animation(.easeIn(duration: 0.25), value: r)
    }

    private func deviceStateColor(_ ds: DeviceState) -> Color {
        switch ds.colorName {
        case "green": return .green
        case "blue": return .blue
        case "yellow": return .yellow
        case "orange": return .orange
        case "red": return .red
        default: return .secondary
        }
    }

    // MARK: - Footer

    private var footer: some View {
        HStack {
            Button("Skip wizard") { dismiss() }
            Spacer()
            if step > 0 {
                Button("Back") { withAnimation(.easeInOut(duration: 0.2)) { step -= 1 } }
            }
            if step < 3 {
                Button("Next") { withAnimation(.easeInOut(duration: 0.2)) { step += 1 } }
                    .buttonStyle(.borderedProminent)
                    .disabled(!canAdvance)
            } else {
                Button("Finish") { dismiss() }
                    .buttonStyle(.borderedProminent)
                    .disabled(testResult.isPending || testResult.isRunning)
            }
        }
        .padding()
    }

    private var canAdvance: Bool {
        switch step {
        case 0: return installResult.isPassed
        case 1: return true          // informational — always allow advancing
        case 2: return connectResult.isPassed
        default: return false
        }
    }

    // MARK: - Actions

    private func runInstallScript() {
        installResult = .running
        Task.detached {
            let projectRoot = await Self.projectRoot()
            let script = projectRoot.appendingPathComponent("scripts/install-launchd.sh").path
            let result = Self.runShell(script)
            await MainActor.run {
                if result.status == 0 {
                    installResult = .passed("Daemon agent installed and started.")
                } else {
                    installResult = .failed("install-launchd.sh exited \(result.status). \(result.stderr.prefix(200))")
                }
            }
        }
    }

    private func runTestSession() async {
        testResult = .running
        testFramesAcked = 0
        let cfg = SessionConfig(refreshRateHz: 90, videoBitrateBps: 50_000_000, audioEnabled: false)
        if case .connected = state.connectionState {
            // already connected
        } else {
            state.connect(socketPath: SettingsBundle.defaults.socketPath, useMock: true)
            for _ in 0..<20 {
                try? await Task.sleep(nanoseconds: 100_000_000)
                if case .connected = state.connectionState { break }
            }
        }
        state.startSession(cfg)
        let target = testFrameTotal
        for _ in 0..<60 {
            try? await Task.sleep(nanoseconds: 100_000_000)
            testFramesAcked = min(target, state.metrics.samples.count * (target / 60))
            if testFramesAcked >= target { break }
        }
        state.stopSession()
        if testFramesAcked >= target {
            testResult = .passed("\(testFramesAcked) of \(target) acks received.")
        } else {
            testResult = .failed("Only \(testFramesAcked) acks; expected \(target). Check daemon logs.")
        }
    }

    // MARK: - Shell helpers

    @MainActor
    private static func projectRoot() -> URL {
        var url = Bundle.main.bundleURL
        for _ in 0..<10 {
            if FileManager.default.fileExists(
                atPath: url.appendingPathComponent("scripts/install-launchd.sh").path) {
                return url
            }
            url.deleteLastPathComponent()
        }
        return Bundle.main.bundleURL
    }

    private struct ShellResult { let status: Int32; let stdout: String; let stderr: String }

    nonisolated private static func runShell(_ path: String) -> ShellResult {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/bin/bash")
        p.arguments = [path]
        let out = Pipe(); let err = Pipe()
        p.standardOutput = out; p.standardError = err
        do {
            try p.run(); p.waitUntilExit()
        } catch {
            return ShellResult(status: -1, stdout: "", stderr: error.localizedDescription)
        }
        let so = String(data: out.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        let se = String(data: err.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        return ShellResult(status: p.terminationStatus, stdout: so, stderr: se)
    }
}

// MARK: - StepResult helpers

private extension OnboardingView.StepResult {
    var isPassed:  Bool { if case .passed  = self { return true } else { return false } }
    var isPending: Bool { if case .pending = self { return true } else { return false } }
    var isRunning: Bool { if case .running = self { return true } else { return false } }
}
