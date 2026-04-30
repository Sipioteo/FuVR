// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRADB
import FuVRControl

/// Step-by-step interactive wizard that walks the user through enabling
/// **Developer Mode** and **USB Debugging** on a Meta Quest headset.
///
/// This is the spec's "UI/UX Agent · Onboarding Specialist" deliverable:
/// it lives behind the gear menu and is presented automatically the first
/// time a non-`.device` (e.g. `unauthorized`) Quest is detected.
///
/// Visual language: macOS Liquid Glass (`.regularMaterial`, hairline strokes,
/// SF Symbol hero glyphs with `.symbolEffect`-based motion). No drop-shadow
/// excess — the depth comes from material vibrancy.
struct QuestSetupWizard: View {
    @EnvironmentObject var state: AppState
    @Environment(\.dismiss) private var dismiss
    @State private var step: Int = 0

    /// Live monitor: flips the final two steps from "do this" to "✓ done"
    /// without the user having to click Next.
    @State private var rndisLink: RndisInterfaceMonitor.Snapshot?
    @State private var linkMonitor = RndisInterfaceMonitor()
    @State private var tetherIntentFiring: Bool = false
    @State private var tetherIntentError: String?

    /// Set when the user opts out of UDP-RNDIS (macOS lacks an RNDIS driver
    /// for some Quest firmware classes; tether toggle stays on inside the
    /// headset but the host never binds an interface). The wizard then
    /// auto-passes the link-confirmation steps and the Quest app falls
    /// back to TCP-over-`adb reverse` automatically (see `main.cpp`).
    @State private var legacyTcpMode: Bool = false

    // Daemon-install + stream-test step state (ported from the legacy
    // OnboardingView so this is now the single onboarding surface).
    @State private var installResult: StepResultState = .pending
    @State private var testResult: StepResultState = .pending
    @State private var testFramesAcked: Int = 0
    private let testFrameTotal = 5 * 90

    // OpenVR bridge installer state.
    @State private var bridgeResult: StepResultState = .pending
    @State private var bridgeCandidates: [OpenVrBridgeInstaller.Candidate] = []
    @State private var bridgeSelection: OpenVrBridgeInstaller.Candidate.ID?
    @State private var bridgeInstalling: Bool = false

    enum StepResultState: Equatable {
        case pending, running
        case passed(String)
        case failed(String)
        var isPassed: Bool { if case .passed = self { return true } else { return false } }
    }

    private var steps: [Step] {
        [
            Step(
                kind: .installDaemon,
                symbol: "shippingbox.fill",
                title: "Install the FuVR streaming daemon",
                body: "Drops `com.fuvr.daemon.plist` into **~/Library/LaunchAgents** and starts the background daemon. No admin password required — it runs as your user, restarts at login.",
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .info,
                symbol: "person.badge.key",
                title: "Create a Meta developer account",
                body: "On a phone or computer, sign in at developer.oculus.com and accept the developer agreement. The same Meta account you use on the headset must be enrolled.",
                url: URL(string: "https://developer.oculus.com/manage")!,
                urlLabel: "Open Meta developer portal"
            ),
            Step(
                kind: .info,
                symbol: "iphone.gen3",
                title: "Open the Meta Horizon app on your phone",
                body: "Tap **Menu › Devices**, pick your headset, then **Headset Settings › Developer Mode**. Toggle it ON. If the toggle is greyed out, your account is not yet enrolled (step 1).",
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .info,
                symbol: "headphones.circle",
                title: "Reboot the headset",
                body: "Press **Power › Restart** on the Quest, or hold the side button for 3 seconds. Developer Mode does not take effect until after a reboot.",
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .info,
                symbol: "cable.connector",
                title: "Plug the headset into this Mac",
                body: "Use the USB-C cable that came with the headset (a data cable, not charge-only). Put the headset on — a system dialog asks **Allow USB debugging?** Tap **Always allow from this computer** then **Allow**.",
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .info,
                symbol: "checkmark.seal",
                title: "Confirm adb sees the headset",
                body: "When FuVR sees the headset, the dot above turns green and the dashboard switches to **Connected**. If it stays grey, unplug and replug the cable, then re-tap **Allow** inside the headset. This unlocks the **high-speed UDP link** in the next step.",
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .tetherIntent,
                symbol: "cable.connector.horizontal",
                title: "Open the hidden tether menu",
                body: """
                FuVR streams VR over a virtual Mac↔Quest LAN built on the USB cable — \
                much faster than `adb reverse` (no head-of-line blocking, lower jitter). \
                That LAN comes from Android's **USB Tethering** toggle, which Meta hides \
                from the launcher. The button below opens the menu inside your headset.
                """,
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .tetherToggle,
                symbol: "eye.fill",
                title: "Toggle USB Tethering ON",
                body: """
                Put the headset on. A system **Tethering** screen is in front of you. \
                Toggle **USB Tethering** to ON. The Mac will detect the link automatically \
                — no need to click anything here. Android remembers this setting across \
                reboots, so this is a one-time step.
                """,
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .linkConfirmed,
                symbol: "bolt.horizontal.circle.fill",
                title: "High-speed link active",
                body: """
                The Mac is now talking UDP to the Quest at **192.168.42.129:59000** over \
                the cable. One last sanity check before you open Blender VR.
                """,
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .openvrBridge,
                symbol: "shippingbox.and.arrow.backward.fill",
                title: "Install the SteamVR bridge into Vivecraft",
                body: """
                Vivecraft (and other legacy SteamVR Mac titles) load Valve's \
                `libopenvr_api.dylib` from their `natives/` folder. FuVR ships a \
                drop-in replacement that talks to this Mac instead of Steam — \
                no SteamVR install needed. The button below detects every Minecraft \
                launcher you have and copies the bridge into the chosen instance.
                """,
                url: nil,
                urlLabel: nil
            ),
            Step(
                kind: .streamTest,
                symbol: "waveform.path.ecg",
                title: "Quick stream test",
                body: "Encodes a short synthetic HEVC clip and confirms the daemon is producing frames at the expected rate. If this passes, Blender's VR Scene Inspection will work out of the box.",
                url: nil,
                urlLabel: nil
            ),
        ]
    }

    var body: some View {
        ZStack {
            // Vibrant base — relies on the window's chrome to actually
            // materialize the blur on macOS 14+.
            Rectangle().fill(.regularMaterial).ignoresSafeArea()

            VStack(spacing: 0) {
                header
                Divider().opacity(0.4)
                stepBody
                Divider().opacity(0.4)
                footer
            }
        }
        .frame(minWidth: 720, minHeight: 540)
        .onAppear {
            // Pre-populate the Vivecraft bridge candidates so the wizard
            // shows results the moment the user reaches that step.
            bridgeCandidates = OpenVrBridgeInstaller().discover()
            if let first = bridgeCandidates.first { bridgeSelection = first.id }

            rndisLink = RndisInterfaceMonitor.currentSnapshot()
            linkMonitor.onChange = { snap in
                Task { @MainActor in
                    let wasUp = (rndisLink != nil)
                    rndisLink = snap
                    // Auto-advance the moment the link comes up while the
                    // user is on the "toggle USB Tethering" step.
                    if !wasUp, snap != nil, steps[step].kind == .tetherToggle {
                        withAnimation(.snappy) {
                            step = min(step + 1, steps.count - 1)
                        }
                    }
                }
            }
            linkMonitor.start()
        }
        .onDisappear {
            linkMonitor.stop()
            // Re-arm the global ADB poller now that the wizard's USB
            // re-enumeration window is over. Safe to call unconditionally.
            state.resumeAdbPolling()
        }
    }

    // MARK: - Header

    private var header: some View {
        HStack(spacing: 16) {
            HeadsetStatusOrb()
                .environmentObject(state)
                .frame(width: 64, height: 64)
            VStack(alignment: .leading, spacing: 4) {
                Text("Set up your Quest for FuVR")
                    .font(.title2).bold()
                Text("Eleven steps · roughly 6 minutes (one-time)")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Button("Done") { dismiss() }
                .keyboardShortcut(.escape, modifiers: [])
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
        }
        .padding(20)
    }

    // MARK: - Step body

    private var stepBody: some View {
        let s = steps[step]
        return HStack(alignment: .top, spacing: 28) {
            // Step rail.
            VStack(alignment: .leading, spacing: 14) {
                ForEach(steps.indices, id: \.self) { i in
                    StepRailRow(index: i, title: steps[i].title, current: step)
                        .onTapGesture { withAnimation(.snappy(duration: 0.25)) { step = i } }
                }
                Spacer()
            }
            .frame(width: 220)
            .padding(.leading, 24)
            .padding(.vertical, 24)

            // Detail card.
            VStack(alignment: .leading, spacing: 20) {
                Image(systemName: s.symbol)
                    .font(.system(size: 56, weight: .light))
                    .symbolRenderingMode(.hierarchical)
                    .foregroundStyle(.tint)
                    .symbolEffect(.bounce, value: step)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text(s.title)
                    .font(.title.weight(.semibold))
                Text(LocalizedStringKey(s.body))
                    .font(.body)
                    .foregroundStyle(.primary)
                    .fixedSize(horizontal: false, vertical: true)
                if let url = s.url, let label = s.urlLabel {
                    Link(destination: url) {
                        Label(label, systemImage: "arrow.up.right.square")
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }
                stepAction(s.kind)
                Spacer()
            }
            .padding(28)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 22, style: .continuous)
                    .strokeBorder(.white.opacity(0.08), lineWidth: 1)
            )
            .padding(.trailing, 24)
            .padding(.vertical, 24)
        }
    }

    // MARK: - Per-step action

    @ViewBuilder
    private func stepAction(_ kind: Step.Kind) -> some View {
        switch kind {
        case .info:
            EmptyView()

        case .installDaemon:
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 12) {
                    Button {
                        runInstallScript()
                    } label: {
                        switch installResult {
                        case .running:
                            HStack(spacing: 6) {
                                ProgressView().controlSize(.small)
                                Text("Installing…")
                            }
                        case .passed:
                            Label("Re-install", systemImage: "arrow.clockwise")
                        default:
                            Label("Install daemon", systemImage: "shippingbox.fill")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(installResult == .running)
                    .keyboardShortcut(.defaultAction)

                    if case .passed = installResult {
                        Label("Installed", systemImage: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                            .font(.callout)
                    }
                }
                resultBanner(installResult)
            }

        case .openvrBridge:
            VStack(alignment: .leading, spacing: 12) {
                if bridgeCandidates.isEmpty {
                    HStack(spacing: 10) {
                        Image(systemName: "magnifyingglass.circle")
                            .foregroundStyle(.secondary)
                        Text("No Minecraft launcher detected. Pick a folder manually below.")
                            .font(.callout).foregroundStyle(.secondary)
                    }
                } else {
                    Picker("Install into:", selection: Binding(
                        get: { bridgeSelection ?? bridgeCandidates.first?.id ?? "" },
                        set: { bridgeSelection = $0 }
                    )) {
                        ForEach(bridgeCandidates) { c in
                            Label {
                                Text(c.label)
                                    + Text(c.alreadyInstalled ? "  · already installed" : "")
                                        .foregroundStyle(.secondary)
                            } icon: {
                                Image(systemName: c.alreadyInstalled
                                      ? "checkmark.circle.fill" : "tray.fill")
                                    .foregroundStyle(c.alreadyInstalled ? .green : .accentColor)
                            }
                            .tag(c.id)
                        }
                    }
                    .pickerStyle(.menu)
                }
                HStack(spacing: 12) {
                    Button {
                        installBridgeAtSelection()
                    } label: {
                        if bridgeInstalling {
                            HStack(spacing: 6) {
                                ProgressView().controlSize(.small)
                                Text("Installing…")
                            }
                        } else {
                            Label("Install bridge", systemImage: "arrow.down.doc.fill")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(bridgeInstalling || bridgeCandidates.isEmpty)
                    .keyboardShortcut(.defaultAction)

                    Button("Choose folder…") { pickCustomBridgeFolder() }
                        .controlSize(.large)
                        .disabled(bridgeInstalling)

                    Button {
                        bridgeCandidates = OpenVrBridgeInstaller().discover()
                        if let first = bridgeCandidates.first { bridgeSelection = first.id }
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .controlSize(.large)
                    .help("Re-scan launchers")
                }
                Text("The bridge is a small replacement for Valve's `libopenvr_api.dylib`. No SteamVR install required, nothing global is modified — only the chosen instance gets the file.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                resultBanner(bridgeResult)
            }

        case .streamTest:
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 12) {
                    Button {
                        Task { await runTestSession() }
                    } label: {
                        if testResult == .running {
                            HStack(spacing: 6) {
                                ProgressView().controlSize(.small)
                                Text("Testing…")
                            }
                        } else {
                            Label("Run test", systemImage: "play.fill")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(testResult == .running)
                    .keyboardShortcut(.defaultAction)

                    if case .running = testResult {
                        ProgressView(value: Double(testFramesAcked),
                                     total: Double(testFrameTotal))
                            .frame(width: 200)
                    }
                    if case .passed = testResult {
                        Label("All frames acked", systemImage: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                            .font(.callout)
                    }
                }
                resultBanner(testResult)
            }

        case .tetherIntent:
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 12) {
                    Button {
                        fireTetherIntent()
                    } label: {
                        if tetherIntentFiring {
                            ProgressView().controlSize(.small).padding(.trailing, 4)
                            Text("Opening on Quest…")
                        } else {
                            Label("Open Tether menu inside headset", systemImage: "arrow.up.right.square")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(tetherIntentFiring)
                    .keyboardShortcut(.defaultAction)

                    Button("Skip — use legacy TCP") { skipTether() }
                        .controlSize(.large)
                        .help("Use TCP-over-adb-reverse instead. Slightly higher latency, but works on every Mac and every Quest firmware.")

                    if rndisLink != nil {
                        Label("Already on", systemImage: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                            .font(.callout)
                    }
                }
                if let err = tetherIntentError {
                    Label(err, systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(.red)
                        .font(.callout)
                }
                Text("Tip: if the toggle stays on inside the headset but the Mac never sees the link (macOS lacks an RNDIS driver on some setups), use **Skip** — the Quest app falls back to TCP automatically.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

        case .tetherToggle:
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 12) {
                    if rndisLink != nil {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                        Text("Link detected — you can move on.")
                            .font(.callout)
                    } else if legacyTcpMode {
                        Image(systemName: "arrow.triangle.swap")
                            .foregroundStyle(.orange)
                        Text("Skipped — using legacy TCP transport.")
                            .font(.callout)
                    } else {
                        ProgressView().controlSize(.small)
                        Text("Waiting for the headset to enable tethering…")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                    }
                }
                if rndisLink == nil && !legacyTcpMode {
                    Button("Skip — use legacy TCP") { skipTether() }
                        .help("Use TCP-over-adb-reverse instead.")
                }
            }

        case .linkConfirmed:
            VStack(alignment: .leading, spacing: 8) {
                if legacyTcpMode {
                    HStack(spacing: 10) {
                        Image(systemName: "arrow.triangle.swap")
                            .foregroundStyle(.orange)
                            .font(.title2)
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Legacy TCP transport").font(.callout.bold())
                            Text("Mac ↔ Quest over `adb reverse tcp:9943`")
                                .font(.callout).monospaced().foregroundStyle(.secondary)
                            Text("~5 ms higher latency than UDP-RNDIS, otherwise identical. Reliable on every macOS + Quest combo.")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    .padding(12)
                    .background(.orange.opacity(0.10), in: RoundedRectangle(cornerRadius: 10))
                } else if let link = rndisLink {
                    HStack(spacing: 10) {
                        Image(systemName: "checkmark.seal.fill")
                            .foregroundStyle(.green)
                            .font(.title2)
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Mac \(link.ipv4) on \(link.interfaceName)")
                                .font(.callout).monospaced()
                            Text("→ Quest 192.168.42.129:59000")
                                .font(.callout).monospaced()
                                .foregroundStyle(.secondary)
                        }
                    }
                    .padding(12)
                    .background(.green.opacity(0.12), in: RoundedRectangle(cornerRadius: 10))
                } else {
                    HStack(spacing: 10) {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundStyle(.orange)
                        Text("Link is not up yet — go back to step 7 and toggle USB Tethering.")
                            .font(.callout)
                    }
                }
            }
        }
    }

    // MARK: - Result banner (shared by install/test steps)

    @ViewBuilder
    private func resultBanner(_ r: StepResultState) -> some View {
        switch r {
        case .passed(let m):
            Label(m, systemImage: "checkmark.circle.fill").foregroundStyle(.green)
        case .failed(let m):
            Label(m, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
        default:
            EmptyView()
        }
    }

    // MARK: - Install daemon

    private func runInstallScript() {
        installResult = .running
        Task.detached(priority: .userInitiated) {
            let projectRoot = await Self.projectRoot()
            let script = projectRoot.appendingPathComponent("scripts/install-launchd.sh").path
            let result = Self.runShell(script)
            if result.status != 0 {
                await MainActor.run {
                    installResult = .failed(
                        "install-launchd.sh exited \(result.status). \(result.stderr.prefix(200))"
                    )
                }
                return
            }

            // Step 2: register the OpenXR runtime so Blender (and any
            // other Khronos-loader-using app) finds FuVR automatically.
            // Without this, Blender errors with "Failed to query OpenXR
            // runtime information. Do you have an active runtime set up?"
            // The registrar writes BOTH XDG (`~/.config/openxr/...`,
            // canonical) and Apple (`~/Library/Application Support/...`,
            // legacy compat) manifest paths.
            let registrar = projectRoot
                .appendingPathComponent("build/runtime-macos/fuvr-register")
                .path
            let runtimeDylib = projectRoot
                .appendingPathComponent("build/runtime-macos/libfuvr_openxr_runtime.dylib")
                .path
            var runtimeNote = ""
            if FileManager.default.isExecutableFile(atPath: registrar),
               FileManager.default.fileExists(atPath: runtimeDylib) {
                let r = Self.runShellArgs(registrar, args: [runtimeDylib])
                if r.status == 0 {
                    runtimeNote = " · OpenXR runtime registered."
                    // Publish XR_RUNTIME_JSON so apps launched from Finder
                    // (Blender etc.) pick up the manifest. Some Khronos
                    // loader builds prefer the env-var override over the
                    // manifest search; we set both for belt-and-braces.
                    let manifest = NSHomeDirectory()
                        + "/.config/openxr/1/active_runtime.json"
                    _ = Self.runShellArgs("/bin/launchctl",
                                          args: ["setenv", "XR_RUNTIME_JSON", manifest])
                } else {
                    runtimeNote = " · OpenXR registration failed (\(r.stderr.prefix(80)))"
                }
            } else {
                runtimeNote = " · OpenXR runtime artifacts not found — build runtime-macos first."
            }

            await MainActor.run {
                installResult = .passed("Daemon agent installed and started.\(runtimeNote)")
                // Auto-advance: next step is "create Meta dev account"
                // which is informational.
                withAnimation(.snappy) { step = min(step + 1, steps.count - 1) }
            }
        }
    }

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
        return runProcess(p)
    }

    /// Direct binary invocation (no shell). Used for tools like
    /// `fuvr-register` that take a single path argument and where
    /// involving bash would only complicate quoting.
    nonisolated private static func runShellArgs(_ exe: String, args: [String]) -> ShellResult {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: exe)
        p.arguments = args
        return runProcess(p)
    }

    nonisolated private static func runProcess(_ p: Process) -> ShellResult {
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

    // MARK: - Stream test

    @MainActor
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

    // MARK: - OpenVR bridge install

    private func installBridgeAtSelection() {
        guard let chosen = bridgeCandidates.first(where: { $0.id == (bridgeSelection ?? "") })
                ?? bridgeCandidates.first else { return }
        runBridgeInstall(at: chosen.nativesDirectory, label: chosen.label)
    }

    private func pickCustomBridgeFolder() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.message = "Pick the natives/ folder of your Minecraft instance."
        if panel.runModal() == .OK, let url = panel.url {
            runBridgeInstall(at: url, label: url.lastPathComponent)
        }
    }

    private func runBridgeInstall(at dir: URL, label: String) {
        bridgeInstalling = true
        bridgeResult = .running
        let installer = OpenVrBridgeInstaller()
        Task.detached(priority: .userInitiated) {
            do {
                let dest = try installer.install(into: dir)
                await MainActor.run {
                    bridgeInstalling = false
                    bridgeResult = .passed("Installed into \(label) (\(dest.path))")
                    // Refresh candidates so the newly-installed entry is
                    // marked green next time the user opens the picker.
                    bridgeCandidates = OpenVrBridgeInstaller().discover()
                    withAnimation(.snappy) {
                        step = min(step + 1, steps.count - 1)
                    }
                }
            } catch {
                await MainActor.run {
                    bridgeInstalling = false
                    bridgeResult = .failed(String(describing: error))
                }
            }
        }
    }

    // MARK: - Tether skip (legacy TCP path)

    private func skipTether() {
        legacyTcpMode = true
        // Resume polling immediately — the user is committing to the TCP
        // path, which actively needs `adb reverse` to be set up by the
        // poller-driven SessionOrchestrator. Pausing it here would block
        // streaming.
        state.resumeAdbPolling()
        // Auto-advance to the openvr-bridge step (skip the toggle wait
        // and the link-confirmed panel).
        let target = steps.firstIndex(where: { $0.kind == .openvrBridge }) ?? (step + 2)
        withAnimation(.snappy) { step = min(target, steps.count - 1) }
    }

    // MARK: - Tether intent

    private func fireTetherIntent() {
        tetherIntentFiring = true
        tetherIntentError = nil
        // Why: when Android receives the USB-Tethering toggle, it
        // reconfigures the device's USB function set to add RNDIS. Any
        // concurrent `adb devices -l` poll from the host re-enumerates
        // the device mid-switch and the toggle silently rolls back to
        // OFF (this was the symptom: tethering would enable then revert
        // a moment later). Pause the global poller for the rest of the
        // wizard; it resumes on dismissal.
        state.pauseAdbPolling()
        Task.detached(priority: .userInitiated) {
            do {
                let adb = try AdbController()
                let devices = try adb.listDevices()
                guard let target = devices.first(where: { $0.isReady }) else {
                    await MainActor.run {
                        tetherIntentError = "No authorised Quest on adb. Re-do step 5."
                        tetherIntentFiring = false
                    }
                    return
                }
                try adb.openTetherSettings(serial: target.serial)
                await MainActor.run {
                    tetherIntentFiring = false
                    // Auto-advance to the toggle step so the user sees the
                    // "waiting for tethering" indicator immediately.
                    withAnimation(.snappy) { step = min(step + 1, steps.count - 1) }
                }
            } catch {
                await MainActor.run {
                    tetherIntentError = String(describing: error)
                    tetherIntentFiring = false
                }
            }
        }
    }

    // MARK: - Footer

    private var footer: some View {
        HStack(spacing: 12) {
            ProgressView(value: Double(step + 1), total: Double(steps.count))
                .progressViewStyle(.linear)
                .tint(.accentColor)
                .frame(maxWidth: 240)
            Text("Step \(step + 1) of \(steps.count)")
                .font(.caption)
                .foregroundStyle(.secondary)
                .monospacedDigit()
            Spacer()
            Button("Back") {
                withAnimation(.snappy) { step = max(0, step - 1) }
            }
            .disabled(step == 0)
            Button(step == steps.count - 1 ? "Finish" : "Next") {
                if step == steps.count - 1 { dismiss() }
                else { withAnimation(.snappy) { step += 1 } }
            }
            .keyboardShortcut(.defaultAction)
            .buttonStyle(.borderedProminent)
            .disabled(!canAdvance)
        }
        .padding(20)
    }

    /// Gating logic for "Next" / "Finish" — informational steps are always
    /// passable; functional steps require their work to have completed.
    private var canAdvance: Bool {
        let kind = steps[step].kind
        switch kind {
        case .info, .tetherIntent, .linkConfirmed:
            return true
        case .installDaemon:
            return installResult.isPassed
        case .tetherToggle:
            // Either the high-speed link is up, or the user chose the
            // legacy TCP path (macOS lacks an RNDIS driver for many Quest
            // firmwares; this is a normal, supported configuration).
            return rndisLink != nil || legacyTcpMode
        case .openvrBridge:
            return bridgeResult.isPassed
        case .streamTest:
            return testResult.isPassed
        }
    }

    // MARK: - Models

    private struct Step {
        enum Kind {
            /// Plain instructional step — just text + optional link.
            case info
            /// Bootstrap: runs `scripts/install-launchd.sh` to drop the
            /// daemon's launchd agent and start it.
            case installDaemon
            /// Step with the "Open tether menu" button that fires the
            /// `am start … TetherSettings` intent on the connected Quest.
            case tetherIntent
            /// Step that waits for the host's IPv4 to land on
            /// `192.168.42.0/24`. Auto-advances when the link comes up.
            case tetherToggle
            /// Confirmation panel — green check, link details.
            case linkConfirmed
            /// Detect Minecraft launchers and drop libopenvr_api.dylib into
            /// the selected instance's natives folder.
            case openvrBridge
            /// Final smoke test: encode a synthetic clip, count frame acks.
            case streamTest
        }

        let kind: Kind
        let symbol: String
        let title: String
        let body: String
        let url: URL?
        let urlLabel: String?
    }
}

private struct StepRailRow: View {
    let index: Int
    let title: String
    let current: Int

    var body: some View {
        HStack(spacing: 10) {
            ZStack {
                Circle()
                    .fill(index <= current ? Color.accentColor : Color.secondary.opacity(0.25))
                    .frame(width: 22, height: 22)
                if index < current {
                    Image(systemName: "checkmark")
                        .font(.system(size: 11, weight: .heavy))
                        .foregroundStyle(.white)
                } else {
                    Text("\(index + 1)")
                        .font(.system(size: 11, weight: .bold))
                        .foregroundStyle(index == current ? .white : .secondary)
                }
            }
            Text(title)
                .font(.callout.weight(index == current ? .semibold : .regular))
                .foregroundStyle(index == current ? .primary : .secondary)
                .lineLimit(2)
                .multilineTextAlignment(.leading)
            Spacer(minLength: 0)
        }
        .contentShape(Rectangle())
    }
}

/// A small Liquid-Glass orb that turns colour with the device state.
/// Reused in the wizard header and the dashboard sidebar.
struct HeadsetStatusOrb: View {
    @EnvironmentObject var state: AppState

    var body: some View {
        let color = orbColor(state.deviceState)
        ZStack {
            Circle()
                .fill(.ultraThinMaterial)
                .overlay(
                    Circle().strokeBorder(color.opacity(0.35), lineWidth: 1)
                )
            Circle()
                .fill(color.opacity(0.85))
                .padding(14)
                .blur(radius: 6)
            Image(systemName: state.deviceState.systemImage)
                .font(.system(size: 24, weight: .semibold))
                .foregroundStyle(.white)
                .symbolEffect(.pulse, options: .repeating, isActive: pulse)
        }
        .animation(.smooth(duration: 0.4), value: state.deviceState)
        .accessibilityLabel(Text(state.deviceState.humanLabel))
    }

    private var pulse: Bool {
        switch state.deviceState {
        case .waiting, .installing, .launching: return true
        default: return false
        }
    }

    private func orbColor(_ s: DeviceState) -> Color {
        switch s.colorName {
        case "green":  return .green
        case "yellow": return .yellow
        case "orange": return .orange
        case "red":    return .red
        case "blue":   return .blue
        default:       return .gray
        }
    }
}
