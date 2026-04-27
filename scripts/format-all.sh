#!/usr/bin/env bash
# Best-effort source formatter. Skips silently when a tool isn't installed —
# this is a developer convenience, not a CI gate.
set -uo pipefail

cd "$(dirname "$0")/.."

have() { command -v "$1" >/dev/null 2>&1; }

# Rust: workspace-wide rustfmt
if have cargo && [ -f transport/Cargo.toml ]; then
  echo "==> cargo fmt (transport)"
  ( cd transport && cargo fmt --all ) || echo "cargo fmt failed (continuing)"
fi

# C++ / Objective-C++: clang-format on all macOS sources
if have clang-format; then
  echo "==> clang-format"
  find runtime-macos encoder-macos virtual-display-helper \
       -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' \) \
       -not -path '*/gen/*' -not -path '*/build/*' \
       -print0 2>/dev/null \
    | xargs -0 -r clang-format -i || true
else
  echo "(skipped) clang-format not installed"
fi

# Swift: prefer swift-format, fall back to `swift format` (Xcode 16+)
if [ -d mac-app ]; then
  if have swift-format; then
    echo "==> swift-format (mac-app)"
    swift-format -i -r mac-app/Sources mac-app/Tests 2>/dev/null || true
  elif swift format --help >/dev/null 2>&1; then
    echo "==> swift format (mac-app)"
    swift format -i -r mac-app/Sources mac-app/Tests 2>/dev/null || true
  else
    echo "(skipped) swift-format not installed"
  fi
fi

# Kotlin: ktlint on quest-app if present
if [ -d quest-app ]; then
  if have ktlint; then
    echo "==> ktlint (quest-app)"
    ( cd quest-app && ktlint -F '**/*.kt' '**/*.kts' ) || true
  else
    echo "(skipped) ktlint not installed"
  fi
fi

echo "format-all done"
