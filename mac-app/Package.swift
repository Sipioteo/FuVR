// swift-tools-version:5.10
// SPDX-License-Identifier: Apache-2.0
import PackageDescription

let package = Package(
    name: "FuVR",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "FuVR", targets: ["FuVRApp"]),
        .library(name: "FuVRControl", targets: ["FuVRControl"]),
        .library(name: "FuVRCapnp", targets: ["FuVRCapnp"]),
        .library(name: "FuVRADB", targets: ["FuVRADB"]),
    ],
    targets: [
        .executableTarget(
            name: "FuVRApp",
            dependencies: ["FuVRControl", "FuVRADB"],
            path: "Sources/FuVRApp",
            exclude: ["Resources/Assets.xcassets"],
            resources: [
                // Standalone PNG used by AppDelegate to set the dock icon at
                // launch. SwiftPM doesn't compile .xcassets for command-line
                // executables, so we ship the raw 1024×1024 PNG and load it
                // via Bundle.module.
                .process("Resources/AppIcon.png"),
            ]
        ),
        .target(
            name: "FuVRControl",
            dependencies: ["FuVRCapnp"],
            path: "Sources/FuVRControl",
            exclude: ["Resources/README.md"],
            resources: [
                // Ship the bundled adb binaries and Quest APK. `.copy` keeps
                // the directory tree so we can pick the right arch at runtime.
                .copy("Resources/adb"),
                .copy("Resources/quest"),
                // Universal-binary mock libopenvr_api.dylib, staged here by
                // the CMake `openvr_api` target's POST_BUILD step. The
                // OpenVrBridgeInstaller copies it next to the user's
                // Vivecraft install on demand.
                .copy("Resources/openvr"),
                // FuVR-flavored Vivecraft fork (built from fuvr-mod/upstream).
                // OpenVrBridgeInstaller drops this into discovered Minecraft
                // instances' mods/ folders.
                .copy("Resources/fuvr-mod.jar"),
            ]
        ),
        .target(
            name: "FuVRCapnp",
            path: "Sources/FuVRCapnp"
        ),
        // ADB device management layer — uses FuVRControl's bundled binaries.
        .target(
            name: "FuVRADB",
            dependencies: ["FuVRControl"],
            path: "Sources/FuVRADB"
        ),
        .testTarget(
            name: "FuVRControlTests",
            dependencies: ["FuVRControl", "FuVRCapnp"],
            path: "Tests/FuVRControlTests"
        ),
        .testTarget(
            name: "FuVRCapnpTests",
            dependencies: ["FuVRCapnp"],
            path: "Tests/FuVRCapnpTests"
        ),
    ]
)
