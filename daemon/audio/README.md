# daemon/audio

Daemon-side audio path. Owns ScreenCaptureKit capture, libopus encoding, and
fan-out of `proto::AudioPacket` envelopes onto the transport's `Audio` channel.

## Integration with `daemon/src/session.cpp` (BETA)

The audio path is intentionally decoupled from `Session` to avoid stepping on
the BETA agent's edits. Two C-callable entry points are exposed in
`fuvr/daemon/audio/audio_session.hpp`:

```cpp
namespace fuvr::daemon::audio {
  void startAudioFor(Session& session, FuvrTransport* transport);
  void stopAudioFor(Session& session);
}
```

Both are idempotent. `startAudioFor` should be invoked from `Session::Session`
(or wherever BETA wires the negotiated wire `SessionConfig`) **only when the
wire `SessionConfig.audioEnabled` flag is true**. `stopAudioFor` should be
invoked from `Session::~Session`. The audio module owns its own lifetime
registry keyed on `session.id()` so it does not leak Session storage.

## Test hook

`AudioSession::injectPcmForTest` lets tests drive PCM straight into the
encode+ship pipeline without ScreenCaptureKit (which requires a GUI session
and TCC consent).

## Wire format

Each Opus packet is wrapped in a packed Cap'n Proto `proto::AudioPacket`
message. There is no extra framing on the wire beyond what the transport
layer adds — the receiver decodes packed Cap'n Proto directly.
