#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Downloads Google's platform-tools and stages `adb` for both Apple Silicon
# and Intel into the FuVR Mac app's resource bundle.
#
# Usage: scripts/fetch-adb.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/mac-app/Sources/FuVRControl/Resources/adb"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

URL="https://dl.google.com/android/repository/platform-tools-latest-darwin.zip"
echo "→ downloading platform-tools (universal darwin) from $URL"
curl -fsSL "$URL" -o "$TMP/pt.zip"
unzip -q "$TMP/pt.zip" -d "$TMP"

# Google ships a single universal `adb` on macOS. We copy it into both arch
# slots so the runtime resolver finds it whichever host it ends up on.
mkdir -p "$DEST/arm64" "$DEST/x86_64"
cp "$TMP/platform-tools/adb" "$DEST/arm64/adb"
cp "$TMP/platform-tools/adb" "$DEST/x86_64/adb"
chmod +x "$DEST/arm64/adb" "$DEST/x86_64/adb"

# adb depends on these helpers — keep them next to each binary so it can
# find dlopen targets without polluting the global system.
for helper in libc++.dylib NOTICE.txt; do
    src="$TMP/platform-tools/$helper"
    [ -e "$src" ] || continue
    cp "$src" "$DEST/arm64/$helper" 2>/dev/null || true
    cp "$src" "$DEST/x86_64/$helper" 2>/dev/null || true
done

echo "✓ adb staged at $DEST"
ls -lh "$DEST/arm64/adb" "$DEST/x86_64/adb"
