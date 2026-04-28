#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# bundle-resources.sh — stage the adb binaries + Quest APK into the
# FuVRControl module resource tree so they get bundled at build time.
#
# Run from the repo root before `swift build` or opening Xcode:
#   scripts/fetch-adb.sh          # download platform-tools
#   (cd quest-app && ./gradlew assembleDebug)
#   scripts/bundle-resources.sh   # copy APK into place
#
# fetch-adb.sh already stages adb into Resources/adb/{arch}/; this script
# only handles the APK.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APK_SRC="$REPO_ROOT/quest-app/app/build/outputs/apk/debug/app-debug.apk"
APK_DEST="$REPO_ROOT/mac-app/Sources/FuVRControl/Resources/quest/fuvr-quest.apk"

if [ ! -f "$APK_SRC" ]; then
    echo "error: APK not found at $APK_SRC" >&2
    echo "Build it first: cd quest-app && ./gradlew assembleDebug" >&2
    exit 1
fi

mkdir -p "$(dirname "$APK_DEST")"
cp "$APK_SRC" "$APK_DEST"
echo "✓ APK staged at $APK_DEST ($(du -sh "$APK_DEST" | cut -f1))"
