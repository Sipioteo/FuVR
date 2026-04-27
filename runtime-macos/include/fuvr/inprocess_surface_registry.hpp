// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr {

// Test-only in-process IOSurface handoff registry. Activated by setting
// FUVR_INPROCESS_HANDOFF=1 in the env so the runtime and the daemon — when
// linked into the same process for unit tests — can exchange IOSurfaces
// without going through the XPC mach service.
//
// `put` CFRetains the surface. `take` removes the entry and transfers the
// retain to the caller (caller must CFRelease).
class InProcessSurfaceRegistry {
 public:
  static InProcessSurfaceRegistry& shared();

  void put(uint64_t token, IOSurfaceRef surface);
  IOSurfaceRef take(uint64_t token);

 private:
  InProcessSurfaceRegistry() = default;
  InProcessSurfaceRegistry(const InProcessSurfaceRegistry&) = delete;
  InProcessSurfaceRegistry& operator=(const InProcessSurfaceRegistry&) = delete;
};

}  // namespace fuvr
