# fuvrd — outstanding work

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
- Clock-sync handshake: `StartSessionResponse.clockOffsetNs` and
  `oneWayDelayNs` are returned as zero. Real values come from the
  `ControlMessage.clockSync` exchange driven by the Quest, which the
  daemon does not yet route.
- Transport RTT/loss in `Metrics` is zero — the FFI does not expose stats
  hooks yet (coordinator: extend `fuvr_transport.h` with `fuvr_transport_stats`).
- `Encoder fragment` -> `EncodeStats` reply: we record metrics internally
  but do not push `EncodeStats` envelopes back to the runtime per frame.
  Add when the runtime actually consumes them.
- `streamLogs` is not handled.

## Hooks needed in other components (do not edit from this subdir)
- Transport FFI: add a stats accessor.
- runtime-macos: open the rpc socket, send `submitFrame` with SCM_RIGHTS.
- mac-app: keep JSON path until the bridge lands.

## Cleanup
- Per-session `Metrics` rather than max across sessions.
- Replace the FragmentSink std::vector copy with a kj::ArrayPtr scatter send
  when the transport gains a vectored API.
