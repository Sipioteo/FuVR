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

## IOSurface XPC handoff

Per ADR-0007 the IOSurface itself does not ride the UDS RPC socket — macOS
SCM_RIGHTS is fd-only and cannot carry mach send-rights. Instead the daemon
hosts an XPC mach service `com.fuvr.daemon.surface` (see
`src/iosurface_xpc_service.mm`) and the runtime ships `(token, mach send-right)`
dictionaries to it. The matching `SubmitFrameRequest` envelope on the UDS
carries the same `surfaceToken` so the daemon can correlate the two halves.
A 16 ms grace window absorbs out-of-order arrival; misses increment the
counter exposed by `fuvr::daemon::missingSurfaceCount`.

To make the service name resolvable, install the LaunchAgent:

```sh
bash scripts/install-launchd.sh                    # uses build/daemon/fuvrd
FUVRD=/path/to/fuvrd bash scripts/install-launchd.sh   # explicit path
bash scripts/uninstall-launchd.sh
```

The plist lives at `daemon/launchd/com.fuvr.daemon.plist`. The script copies
it (with the binary path substituted) to `~/Library/LaunchAgents/` and runs
`launchctl bootstrap gui/$UID`. Without this step the runtime's XPC client
will fail to find the service at session start.

## Clock sync

The daemon drives an NTP-style clock sync over the `Control` channel against
the Quest. The Mac sends `ControlMessage.clockSync.ping(t0)` once per second
(state machine in `src/clock_sync.cpp`); the Quest replies with
`pong(t0, t1, t2)` where `t1` is the Quest steady-clock receive time and
`t2` is the Quest steady-clock send time. On pong arrival at Mac time
`T_recv` the daemon computes:

```
oneWayDelayNs = ((T_recv - T_send) - (t2 - t1)) / 2
offsetNs      = ((t1 - T_send) + (t2 - T_recv)) / 2     # Quest = Mac + offset
```

A rolling window of the last 16 samples (or 5 s, whichever is shorter) is
kept; `snapshot()` returns the median to dampen single-packet jitter.
`StartSessionResponse.clockOffsetNs` and `oneWayDelayNs` are populated from
the latest snapshot at session start. The daemon kicks an extra ping at
session-start and waits up to 200 ms for the first round trip; if no pong
arrives the response carries zeros and a `ControlMessage.error` envelope is
also pushed to the runtime.

## EncodeStats forwarding

The encoder fires per-fragment callbacks (`fuvr::EncodedFragment`). On the
fragment whose `endOfFrame` flag is set, the daemon assembles an
`EncodeStats { frameId, encodeDurationNs, encodedSizeBytes, wasKeyframe }`
envelope and pushes it to every subscriber currently registered on the
`streamMetrics` stream — i.e. the `streamMetrics` subscription is
double-purpose: subscribers receive both the 10 Hz `Metrics` aggregate
*and* a per-frame `EncodeStats` envelope on the same `streamId`.

This piggy-back is a deliberate workaround: Cap'n Proto union arms cannot
be added to `Envelope.body` without changing wire semantics, and adding a
new top-level subscription verb would require a schema id bump that breaks
deployed Quest apps. A future minor schema bump that introduces a dedicated
`streamEncodeStats` arm is tracked in `TODO.md`.

### Test bypass: `FUVR_INPROCESS_HANDOFF`

Unit tests link the runtime and the daemon into a single process, where the
launchd-backed mach service makes no sense. Setting `FUVR_INPROCESS_HANDOFF=1`
causes both sides to exchange `IOSurfaceRef`s through a shared in-process
registry (`runtime-macos/include/fuvr/inprocess_surface_registry.hpp`). The
daemon skips creating the XPC listener; the runtime client skips the XPC
send and instead `put`s into the registry. The daemon's `pixelBufferFromToken`
takes from the same registry.
