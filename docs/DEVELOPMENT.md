# Development

This is the document you should read on your first morning hacking on
FuVR. It complements (does not replace) `docs/ARCHITECTURE.md`,
`docs/STATUS.md`, and the ADRs under `docs/adr/`. Read this first, then
those, in that order.

## What FuVR is, in 90 seconds

A native OpenXR runtime on Apple Silicon (`runtime-macos`) hosts your XR
app. Frames are pulled by an encoder process (`encoder-macos`), shipped
over UDP+FEC by a Rust transport crate (`transport`), and decoded on a
Quest companion app (`quest-app`). A coordinating daemon (`daemon`) owns
session lifecycle, control RPC over Cap'n Proto, and the cross-process
IOSurface handoff via XPC. A SwiftUI control panel (`mac-app`) sits on
top.

There is no SteamVR, no Quest Link, no closed reverse engineering — every
wire format and every Apple SPI is documented in this repo.

## Your first hour

### 1. Clone and prerequisites

```bash
git clone https://github.com/luminos-srl/FuVR.git
cd FuVR

brew install cmake capnp rust capnpc-rust pkg-config shellcheck \
             android-platform-tools markdownlint-cli
```

You also need:
- Xcode 15+ (`xcode-select --install` is not enough; the SwiftUI app
  needs the full Xcode).
- A working JDK 17 + Android SDK if you intend to touch the Quest app.
  Android Studio is the path of least resistance.

### 2. Build everything

```bash
scripts/gen-proto.sh
cmake -B build -S .
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Then the language-specific suites:

```bash
cargo test --workspace                   # transport
swift test --package-path mac-app        # control panel
(cd quest-app && ./gradlew :app:test)    # decoder + host-side tests
```

A green run on all four is the bar for landing a PR.

### 3. Install locally and play

```bash
scripts/fuvrctl install   # builds, registers LaunchAgent + OpenXR runtime
scripts/fuvrctl status    # confirm everything's wired
```

Then run any OpenXR app (the conformance suite under
`runtime-macos/test` is a fine smoke test) and watch
`scripts/fuvrctl logs`.

## Layout cheat-sheet

```
daemon/                fuvrd: control plane, XPC mach service, lifecycle
runtime-macos/         OpenXR runtime dylib loaded by host apps
encoder-macos/         VideoToolbox-backed encoder process
transport/             Rust crate: UDP + FEC + clock sync
quest-app/             Android (Quest) decoder + renderer
mac-app/               SwiftUI control panel
virtual-display-helper/ subprocess wrapping CGVirtualDisplay (ADR-0004)
proto/                 Cap'n Proto schemas (single source of truth)
proto/gen/             generated bindings — checked in, regenerate with gen-proto.sh
scripts/               install / uninstall / build / fuvrctl
docs/                  this directory; read STATUS.md and ARCHITECTURE.md
docs/adr/              numbered architectural decisions
.github/               CI, issue templates, dependabot, security policy
```

For deeper component-level explanation, see `docs/ARCHITECTURE.md`. For
"what's done, what isn't", see `docs/STATUS.md`. Both are kept current.

## ADR index

When you change something architecturally interesting, add an ADR. The
existing decisions are:

| # | Topic |
|---|---|
| 0001 | Record architecture decisions (the meta-ADR) |
| 0002 | OpenXR runtime hosted in-process |
| 0003 | Cap'n Proto on the wire, JSON for human-edited control |
| 0004 | `CGVirtualDisplay` via subprocess (sandbox-friendly) |
| 0005 | FEC-only transport, no ARQ |
| 0006 | adb-reverse over USB as a discovery fallback |
| 0007 | IOSurface + Mach port handoff for zero-copy frames |
| 0008 | Extension scope policy for v1 |
| 0009 | mDNS/Bonjour for primary discovery |

Read 0001 first. New ADRs go in `docs/adr/NNNN-title.md` with the next
sequence number; never edit a merged ADR's body — supersede it instead
and link from the old one to the new.

## Where to add things

### A new OpenXR extension

1. Add a feature gate under `runtime-macos/include/extensions/`.
2. Wire it into the `xrEnumerateInstanceExtensionProperties` table.
3. If the extension affects what crosses the wire, **also** edit
   `proto/fuvr.capnp` and add a corresponding ADR before any client work.
4. Add a conformance-style test under `runtime-macos/test/`.

ADR-0008 lays out which extension classes are in scope for v1 vs.
explicitly deferred. Read it before writing the implementation, not
after.

### A new field on the wire

Cap'n Proto is the single source of truth. Workflow:

1. Edit `proto/fuvr.capnp` (or `fuvrd.capnp` for control-plane only).
2. Run `scripts/gen-proto.sh`. Commit the regenerated files under
   `proto/gen/` in the same change.
3. Update every consumer: C++ for runtime/encoder/quest-app/JNI, Rust for
   transport.
4. Bump the wire version constant if the change is not strictly additive.
   See ADR-0003 for the policy.

### A new script or CLI surface

Goes under `scripts/`. Convention is bash with `set -euo pipefail`, an
SPDX header on line 2, and `--help` for any non-trivial subcommand.
`scripts/fuvrctl` is the front door — prefer adding a subcommand there
over a new top-level script.

## PR review etiquette

- Keep PRs focused; one ADR-worth of change per branch is a good
  ceiling.
- For wire changes, link the ADR and the regenerated bindings explicitly
  in the PR description.
- All native code carries the SPDX header (`scripts/check-licenses.sh`
  enforces this in CI).
- Tests at the lowest level that proves the behavior. Integration tests
  are slow; unit tests on the same compile unit are fast.
- New documentation lives next to the thing it documents — top-level
  cross-cutting docs go in `docs/`, component-specific README files go
  in the component directory.
- Ask in the PR if you don't know — the project is small enough that
  reviewers will gladly tell you which layer something belongs in.
