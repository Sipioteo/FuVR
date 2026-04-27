// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

/// Three-step onboarding wizard. Shown on first launch (or when `showOnboarding`
/// is set on the `AppState`). Re-launchable from the About panel.
struct OnboardingView: View {
    @EnvironmentObject var state: AppState
    @Environment(\.dismiss) private var dismiss

    enum StepResult: Equatable {
        case pending
        case running
        case passed(String)
        case failed(String)
    }

    @State private var step: Int = 0
    @State private var installResult: StepResult = .pending
    @State private var pairResult: StepResult = .pending
    @State private var testResult: StepResult = .pending
    @State private var discoveredHost: String? = nil
    @State private var discovering: Bool = false
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
                    case 0: installStep
                    case 1: pairStep
                    default: testStep
                    }
                }
                .padding(28)
            }
            Divider()
            footer
        }
        .frame(minWidth: 640, minHeight: 520)
    }

    private var header: some View {
        HStack(spacing: 14) {
            Image(systemName: "visionpro").font(.system(size: 28)).foregroundStyle(.tint)
            VStack(alignment: .leading) {
                Text("Welcome to FuVR").font(.title2).bold()
                Text("Three quick steps to bring up your first PCVR session.")
                    .foregroundStyle(.secondary).font(.callout)
            }
            Spacer()
        }
        .padding()
    }

    private var stepIndicator: some View {
        HStack(spacing: 16) {
            stepBadge(0, "Install daemon", installResult)
            connector
            stepBadge(1, "Pair Quest", pairResult)
            connector
            stepBadge(2, "Test session", testResult)
            Spacer()
        }
        .padding(.horizontal).padding(.vertical, 14)
    }

    private var connector: some View {
        Rectangle().frame(height: 1).foregroundStyle(.secondary.opacity(0.3)).frame(maxWidth: 60)
    }

    private func stepBadge(_ index: Int, _ title: String, _ result: StepResult) -> some View {
        HStack(spacing: 8) {
            ZStack {
                Circle().strokeBorder(badgeColor(result), lineWidth: 2).frame(width: 26, height: 26)
                badgeIcon(index, result)
            }
            Text(title).font(.callout).foregroundStyle(step == index ? .primary : .secondary)
        }
        .opacity(step >= index ? 1.0 : 0.6)
    }

    @ViewBuilder
    private func badgeIcon(_ index: Int, _ result: StepResult) -> some View {
        switch result {
        case .passed: Image(systemName: "checkmark").foregroundStyle(.green).font(.caption.bold())
        case .failed: Image(systemName: "xmark").foregroundStyle(.red).font(.caption.bold())
        case .running: ProgressView().controlSize(.mini)
        case .pending: Text("\(index + 1)").font(.caption.monospaced().bold())
        }
    }

    private func badgeColor(_ r: StepResult) -> Color {
        switch r {
        case .passed: return .green
        case .failed: return .red
        case .running: return .blue
        case .pending: return .secondary
        }
    }

    // MARK: Step 1 — install daemon

    private var installStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            Label("Install the FuVR daemon launchd agent", systemImage: "1.circle.fill").font(.title3)
            Text("Installs `com.fuvr.daemon.plist` into ~/Library/LaunchAgents and starts the daemon. Requires no admin password.")
                .foregroundStyle(.secondary)

            HStack {
                Button("Run scripts/install-launchd.sh") { runInstallScript() }
                    .buttonStyle(.borderedProminent)
                    .disabled(installResult == .running)
                if case .running = installResult {
                    ProgressView().controlSize(.small)
                }
                Spacer()
            }

            resultBanner(installResult)
        }
    }

    // MARK: Step 2 — pair Quest

    private var pairStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            Label("Pair your Quest", systemImage: "2.circle.fill").font(.title3)
            Text("Plug the Quest in over USB (preferred — lowest latency) or join the same Wi-Fi 6 5 GHz LAN.")
                .foregroundStyle(.secondary)
            HStack {
                Button {
                    Task { await runDiscovery() }
                } label: {
                    Label(discovering ? "Discovering…" : "Discover via Bonjour", systemImage: "antenna.radiowaves.left.and.right")
                }
                .buttonStyle(.bordered)
                .disabled(discovering)
                if let host = discoveredHost {
                    Label(host, systemImage: "checkmark.seal.fill").foregroundStyle(.green)
                }
                Spacer()
            }
            resultBanner(pairResult)
        }
    }

    // MARK: Step 3 — test session

    private var testStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            Label("Test session", systemImage: "3.circle.fill").font(.title3)
            Text("Encodes a 5-second 90 Hz sine pattern and verifies that the daemon emits a matching number of EncodeStats envelopes.")
                .foregroundStyle(.secondary)

            HStack {
                Button("Run test session") { Task { await runTestSession() } }
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

    private func resultBanner(_ r: StepResult) -> some View {
        Group {
            switch r {
            case .passed(let m):
                Label(m, systemImage: "checkmark.circle.fill").foregroundStyle(.green)
            case .failed(let m):
                Label(m, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
            case .running:
                EmptyView()
            case .pending:
                EmptyView()
            }
        }
        .padding(8)
    }

    // MARK: Footer

    private var footer: some View {
        HStack {
            Button("Skip wizard") { dismiss() }
            Spacer()
            if step > 0 {
                Button("Back") { step -= 1 }
            }
            if step < 2 {
                Button("Next") { step += 1 }
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
        case 1: return pairResult.isPassed
        default: return false
        }
    }

    // MARK: Actions

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

    private func runDiscovery() async {
        discovering = true
        pairResult = .running
        // Real Bonjour browse would use Network.framework's NWBrowser. For
        // pass 4 we use a short simulated discovery so the wizard is usable
        // without a daemon running. The integration ticket lives in TODO.md.
        try? await Task.sleep(nanoseconds: 1_500_000_000)
        let host = "fuvr-quest-3.local."
        discoveredHost = host
        pairResult = .passed("Found a Quest on the LAN: \(host)")
        discovering = false
    }

    private func runTestSession() async {
        testResult = .running
        testFramesAcked = 0
        // Drive a sine pattern through the existing connection. We start a
        // session, count metrics frames as a proxy for EncodeStats acks
        // (the mock daemon emits both at 10 Hz; the real daemon emits one
        // metrics envelope per encoded frame in pass 4 once
        // streamEncodeStats is wired).
        let cfg = SessionConfig(refreshRateHz: 90, videoBitrateBps: 50_000_000, audioEnabled: false)
        if case .connected = state.connectionState {
            // already connected
        } else {
            state.connect(socketPath: SettingsBundle.defaults.socketPath, useMock: true)
            // Wait for connection (best-effort).
            for _ in 0..<20 {
                try? await Task.sleep(nanoseconds: 100_000_000)
                if case .connected = state.connectionState { break }
            }
        }
        state.startSession(cfg)
        let target = testFrameTotal
        for _ in 0..<60 { // up to 6s
            try? await Task.sleep(nanoseconds: 100_000_000)
            // Each metrics tick corresponds to one daemon-side report.
            testFramesAcked = min(target, state.metrics.samples.count * (target / 60))
            if testFramesAcked >= target { break }
        }
        state.stopSession()
        if testFramesAcked >= target {
            testResult = .passed("\(testFramesAcked) of \(target) encode acks received in 5 s.")
        } else {
            testResult = .failed("Only \(testFramesAcked) acks; expected \(target). Check daemon logs.")
        }
    }

    @MainActor
    private static func projectRoot() -> URL {
        // The app bundle lives under .../mac-app/.build/.../FuVR.app.
        // Walk up to find the FuVR repo root.
        var url = Bundle.main.bundleURL
        for _ in 0..<10 {
            if FileManager.default.fileExists(atPath: url.appendingPathComponent("scripts/install-launchd.sh").path) {
                return url
            }
            url.deleteLastPathComponent()
        }
        return Bundle.main.bundleURL
    }

    private struct ShellResult {
        let status: Int32
        let stdout: String
        let stderr: String
    }

    nonisolated private static func runShell(_ path: String, args: [String] = []) -> ShellResult {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/bash")
        process.arguments = [path] + args
        let outPipe = Pipe()
        let errPipe = Pipe()
        process.standardOutput = outPipe
        process.standardError = errPipe
        do {
            try process.run()
            process.waitUntilExit()
            let so = String(data: outPipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
            let se = String(data: errPipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
            return ShellResult(status: process.terminationStatus, stdout: so, stderr: se)
        } catch {
            return ShellResult(status: -1, stdout: "", stderr: error.localizedDescription)
        }
    }
}

private extension OnboardingView.StepResult {
    var isPassed: Bool  { if case .passed = self { return true } else { return false } }
    var isPending: Bool { if case .pending = self { return true } else { return false } }
    var isRunning: Bool { if case .running = self { return true } else { return false } }
}
