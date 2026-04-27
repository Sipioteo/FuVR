// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path runtimeConfigPath() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  return fs::path(home) / "Library" / "Application Support" / "OpenXR" / "1" /
         "active_runtime.json";
}

int doRegister(const std::string& dylibPath) {
  if (dylibPath.empty()) {
    std::fprintf(stderr,
                 "fuvr-register: missing path to libfuvr_openxr_runtime.dylib\n");
    return 2;
  }
  const fs::path target = runtimeConfigPath();
  if (target.empty()) {
    std::fprintf(stderr, "fuvr-register: HOME is not set\n");
    return 2;
  }
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  if (ec) {
    std::fprintf(stderr, "fuvr-register: cannot create %s: %s\n",
                 target.parent_path().c_str(), ec.message().c_str());
    return 1;
  }
  fs::path absDylib = fs::weakly_canonical(dylibPath, ec);
  if (ec) {
    absDylib = dylibPath;
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
  if (!out.good()) {
    return 1;
  }
  std::printf("fuvr-register: wrote %s -> %s\n", target.c_str(),
              absDylib.c_str());
  return 0;
}

int doUnregister() {
  const fs::path target = runtimeConfigPath();
  if (target.empty()) {
    return 2;
  }
  std::error_code ec;
  if (!fs::exists(target, ec)) {
    std::printf("fuvr-register: nothing to remove (%s)\n", target.c_str());
    return 0;
  }
  fs::remove(target, ec);
  if (ec) {
    std::fprintf(stderr, "fuvr-register: failed to remove %s: %s\n",
                 target.c_str(), ec.message().c_str());
    return 1;
  }
  std::printf("fuvr-register: removed %s\n", target.c_str());
  return 0;
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
