# SPDX-License-Identifier: Apache-2.0
# FuVR wire protocol — Cap'n Proto schema
#
# This schema is the contract between the macOS side and the Quest side.
# It is the single source of truth: macOS encoder/transport, Rust transport
# crate, and Android NDK client all generate code from this file.
#
# Generated with `capnp id` — DO NOT change once published, or bump major.

@0xb1f5d4f7c2a830e5;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("fuvr::proto");

# ---------------------------------------------------------------------------
# Primitive math types
# ---------------------------------------------------------------------------

struct Vec3 {
  x @0 :Float32;
  y @1 :Float32;
  z @2 :Float32;
}

struct Quat {
  # XR convention: (x, y, z, w) with w being the scalar component.
  x @0 :Float32;
  y @1 :Float32;
  z @2 :Float32;
  w @3 :Float32;
}

struct Pose {
  position    @0 :Vec3;
  orientation @1 :Quat;
}

struct Fov {
  # OpenXR convention: half-angle radians, signed (left/down negative).
  angleLeft  @0 :Float32;
  angleRight @1 :Float32;
  angleUp    @2 :Float32;
  angleDown  @3 :Float32;
}

struct ViewState {
  pose @0 :Pose;
  fov  @1 :Fov;
}

# ---------------------------------------------------------------------------
# Pose / input upstream (Quest -> Mac)
# ---------------------------------------------------------------------------

# Headset pose snapshot. Sent at 500-1000 Hz so the Mac can run prediction
# and serve `xrLocateViews` for arbitrary `displayTime` requested by apps.
struct HmdPoseSample {
  # Quest's monotonic clock, nanoseconds. Used to estimate one-way delay
  # via the round trip on the control channel.
  timestampNs       @0 :UInt64;
  # Predicted display time on Quest for this sample, also Quest clock ns.
  predictedDisplayTimeNs @1 :UInt64;
  leftView          @2 :ViewState;
  rightView         @3 :ViewState;
  # Linear & angular velocity of the head, useful for server-side prediction.
  linearVelocity    @4 :Vec3;
  angularVelocity   @5 :Vec3;
}

enum ControllerHand {
  left  @0;
  right @1;
}

struct ControllerSample {
  hand            @0 :ControllerHand;
  isActive        @1 :Bool;
  pose            @2 :Pose;
  linearVelocity  @3 :Vec3;
  angularVelocity @4 :Vec3;
}

# Touch Plus controller input set. Indexed exactly like the OpenXR
# /interaction_profiles/oculus/touch_plus_controller binding.
struct TouchInputState {
  hand              @0 :ControllerHand;
  trigger           @1 :Float32;  # 0..1
  squeeze           @2 :Float32;  # 0..1
  thumbstickX       @3 :Float32;  # -1..1
  thumbstickY       @4 :Float32;  # -1..1
  thumbstickClick   @5 :Bool;
  thumbstickTouch   @6 :Bool;
  triggerTouch      @7 :Bool;
  buttonAClick      @8 :Bool;     # right hand A / left hand X (re-mapped)
  buttonAtouch      @9 :Bool;
  buttonBClick     @10 :Bool;     # right hand B / left hand Y
  buttonBtouch     @11 :Bool;
  systemClick      @12 :Bool;     # Meta button (left hand only)
  thumbrest        @13 :Float32;  # capacitive thumbrest
}

# Per-frame upstream packet. Pose channel.
struct UpstreamFrame {
  # Frame this sample answers. 0 if free-running (no associated rendered frame).
  correlationFrameId @0 :UInt64;
  hmd                @1 :HmdPoseSample;
  controllers        @2 :List(ControllerSample);
  inputs             @3 :List(TouchInputState);
}

# ---------------------------------------------------------------------------
# Video downstream (Mac -> Quest)
# ---------------------------------------------------------------------------

enum VideoCodec {
  hevc @0;
  h264 @1;
  av1  @2;  # not in v1, reserved
}

enum VideoFlag {
  idr       @0;  # this fragment belongs to a keyframe
  endOfFrame @1; # last fragment of the frame
  csdHeader @2;  # codec specific data (SPS/PPS/VPS) inline
}

# Header that prefixes every video fragment on the wire. The codec payload
# follows immediately after this header in raw bytes.
struct VideoFragmentHeader {
  frameId         @0 :UInt64;
  # Mac monotonic clock when rendering started, ns.
  renderStartNs   @1 :UInt64;
  # Pose used by the application to render this exact frame.
  # Quest needs this to apply ATW correctly.
  renderedLeft    @2 :ViewState;
  renderedRight   @3 :ViewState;
  totalSizeBytes  @4 :UInt32;
  fragmentIndex   @5 :UInt32;
  fragmentCount   @6 :UInt32;
  codec           @7 :VideoCodec;
  flags           @8 :UInt16;  # bitfield of VideoFlag
  # Display time on Quest for which this frame was prepared (Mac estimate
  # in Quest clock domain). Compositor may use this if late.
  targetDisplayTimeNs @9 :UInt64;
}

# ---------------------------------------------------------------------------
# Audio downstream (Mac -> Quest)
# ---------------------------------------------------------------------------

enum AudioCodec {
  opus @0;
  pcm  @1;
}

struct AudioPacket {
  timestampNs @0 :UInt64;
  codec       @1 :AudioCodec;
  sampleRate  @2 :UInt32;
  channels    @3 :UInt8;
  payload     @4 :Data;
}

# ---------------------------------------------------------------------------
# Haptics downstream (Mac -> Quest)
# ---------------------------------------------------------------------------

struct HapticPulse {
  hand      @0 :ControllerHand;
  durationNs @1 :UInt64;
  amplitude  @2 :Float32;  # 0..1
  frequencyHz @3 :Float32;
}

# ---------------------------------------------------------------------------
# Control channel (bidirectional, low-frequency)
# ---------------------------------------------------------------------------

# Sent once at session start by the Quest to advertise its capabilities,
# so the Mac side can pick codec, resolution, refresh rate, etc.
struct DeviceCapabilities {
  deviceModel       @0 :Text;        # "Quest 3", "Quest 3S", ...
  systemVersion     @1 :Text;
  perEyeWidth       @2 :UInt32;
  perEyeHeight      @3 :UInt32;
  refreshRatesHz    @4 :List(UInt32);
  supportedCodecs   @5 :List(VideoCodec);
  hasHandTracking   @6 :Bool;
  hasEyeTracking    @7 :Bool;
}

# Sent by Mac to negotiate the session parameters chosen for this run.
struct SessionConfig {
  perEyeWidth       @0 :UInt32;
  perEyeHeight      @1 :UInt32;
  refreshRateHz     @2 :UInt32;
  videoCodec        @3 :VideoCodec;
  videoBitrateBps   @4 :UInt32;
  audioEnabled      @5 :Bool;
}

# Lightweight clock sync. Quest sends Ping with t0; Mac echoes with t1, t2.
# Quest computes offset and one-way delay.
struct ClockSync {
  union {
    ping  :group { t0 @0 :UInt64; }
    pong  :group {
      t0 @1 :UInt64;
      t1 @2 :UInt64;  # Mac receive
      t2 @3 :UInt64;  # Mac send
    }
  }
}

struct ControlMessage {
  union {
    helloFromQuest @0 :DeviceCapabilities;
    helloFromMac   @1 :SessionConfig;
    sessionStart   @2 :Void;
    sessionStop    @3 :Void;
    clockSync      @4 :ClockSync;
    haptic         @5 :HapticPulse;
    error          @6 :Text;
  }
}
