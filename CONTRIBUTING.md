# Contributing to FuVR

Thanks for considering a contribution. This file is the short version of how
the project is run.

## Before you write code

1. Read [`SPEC.md`](SPEC.md) for the full design rationale.
2. Read [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the engineering map.
3. Read [`docs/STATUS.md`](docs/STATUS.md) to see what's built and what's not.
4. Skim [`docs/adr/`](docs/adr/) — those are the structural decisions you
   should not relitigate without a new ADR.

## Where to put code

Each top-level directory has a clear owner (see ARCHITECTURE.md). Cross-
boundary changes need to update the relevant ADR or add a new one.

## Style

- Default to **no comments**. Explain WHY, never WHAT, only when non-obvious.
- C++: `.clang-format` enforced. C++20, no RTTI, no exceptions in first-party
  code.
- Rust: `cargo fmt` + `cargo clippy -D warnings`.
- Swift: project conventions; no third-party SPM deps.
- Kotlin: ktlint-compatible.
- Every source file gets `SPDX-License-Identifier: Apache-2.0` in the first
  five lines. CI fails otherwise.

## License

By submitting a contribution you agree to the [Apache 2.0 license](LICENSE),
including its patent grant clause. We use the standard Apache ICLA via
[cla-assistant.io](https://cla-assistant.io) once the project is public.

## Reporting issues

Use the GitHub issue forms. Bug reports need: macOS version, Quest model and
firmware, the failing component, and (if reproducible) a `fuvr-transport dump`
or analogous log.

## Security

Do not file public issues for security vulnerabilities. Email
`security@` (placeholder until the project is public) and we will coordinate
disclosure.
