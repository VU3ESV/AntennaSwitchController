// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "AntennaSwitchControllerApp",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        // Standalone .app. Product name differs from the library target
        // ("AntennaSwitchController") on purpose — a product whose name collides
        // with a target makes multi-arch (universal) XCBuild emit
        // "Multiple commands produce …" duplicate-output errors.
        .executable(name: "AntennaSwitchControllerApp", targets: ["AntennaSwitchControllerMain"]),
        // Plugin library consumed by the Amateur Radio Suite container
        .library(name: "AntennaSwitchControllerKit", targets: ["AntennaSwitchController"]),
    ],
    dependencies: [
        // Published contract, same Git URL the container resolves, so every chain
        // resolves one identical RadioPluginKit (no path-vs-URL identity clash).
        .package(url: "https://github.com/VU3ESV/RadioPluginKit.git", from: "1.2.0"),
    ],
    targets: [
        .target(
            name: "AntennaSwitchController",
            dependencies: [
                .product(name: "RadioPluginKit", package: "RadioPluginKit"),
                .product(name: "RadioPluginUI", package: "RadioPluginKit"),
            ],
            path: "Sources/AntennaSwitchController"
        ),
        .executableTarget(
            name: "AntennaSwitchControllerMain",
            dependencies: ["AntennaSwitchController"],
            path: "Sources/AntennaSwitchControllerMain"
        ),
    ]
)
