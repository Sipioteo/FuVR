# SPDX-License-Identifier: Apache-2.0
# Homebrew formula for FuVR. Intended to be served from a tap such as
# `luminos-srl/fuvr`. See docs/RELEASE.md for tap maintenance.
class Fuvr < Formula
  desc "Open-source PCVR streaming from Apple Silicon to Meta Quest"
  homepage "https://github.com/luminos-srl/FuVR"
  url "https://github.com/luminos-srl/FuVR/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "Apache-2.0"
  head "https://github.com/luminos-srl/FuVR.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "capnp" => :build
  depends_on "rust" => :build
  depends_on "pkg-config" => :build
  depends_on :macos
  depends_on macos: :sonoma

  def install
    # Generate Cap'n Proto bindings before configuring CMake.
    system "scripts/gen-proto.sh"

    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build", "--parallel"

    # Daemon binary.
    bin.install "build/daemon/fuvrd"

    # OpenXR runtime dylib + helper scripts.
    lib.install "build/runtime-macos/libfuvr_openxr_runtime.dylib"
    libexec.install "build/virtual-display-helper/fuvr-vdisplay-helper"

    # Convenience CLI.
    bin.install "scripts/fuvrctl"
    libexec.install "scripts/install-launchd.sh"
    libexec.install "scripts/uninstall-launchd.sh"
    libexec.install "scripts/install-quest.sh"
    libexec.install "scripts/gen-proto.sh"

    # Sample LaunchAgent plist (user installs into ~/Library/LaunchAgents).
    pkgshare.install "daemon/launchd/com.fuvr.daemon.plist"
  end

  def caveats
    <<~EOS
      FuVR ships a launchd agent that runs `fuvrd` on login and exposes the
      `com.fuvr.daemon.surface` mach service. To enable it for your user:

        fuvrctl install

      That command will:
        1. Copy the LaunchAgent plist to ~/Library/LaunchAgents
        2. Bootstrap the service via `launchctl bootstrap gui/$UID`
        3. Register the OpenXR runtime under
           ~/Library/Application Support/OpenXR/1/active_runtime.json

      To remove:

        fuvrctl uninstall

      The Quest companion app is distributed via App Lab / SideQuest, not
      Homebrew. See https://github.com/luminos-srl/FuVR for current links.
    EOS
  end

  service do
    run [opt_bin/"fuvrd"]
    keep_alive true
    log_path "/tmp/fuvrd.out.log"
    error_log_path "/tmp/fuvrd.err.log"
  end

  test do
    assert_match "fuvrd", shell_output("#{bin}/fuvrd --version 2>&1", 0)
    assert_match "fuvrctl", shell_output("#{bin}/fuvrctl --help 2>&1", 0)
  end
end
