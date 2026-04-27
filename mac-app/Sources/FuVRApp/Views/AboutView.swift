// SPDX-License-Identifier: Apache-2.0
import SwiftUI

struct AboutView: View {
    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "visionpro")
                .font(.system(size: 56))
                .foregroundStyle(.tint)
            Text("FuVR").font(.largeTitle).bold()
            Text("Open-source PCVR streaming for macOS")
                .foregroundStyle(.secondary)
            Text("Apache-2.0").font(.caption).foregroundStyle(.tertiary)
            Divider().padding(.vertical, 8)
            Text("Control surface v0.1 · JSON over Unix domain socket")
                .font(.callout).foregroundStyle(.secondary)
            Spacer()
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
