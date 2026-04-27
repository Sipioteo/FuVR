# Releasing FuVR

This is the maintainer-facing playbook for cutting a public FuVR release.
End users do not need to read this; contributors only need it when they
own the cut. If you are landing a normal PR, see `CONTRIBUTING.md`.

A release covers four artifacts that must move together:

1. The **`fuvrd` + OpenXR runtime + scripts** bundle for macOS, distributed
   via the Homebrew tap and as a signed `.app` bundle on GitHub Releases.
2. The **Quest APK**, distributed via App Lab and SideQuest.
3. The **source tag** on GitHub.
4. The **changelog** entry promoted from `UNRELEASED` to a dated section.

The wire protocol is versioned independently — see ADR-0008 for the
extension/scope policy and ADR-0003 for the Cap'n Proto schema. A release
that breaks wire compat must bump the major version and call it out under
"Breaking changes" in `CHANGELOG.md`.

## 0. Pre-flight: release-readiness checklist

All of the following CI checks must be green on the commit you intend to
tag. The names match the workflows under `.github/workflows/`.

- [ ] `macos-cmake` — daemon, runtime, encoder, vdisplay helper build and
      `ctest` is fully green.
- [ ] `rust` — `cargo test --workspace` passes for the transport crate.
- [ ] `swift` — `swift test --package-path mac-app` passes.
- [ ] `android` — Gradle assemble + unit tests for the Quest app.
- [ ] `proto-check` — generated bindings are in sync with `proto/*.capnp`.
- [ ] `license-check` — `scripts/check-licenses.sh` clean.
- [ ] Manual M0 bench (`fuvrctl bench`) attached to the release issue, with
      the four numbers from the spike tools.
- [ ] At least one human-verified end-to-end run against a real Quest.

If any item is red, do not tag.

## 1. Bump versions

Versions are tracked in three places. Keep them in sync.

| Component | File | Field |
|---|---|---|
| Top-level | `CMakeLists.txt` | `project(FuVR VERSION ...)` |
| Quest app | `quest-app/app/build.gradle.kts` | `versionName`, `versionCode` |
| Brew formula | `Formula/fuvr.rb` | `url` tag, `sha256` |

Version scheme is SemVer with a leading `v` on git tags (`v0.2.0`).
`versionCode` is monotonically increasing across all releases including
pre-releases, regardless of `versionName`.

## 2. Regenerate bindings

Anyone can build from a tag, so the generated Cap'n Proto code must match
`proto/*.capnp`. Run:

```bash
scripts/gen-proto.sh
git status proto/gen
```

Bindings live under `proto/gen/`. Commit any drift on the release branch
before tagging.

## 3. Sign and notarize the macOS components

Apple notarization runs against:

- `build/daemon/fuvrd` (Mach-O executable)
- `build/runtime-macos/libfuvr_openxr_runtime.dylib`
- `build/virtual-display-helper/fuvr-vdisplay-helper`
- The `mac-app/FuVR.app` bundle

Credentials live at `~/.fuvr/notary-creds` and are **not** committed. The
file format is:

```
APPLE_ID=releases@luminosfilm.com
APPLE_TEAM_ID=XXXXXXXXXX
APPLE_APP_PASSWORD=app-specific-password-here
SIGNING_IDENTITY="Developer ID Application: Luminos SRL (XXXXXXXXXX)"
```

Source it before signing:

```bash
set -a; source ~/.fuvr/notary-creds; set +a

# Per-binary codesign (hardened runtime + timestamp).
codesign --force --options runtime --timestamp \
  --sign "$SIGNING_IDENTITY" \
  build/daemon/fuvrd \
  build/runtime-macos/libfuvr_openxr_runtime.dylib \
  build/virtual-display-helper/fuvr-vdisplay-helper

# App bundle.
codesign --force --options runtime --timestamp --deep \
  --sign "$SIGNING_IDENTITY" \
  mac-app/build/Release/FuVR.app

# Notary submission (zip the app first).
ditto -c -k --keepParent mac-app/build/Release/FuVR.app FuVR.zip
xcrun notarytool submit FuVR.zip \
  --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
  --password "$APPLE_APP_PASSWORD" --wait

# Staple once approved.
xcrun stapler staple mac-app/build/Release/FuVR.app
```

A failed notarization always means a missing entitlement or an unsigned
nested binary. `spctl -a -vv FuVR.app` is the fastest local check.

## 4. Cut the tag

```bash
git tag -s vX.Y.Z -m "FuVR vX.Y.Z"
git push origin vX.Y.Z
```

Use a signed tag. The CI release workflow keys off the tag pattern.

## 5. GitHub Release

The `.github/workflows/release.yml` job (when present) attaches:

- `FuVR-vX.Y.Z.app.zip`     — notarized app bundle
- `FuVR-vX.Y.Z-cli.tar.gz`  — `fuvrd`, runtime dylib, vdisplay helper, scripts
- `app-debug.apk`           — for SideQuest direct sideload
- `SHA256SUMS`              — checksums for everything

Attach the bench report (`bench-output/M0-report-*.md`) as a release note.

## 6. Update the Homebrew tap

The tap repo is `luminos-srl/homebrew-fuvr`. Workflow:

```bash
git clone git@github.com:luminos-srl/homebrew-fuvr.git
cd homebrew-fuvr
cp ../FuVR/Formula/fuvr.rb Formula/fuvr.rb

# Patch the URL and sha256 to match the new tag.
NEW_TAG=v0.2.0
SHA=$(curl -sL https://github.com/luminos-srl/FuVR/archive/refs/tags/${NEW_TAG}.tar.gz | shasum -a 256 | awk '{print $1}')
sed -i '' "s|/v[0-9.]*\.tar\.gz|/${NEW_TAG}.tar.gz|" Formula/fuvr.rb
sed -i '' "s/sha256 \"[a-f0-9]\{64\}\"/sha256 \"${SHA}\"/" Formula/fuvr.rb

git commit -am "fuvr ${NEW_TAG}"
git push
```

Verify by running `brew install --build-from-source luminos-srl/fuvr/fuvr`
on a clean machine.

## 7. Push the Quest APK

### App Lab (production)

1. In the Meta Quest Developer Hub, open the FuVR app entry.
2. Upload `app-release.aab` (Gradle: `:app:bundleRelease`).
3. Note the build number — must equal `versionCode` from step 1.
4. Submit for review with the changelog excerpt as release notes.

### SideQuest (immediate, opt-in)

1. Upload `app-debug.apk` and `quest-app/sidequest.json`.
2. Bump the version field in the manifest if you didn't already.
3. Publish.

App Lab review takes 1–3 business days; SideQuest is immediate. Most
releases ride App Lab as the primary channel and SideQuest as a hedge.

## 8. Promote the changelog

In `CHANGELOG.md`, rename the `## [Unreleased]` heading to
`## [X.Y.Z] — YYYY-MM-DD` and start a fresh empty `Unreleased` section
above it. Reorder entries within each subsection (Added/Changed/Fixed/
Removed/Security) by impact. Commit with `docs: changelog for vX.Y.Z`.

## 9. Announce

- Pin the release on the GitHub Discussions board.
- Cross-post to the FuVR Matrix room and the project README badges.
- If there are breaking wire changes, mention the minimum daemon and
  client versions explicitly so users on older builds know to update both.
