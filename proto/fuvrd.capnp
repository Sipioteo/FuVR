# SPDX-License-Identifier: Apache-2.0
# FuVR daemon RPC schema (runtime <-> fuvrd, mac-app <-> fuvrd).
#
# Distinct from `fuvr.capnp` (the Mac<->Quest wire schema) because this
# protocol evolves on its own cadence and is local-only — schema id changes
# here do not break compatibility with deployed Quest apps.
#
# Transport: length-prefixed Cap'n Proto packed messages over a Unix domain
# socket at $XDG_RUNTIME_DIR/fuvr/rpc.sock (fallback ~/Library/Caches/fuvr/rpc.sock).
#
# IOSurface handoff CANNOT ride this socket: macOS's SCM_RIGHTS only carries
# file descriptors, not mach send-rights, and IOSurfaces are mach-port-shaped.
# The runtime transfers the IOSurface over a parallel XPC mach service named
# `com.fuvr.daemon.surface` — see ADR-0007. The `surfaceToken` field in
# `SubmitFrameRequest` below is a correlation id chosen by the runtime; the
# same id is set as the `"token"` key on the matching XPC dictionary so the
# daemon can pair frame submission with its inbound IOSurface send-right.

@0xc8a4f30f6df21a7b;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("fuvr::daemon");

# ---------------------------------------------------------------------------
# Session lifecycle
# ---------------------------------------------------------------------------

enum VideoCodec {
  hevc @0;
  h264 @1;
}

struct StartSessionRequest {
  perEyeWidth     @0 :UInt32;
  perEyeHeight    @1 :UInt32;
  refreshRateHz   @2 :UInt32;
  videoCodec      @3 :VideoCodec;
  videoBitrateBps @4 :UInt32;
  forceIdrEveryFrames @5 :UInt32 = 240;
  audioEnabled    @6 :Bool = false;
  # If true, daemon spawns the virtual-display-helper subprocess. Phase 2.
  enableVirtualDisplay @7 :Bool = false;
}

struct StartSessionResponse {
  sessionId       @0 :UInt64;
  # Quest's clock offset relative to Mac (Quest = Mac + offsetNs).
  clockOffsetNs   @1 :Int64;
  oneWayDelayNs   @2 :UInt64;
  # If a virtual display was spawned, its CGDirectDisplayID.
  virtualDisplayId @3 :UInt32;
}

struct StopSessionRequest {
  sessionId @0 :UInt64;
}

# ---------------------------------------------------------------------------
# Frame submission (runtime -> daemon)
#
# The runtime renders into an IOSurface that it owns. To hand it to the
# daemon-side encoder, the runtime:
#   1. Mints a mach send-right for the IOSurface via IOSurfaceCreateMachPort,
#   2. Sends an xpc_dictionary to the daemon's XPC mach service
#      `com.fuvr.daemon.surface`, with `"token"` = surfaceToken and
#      `"surface"` = mach send-right (xpc_dictionary_set_mach_send),
#   3. Sends a SubmitFrameRequest envelope on the UDS RPC socket carrying the
#      same `surfaceToken`.
# The daemon receives the mach send-right on its registered service, looks up
# the IOSurface via IOSurfaceLookupFromMachPort, indexes it by `surfaceToken`,
# correlates with the matching SubmitFrameRequest, builds a CVPixelBuffer via
# CVPixelBufferCreateWithIOSurface, and feeds it to VTCompressionSession. The
# daemon then deallocates the local mach send-right. EncodeStats is replied
# asynchronously once the encode completes.
# See docs/adr/0007-iosurface-mach-handoff.md.
# ---------------------------------------------------------------------------

struct SubmitFrameRequest {
  sessionId       @0 :UInt64;
  frameId         @1 :UInt64;
  renderStartNs   @2 :UInt64;
  # Index into the SCM_RIGHTS payload accompanying this RPC envelope.
  surfaceToken    @3 :UInt32;
  forceIdr        @4 :Bool;
  # Pose used by the app to render this frame. Embedded into the
  # VideoFragmentHeader on the wire so the Quest can apply ATW correctly.
  renderedLeftPosX  @5 :Float32;
  renderedLeftPosY  @6 :Float32;
  renderedLeftPosZ  @7 :Float32;
  renderedLeftRotX  @8 :Float32;
  renderedLeftRotY  @9 :Float32;
  renderedLeftRotZ  @10 :Float32;
  renderedLeftRotW  @11 :Float32;
  renderedRightPosX @12 :Float32;
  renderedRightPosY @13 :Float32;
  renderedRightPosZ @14 :Float32;
  renderedRightRotX @15 :Float32;
  renderedRightRotY @16 :Float32;
  renderedRightRotZ @17 :Float32;
  renderedRightRotW @18 :Float32;
  # Per-eye FOV the runtime told the app to render with. Embedded into the
  # wire VideoFragmentHeader so the Quest's ATW shader can use a correct
  # fov_render — without it, the shader falls back to fov_now, which is
  # narrower than what the runtime gives Blender (overscan), so the warp
  # samples outside the rendered area during fast head turns ("see the
  # screen edge"). Default 0 = pre-overscan daemons; Quest then assumes
  # fov_render == fov_now (legacy behavior).
  renderedLeftFovAngleLeft   @19 :Float32;
  renderedLeftFovAngleRight  @20 :Float32;
  renderedLeftFovAngleUp     @21 :Float32;
  renderedLeftFovAngleDown   @22 :Float32;
  renderedRightFovAngleLeft  @23 :Float32;
  renderedRightFovAngleRight @24 :Float32;
  renderedRightFovAngleUp    @25 :Float32;
  renderedRightFovAngleDown  @26 :Float32;
}

struct EncodeStats {
  frameId          @0 :UInt64;
  encodeDurationNs @1 :UInt64;
  encodedSizeBytes @2 :UInt32;
  wasKeyframe      @3 :Bool;
}

# ---------------------------------------------------------------------------
# Pose snapshot delivery (daemon -> runtime)
#
# The daemon receives `UpstreamFrame` messages from the Quest at ~1 kHz, runs
# basic prediction, and exposes the latest sample to the runtime. The runtime
# subscribes once (`StreamPosesRequest`) and consumes a long-lived stream.
# ---------------------------------------------------------------------------

struct StreamPosesRequest {
  sessionId @0 :UInt64;
}

struct PoseSnapshot {
  receivedAtNs       @0 :UInt64;     # Mac clock when daemon received this
  questTimestampNs   @1 :UInt64;     # Quest clock at sample time
  predictedDisplayTimeNs @2 :UInt64; # Quest's own prediction target
  leftPosX  @3 :Float32;
  leftPosY  @4 :Float32;
  leftPosZ  @5 :Float32;
  leftRotX  @6 :Float32;
  leftRotY  @7 :Float32;
  leftRotZ  @8 :Float32;
  leftRotW  @9 :Float32;
  rightPosX @10 :Float32;
  rightPosY @11 :Float32;
  rightPosZ @12 :Float32;
  rightRotX @13 :Float32;
  rightRotY @14 :Float32;
  rightRotZ @15 :Float32;
  rightRotW @16 :Float32;
  linVelX @17 :Float32;
  linVelY @18 :Float32;
  linVelZ @19 :Float32;
  angVelX @20 :Float32;
  angVelY @21 :Float32;
  angVelZ @22 :Float32;

  # Controller poses, added in pass 4. Cap'n Proto allows additive fields
  # without rotating the schema id; older readers see the new fields as
  # default zero, so daemon and runtime can be deployed independently.
  leftControllerActive @23 :Bool;
  leftControllerPosX  @24 :Float32;
  leftControllerPosY  @25 :Float32;
  leftControllerPosZ  @26 :Float32;
  leftControllerRotX  @27 :Float32;
  leftControllerRotY  @28 :Float32;
  leftControllerRotZ  @29 :Float32;
  leftControllerRotW  @30 :Float32;
  leftControllerLinVelX @31 :Float32;
  leftControllerLinVelY @32 :Float32;
  leftControllerLinVelZ @33 :Float32;
  leftControllerAngVelX @34 :Float32;
  leftControllerAngVelY @35 :Float32;
  leftControllerAngVelZ @36 :Float32;

  rightControllerActive @37 :Bool;
  rightControllerPosX  @38 :Float32;
  rightControllerPosY  @39 :Float32;
  rightControllerPosZ  @40 :Float32;
  rightControllerRotX  @41 :Float32;
  rightControllerRotY  @42 :Float32;
  rightControllerRotZ  @43 :Float32;
  rightControllerRotW  @44 :Float32;
  rightControllerLinVelX @45 :Float32;
  rightControllerLinVelY @46 :Float32;
  rightControllerLinVelZ @47 :Float32;
  rightControllerAngVelX @48 :Float32;
  rightControllerAngVelY @49 :Float32;
  rightControllerAngVelZ @50 :Float32;

  # Per-eye FOV reported by the headset's xrLocateViews. Critical for the
  # runtime: if Blender renders with a different FOV than the headset's
  # actual asymmetric per-eye FOV, the two eye images cannot fuse and the
  # ATW reprojection distorts the picture. Fields default to 0 for older
  # daemons; the runtime falls back to its hardcoded approximation in that
  # case. (Cap'n Proto additive-field rule keeps the schema id stable.)
  leftFovAngleLeft   @51 :Float32;
  leftFovAngleRight  @52 :Float32;
  leftFovAngleUp     @53 :Float32;
  leftFovAngleDown   @54 :Float32;
  rightFovAngleLeft  @55 :Float32;
  rightFovAngleRight @56 :Float32;
  rightFovAngleUp    @57 :Float32;
  rightFovAngleDown  @58 :Float32;
}

# ---------------------------------------------------------------------------
# Input snapshot delivery (daemon -> runtime), pass 4.
#
# The daemon parses `proto::TouchInputState` from each `UpstreamFrame.inputs`,
# converts to this schema-friendly shape, and fans it out to subscribers of
# the `streamInputs` stream. The runtime caches the latest snapshot and
# answers `xrGetActionState{Boolean,Float,Vector2f}` from it.
# ---------------------------------------------------------------------------

struct StreamInputsRequest {
  sessionId @0 :UInt64;
}

struct ControllerInput {
  active           @0 :Bool;
  trigger          @1 :Float32;
  squeeze          @2 :Float32;
  thumbstickX      @3 :Float32;
  thumbstickY      @4 :Float32;
  thumbstickClick  @5 :Bool;
  thumbstickTouch  @6 :Bool;
  triggerTouch     @7 :Bool;
  buttonAClick     @8 :Bool;
  buttonATouch     @9 :Bool;
  buttonBClick    @10 :Bool;
  buttonBTouch    @11 :Bool;
  systemClick     @12 :Bool;
  thumbrest       @13 :Float32;
}

struct InputSnapshot {
  receivedAtNs   @0 :UInt64;
  questClockNs   @1 :UInt64;
  left           @2 :ControllerInput;
  right          @3 :ControllerInput;
}

# ---------------------------------------------------------------------------
# Diagnostics (consumed by mac-app)
# ---------------------------------------------------------------------------

struct Metrics {
  capturedAtNs        @0 :UInt64;
  encoderFps          @1 :Float32;
  encoderEncodeMsAvg  @2 :Float32;
  encoderEncodeMsP95  @3 :Float32;
  transportRttMs      @4 :Float32;
  transportLossPct    @5 :Float32;
  decoderFps          @6 :Float32;  # echoed from Quest control channel
  decoderDecodeMsP95  @7 :Float32;
  videoBitrateMbps    @8 :Float32;
}

struct LogLine {
  timestampNs @0 :UInt64;
  level       @1 :UInt8;       # 0=trace 1=debug 2=info 3=warn 4=error
  module      @2 :Text;
  message     @3 :Text;
}

# ---------------------------------------------------------------------------
# Top-level RPC envelope
#
# Every message on the wire is a `Envelope` with a monotonically increasing
# `seq`. Requests carry a non-zero `seq`; responses echo it. Streams use a
# dedicated `streamId` so that multiple concurrent streams (poses, metrics,
# logs) do not collide.
# ---------------------------------------------------------------------------

struct Envelope {
  seq      @0 :UInt64;
  streamId @1 :UInt64;          # 0 if not a stream message
  body :union {
    # Requests
    startSession      @2 :StartSessionRequest;
    stopSession       @3 :StopSessionRequest;
    submitFrame       @4 :SubmitFrameRequest;
    streamPoses       @5 :StreamPosesRequest;
    streamMetrics     @6 :Void;
    streamLogs        @7 :Void;
    ping              @8 :Void;

    # Responses
    startSessionAck   @9 :StartSessionResponse;
    encodeStats       @10 :EncodeStats;
    poseSnapshot      @11 :PoseSnapshot;
    metrics           @12 :Metrics;
    log               @13 :LogLine;
    pong              @14 :Void;
    ok                @15 :Void;
    error             @16 :Text;

    # Pass 4 additions. Cap'n Proto allows growing a union; old readers
    # decode unknown arms as `Envelope.Body.Which.UNKNOWN_` and skip.
    streamInputs      @17 :StreamInputsRequest;
    inputSnapshot     @18 :InputSnapshot;
    streamEncodeStats @19 :Void;       # dedicated subscription, decoupled from streamMetrics

    # Pass 6: device-capability handshake. Runtime asks the daemon for the
    # latest capabilities forwarded by the Quest's helloFromQuest. If the
    # Quest hasn't connected yet, the daemon replies with `valid=false` and
    # the runtime falls back to internal defaults. Additive arms keep the
    # union stable across deploy skews.
    getDeviceCapabilities      @20 :Void;
    deviceCapabilitiesResponse @21 :DeviceCapabilitiesResponse;
  }
}

# Snapshot of the latest helloFromQuest received by the daemon. `valid` is
# false when no Quest has reported caps yet; runtime falls back to defaults.
struct DeviceCapabilitiesResponse {
  valid           @0 :Bool;
  deviceModel     @1 :Text;
  systemVersion   @2 :Text;
  perEyeWidth     @3 :UInt32;
  perEyeHeight    @4 :UInt32;
  refreshRatesHz  @5 :List(UInt32);
  hasHandTracking @6 :Bool;
  hasEyeTracking  @7 :Bool;
}
