// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import FuVRControl

struct TransportSettingsView: View {
    @AppStorage(SettingsKey.transportMode) private var mode: String = TransportMode.usb.rawValue
    @AppStorage(SettingsKey.wifiHost)      private var host: String = "192.168.1.10"
    @AppStorage(SettingsKey.wifiPort)      private var port: Int = 9943

    var body: some View {
        Form {
            Section("Mode") {
                Picker("Transport", selection: $mode) {
                    Text("USB / ADB reverse").tag(TransportMode.usb.rawValue)
                    Text("Wi-Fi (UDP)").tag(TransportMode.wifi.rawValue)
                }
                .pickerStyle(.segmented)
                Text(modeDescription)
                    .font(.callout).foregroundStyle(.secondary)
            }
            if mode == TransportMode.wifi.rawValue {
                Section("Wi-Fi target") {
                    TextField("Quest IP", text: $host)
                    Stepper("UDP port \(port)", value: $port, in: 1024...65535)
                }
            } else {
                Section("USB") {
                    Label("Plug Quest into the Mac and run `adb reverse tcp:9943 tcp:9943` once.",
                          systemImage: "cable.connector.horizontal")
                        .font(.callout)
                }
            }
        }
        .formStyle(.grouped)
        .padding()
        .navigationTitle("Transport")
    }

    private var modeDescription: String {
        switch mode {
        case TransportMode.wifi.rawValue:
            return "Wi-Fi 6 5GHz recommended. Higher latency, no cable."
        default:
            return "Lowest latency. Requires Quest in developer mode and ADB."
        }
    }
}
