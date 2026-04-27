// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fuvr {

enum class ConnectionState {
    Discovering,    // mDNS browse for `_fuvr._udp.local.`
    Connecting,     // TCP handshake to discovered daemon
    Negotiating,    // helloFromQuest sent, awaiting helloFromMac
    WaitingForFrames,
    Connected,      // first video frame arrived; UI fades out
};

const char* state_text(ConnectionState s);

// Pure-logic bitmap-rasterizer for the "Connecting to fuvrd..." overlay.
//
// The real shipping path renders text via a precomposed PNG atlas vendored
// at app/src/main/assets/font_atlas.png + font_atlas.json. That asset has
// not been generated yet (TODO; tracked in quest-app/TODO.md). Until then,
// this rasterizer paints a procedural 8x16 monospace bitmap into an RGBA8
// buffer of fixed dimensions (kQuadW x kQuadH) so the OpenXR quad layer
// always has non-empty content to display.
class ConnectionUi {
public:
    static constexpr int kQuadW = 512;
    static constexpr int kQuadH = 64;

    // Renders `state_text(state)` centered on a dim background into `out`,
    // sized exactly kQuadW * kQuadH * 4 bytes (RGBA8). Returns false if
    // out is the wrong size.
    static bool render(ConnectionState state, std::vector<uint8_t>& out);

    // The on-disk asset path the app would prefer (loaded by the Kotlin
    // bootstrap and uploaded into a GLES2 texture by the compositor).
    static const char* asset_path() { return "font_atlas.png"; }
    static const char* metadata_path() { return "font_atlas.json"; }
};

}
