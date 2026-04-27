// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fuvr/runtime.hpp"
#include "fuvr/xr_fuvr_metal_enable.h"

namespace fr = fuvr::runtime;

namespace fuvr::runtime {
extern XrResult xrCreateInstance_impl(const XrInstanceCreateInfo*,
                                       XrInstance*) noexcept;
extern XrResult xrDestroyInstance_impl(XrInstance) noexcept;
extern XrResult xrCreateSession_impl(XrInstance, const XrSessionCreateInfo*,
                                      XrSession*) noexcept;
extern XrResult xrDestroySession_impl(XrSession) noexcept;
extern XrResult xrCreateSwapchain_impl(XrSession,
                                        const XrSwapchainCreateInfo*,
                                        XrSwapchain*) noexcept;
extern XrResult xrEnumerateSwapchainImages_impl(
    XrSwapchain, uint32_t, uint32_t*,
    XrSwapchainImageBaseHeader*) noexcept;
}

TEST(SwapchainImages, EnumerateReturnsMetalTextures) {
  XrInstanceCreateInfo ici{};
  ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
  XrInstance inst = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateInstance_impl(&ici, &inst), XR_SUCCESS);
  XrSessionCreateInfo sci{};
  sci.type = XR_TYPE_SESSION_CREATE_INFO;
  XrSession sess = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateSession_impl(inst, &sci, &sess), XR_SUCCESS);

  XrSwapchainCreateInfo info{};
  info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
  info.width = 256;
  info.height = 128;
  info.format = 37;
  info.arraySize = 1;
  info.faceCount = 1;
  info.mipCount = 1;
  info.sampleCount = 1;
  XrSwapchain sc = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateSwapchain_impl(sess, &info, &sc), XR_SUCCESS);

  uint32_t count = 0;
  ASSERT_EQ(fr::xrEnumerateSwapchainImages_impl(sc, 0, &count, nullptr),
            XR_SUCCESS);
  EXPECT_EQ(count, 3u);

  std::vector<XrSwapchainImageMetalFUVR> imgs(count);
  for (auto& im : imgs) {
    im.type = XR_TYPE_SWAPCHAIN_IMAGE_METAL_FUVR;
    im.next = nullptr;
  }
  ASSERT_EQ(fr::xrEnumerateSwapchainImages_impl(
                sc, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data())),
            XR_SUCCESS);
  EXPECT_EQ(count, 3u);
  for (auto& im : imgs) {
    EXPECT_NE(im.texture, nullptr);
  }

  fr::xrDestroySession_impl(sess);
  fr::xrDestroyInstance_impl(inst);
}
