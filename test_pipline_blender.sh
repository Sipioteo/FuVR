#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# test_pipline_blender.sh — full FuVR end-to-end pipeline restart for Blender VR.
#
# What it does:
#   1. Stops Blender, reloads fuvrd via launchd (re-registers the
#      com.fuvr.daemon.surface XPC mach service), kickstarts the daemon.
#   2. Re-establishes adb reverse tcp:9943 to the connected Quest.
#   3. Restarts the FuVR Quest app with a fresh logcat.
#   4. Launches Blender headlessly toggling VR Scene Inspection on,
#      pointing the OpenXR loader at the FuVR runtime via XR_RUNTIME_JSON.
#   5. Waits ~18s and prints status of all four components.
#
# Output logs:
#   /tmp/fuvrd.err.log                 daemon (launchd-managed)
#   /tmp/blender_vr_pipeline.log       Blender stdout/stderr + Python timer
#
# Re-run any time after the user takes the visor off (Quest sleeps + drops
# the OpenXR app) or after rebuilding the runtime/daemon.

set -u

ADB_SERIAL="${ADB_SERIAL:-2G0YC5ZH0G018Z}"
QUEST_PKG="com.fuvr.quest"
QUEST_ACT="${QUEST_PKG}/.MainActivity"
RUNTIME_JSON="${HOME}/Library/Application Support/OpenXR/1/active_runtime.json"
LAUNCH_PLIST="${HOME}/Library/LaunchAgents/com.fuvr.daemon.plist"
DAEMON_LABEL="com.fuvr.daemon"
DAEMON_DOMAIN="gui/$(id -u)"
START_VR_PY="/tmp/start_vr3.py"
BLENDER_BIN="/Applications/Blender.app/Contents/MacOS/Blender"
BLENDER_LOG="/tmp/blender_vr_pipeline.log"

if [[ ! -f "$START_VR_PY" ]]; then
  cat > "$START_VR_PY" <<'PY'
import bpy, addon_utils, sys

addon_utils.enable("viewport_vr_preview", default_set=True, persistent=True)
sys.stderr.write("[FuVR-py] addon enabled\n"); sys.stderr.flush()

ATTEMPTS = {"n": 0}

def _try_start():
    ATTEMPTS["n"] += 1
    wm = bpy.context.window_manager
    if not wm.windows:
        return 0.5
    win = wm.windows[0]
    target = None
    for area in win.screen.areas:
        if area.type == 'VIEW_3D':
            for region in area.regions:
                if region.type == 'WINDOW':
                    target = (area, region); break
            if target: break
    if not target:
        return 0.5
    area, region = target
    try:
        with bpy.context.temp_override(window=win, area=area, region=region):
            bpy.ops.wm.xr_session_toggle()
        sys.stderr.write("[FuVR-py] VR session toggled ON\n"); sys.stderr.flush()
        return None
    except Exception as e:
        sys.stderr.write(f"[FuVR-py] toggle exc: {e}\n"); sys.stderr.flush()
        if ATTEMPTS["n"] > 20:
            return None
        return 1.0

bpy.app.timers.register(_try_start, first_interval=2.0)
PY
fi

echo "[1/6] stopping previous Blender, daemon, Quest app..."
pkill -x Blender 2>/dev/null
sleep 1
launchctl bootout "$DAEMON_DOMAIN/$DAEMON_LABEL" 2>/dev/null || true
sleep 1

echo "[2/6] bootstrapping fuvrd via launchd + kickstart..."
launchctl bootstrap "$DAEMON_DOMAIN" "$LAUNCH_PLIST"
sleep 1
launchctl kickstart -k "$DAEMON_DOMAIN/$DAEMON_LABEL" 2>/dev/null
sleep 2
: > /tmp/fuvrd.err.log

echo "[3/6] re-establishing adb reverse tcp:9943..."
adb -s "$ADB_SERIAL" reverse --remove tcp:9943 2>/dev/null || true
adb -s "$ADB_SERIAL" reverse tcp:9943 tcp:9943

echo "[4/6] restarting Quest app ($QUEST_PKG)..."
adb -s "$ADB_SERIAL" shell am force-stop "$QUEST_PKG"
sleep 1
adb -s "$ADB_SERIAL" logcat -c
adb -s "$ADB_SERIAL" shell am start -n "$QUEST_ACT" >/dev/null
sleep 3

echo "[5/6] launching Blender (FuVR runtime, VR auto-toggle)..."
XR_RUNTIME_JSON="$RUNTIME_JSON" \
  "$BLENDER_BIN" --python "$START_VR_PY" > "$BLENDER_LOG" 2>&1 &

echo "[6/6] waiting 18s for VR session to handshake..."
sleep 18

echo
echo "===== STATUS ====="
echo "Blender PID:  $(pgrep -f 'Blender.app' | head -1)"
echo "fuvrd PID:    $(pgrep -f fuvrd | head -1)"
echo "Quest PID:    $(adb -s "$ADB_SERIAL" shell pidof "$QUEST_PKG")"
echo "TCP 9943:     $(lsof -i :9943 2>/dev/null | grep LISTEN | awk '{print $1, $2}')"
echo
echo "----- Blender VR -----"
grep -E "FuVR-py|VR session" "$BLENDER_LOG" | tail -5
echo
echo "----- fuvrd -----"
grep -vE "DEBUG-POSE|LATENCY-DEBUG" /tmp/fuvrd.err.log | tail -5
echo
echo "----- Quest fuvr.comp -----"
adb -s "$ADB_SERIAL" logcat -d 2>&1 | grep -E "fuvr\.comp" | grep -vE "DRIFT #1" | tail -5
echo
echo "Tail logs:"
echo "  daemon:  tail -f /tmp/fuvrd.err.log"
echo "  blender: tail -f $BLENDER_LOG"
echo "  quest:   adb -s $ADB_SERIAL logcat -s fuvr.comp fuvr.proto fuvr.drift"
