# FuVR — Hardware test runbook

Sequence to run the first time you sit down with a Mac + Quest + USB-C cable
in front of you. Assumes pass 4 is the current HEAD: all components build,
all unit tests pass, daemon and runtime can connect via XPC and UDS, the
mDNS browser is shipped, the Quest decoder pipeline is plumbed end-to-end.

## 0. Hardware preflight (15 minutes)

### Mac side

```bash
# Toolchain & runtime deps
brew install capnp ninja pkg-config opus android-platform-tools
rustup default stable
xcode-select --install     # if not already done

# Verify Quest is visible to ADB
adb devices
# expected:  1WMHHxxxxxxxxx  device
# if "unauthorized": don the headset, accept the USB-debug prompt.
# if no device: cable is power-only, swap it (Meta Link cable or any
# certified-data USB-C 3.x cable).

# Capture hardware identity for the M0 report
mkdir -p bench-output
{
  echo "## Mac"
  system_profiler SPHardwareDataType | grep -E "(Model Name|Chip|Memory)"
  echo
  echo "## Quest"
  echo "model:    $(adb shell getprop ro.product.model)"
  echo "build:    $(adb shell getprop ro.build.version.release)"
  echo "incr:     $(adb shell getprop ro.build.version.incremental)"
  echo "serial:   $(adb shell getprop ro.serialno)"
} | tee bench-output/hw-info.txt
```

### Build everything (one-time)

```bash
cd /Users/sipioteo/Developer/FuVR

# Generate Cap'n Proto bindings for every consumer
./scripts/gen-proto.sh

# Rust transport (must be built before the daemon links the FFI)
cargo build --manifest-path transport/Cargo.toml --workspace --release

# Top-level CMake build (runtime, encoder, daemon, vdisplay helper)
cmake -S . -B build -G Ninja
cmake --build build -j 8

# Mac control app
swift build --package-path mac-app -c release

# Quest APK (requires Android SDK + NDK)
cd quest-app && ./gradlew assembleDebug --no-daemon && cd -

# Quick sanity: every test must be green before moving on
ctest --test-dir build --output-on-failure
cargo test --manifest-path transport/Cargo.toml --workspace
swift test --package-path mac-app
```

## 1. M0 spike #1 — ADB throughput & RTT (5 minutes)

**Question (SPEC §5.M0.1):** Does ADB reverse over USB sustain ≥100 Mbps with
RTT under 15 ms?

```bash
# Set up the reverse tunnel
adb reverse tcp:9943 tcp:9943
# Verify
adb reverse --list   # expected: tcp:9943 tcp:9943

# Build the spike binary if not already built
cargo build --manifest-path transport/Cargo.toml --release --bin fuvr-transport

# Run the loopback bench. The Quest end is just adb's TCP loopback for now;
# this measures the ADB tunnel itself, not the Quest app.
./transport/target/release/fuvr-transport loopback-bench \
    --transport adb --duration 30 --bitrate 150000000 \
    | tee bench-output/M0-1-adb-loopback.txt

# Pass criteria: throughput ≥ 100 Mbps sustained, p95 RTT ≤ 15 ms.
# If failure: see docs/TROUBLESHOOTING.md "Latency feels off"; try a
# different cable or USB port.
```

## 2. M0 spike #2 — VideoToolbox HEVC encode latency (5 minutes)

**Question:** Does HEVC `RealTime=true` encode under 15 ms on M2/M3?

```bash
# The synthetic encoder produces 256 frames of 4128×2208 @ 90 Hz HEVC and
# logs encode timing per frame.
./build/encoder-macos/fuvr-encode-synthetic \
    --width 4128 --height 2208 --fps 90 --frames 256 --codec hevc \
    --bitrate 100000000 --output /tmp/synth.h265 \
    | tee bench-output/M0-2-encode-hevc.txt

# Inspect distribution
./build/encoder-macos/fuvr-encode-synthetic --analyze bench-output/M0-2-encode-hevc.txt

# Pass criteria: mean encode ≤ 10 ms, p95 ≤ 15 ms.
# If failure: rerun with --codec h264 --low-latency-rc to try the
# WWDC21 path (H.264 only, but lower latency).
```

## 3. M0 spike #3 — Quest decode + projection layer at 90 Hz (10 minutes)

**Question:** Does the Quest sustain UDP receive + MediaCodec decode +
OpenXR projection layer at 90 Hz?

```bash
# Install the Quest APK
./scripts/install-quest.sh

# Launch the daemon (foreground; logs to terminal)
./build/daemon/fuvrd &
FUVRD_PID=$!

# Launch the Mac app (mock data is fine for spike 3)
swift run --package-path mac-app FuVR &
APP_PID=$!

# Don the Quest, launch the FuVR app from the App Library.
# In the connection UI you should see "Discovering..." → "Connecting..." →
# "Connected", then a side-by-side test pattern from fuvr-encode-synthetic
# replayed at 90 Hz.

# Alongside, monitor decode timing and frame drops:
adb logcat -s fuvr-quest:* | tee bench-output/M0-3-quest-decode.txt &
LOGCAT_PID=$!

# Run for 60 seconds, then cleanup
sleep 60
kill $LOGCAT_PID $APP_PID $FUVRD_PID

# Pass criteria: q-metrics lines show fps ≥ 89.0 sustained, decode_p95_ms
# ≤ 8 ms, dropped_frames ≤ 5.
```

## 4. M0 spike #4 — `CGVirtualDisplay` on macOS 14/15/16 (5 minutes per macOS)

**Question:** Does the private CGVirtualDisplay API still work on each
target macOS release?

```bash
# Run on whichever macOS the Mac is on. Repeat on a second Mac (or VM)
# with a different macOS version if available.
./build/virtual-display-helper/fuvr-vdisplay-helper \
    --width 4128 --height 2208 --refresh 90 --name "FuVR Test" \
    | tee bench-output/M0-4-vdisplay-$(sw_vers -productVersion).txt &
HELPER_PID=$!

# In another terminal, verify the display appeared
sleep 1
displayplacer list | grep -i FuVR

# Cleanup
kill $HELPER_PID

# Pass criteria: display_id printed to stdout, displayplacer sees it.
# If failure on macOS 16+: see virtual-display-helper/docs/MACOS_QUIRKS.md.
```

## 5. First end-to-end run (M1 entry)

```bash
# Same sequence as spike 3 but with a real OpenXR app on the Mac.
# Easiest target: Blender 4.x with VR Scene Inspection enabled.

# 1. Register the runtime
./build/runtime-macos/fuvr-register

# 2. Verify
cat ~/Library/Application\ Support/OpenXR/1/active_runtime.json

# 3. Install launchd plist for the daemon (so XPC handoff works)
./scripts/install-launchd.sh

# 4. Launch the daemon (now via launchctl)
launchctl kickstart -k gui/$UID/com.fuvr.daemon

# 5. Plug the Quest, install + launch the FuVR Quest app

# 6. On the Mac, open Blender → Add-ons → enable "VR Scene Inspection"
#    → set "VR Setup" → Start VR Session

# 7. The Quest should display the Blender scene in stereo. Move your head.
#    Watch fuvr-runtime-metrics for live numbers:
./build/runtime-macos/fuvr-runtime-metrics --watch
```

## 6. Reporting back

Push `bench-output/` content (sanitised — strip serial numbers if
publishing) into a GitHub issue using the `hardware-test-report.yml`
template. The community needs concrete numbers to validate the M0
go/no-go decision per SPEC §5.M0.

## Cleanup

```bash
adb reverse --remove tcp:9943
launchctl bootout gui/$UID/com.fuvr.daemon 2>/dev/null || true
./build/runtime-macos/fuvr-register --unregister
```
