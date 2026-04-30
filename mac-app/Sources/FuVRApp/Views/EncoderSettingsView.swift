// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

struct EncoderSettingsView: View {
    @AppStorage(SettingsKey.codec)        private var codec: String = VideoCodec.hevc.rawValue
    @AppStorage(SettingsKey.bitrateMbps)  private var bitrateMbps: Int = 150
    @AppStorage(SettingsKey.refreshRate)  private var refreshRate: Int = 90
    @AppStorage(SettingsKey.perEyeWidth)  private var perEyeWidth: Int = 2064
    @AppStorage(SettingsKey.perEyeHeight) private var perEyeHeight: Int = 2208
    @AppStorage(SettingsKey.audioEnabled) private var audioEnabled: Bool = true

    private let refreshChoices: [Int] = [72, 90, 120]
    private let presetResolutions: [(String, Int, Int)] = [
        ("Quest 3 native", 2064, 2208),
        ("1.5x", 3072, 3216),
        ("0.75x", 1536, 1632)
    ]

    var body: some View {
        Form {
            Section("Codec") {
                Picker("Video codec", selection: $codec) {
                    Text("HEVC").tag(VideoCodec.hevc.rawValue)
                    Text("H.264").tag(VideoCodec.h264.rawValue)
                    Text("AV1 (reserved)").tag(VideoCodec.av1.rawValue).disabled(true)
                }
                .pickerStyle(.segmented)
                Toggle("Audio enabled", isOn: $audioEnabled)
            }
            Section("Bitrate") {
                HStack {
                    Slider(value: Binding(
                        get: { Double(bitrateMbps) },
                        set: { bitrateMbps = Int($0) }
                    ), in: 20...500, step: 5)
                    Text("\(bitrateMbps) Mb/s").monospacedDigit().frame(width: 90, alignment: .trailing)
                }
            }
            Section("Refresh") {
                Picker("Refresh rate", selection: $refreshRate) {
                    ForEach(refreshChoices, id: \.self) { v in Text("\(v) Hz").tag(v) }
                }.pickerStyle(.segmented)
            }
            Section("Per-eye resolution") {
                Picker("Preset", selection: presetSelection) {
                    ForEach(0..<presetResolutions.count, id: \.self) { i in
                        Text(presetResolutions[i].0).tag(i)
                    }
                    Text("Custom").tag(-1)
                }
                HStack {
                    Stepper("Width \(perEyeWidth)", value: $perEyeWidth, in: 512...4096, step: 16)
                    Stepper("Height \(perEyeHeight)", value: $perEyeHeight, in: 512...4096, step: 16)
                }
            }
        }
        .formStyle(.grouped)
        .padding()
        .navigationTitle("Encoder")
        // Republish env vars whenever the user touches the Encoder UI so
        // the next Blender launch picks up the change. Cheap (3 launchctl
        // setenvs); idempotent.
        .onChange(of: bitrateMbps) { _ in republishEnv() }
        .onChange(of: codec)       { _ in republishEnv() }
        .onChange(of: refreshRate) { _ in republishEnv() }
        .onAppear { republishEnv() }
    }

    private func republishEnv() {
        AppState.publishEncoderEnv(
            bitrateMbps: bitrateMbps,
            codec: codec,
            refreshHz: refreshRate)
    }

    private var presetSelection: Binding<Int> {
        Binding(
            get: {
                presetResolutions.firstIndex(where: { $0.1 == perEyeWidth && $0.2 == perEyeHeight }) ?? -1
            },
            set: { idx in
                guard idx >= 0, idx < presetResolutions.count else { return }
                perEyeWidth = presetResolutions[idx].1
                perEyeHeight = presetResolutions[idx].2
            }
        )
    }
}
