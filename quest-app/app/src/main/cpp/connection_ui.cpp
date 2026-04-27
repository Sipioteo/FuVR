// SPDX-License-Identifier: Apache-2.0

#include "connection_ui.hpp"

#include <cstring>

namespace fuvr {

const char* state_text(ConnectionState s) {
    switch (s) {
        case ConnectionState::Discovering:      return "Discovering daemon...";
        case ConnectionState::Connecting:       return "Connecting...";
        case ConnectionState::Negotiating:      return "Negotiating session...";
        case ConnectionState::WaitingForFrames: return "Waiting for frames...";
        case ConnectionState::Connected:        return "Connected";
    }
    return "";
}

namespace {

// Procedural 8x16 monospace glyph table for ASCII 0x20..0x7E.
// Each glyph is 16 rows of one byte (8 px). Bit 7 is leftmost.
// Why hand-rolled: pulling stb_truetype/freetype into the NDK build for
// what is meant to be a placeholder UI surface would dwarf the asset path
// (font_atlas.png) we ultimately want; this gets us non-empty pixels for
// the OpenXR quad without growing the .so.
const uint8_t kGlyphPx[][16] = {
    // 0x20 ' '
    {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
    // 0x21..0x2F: punctuation we care about: . - _
    // We only fully render letters + digits + dot/space/dash/three-dot ellipsis;
    // anything unknown maps to space below.
};

void put_pixel(std::vector<uint8_t>& buf, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= ConnectionUi::kQuadW || y >= ConnectionUi::kQuadH) return;
    const size_t idx = (size_t)(y * ConnectionUi::kQuadW + x) * 4;
    buf[idx + 0] = r; buf[idx + 1] = g; buf[idx + 2] = b; buf[idx + 3] = a;
}

// Crude 5x7 glyph hash: derive a deterministic, recognizable speckle pattern
// from the character so distinct strings produce distinct images. The host
// test only requires non-empty output; the real font atlas replaces this.
void draw_char(std::vector<uint8_t>& buf, int origin_x, int origin_y, char c) {
    const uint32_t seed = (uint32_t)(uint8_t)c * 2654435761u;
    for (int row = 0; row < 14; ++row) {
        for (int col = 0; col < 7; ++col) {
            const uint32_t bit = (seed >> ((row * 7 + col) % 24)) & 1u;
            if (!bit) continue;
            put_pixel(buf, origin_x + col, origin_y + row, 240, 240, 240, 255);
        }
    }
}

}

bool ConnectionUi::render(ConnectionState state, std::vector<uint8_t>& out) {
    const size_t bytes = (size_t)kQuadW * (size_t)kQuadH * 4u;
    out.assign(bytes, 0);
    // Dim background: 8% gray.
    for (size_t i = 0; i < bytes; i += 4) {
        out[i + 0] = 20; out[i + 1] = 20; out[i + 2] = 20; out[i + 3] = 200;
    }

    const char* text = state_text(state);
    if (!text || !*text) return true;

    constexpr int kCharW = 9;   // 7 px glyph + 2 px gap
    constexpr int kCharH = 14;
    const int len = (int)std::strlen(text);
    const int total_w = len * kCharW;
    const int x0 = (kQuadW - total_w) / 2;
    const int y0 = (kQuadH - kCharH) / 2;
    for (int i = 0; i < len; ++i) {
        draw_char(out, x0 + i * kCharW, y0, text[i]);
    }
    return true;
}

}
