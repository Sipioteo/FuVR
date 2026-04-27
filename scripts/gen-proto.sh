#!/usr/bin/env bash
# Generate Cap'n Proto bindings for every consumer of `proto/fuvr.capnp`.
# Outputs land in `proto/gen/<lang>/` and are consumed by each component's
# build system.
set -euo pipefail

cd "$(dirname "$0")/.."

OUT=proto/gen
mkdir -p "$OUT/cpp" "$OUT/rust"

if ! command -v capnp >/dev/null 2>&1; then
  echo "error: capnp not found; install with 'brew install capnp'" >&2
  exit 1
fi

# C++ generation (used by runtime-macos, encoder-macos, quest-app NDK).
capnp compile -oc++:"$OUT/cpp" --src-prefix=proto proto/fuvr.capnp proto/fuvrd.capnp

# Rust generation (used by transport crate).
# capnpc-rust is installed via `cargo install capnpc`; check optimistically.
if command -v capnpc-rust >/dev/null 2>&1; then
  capnp compile -orust:"$OUT/rust" --src-prefix=proto proto/fuvr.capnp proto/fuvrd.capnp
else
  echo "warning: capnpc-rust missing; Rust bindings skipped (cargo install capnpc)" >&2
fi

echo "generated bindings in $OUT/"
