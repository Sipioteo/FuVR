// SPDX-License-Identifier: Apache-2.0
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <gtest/gtest.h>

#include <cstdlib>

#include "fuvr/inprocess_surface_registry.hpp"
#include "fuvr/iosurface_xpc_client.hpp"

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

TEST(IOSurfaceXpcClient, InProcessSendPopulatesRegistry) {
  ::setenv("FUVR_INPROCESS_HANDOFF", "1", 1);

  IOSurfaceRef surf = makeSurface(32, 32);
  ASSERT_NE(surf, nullptr);
  const uint32_t origId = IOSurfaceGetID(surf);

  auto client = fuvr::runtime::IOSurfaceXpcClient::create(
      "com.fuvr.daemon.surface");
  ASSERT_NE(client, nullptr);
  client->sendSurface(7, surf);

  IOSurfaceRef taken = fuvr::InProcessSurfaceRegistry::shared().take(7);
  ASSERT_NE(taken, nullptr);
  EXPECT_EQ(IOSurfaceGetID(taken), origId);

  CFRelease(taken);
  CFRelease(surf);
  ::unsetenv("FUVR_INPROCESS_HANDOFF");
}
