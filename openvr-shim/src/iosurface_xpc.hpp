// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// XPC mach-service client that ships an `IOSurfaceRef` (mach send-right)
// to the FuVR daemon, keyed by a 64-bit token. Mirrors the contract used
// by the OpenXR runtime in `runtime-macos/src/iosurface_xpc_client.mm`,
// just rebuilt here so the openvr-shim does not depend on the runtime
// target. The underlying mach port is arch-neutral, so this works
// transparently across Rosetta 2 (x86_64 game ↔ arm64 daemon).

#include <cstdint>

typedef struct __IOSurface* IOSurfaceRef;

namespace fuvr::openvr_shim {

class IOSurfaceXpc {
 public:
  IOSurfaceXpc();
  ~IOSurfaceXpc();

  IOSurfaceXpc(const IOSurfaceXpc&) = delete;
  IOSurfaceXpc& operator=(const IOSurfaceXpc&) = delete;

  /// Connect to `com.fuvr.daemon.surface`. Idempotent.
  bool start();

  /// Fire-and-forget. Caller retains ownership of the IOSurface.
  void send(uint64_t token, IOSurfaceRef surface);

  /// Release the connection.
  void stop();

 private:
  void* conn_{nullptr};  // xpc_connection_t (kept opaque to keep the C++
                         // header free of <xpc/xpc.h>).
};

}  // namespace fuvr::openvr_shim
