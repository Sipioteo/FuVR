#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Remove the fuvrd LaunchAgent.
set -euo pipefail

DEST_PLIST="$HOME/Library/LaunchAgents/com.fuvr.daemon.plist"
DOMAIN="gui/$(id -u)"

if [ -f "$DEST_PLIST" ]; then
  launchctl bootout "$DOMAIN" "$DEST_PLIST" || true
  rm -f "$DEST_PLIST"
  echo "removed: $DEST_PLIST"
else
  echo "no plist at $DEST_PLIST (already removed)"
fi
