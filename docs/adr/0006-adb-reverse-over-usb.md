# ADR-0006: ADB reverse tunnel for the wired transport

- Status: accepted
- Date: 2026-04-27

## Context

A Quest connected via USB-C is, electrically and from the host's perspective,
an Android device. To stream from the Mac, we have three viable options:

1. **ADB reverse port forwarding.** `adb reverse tcp:9943 tcp:9943` creates a
   TCP tunnel through the USB cable. Quest connects to `127.0.0.1:9943`,
   which actually reaches the Mac.
2. **Android Open Accessory (AOA) protocol.** A bulk USB endpoint pair the
   Android device negotiates with a host that announces itself as an
   accessory. Lower overhead than ADB. Requires the Quest to leave Developer
   Mode debug context; we'd own the USB device.
3. **Custom USB driver / DriverKit.** Talk to the Quest as a generic USB
   device. Full driver work; requires user-side install and entitlements.

ALVR's wired mode and Lumen both use ADB reverse. Numbers from SPEC §3.1.4:
~5–10 ms additional latency vs raw bulk, throughput limit ~200–300 Mbps on
USB 3 due to ADB protocol overhead. Our HEVC bitrate target (100–150 Mbps)
fits comfortably.

## Decision

Wired transport in v1 is **ADB reverse**, implemented in `transport-usb`. Mac
side spawns `adb reverse` and accepts loopback TCP on port 9943; Quest side
connects to `127.0.0.1:9943`.

AOA is the documented escalation path if measurements during M0 show ADB
overhead is unacceptable.

## Consequences

- Zero driver work, zero user-side install beyond enabling Developer Mode on
  the Quest. Compatible with the Quest in standard debug context.
- We pay the ADB protocol overhead. Acceptable per measured numbers.
- We have an `adb` binary as a runtime dependency. Bundled in distribution
  (10 MB); detect and bail if missing.

## Alternatives considered

- **AOA.** Listed as escalation. Forces the Quest into accessory mode and
  takes over the USB device exclusively — incompatible with the developer
  workflow we want. Will revisit if M0 measurements demand it.
- **DriverKit USB.** Real driver work, signing, installer complexity.
  Disproportionate for v1.
