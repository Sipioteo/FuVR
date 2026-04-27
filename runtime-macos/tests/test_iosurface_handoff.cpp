// SPDX-License-Identifier: Apache-2.0
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <gtest/gtest.h>
#include <mach/mach.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <vector>

#include "fuvr/iosurface_swapchain.hpp"

using fuvr::runtime::iosurfaceCreateMachSendRight;
using fuvr::runtime::iosurfaceFromMachSendRight;
using fuvr::runtime::iosurfaceID;
using fuvr::runtime::iosurfaceReleaseMachSendRight;

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

// Why: macOS SCM_RIGHTS only transports file descriptors, not mach send-rights;
// real cross-task transfer requires mach_msg with MACH_MSG_TYPE_COPY_SEND. For
// in-process round-tripping (and the daemon currently lives in the same task
// during early dev) the mach port name itself is portable. This test sends
// the name as raw payload over a socketpair and verifies the lookup yields
// the same IOSurface (matching IOSurfaceGetID).
TEST(IOSurfaceHandoff, MachSendRightRoundTripPreservesID) {
  IOSurfaceRef surf = makeSurface(64, 64);
  ASSERT_NE(surf, nullptr);
  const uint32_t origId = iosurfaceID(surf);

  uint32_t right = iosurfaceCreateMachSendRight(surf);
  ASSERT_NE(right, 0u);

  int sv[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

  ASSERT_EQ(::send(sv[0], &right, sizeof(right), 0),
            static_cast<ssize_t>(sizeof(right)));
  uint32_t recvRight = 0;
  ASSERT_EQ(::recv(sv[1], &recvRight, sizeof(recvRight), 0),
            static_cast<ssize_t>(sizeof(recvRight)));
  ASSERT_EQ(recvRight, right);

  IOSurfaceRef looked = iosurfaceFromMachSendRight(recvRight);
  ASSERT_NE(looked, nullptr);
  EXPECT_EQ(iosurfaceID(looked), origId);

  CFRelease(looked);
  iosurfaceReleaseMachSendRight(right);
  CFRelease(surf);
  ::close(sv[0]);
  ::close(sv[1]);
}
