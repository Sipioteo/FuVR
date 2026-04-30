// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// Tiny logger. Emits to stderr AND tees to /tmp/fuvr_shim.log.
//
// Reason for the file tee: under GDLauncher / Fabric, the JVM redirects
// fd 2 to a pipe that block-buffers our writes for many seconds before
// the parent's Log4j drain happens. The file write is synchronous and
// gives us immediate visibility regardless of how the JVM treats stderr.
//
// Toggled by `FUVR_OPENVR_LOG=0` to mute (default ON during bring-up).

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace fuvr::openvr_shim {

bool log_enabled();

inline FILE* log_file() {
  static FILE* f = std::fopen("/tmp/fuvr_shim.log", "a");
  return f;  // may be null if /tmp is unwritable; callers null-check.
}

inline std::mutex& log_mutex() {
  static std::mutex m;
  return m;
}

inline void log_printf(const char* fmt, ...) {
  if (!log_enabled()) return;
  std::lock_guard<std::mutex> lk(log_mutex());

  va_list ap;
  va_start(ap, fmt);
  std::fputs("[fuvr-openvr] ", stderr);
  va_list ap_stderr;
  va_copy(ap_stderr, ap);
  std::vfprintf(stderr, fmt, ap_stderr);
  va_end(ap_stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);

  if (FILE* f = log_file()) {
    std::fputs("[fuvr-openvr] ", f);
    std::vfprintf(f, fmt, ap);
    std::fputc('\n', f);
    std::fflush(f);
  }
  va_end(ap);
}

}  // namespace fuvr::openvr_shim

#define FUVR_LOG(...) ::fuvr::openvr_shim::log_printf(__VA_ARGS__)
