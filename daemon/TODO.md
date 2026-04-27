# fuvrd — outstanding work

## Pass 5 — vdisplay full integration

THETA shipped `daemon/vdisplay/VirtualDisplaySession` (ScreenCaptureKit
against `CGVirtualDisplay`, ready to feed the encoder directly). Pass 4
left the pass-1 `fuvr_vdisplay_spawn` path in `Session::Session` for
backwards compatibility — it spawns the display but does not yet pipe
captured frames to the encoder. Apply the 3-line patch documented in
`daemon/vdisplay/INTEGRATION.md` when extended-display mode is wanted live.

## Stubbed
- Transport FFI: `FUVR_DAEMON_NO_TRANSPORT` provides weak C stubs so the
  daemon links without the Rust dylib. Once `cargo build --release` produces
  `transport/target/release/libfuvr_transport.dylib`, reconfigure CMake — the
  define disappears and the real symbols link. The current stub
  `transport_create` returns `nullptr`, so the daemon runs but neither sends
  video nor receives poses.
- `json_bridge.cpp` is a placeholder. The mac-app currently speaks JSON over
  `~/Library/Caches/fuvr/control.sock`; we still need to mirror its verbs
  (start/stop/status) into Cap'n Proto envelopes.
- ~~Clock-sync handshake~~: implemented in `src/clock_sync.cpp`. The daemon
  pings the Quest at 1 Hz on the `Control` channel, demuxes incoming pongs,
  and populates `StartSessionResponse.clockOffsetNs` / `oneWayDelayNs` from
  the latest median sample. Note: with `FUVR_DAEMON_NO_TRANSPORT=1` the
  send-callback is a no-op and the snapshot stays at zero — real values
  appear once `libfuvr_transport.dylib` is linked.
- Transport RTT/loss in `Metrics` is zero — the FFI does not expose stats
  hooks yet (coordinator: extend `fuvr_transport.h` with `fuvr_transport_stats`).
- ~~`Encoder fragment` -> `EncodeStats` reply~~: implemented. Per-frame
  `EncodeStats` envelopes are now pushed to every `streamMetrics` subscriber
  (piggy-backed because `Envelope.body` cannot grow a new union arm without
  a schema id bump). See README "EncodeStats forwarding".
- `streamLogs` is not handled.

## Future schema work
- Add a dedicated `streamEncodeStats` subscription verb (and a matching
  body arm if a clean way exists). This requires a minor schema bump on
  `proto/fuvrd.capnp`; the schema id stays stable so the existing
  `streamMetrics` piggy-back remains a graceful fallback for older clients.

## Hooks needed in other components (do not edit from this subdir)
- Transport FFI: add a stats accessor.
- mac-app: keep JSON path until the bridge lands.

## Cleanup
- Per-session `Metrics` rather than max across sessions.
- Replace the FragmentSink std::vector copy with a kj::ArrayPtr scatter send
  when the transport gains a vectored API.
