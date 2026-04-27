# fuvrd

`fuvrd` is the FuVR session daemon. Per ADR-0002, the OpenXR runtime
(`libopenxr_loader.dylib` consumer plus `libfuvr_runtime.dylib`) lives
in-process inside the host XR application. The runtime delegates everything
that touches a long-lived background resource — VideoToolbox encoder
sessions, the Rust transport, the virtual-display helper subprocess, and the
mac-app control plane — to this daemon over a Unix domain socket carrying
packed Cap'n Proto envelopes (`proto/fuvrd.capnp`).

## Role

- Hosts one `fuvr::Encoder` per active session.
- Receives IOSurface mach send-rights via SCM_RIGHTS, resolves them to
  `CVPixelBuffer`, and feeds them to the encoder. See ADR-0007 (TBD) for
  the rationale behind the mach-port handoff.
- Calls into `libfuvr_transport.dylib` on the `Video` channel for fragments
  and registers a recv callback for `proto::UpstreamFrame` poses.
- Fans out poses to runtime subscribers via `PoseSnapshot` envelopes.
- Optionally spawns the virtual-display helper.
- Aggregates encoder/transport metrics and pushes a `Metrics` envelope at
  10 Hz to mac-app subscribers.

## Build & run

```sh
bash scripts/gen-proto.sh
cmake -S . -B build -G Ninja
cmake --build build --target fuvrd
./build/daemon/fuvrd                          # foreground; SIGTERM/SIGINT to stop
./build/daemon/fuvrd --socket /tmp/fuvr.sock  # override socket path
```

The default socket is `$XDG_RUNTIME_DIR/fuvr/rpc.sock`, falling back to
`~/Library/Caches/fuvr/rpc.sock`. Parent dir is `0700`, socket is `0600`.

## How the runtime connects

The runtime opens a `SOCK_STREAM` Unix socket to the path above, sends an
`Envelope.startSession`, and reads back a `startSessionAck`. To submit a
frame it transmits a length-prefixed packed `Envelope.submitFrame` with a
mach send-right for the IOSurface attached via `SCM_RIGHTS`; the daemon
indexes into the cmsg payload using `surfaceToken`.

To consume poses the runtime sends `Envelope.streamPoses(sessionId)` once;
every inbound pose from the Quest yields a `PoseSnapshot` envelope tagged
with the returned `streamId`.

## Threading

- Accept thread + one reader thread per client (`RpcServer`).
- One metrics ticker thread (10 Hz).
- Encoder fragment callbacks come from VideoToolbox's own dispatch threads.
- Transport recv callback comes from the Rust transport thread.

See `include/fuvr/daemon.hpp` for the rationale comments.
