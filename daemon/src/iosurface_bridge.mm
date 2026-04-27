// SPDX-License-Identifier: Apache-2.0
#include "fuvr/iosurface_bridge.hpp"

#import <Foundation/Foundation.h>
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "fuvr/inprocess_surface_registry.hpp"
#include "fuvr/iosurface_xpc_service.hpp"

namespace fuvr::daemon {

namespace {

std::atomic<uint64_t> g_missing{0};

bool inprocessMode() {
  const char* v = std::getenv("FUVR_INPROCESS_HANDOFF");
  return v && std::strcmp(v, "0") != 0 && v[0] != '\0';
}

CVPixelBufferRef wrap(IOSurfaceRef surface) {
  if (surface == nullptr) return nullptr;
  NSDictionary* attrs = @{(id)kCVPixelBufferIOSurfacePropertiesKey : @{}};
  CVPixelBufferRef pb = nullptr;
  CVReturn rv = CVPixelBufferCreateWithIOSurface(
      kCFAllocatorDefault, surface, (__bridge CFDictionaryRef)attrs, &pb);
  CFRelease(surface);
  if (rv != kCVReturnSuccess) return nullptr;
  return pb;
}

}  // namespace

CVPixelBufferRef pixelBufferFromMachPort(mach_port_t port) {
  if (port == MACH_PORT_NULL) return nullptr;
  IOSurfaceRef surface = IOSurfaceLookupFromMachPort(port);
  return wrap(surface);
}

CVPixelBufferRef pixelBufferFromToken(IOSurfaceXpcService* xpcService,
                                      uint64_t surfaceToken) {
  IOSurfaceRef surface = nullptr;

  if (inprocessMode()) {
    surface = fuvr::InProcessSurfaceRegistry::shared().take(surfaceToken);
  } else if (xpcService != nullptr) {
    // Grace window: 16 ms (~1 frame at 60 Hz) to absorb XPC/UDS reorder.
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(16);
    while (true) {
      surface = xpcService->takeSurface(surfaceToken);
      if (surface != nullptr) break;
      if (clock::now() >= deadline) break;
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  }

  if (surface == nullptr) {
    g_missing.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
  }
  return wrap(surface);
}

uint64_t missingSurfaceCount() {
  return g_missing.load(std::memory_order_relaxed);
}

}  // namespace fuvr::daemon
