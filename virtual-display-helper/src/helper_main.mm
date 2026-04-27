// SPDX-License-Identifier: Apache-2.0
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>

#include "private/CGVirtualDisplay.h"
#include "clamp_dimensions.h"
#include "cli_args.h"

namespace {

// Enumerate currently-active CGDirectDisplayIDs and emit one per line on
// stdout. Used by `fuvrctl status` and tooling that wants to discover whether
// any virtual display is alive without spawning a new one.
int runListMode() {
    static constexpr uint32_t kMaxDisplays = 32;
    CGDirectDisplayID ids[kMaxDisplays] = {0};
    uint32_t count = 0;
    if (CGGetActiveDisplayList(kMaxDisplays, ids, &count) != kCGErrorSuccess) {
        std::fprintf(stderr, "fuvr-vdisplay-helper: CGGetActiveDisplayList failed\n");
        return 1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        std::printf("%u\n", static_cast<unsigned>(ids[i]));
    }
    std::fflush(stdout);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        auto args = fuvr::vdisplay::parseCli(argc, argv);
        if (args.parseError) {
            std::fprintf(stderr,
                         "fuvr-vdisplay-helper: argument error (%s)\n",
                         args.parseErrorMsg.c_str());
            return 64;  // EX_USAGE
        }

        if (args.listOnly) {
            return runListMode();
        }

        // Resolve the mode list: explicit --mode tuples take priority. Otherwise
        // we fall back to clamp_dimensions on the width/height/refresh triple.
        std::vector<fuvr::vdisplay::Mode> modes;
        bool split = false;
        if (!args.modes.empty()) {
            for (const auto& m : args.modes) {
                modes.push_back(fuvr::vdisplay::Mode{m.width, m.height,
                                                    static_cast<double>(m.hz)});
            }
        } else {
            modes = fuvr::vdisplay::clamp_dimensions(args.width, args.height,
                                                    args.hz, &split);
        }
        if (split) {
            std::fprintf(stderr,
                         "fuvr-vdisplay-helper: WARNING %ux%u exceeds M4/M5 pipe-0 limit (%u px); "
                         "splitting into %zu modes.\n",
                         args.width, args.height,
                         fuvr::vdisplay::kPipe0PixelLimit, modes.size());
        }

        CGVirtualDisplayDescriptor* desc = [[CGVirtualDisplayDescriptor alloc] init];
        [desc setName:[NSString stringWithUTF8String:args.name.c_str()]];
        [desc setMaxPixelsWide:args.width];
        [desc setMaxPixelsHigh:args.height];
        [desc setSizeInMillimeters:CGSizeMake(600, 340)];
        [desc setProductID:0xF0F0];
        [desc setVendorID:0x46565652];  // 'FVVR'
        [desc setSerialNum:0x00000001];

        CGVirtualDisplay* disp = [[CGVirtualDisplay alloc] initWithDescriptor:desc];
        if (!disp) {
            std::fprintf(stderr, "fuvr-vdisplay-helper: CGVirtualDisplay alloc failed (private API may have changed)\n");
            return 2;
        }

        NSMutableArray* modeObjs = [NSMutableArray arrayWithCapacity:modes.size()];
        for (const auto& m : modes) {
            CGVirtualDisplayMode* mo = [[CGVirtualDisplayMode alloc] initWithWidth:m.width
                                                                            height:m.height
                                                                       refreshRate:m.hz];
            if (mo) [modeObjs addObject:mo];
        }
        CGVirtualDisplaySettings* settings = [[CGVirtualDisplaySettings alloc] init];
        [settings setHiDPI:1];
        [settings setModes:modeObjs];

        if (![disp applySettings:settings]) {
            std::fprintf(stderr, "fuvr-vdisplay-helper: applySettings failed\n");
            return 3;
        }

        uint32_t did = [disp displayID];
        std::printf("display_id=%u\n", did);
        std::fflush(stdout);

        // Watchdog: stdin-EOF death. When the parent closes its write end, the
        // read loop exits and we drop the CGVirtualDisplay. This is the
        // simplest cross-version mechanism (no PR_SET_PDEATHSIG on macOS, no
        // Mach exception ports). `--no-watchdog` keeps the helper alive on EOF
        // for standalone debugging — Ctrl-D detaches but the display persists
        // until SIGTERM.
        char buf[64];
        while (true) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) continue;
            if (n == 0) {
                if (args.watchdog) break;
                // Without watchdog, fall asleep instead of busy-looping.
                pause();
            } else {
                if (errno == EINTR) continue;
                break;
            }
        }
        return 0;
    }
}
