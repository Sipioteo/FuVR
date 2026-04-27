#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# install-quest.sh — install the FuVR debug APK on a connected Quest.
#
# Idempotent: `adb install -r` reinstalls if the package is already present.
# Detects multiple devices and prompts. Detects missing build artifact and
# offers to run the Gradle build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QUEST_APP_DIR="$REPO_ROOT/quest-app"
APK="$QUEST_APP_DIR/app/build/outputs/apk/debug/app-debug.apk"

usage() {
  cat <<'USAGE'
install-quest.sh — install the FuVR debug APK on a Quest.

Usage:
  scripts/install-quest.sh [--serial <device>] [--build]

Options:
  --serial <id>   Target a specific device (from `adb devices`).
                  Required if multiple devices are connected.
  --build         Skip the prompt and run `./gradlew :app:assembleDebug`
                  before installing if the APK is missing.
  -h, --help      Show this help.

The APK lives at:
  quest-app/app/build/outputs/apk/debug/app-debug.apk
USAGE
}

SERIAL=""
AUTO_BUILD=0
while [ $# -gt 0 ]; do
  case "$1" in
    --serial) SERIAL="$2"; shift 2 ;;
    --build)  AUTO_BUILD=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

command -v adb >/dev/null 2>&1 || { echo "error: adb not in PATH (brew install android-platform-tools)" >&2; exit 1; }

# --- Build artifact check -----------------------------------------------------
if [ ! -f "$APK" ]; then
  echo "APK not found at $APK"
  if [ "$AUTO_BUILD" -eq 1 ]; then
    do_build=1
  else
    read -r -p "Run './gradlew :app:assembleDebug' now? [y/N] " ans
    case "$ans" in y|Y|yes|YES) do_build=1 ;; *) do_build=0 ;; esac
  fi
  if [ "${do_build:-0}" -eq 1 ]; then
    (cd "$QUEST_APP_DIR" && ./gradlew :app:assembleDebug)
  else
    echo "aborting; build the APK first." >&2
    exit 1
  fi
fi

[ -f "$APK" ] || { echo "build did not produce $APK" >&2; exit 1; }

# --- Device selection ---------------------------------------------------------
# `adb devices` output looks like:
#   List of devices attached
#   <serial>\tdevice
mapfile -t DEVICES < <(adb devices | awk 'NR>1 && $2=="device" {print $1}')

if [ -z "$SERIAL" ]; then
  case ${#DEVICES[@]} in
    0) echo "error: no Android devices attached. Plug in the Quest with USB debugging on." >&2; exit 1 ;;
    1) SERIAL="${DEVICES[0]}" ;;
    *)
      echo "Multiple devices attached:"
      i=1
      for d in "${DEVICES[@]}"; do
        echo "  [$i] $d"
        i=$((i+1))
      done
      read -r -p "Pick one [1-${#DEVICES[@]}]: " choice
      idx=$((choice-1))
      [ "$idx" -ge 0 ] && [ "$idx" -lt "${#DEVICES[@]}" ] || { echo "invalid choice" >&2; exit 1; }
      SERIAL="${DEVICES[$idx]}"
      ;;
  esac
fi

echo "Installing $APK to device $SERIAL"
adb -s "$SERIAL" install -r "$APK"
echo "done."
