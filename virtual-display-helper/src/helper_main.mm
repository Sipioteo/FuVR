// SPDX-License-Identifier: Apache-2.0
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "private/CGVirtualDisplay.h"
#include "clamp_dimensions.h"

namespace {

struct Args {
  uint32_t width  = 4128;
  uint32_t height = 2208;
  uint32_t hz     = 90;
  const char* name = "FuVR Mirror";
};

bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  unsigned long v = std::strtoul(s, &end, 10);
  if (end == s) return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

Args parse_args(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--width")  && i + 1 < argc) parse_u32(argv[++i], &a.width);
    else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) parse_u32(argv[++i], &a.height);
    else if (!std::strcmp(argv[i], "--refresh") && i + 1 < argc) parse_u32(argv[++i], &a.hz);
    else if (!std::strcmp(argv[i], "--name")    && i + 1 < argc) a.name = argv[++i];
  }
  return a;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    Args args = parse_args(argc, argv);

    bool split = false;
    auto modes = fuvr::vdisplay::clamp_dimensions(args.width, args.height, args.hz, &split);
    if (split) {
      std::fprintf(stderr,
                   "fuvr-vdisplay-helper: WARNING %ux%u exceeds M4/M5 pipe-0 limit (%u px); "
                   "splitting into %zu modes.\n",
                   args.width, args.height, fuvr::vdisplay::kPipe0PixelLimit, modes.size());
    }

    CGVirtualDisplayDescriptor* desc = [[CGVirtualDisplayDescriptor alloc] init];
    [desc setName:[NSString stringWithUTF8String:args.name]];
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

    char buf[64];
    while (read(STDIN_FILENO, buf, sizeof(buf)) > 0) { /* parent owns lifetime */ }
    return 0;
  }
}
