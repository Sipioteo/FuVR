# Changelog

All notable changes to FuVR are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The "wire version" — the schema in `proto/*.capnp` plus the framing in
`transport/` — is versioned separately from the released artifacts. A
breaking wire change is always called out under **Breaking changes** in
the relevant section.

## [Unreleased]

Coordinator pass 4 work in flight; entries land here as components are
finished. Tier-1 leads are working on:

### Added

- Developer experience tooling: `scripts/fuvrctl` umbrella CLI with
  `install`, `uninstall`, `status`, `logs`, `quest install`,
  `quest reverse`, and `bench` subcommands.
- Quest-side install helper (`scripts/install-quest.sh`) with multi-device
  selection and Gradle build fallback.
- Homebrew formula stub at `Formula/fuvr.rb` for tap-based distribution.
- SideQuest manifest at `quest-app/sidequest.json`.
- Maintainer playbook (`docs/RELEASE.md`), troubleshooting guide
  (`docs/TROUBLESHOOTING.md`), and contributor onboarding
  (`docs/DEVELOPMENT.md`).
- Repo health: `.github/SECURITY.md`, `.github/FUNDING.yml`,
  `.github/dependabot.yml`, and a hardware-test-report issue template.

### Changed

- _(filled in by other tier-1 leads as their work lands)_

### Fixed

- _(filled in by other tier-1 leads as their work lands)_

## [0.3.0] — 2026-04-13 (coordinator pass 3)

### Added

- Cross-process IOSurface handoff between the encoder and the runtime
  via Mach ports over XPC (ADR-0007). Zero-copy frame path is end-to-end
  on the Mac side.
- Runtime↔daemon clock sync, including drift compensation, plus the
  Quest-side host implementation (`test_clock_sync` covers regressions).
- Per-frame `EncodeStats` flowed back from the encoder to the daemon and
  surfaced in the SwiftUI control panel.
- Real OpenXR event queue, reference space management, and
  `xrLocateSpace` for the head-pose path.
- Quest decoder pipeline wired all the way through `AImageReader` →
  `AHardwareBuffer` → `EGLImage` → `GL_TEXTURE_EXTERNAL_OES`. Frames
  reach the swapchain renderer.
- Session lifecycle state machine in `fuvrd` aligned with the OpenXR
  session-state diagram.

### Changed

- `daemon/launchd/com.fuvr.daemon.plist` now ships an out/err log path
  pair so `fuvrctl logs` can find them deterministically.

## [0.2.0] — 2026-03-09 (coordinator pass 2)

### Added

- `fuvrd` daemon process with a Cap'n Proto control RPC over a UDS,
  matching `proto/fuvrd.capnp`.
- Rust transport crate covering UDP framing, FEC encode/decode
  (ADR-0005), and a loopback bench tool.
- LaunchAgent plist + `scripts/install-launchd.sh` /
  `scripts/uninstall-launchd.sh` so `fuvrd` is reachable as
  `com.fuvr.daemon.surface`.
- Encoder process backed by VideoToolbox; HEVC and AV1 paths.
- mDNS advertisement for `_fuvr._udp` discovery (ADR-0009).

## [0.1.0] — 2026-02-02 (coordinator pass 1)

### Added

- Initial repository scaffold: top-level CMake, component subdirectories,
  Cap'n Proto schemas under `proto/`, generated-binding script.
- ADRs 0001 through 0009 establishing the architecture.
- CI workflows for macOS CMake, Rust, Swift, Android, license check, and
  proto-binding drift detection.
- Issue and PR templates (`bug.yml`, `feature.yml`,
  `PULL_REQUEST_TEMPLATE.md`), CODEOWNERS, contributor guide.
- License (Apache-2.0) and SPDX headers across every source file.

[Unreleased]: https://github.com/luminos-srl/FuVR/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/luminos-srl/FuVR/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/luminos-srl/FuVR/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/luminos-srl/FuVR/releases/tag/v0.1.0
