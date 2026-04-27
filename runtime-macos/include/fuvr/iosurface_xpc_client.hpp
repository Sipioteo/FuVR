// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>

typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr::runtime {

// XPC mach-service client that ships an IOSurface mach send-right to the
// daemon, keyed by `token`. See ADR-0007.
class IOSurfaceXpcClient {
 public:
  static std::unique_ptr<IOSurfaceXpcClient> create(const char* serviceName);
  virtual ~IOSurfaceXpcClient() = default;

  // Fire-and-forget. The caller retains ownership of `surface`.
  virtual void sendSurface(uint64_t token, IOSurfaceRef surface) = 0;
};

}  // namespace fuvr::runtime
