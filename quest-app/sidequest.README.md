# SideQuest manifest

`sidequest.json` is the manifest SideQuest's "App Lab + Sideload" tooling
reads when ingesting third-party builds. The schema is loosely tracked
upstream; treat it as documentation that happens to be machine-readable.
The `version.source = "gradle"` block instructs the release tooling to
read the current `versionName` out of `app/build.gradle.kts` rather than
requiring a manual bump in two places.

When publishing a new build, see the **SideQuest** section of
`docs/RELEASE.md`.
