// SPDX-License-Identifier: Apache-2.0
#include <mutex>
#include <unordered_map>

#include "fuvr/runtime.hpp"

namespace fuvr::runtime {

namespace detail {
std::unordered_map<uint64_t, Swapchain*>& swapchains() noexcept;
uint64_t nextHandleAlloc() noexcept;
std::mutex& globalMutex() noexcept;
}  // namespace detail

namespace {

constexpr int64_t kSupportedFormats[] = {
    37,  // VK_FORMAT_R8G8B8A8_UNORM
    43,  // VK_FORMAT_R8G8B8A8_SRGB
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
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  auto sc = std::make_unique<Swapchain>();
  sc->session = s;
  sc->width = info->width;
  sc->height = info->height;
  sc->format = info->format;
  sc->arraySize = info->arraySize;
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
  if (lookupSwapchain(handle) == nullptr || count == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  (void)images;
  if (capacity == 0) {
    *count = 0;
    return XR_SUCCESS;
  }
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

XrResult xrAcquireSwapchainImage_impl(XrSwapchain handle,
                                       const XrSwapchainImageAcquireInfo*,
                                       uint32_t* index) noexcept {
  Swapchain* sc = lookupSwapchain(handle);
  if (sc == nullptr || index == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  *index = sc->acquiredIndex;
  sc->acquiredIndex = (sc->acquiredIndex + 1) % 3;
  return XR_SUCCESS;
}

XrResult xrWaitSwapchainImage_impl(XrSwapchain handle,
                                    const XrSwapchainImageWaitInfo*) noexcept {
  return lookupSwapchain(handle) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult xrReleaseSwapchainImage_impl(XrSwapchain handle,
                                       const XrSwapchainImageReleaseInfo*) noexcept {
  return lookupSwapchain(handle) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

}  // namespace fuvr::runtime
