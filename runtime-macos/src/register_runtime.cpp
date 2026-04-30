// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Khronos OpenXR loader on macOS searches XDG-style paths (NOT Apple-style
// `~/Library/Application Support/...`):
//   1. $XR_RUNTIME_JSON   (env override)
//   2. $XDG_CONFIG_HOME/openxr/1/active_runtime.json
//   3. $HOME/.config/openxr/1/active_runtime.json   ← canonical user path
//   4. /etc/xdg/openxr/1/active_runtime.json
//
// This caught us once: writing only to `~/Library/Application Support/...`
// looks like the Apple-y thing to do but Blender (which bundles the
// Khronos loader) ignores that location entirely. We now write to BOTH
// for belt-and-braces — the XDG path is the one that actually matters,
// the Apple path is a courtesy for any tool that uses it.
std::vector<fs::path> runtimeConfigPaths() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') return {};
  std::vector<fs::path> out;
  // 1. Khronos XDG (canonical) — honour XDG_CONFIG_HOME if set.
  fs::path xdg;
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && x[0] != '\0') {
    xdg = fs::path(x);
  } else {
    xdg = fs::path(home) / ".config";
  }
  out.push_back(xdg / "openxr" / "1" / "active_runtime.json");
  // 2. Apple-style (legacy compatibility for tools that prefer it).
  out.push_back(fs::path(home) / "Library" / "Application Support" / "OpenXR" /
                "1" / "active_runtime.json");
  return out;
}

int writeManifest(const fs::path& target, const fs::path& absDylib) {
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  if (ec) {
    std::fprintf(stderr, "fuvr-register: cannot create %s: %s\n",
                 target.parent_path().c_str(), ec.message().c_str());
    return 1;
  }
  std::ofstream out(target, std::ios::trunc);
  if (!out.good()) {
    std::fprintf(stderr, "fuvr-register: cannot open %s\n", target.c_str());
    return 1;
  }
  out << "{\n"
      << "  \"file_format_version\": \"1.0.0\",\n"
      << "  \"runtime\": {\n"
      << "    \"library_path\": \"" << absDylib.string() << "\",\n"
      << "    \"name\": \"FuVR\"\n"
      << "  }\n"
      << "}\n";
  if (!out.good()) return 1;
  std::printf("fuvr-register: wrote %s -> %s\n", target.c_str(),
              absDylib.c_str());
  return 0;
}

int doRegister(const std::string& dylibPath) {
  if (dylibPath.empty()) {
    std::fprintf(stderr,
                 "fuvr-register: missing path to libfuvr_openxr_runtime.dylib\n");
    return 2;
  }
  const auto targets = runtimeConfigPaths();
  if (targets.empty()) {
    std::fprintf(stderr, "fuvr-register: HOME is not set\n");
    return 2;
  }
  std::error_code ec;
  fs::path absDylib = fs::weakly_canonical(dylibPath, ec);
  if (ec) absDylib = dylibPath;

  // Write every known location. Failure on the legacy Apple path is
  // non-fatal — only the XDG path actually matters for the Khronos loader.
  int rc = 0;
  for (size_t i = 0; i < targets.size(); ++i) {
    int r = writeManifest(targets[i], absDylib);
    if (i == 0 && r != 0) rc = r;  // canonical path failure is fatal
  }
  return rc;
}

int doUnregister() {
  const auto targets = runtimeConfigPaths();
  if (targets.empty()) return 2;
  int rc = 0;
  for (const auto& target : targets) {
    std::error_code ec;
    if (!fs::exists(target, ec)) {
      std::printf("fuvr-register: nothing to remove (%s)\n", target.c_str());
      continue;
    }
    fs::remove(target, ec);
    if (ec) {
      std::fprintf(stderr, "fuvr-register: failed to remove %s: %s\n",
                   target.c_str(), ec.message().c_str());
      rc = 1;
      continue;
    }
    std::printf("fuvr-register: removed %s\n", target.c_str());
  }
  return rc;
}

void printUsage() {
  std::fprintf(stderr,
               "usage: fuvr-register <path-to-libfuvr_openxr_runtime.dylib>\n"
               "       fuvr-register --unregister\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printUsage();
    return 2;
  }
  if (std::strcmp(argv[1], "--unregister") == 0 ||
      std::strcmp(argv[1], "-u") == 0) {
    return doUnregister();
  }
  if (std::strcmp(argv[1], "--help") == 0 ||
      std::strcmp(argv[1], "-h") == 0) {
    printUsage();
    return 0;
  }
  return doRegister(argv[1]);
}
