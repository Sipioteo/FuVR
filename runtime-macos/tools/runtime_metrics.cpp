// SPDX-License-Identifier: Apache-2.0
#include <openxr/openxr.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "fuvr/internal/diag.hpp"

namespace fr = fuvr::runtime;

extern "C" XrResult xrCreateInstance(const XrInstanceCreateInfo*, XrInstance*);
extern "C" XrResult xrDestroyInstance(XrInstance);

namespace fuvr::runtime {
extern XrResult xrCreateInstance_impl(const XrInstanceCreateInfo*,
                                       XrInstance*) noexcept;
extern XrResult xrDestroyInstance_impl(XrInstance) noexcept;
extern XrResult xrCreateSession_impl(XrInstance, const XrSessionCreateInfo*,
                                      XrSession*) noexcept;
extern XrResult xrDestroySession_impl(XrSession) noexcept;
}

int main() {
  XrInstanceCreateInfo ici{};
  ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
  std::strncpy(ici.applicationInfo.applicationName, "fuvr-runtime-metrics",
               XR_MAX_APPLICATION_NAME_SIZE - 1);
  ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
  XrInstance inst = XR_NULL_HANDLE;
  if (fr::xrCreateInstance_impl(&ici, &inst) != XR_SUCCESS) {
    std::fprintf(stderr, "xrCreateInstance failed\n");
    return 1;
  }

  XrSessionCreateInfo sci{};
  sci.type = XR_TYPE_SESSION_CREATE_INFO;
  XrSession sess = XR_NULL_HANDLE;
  if (fr::xrCreateSession_impl(inst, &sci, &sess) != XR_SUCCESS) {
    std::fprintf(stderr, "xrCreateSession failed\n");
    fr::xrDestroyInstance_impl(inst);
    return 1;
  }

  for (int i = 0; i < 600; ++i) {
    auto s = fr::diag::encoderStatsForSession(sess);
    std::printf(
        "[%4ds] samples=%u meanEnc=%.2fms p95=%.2fms fps=%.1f bitrate=%.1fMbps\n",
        i, s.sampleCount, s.meanEncodeMs, s.p95EncodeMs, s.fps, s.bitrateMbps);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  fr::xrDestroySession_impl(sess);
  fr::xrDestroyInstance_impl(inst);
  return 0;
}
