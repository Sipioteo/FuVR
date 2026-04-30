// SPDX-License-Identifier: Apache-2.0
#include "log.hpp"

#include <cstdlib>
#include <cstring>

namespace fuvr::openvr_shim {

namespace {
// Cache the env-var lookup. Games hit this on every Submit() so a strcmp
// on each call is wasteful.
//
// Default-ON during Vivecraft bring-up so we can observe the shim from
// GDLauncher without setting an env var. Set FUVR_OPENVR_LOG=0 to mute.
int g_enabled = -1;
}

bool log_enabled() {
  if (g_enabled < 0) {
    const char* v = std::getenv("FUVR_OPENVR_LOG");
    if (v != nullptr && v[0] != '\0' && std::strcmp(v, "0") == 0) {
      g_enabled = 0;
    } else {
      g_enabled = 1;
    }
  }
  return g_enabled != 0;
}

}  // namespace fuvr::openvr_shim
