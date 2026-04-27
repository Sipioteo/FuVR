// SPDX-License-Identifier: Apache-2.0
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <gtest/gtest.h>

#include <cstdlib>

#include "fuvr/inprocess_surface_registry.hpp"
#include "fuvr/iosurface_bridge.hpp"
#include "fuvr/iosurface_xpc_service.hpp"

namespace {

IOSurfaceRef makeSurface(uint32_t w, uint32_t h) {
  const void* keys[] = {kIOSurfaceWidth, kIOSurfaceHeight,
                        kIOSurfaceBytesPerElement, kIOSurfacePixelFormat};
  CFNumberRef wn = CFNumberCreate(nullptr, kCFNumberSInt32Type, &w);
  CFNumberRef hn = CFNumberCreate(nullptr, kCFNumberSInt32Type, &h);
  uint32_t bpe = 4;
  CFNumberRef bn = CFNumberCreate(nullptr, kCFNumberSInt32Type, &bpe);
  uint32_t fmt = 'BGRA';
  CFNumberRef fn = CFNumberCreate(nullptr, kCFNumberSInt32Type, &fmt);
  const void* values[] = {wn, hn, bn, fn};
  CFDictionaryRef d = CFDictionaryCreate(nullptr, keys, values, 4,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
  IOSurfaceRef s = IOSurfaceCreate(d);
  CFRelease(d);
  CFRelease(wn);
  CFRelease(hn);
  CFRelease(bn);
  CFRelease(fn);
  return s;
}

}  // namespace

TEST(IOSurfaceXpc, InProcessHandoff_RoundTrip) {
  ::setenv("FUVR_INPROCESS_HANDOFF", "1", 1);

  IOSurfaceRef surf = makeSurface(64, 64);
  ASSERT_NE(surf, nullptr);
  const uint32_t origId = IOSurfaceGetID(surf);

  fuvr::InProcessSurfaceRegistry::shared().put(42, surf);

  // The bridge takes from the registry, wraps in a CVPixelBuffer, and the
  // pixel buffer's IOSurface should match the original ID.
  CVPixelBufferRef pb =
      fuvr::daemon::pixelBufferFromToken(nullptr, 42);
  ASSERT_NE(pb, nullptr);
  IOSurfaceRef seen = CVPixelBufferGetIOSurface(pb);
  ASSERT_NE(seen, nullptr);
  EXPECT_EQ(IOSurfaceGetID(seen), origId);

  CFRelease(pb);
  CFRelease(surf);
  ::unsetenv("FUVR_INPROCESS_HANDOFF");
}

TEST(IOSurfaceXpc, EndToEndXpc_SkippedWithoutEnv) {
  if (::getenv("FUVR_E2E_XPC") == nullptr) {
    GTEST_SKIP() << "FUVR_E2E_XPC not set; skipping launchd-dependent test";
  }
  // The full XPC round-trip requires the daemon to be registered with launchd
  // for `com.fuvr.daemon.surface`. Pass 3 ships the plist + installer; CI
  // does not bootstrap launchd, hence the gate.
  auto svc = fuvr::daemon::IOSurfaceXpcService::create(
      "com.fuvr.daemon.surface.test");
  ASSERT_NE(svc, nullptr);
  // No client side here in this minimal smoke; the runtime side has its own
  // dedicated test that wires both halves together.
  SUCCEED();
}
