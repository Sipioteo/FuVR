// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

struct LogView: View {
    @EnvironmentObject var state: AppState
    @State private var filter: String = ""
    @State private var minLevel: LogLine.Level = .debug

    private let levels: [LogLine.Level] = [.debug, .info, .warn, .error]

    private var filtered: [LogLine] {
        let levelOrder: [LogLine.Level: Int] = [.debug: 0, .info: 1, .warn: 2, .error: 3]
        let threshold = levelOrder[minLevel] ?? 0
        return state.logs.filter { line in
            (levelOrder[line.level] ?? 0) >= threshold &&
            (filter.isEmpty || line.message.localizedCaseInsensitiveContains(filter)
                            || line.source.localizedCaseInsensitiveContains(filter))
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                TextField("Filter", text: $filter).textFieldStyle(.roundedBorder).frame(maxWidth: 300)
                Picker("Level", selection: $minLevel) {
                    ForEach(levels, id: \.self) { Text($0.rawValue.uppercased()).tag($0) }
                }.pickerStyle(.segmented).frame(maxWidth: 280)
                Spacer()
                Button("Clear") { state.logs.removeAll() }
            }
            .padding()

            Divider()

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
                    withAnimation { proxy.scrollTo(max(0, n - 1), anchor: .bottom) }
                }
            }
        }
        .navigationTitle("Log")
    }

    private func row(_ l: LogLine) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(timestamp(l.timestampMs))
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 92, alignment: .leading)
            Text(l.level.rawValue.uppercased())
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(color(for: l.level))
                .frame(width: 56, alignment: .leading)
            Text(l.source)
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 80, alignment: .leading)
            Text(l.message).font(.system(.body, design: .monospaced))
            Spacer(minLength: 0)
        }
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
