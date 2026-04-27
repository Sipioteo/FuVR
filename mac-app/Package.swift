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
    ],
    targets: [
        .executableTarget(
            name: "FuVRApp",
            dependencies: ["FuVRControl"],
            path: "Sources/FuVRApp",
            exclude: ["Resources/Assets.xcassets"],
            resources: []
        ),
        .target(
            name: "FuVRControl",
            dependencies: ["FuVRCapnp"],
            path: "Sources/FuVRControl"
        ),
        .target(
            name: "FuVRCapnp",
            path: "Sources/FuVRCapnp"
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
