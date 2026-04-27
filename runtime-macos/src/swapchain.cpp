// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

#include "fuvr/iosurface_swapchain.hpp"
#include "fuvr/runtime.hpp"
#include "fuvr/xr_fuvr_metal_enable.h"

namespace fuvr::runtime {

namespace detail {
std::unordered_map<uint64_t, Swapchain*>& swapchains() noexcept;
uint64_t nextHandleAlloc() noexcept;
std::mutex& globalMutex() noexcept;
}  // namespace detail

namespace {

// Why: when the session uses XR_KHR_metal_enable these are MTLPixelFormat
// raw values. Order matters — apps pick the first they support.
//   80 = MTLPixelFormatBGRA8Unorm        (matches IOSurface alloc default)
//   81 = MTLPixelFormatBGRA8Unorm_sRGB
//   70 = MTLPixelFormatRGBA8Unorm
//   71 = MTLPixelFormatRGBA8Unorm_sRGB
constexpr int64_t kSupportedFormats[] = {
    80,
    81,
    70,
    71,
};

Swapchain* lookupSwapchain(XrSwapchain h) noexcept {
  std::lock_guard<std::mutex> lk(detail::globalMutex());
  auto& m = detail::swapchains();
  auto it = m.find(reinterpret_cast<uint64_t>(h));
  return it == m.end() ? nullptr : it->second;
}

}  // namespace

XrResult xrEnumerateSwapchainFormats_impl(XrSession sessionHandle,
                                           uint32_t capacity, uint32_t* count,
                                           int64_t* formats) noexcept {
  if (lookupSession(sessionHandle) == nullptr || count == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  const uint32_t total =
      sizeof(kSupportedFormats) / sizeof(kSupportedFormats[0]);
  if (formats == nullptr || capacity == 0) {
    *count = total;
    return XR_SUCCESS;
  }
  if (capacity < total) {
    *count = total;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  for (uint32_t i = 0; i < total; ++i) {
    formats[i] = kSupportedFormats[i];
  }
  *count = total;
  return XR_SUCCESS;
}

XrResult xrCreateSwapchain_impl(XrSession sessionHandle,
                                 const XrSwapchainCreateInfo* info,
                                 XrSwapchain* out) noexcept {
  if (std::getenv("FUVR_RT_DEBUG") && info)
    std::fprintf(stderr,
                 "[fuvr-rt] xrCreateSwapchain(format=%lld w=%u h=%u "
                 "arraySize=%u sampleCount=%u faceCount=%u mip=%u usage=0x%llx)\n",
                 (long long)info->format, info->width, info->height,
                 info->arraySize, info->sampleCount, info->faceCount,
                 info->mipCount, (unsigned long long)info->usageFlags);
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || out == nullptr) {
    if (std::getenv("FUVR_RT_DEBUG"))
      std::fprintf(stderr, "[fuvr-rt]   -> handle invalid\n");
    return XR_ERROR_HANDLE_INVALID;
  }
  auto sc = std::make_unique<Swapchain>();
  sc->session = s;
  sc->width = info->width;
  sc->height = info->height;
  sc->format = info->format;
  sc->arraySize = info->arraySize;
  sc->images = allocateIOSurfaceSwapchain(s->metalDevice, info->width,
                                          info->height, 3);
  if (sc->images.empty()) {
    return XR_ERROR_OUT_OF_MEMORY;
  }
  // Per-image FOV slots — see Swapchain definition. Sized to images[].
  sc->imageLeftFov.assign(sc->images.size(), Fov{});
  sc->imageRightFov.assign(sc->images.size(), Fov{});
  const uint64_t h = detail::nextHandleAlloc();
  sc->handle = reinterpret_cast<XrSwapchain>(h);
  Swapchain* raw = sc.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::swapchains().emplace(h, raw);
  }
  {
    std::lock_guard<std::mutex> lk(s->mutex);
    s->swapchains.push_back(std::move(sc));
  }
  *out = raw->handle;
  return XR_SUCCESS;
}

XrResult xrDestroySwapchain_impl(XrSwapchain handle) noexcept {
  Swapchain* sc = lookupSwapchain(handle);
  if (sc == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  Session* s = sc->session;
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::swapchains().erase(reinterpret_cast<uint64_t>(handle));
  }
  std::lock_guard<std::mutex> lk(s->mutex);
  for (auto it = s->swapchains.begin(); it != s->swapchains.end(); ++it) {
    if (it->get() == sc) {
      s->swapchains.erase(it);
      break;
    }
  }
  return XR_SUCCESS;
}

XrResult xrEnumerateSwapchainImages_impl(
    XrSwapchain handle, uint32_t capacity, uint32_t* count,
    XrSwapchainImageBaseHeader* images) noexcept {
  Swapchain* sc = lookupSwapchain(handle);
  if (sc == nullptr || count == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  const uint32_t total = static_cast<uint32_t>(sc->images.size());
  if (images == nullptr || capacity == 0) {
    *count = total;
    return XR_SUCCESS;
  }
  if (capacity < total) {
    *count = total;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  // Why: XR_KHR_metal_enable and XR_FUVR_metal_enable share an identical
  // layout {type, next, void* texture}. We honour whichever type the caller
  // pre-filled. Default to KHR for apps (Blender) that don't fill it.
  auto* mtl = reinterpret_cast<XrSwapchainImageMetalKHR*>(images);
  const XrStructureType requestedType =
      images[0].type == XR_TYPE_SWAPCHAIN_IMAGE_METAL_FUVR
          ? XR_TYPE_SWAPCHAIN_IMAGE_METAL_FUVR
          : XR_TYPE_SWAPCHAIN_IMAGE_METAL_KHR;
  for (uint32_t i = 0; i < total; ++i) {
    mtl[i].type = requestedType;
    mtl[i].next = nullptr;
    mtl[i].texture = sc->images[i]->mtlTexture;
  }
  *count = total;
  return XR_SUCCESS;
}

XrResult xrAcquireSwapchainImage_impl(XrSwapchain handle,
                                       const XrSwapchainImageAcquireInfo*,
                                       uint32_t* index) noexcept {
  Swapchain* sc = lookupSwapchain(handle);
  if (sc == nullptr || index == nullptr || sc->images.empty()) {
    return XR_ERROR_HANDLE_INVALID;
  }
  *index = sc->acquiredIndex;
  sc->acquiredIndex =
      (sc->acquiredIndex + 1) % static_cast<uint32_t>(sc->images.size());
  return XR_SUCCESS;
}

XrResult xrWaitSwapchainImage_impl(XrSwapchain handle,
                                    const XrSwapchainImageWaitInfo*) noexcept {
  return lookupSwapchain(handle) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult xrReleaseSwapchainImage_impl(XrSwapchain handle,
                                       const XrSwapchainImageReleaseInfo*) noexcept {
  Swapchain* sc = lookupSwapchain(handle);
  if (sc == nullptr) return XR_ERROR_HANDLE_INVALID;
  if (!sc->images.empty()) {
    // Track which image was just released so xrEndFrame can locate the
    // IOSurface backing the frame the app just finished rendering.
    const uint32_t idx =
        (sc->acquiredIndex + static_cast<uint32_t>(sc->images.size()) - 1) %
        static_cast<uint32_t>(sc->images.size());
    sc->lastReleasedIndex = idx;
    // Bind the FOV that xrLocateViews most recently returned to *this*
    // swapchain image. Apps render to the released image immediately after
    // locate→acquire→render→release, so the pending FOV at release time is
    // what was used to draw the pixels in this image. This is the per-frame
    // FOV xrEndFrame will later stamp into VideoFragmentHeader.
    if (sc->session != nullptr && idx < sc->imageLeftFov.size()) {
      sc->imageLeftFov[idx]  = sc->session->pendingLocateLeftFov;
      sc->imageRightFov[idx] = sc->session->pendingLocateRightFov;
    }
  }
  return XR_SUCCESS;
}

}  // namespace fuvr::runtime
