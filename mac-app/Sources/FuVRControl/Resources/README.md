# Bundled tools

This directory is shipped inside the FuVR Mac app via `Bundle.module`.
**End users never touch this** — the binaries are baked into the `.app`
bundle at build time. The fetch script below is a developer convenience for
refreshing them; it is not part of the user-facing install flow.

## Layout

```
Resources/
  adb/
    arm64/adb        ← Apple Silicon platform-tools binary
    x86_64/adb       ← Intel platform-tools binary
  quest/
    fuvr-quest.apk   ← Quest client (built from /quest-app)
```

## Populating the resources

Run from the repo root:

```sh
scripts/fetch-adb.sh           # downloads platform-tools, extracts adb for each arch
(cd quest-app && ./gradlew assembleDebug)
cp quest-app/app/build/outputs/apk/debug/app-debug.apk \
   mac-app/Sources/FuVRControl/Resources/quest/fuvr-quest.apk
```

The Mac app falls back to clear, user-visible errors when a binary is
missing, so the build does not fail — you just cannot install or launch the
Quest client until both files are in place.
