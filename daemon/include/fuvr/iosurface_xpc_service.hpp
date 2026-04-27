// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>

typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr::daemon {

// XPC mach-service listener that receives IOSurface mach send-rights from the
// runtime, keyed by a per-frame `token` correlated with SubmitFrameRequest.
// See ADR-0007.
class IOSurfaceXpcService {
 public:
  static std::unique_ptr<IOSurfaceXpcService> create(const char* serviceName);
  virtual ~IOSurfaceXpcService() = default;

  // Take and consume the IOSurface that arrived under `token`. Returns
  // nullptr if not yet received; caller may retry within the grace window
  // documented in ADR-0007. The retain is transferred to the caller.
  virtual IOSurfaceRef takeSurface(uint64_t token) = 0;

  // Drop everything older than `maxAgeNs` from the pending map.
  virtual void evict(uint64_t maxAgeNs) = 0;
};

}  // namespace fuvr::daemon
