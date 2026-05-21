// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "KrishaHost",
    platforms: [
        .macOS(.v13)
    ],
    targets: [
        .target(
            name: "CKrishaAudio",
            path: "Sources/CKrishaAudio",
            publicHeadersPath: "include"
        ),
        .target(
            name: "CKrishaDSP",
            path: "Sources/CKrishaDSP",
            publicHeadersPath: "include"
        ),
        .executableTarget(
            name: "KrishaHost",
            dependencies: ["CKrishaAudio", "CKrishaDSP"],
            linkerSettings: [
                .linkedFramework("CoreAudio"),
                .linkedFramework("AudioToolbox"),
                .linkedFramework("CoreFoundation"),
                .linkedFramework("IOKit"),
                .linkedLibrary("c++"),
                .unsafeFlags([
                    "-L../dsp/build",
                    "-lkrisha_dsp"
                ])
            ]
        ),
    ]
)
