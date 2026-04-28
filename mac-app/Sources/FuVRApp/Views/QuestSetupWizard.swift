// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRADB

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

    private let steps: [Step] = [
        Step(
            symbol: "person.badge.key",
            title: "Create a Meta developer account",
            body: "On a phone or computer, sign in at developer.oculus.com and accept the developer agreement. The same Meta account you use on the headset must be enrolled.",
            url: URL(string: "https://developer.oculus.com/manage")!,
            urlLabel: "Open Meta developer portal"
        ),
        Step(
            symbol: "iphone.gen3",
            title: "Open the Meta Horizon app on your phone",
            body: "Tap **Menu › Devices**, pick your headset, then **Headset Settings › Developer Mode**. Toggle it ON. If the toggle is greyed out, your account is not yet enrolled (step 1).",
            url: nil,
            urlLabel: nil
        ),
        Step(
            symbol: "headphones.circle",
            title: "Reboot the headset",
            body: "Press **Power › Restart** on the Quest, or hold the side button for 3 seconds. Developer Mode does not take effect until after a reboot.",
            url: nil,
            urlLabel: nil
        ),
        Step(
            symbol: "cable.connector",
            title: "Plug the headset into this Mac",
            body: "Use the USB-C cable that came with the headset (a data cable, not charge-only). Put the headset on — a system dialog asks **Allow USB debugging?** Tap **Always allow from this computer** then **Allow**.",
            url: nil,
            urlLabel: nil
        ),
        Step(
            symbol: "checkmark.seal",
            title: "Confirm the connection",
            body: "When FuVR sees the headset, the dot above turns green and the dashboard switches to **Connected**. If it stays grey, unplug and replug the cable, then re-tap **Allow** inside the headset.",
            url: nil,
            urlLabel: nil
        ),
    ]

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
                Text("Five steps · roughly 3 minutes")
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
        }
        .padding(20)
    }

    // MARK: - Models

    private struct Step {
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
