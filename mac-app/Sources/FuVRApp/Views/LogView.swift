// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

struct LogView: View {
    @EnvironmentObject var state: AppState
    @State private var filter: String = ""
    @State private var levelMask: Set<LogLine.Level> = [.info, .warn, .error, .debug]
    @State private var paused: Bool = false
    @State private var pausedSnapshot: [LogLine] = []

    private let allLevels: [LogLine.Level] = [.debug, .info, .warn, .error]

    private var sourceLog: [LogLine] {
        paused ? pausedSnapshot : state.logs
    }

    private var filtered: [LogLine] {
        sourceLog.filter { line in
            levelMask.contains(line.level) &&
            (filter.isEmpty
                || line.message.localizedCaseInsensitiveContains(filter)
                || line.source.localizedCaseInsensitiveContains(filter))
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            controlBar
            Divider()
            logScroller
        }
        .navigationTitle("Log")
    }

    private var controlBar: some View {
        HStack(spacing: 12) {
            TextField("Search", text: $filter)
                .textFieldStyle(.roundedBorder)
                .frame(maxWidth: 280)

            HStack(spacing: 6) {
                ForEach(allLevels, id: \.self) { lvl in
                    levelChip(lvl)
                }
            }

            Spacer()

            Toggle(isOn: Binding(
                get: { paused },
                set: { newValue in
                    if newValue {
                        pausedSnapshot = state.logs
                    }
                    paused = newValue
                }
            )) {
                Label(paused ? "Paused" : "Live",
                      systemImage: paused ? "pause.circle.fill" : "play.circle.fill")
            }
            .toggleStyle(.button)

            Button("Clear") { state.logs.removeAll() }
        }
        .padding()
    }

    private func levelChip(_ lvl: LogLine.Level) -> some View {
        let active = levelMask.contains(lvl)
        return Button {
            if active { levelMask.remove(lvl) }
            else      { levelMask.insert(lvl) }
        } label: {
            Text(lvl.rawValue.uppercased())
                .font(.caption.monospaced().bold())
                .padding(.horizontal, 8).padding(.vertical, 4)
                .foregroundStyle(active ? Color.white : color(for: lvl))
                .background(active ? color(for: lvl) : Color.clear,
                            in: Capsule())
                .overlay(Capsule().strokeBorder(color(for: lvl), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }

    private var logScroller: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(Array(filtered.enumerated()), id: \.offset) { idx, line in
                        row(line).id(idx)
                    }
                }
                .padding(.horizontal, 14).padding(.vertical, 8)
            }
            .onChange(of: filtered.count) { _, n in
                guard !paused else { return }
                withAnimation(.easeOut(duration: 0.15)) {
                    proxy.scrollTo(max(0, n - 1), anchor: .bottom)
                }
            }
        }
    }

    private func row(_ l: LogLine) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(timestamp(l.timestampMs))
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 92, alignment: .leading)
            Text(l.level.rawValue.uppercased())
                .font(.system(.caption, design: .monospaced).bold())
                .foregroundStyle(color(for: l.level))
                .frame(width: 56, alignment: .leading)
            Text(l.source)
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 96, alignment: .leading)
            Text(l.message).font(.system(.body, design: .monospaced))
            Spacer(minLength: 0)
        }
        .padding(.vertical, 1)
    }

    private func color(for l: LogLine.Level) -> Color {
        switch l {
        case .debug: return .secondary
        case .info:  return .blue
        case .warn:  return .yellow
        case .error: return .red
        }
    }

    private func timestamp(_ ms: UInt64) -> String {
        let d = Date(timeIntervalSince1970: TimeInterval(ms) / 1000)
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f.string(from: d)
    }
}
