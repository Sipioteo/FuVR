# ADR-0002: OpenXR runtime is in-process; auxiliary work lives in a daemon

- Status: accepted
- Date: 2026-04-27

## Context

OpenXR loaders (the Khronos reference loader and every vendor loader) `dlopen`
the runtime dylib into the application's address space. The standard
`active_runtime.json` mechanism assumes this. Windows runtimes (SteamVR,
Oculus, Varjo) all follow the same pattern.

But FuVR has long-running work that isn't well-suited to live inside a transient
app process: the network transport (TCP keepalive, UDP receiver thread, FEC
reassembly), the encoder, and eventually the virtual display lifecycle. If
those crashed, they would take the application with them. If the application
exited, we would lose connection state.

## Decision

The OpenXR runtime stays **in-process**, exactly as the loader contract
requires. It is `libfuvr_openxr_runtime.dylib`, loaded by the app via the
standard mechanism.

Long-running, app-independent work moves into a **daemon** (`fuvrd`, to be
implemented) which is started on first runtime load and stops on a configurable
idle timeout. The runtime and daemon talk over a Unix domain socket using a
small RPC protocol (Cap'n Proto, separate schema from the wire format).

The daemon owns:
- the transport sockets (USB ADB tunnel and UDP)
- the encoder session(s)
- the `virtual-display-helper` subprocess
- session-level state (Quest capabilities, negotiated session config, clock
  sync state, metrics)

The runtime owns:
- swapchain allocation in the app's GPU context
- `xrLocateViews` predictions (against pose state shared from the daemon)
- the OpenXR API surface

## Consequences

- App crashes do not lose the daemon, the Quest connection, or the encoder
  session. Reconnect is instantaneous.
- The runtime is small enough to ship in every consumer; the daemon is the one
  artefact we install per machine.
- We pay one IPC hop per frame for the encoded output. Local UDS at this size
  is ~5 µs — negligible vs the ~10 ms encode budget.

## Alternatives considered

- **All in-process.** Simpler, but every app crash kills the streaming session
  and the transport state. Rejected.
- **All out-of-process via a custom OpenXR loader shim.** Would require us to
  ship our own loader, which breaks compatibility with apps that bundle the
  Khronos one. Rejected.
