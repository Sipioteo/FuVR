// swift-tools-version:5.10
// SPDX-License-Identifier: Apache-2.0
import PackageDescription

let package = Package(
    name: "FuVR",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "FuVR", targets: ["FuVRApp"]),
        .library(name: "FuVRControl", targets: ["FuVRControl"]),
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
            path: "Sources/FuVRControl"
        ),
        .testTarget(
            name: "FuVRControlTests",
            dependencies: ["FuVRControl"],
            path: "Tests/FuVRControlTests"
        ),
    ]
)
