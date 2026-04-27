// SPDX-License-Identifier: Apache-2.0
//
// CLI argument parsing for `fuvr-vdisplay-helper`. Split out so it can be
// unit-tested without compiling the Objective-C++ helper.
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace fuvr::vdisplay {

struct ModeArg {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t hz     = 0;
};

struct CliArgs {
    uint32_t              width      = 4128;
    uint32_t              height     = 2208;
    uint32_t              hz         = 90;
    std::string           name       = "FuVR Mirror";
    std::vector<ModeArg>  modes;        // explicit --mode tuples (overrides w/h/hz)
    bool                  listOnly   = false;
    bool                  watchdog   = true;  // stdin-EOF death is the default
    bool                  parseError = false;
    std::string           parseErrorMsg;
};

inline bool parseU32(const char* s, uint32_t* out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(s, &end, 10);
    if (end == s) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

// Parse a single "WxHxR" triple, e.g. "1920x1080x60".
inline bool parseModeTriple(const char* s, ModeArg* m) {
    if (!s || !*s || !m) return false;
    const std::string str(s);
    size_t x1 = str.find('x');
    if (x1 == std::string::npos) return false;
    size_t x2 = str.find('x', x1 + 1);
    if (x2 == std::string::npos) return false;
    uint32_t w = 0, h = 0, r = 0;
    if (!parseU32(str.substr(0, x1).c_str(), &w)) return false;
    if (!parseU32(str.substr(x1 + 1, x2 - x1 - 1).c_str(), &h)) return false;
    if (!parseU32(str.substr(x2 + 1).c_str(), &r)) return false;
    if (w == 0 || h == 0 || r == 0) return false;
    *m = ModeArg{w, h, r};
    return true;
}

inline CliArgs parseCli(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        const char* opt = argv[i];
        if (!std::strcmp(opt, "--width")  && i + 1 < argc) {
            if (!parseU32(argv[++i], &a.width)) { a.parseError = true; a.parseErrorMsg = "--width"; }
        } else if (!std::strcmp(opt, "--height") && i + 1 < argc) {
            if (!parseU32(argv[++i], &a.height)) { a.parseError = true; a.parseErrorMsg = "--height"; }
        } else if (!std::strcmp(opt, "--refresh") && i + 1 < argc) {
            if (!parseU32(argv[++i], &a.hz)) { a.parseError = true; a.parseErrorMsg = "--refresh"; }
        } else if (!std::strcmp(opt, "--name") && i + 1 < argc) {
            a.name = argv[++i];
        } else if (!std::strcmp(opt, "--mode") && i + 1 < argc) {
            ModeArg m;
            if (!parseModeTriple(argv[++i], &m)) {
                a.parseError = true;
                a.parseErrorMsg = "--mode (expected WxHxR)";
            } else {
                a.modes.push_back(m);
            }
        } else if (!std::strcmp(opt, "--list")) {
            a.listOnly = true;
        } else if (!std::strcmp(opt, "--watchdog")) {
            a.watchdog = true;
        } else if (!std::strcmp(opt, "--no-watchdog")) {
            a.watchdog = false;
        } else {
            a.parseError = true;
            a.parseErrorMsg = std::string("unknown arg: ") + opt;
        }
    }
    return a;
}

}  // namespace fuvr::vdisplay
