#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Install the fuvrd LaunchAgent so the XPC mach service
# `com.fuvr.daemon.surface` is reachable. Idempotent.
set -euo pipefail

cd "$(dirname "$0")/.."

REPO_ROOT="$(pwd)"
SRC_PLIST="$REPO_ROOT/daemon/launchd/com.fuvr.daemon.plist"
DEST_DIR="$HOME/Library/LaunchAgents"
DEST_PLIST="$DEST_DIR/com.fuvr.daemon.plist"

# Resolve the daemon binary path. Prefer an explicit FUVRD env override,
# fall back to the build tree.
FUVRD="${FUVRD:-$REPO_ROOT/build/daemon/fuvrd}"
if [ ! -x "$FUVRD" ]; then
  echo "error: fuvrd binary not found at $FUVRD; build it or set FUVRD=..." >&2
  exit 1
fi

mkdir -p "$DEST_DIR"

# Substitute the binary path placeholder.
sed "s|/usr/local/bin/fuvrd|$FUVRD|g" "$SRC_PLIST" > "$DEST_PLIST"

DOMAIN="gui/$(id -u)"

if launchctl print "$DOMAIN/com.fuvr.daemon" >/dev/null 2>&1; then
  echo "fuvrd already loaded; reloading"
  launchctl bootout "$DOMAIN" "$DEST_PLIST" || true
fi

launchctl bootstrap "$DOMAIN" "$DEST_PLIST"
echo "installed: $DEST_PLIST"
echo "service:   com.fuvr.daemon.surface"
