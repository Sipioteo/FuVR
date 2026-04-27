# fuvr_encoder — VideoToolbox HEVC/H.264 low-latency wrapper

Wraps `VTCompressionSession` with a minimal sink-based API. Produces
Annex-B framed bitstreams ready for the FuVR transport layer.

## API

See `include/fuvr/encoder.hpp`. Submit `CVPixelBufferRef` (IOSurface-backed
recommended), receive `EncodedFragment`s on the compression callback thread.

## Configuration choices

### Codec selection

- **HEVC**: realtime mode via `kVTCompressionPropertyKey_RealTime = true`,
  `AllowFrameReordering = false`, `MaxKeyFrameInterval`,
  `AverageBitRate` + `DataRateLimits` cap (1.5x average over 1s),
  `ProfileLevel = HEVC_Main_AutoLevel`. No B-frames; only I/P. This is the
  recommended path for stereo 4128x2208 @ 90 Hz with hardware encoder on
  Apple Silicon (M2+).

- **H.264**: same realtime tuning **plus**
  `kVTVideoEncoderSpecification_EnableLowLatencyRateControl = YES` (WWDC21).
  Used as a fallback because Apple's official low-latency rate-control mode
  is **H.264 only** as of macOS 14/15/26 — HEVC must be tuned manually.

### Annex-B framing

VideoToolbox emits AVCC-style 4-byte length-prefixed NAL units. We rewrite
each NAL with a `00 00 00 01` start code before handing to the sink.
Codec-specific data (SPS/PPS for H.264; VPS/SPS/PPS for HEVC) is extracted
from the `CMFormatDescription` and emitted as a separate fragment with
`isCsd = true` immediately before each keyframe — receivers can prepend it
to the keyframe to make every IDR self-contained.

### Threading

The `FrameSink::onFragment` callback is invoked **directly on
VideoToolbox's compression callback thread**. The encoder does not dispatch
to a queue — the caller decides whether to memcpy, ring-buffer, or hand off
to the transport. Keep work in the callback short.

## Build

```
cmake -S . -B build -G Ninja
cmake --build build --target fuvr_encoder fuvr-encode-synthetic
```

## Tools

`fuvr-encode-synthetic [out.h265]` — generates 256 frames of a moving Y
gradient at 4128x2208 @ 90 Hz HEVC and writes raw Annex-B bytes. Used for
M0 spike validation (SPEC §5.M0 question 2).

Inspect with ffmpeg / ffprobe:

```
ffprobe -hide_banner synthetic.h265
ffplay synthetic.h265
```

## Caveats

- `EnableLowLatencyRateControl` is documented as H.264 only; do not enable
  it for HEVC — VideoToolbox will silently ignore it on some macOS versions
  and reject the session on others.
- Hardware-only encoding is required (`RequireHardwareAcceleratedVideoEncoder
  = YES`); software fallback would blow the latency budget.
- AV1 hardware encode is not exposed by VideoToolbox as of this writing
  (M3+ has the silicon, no API).
