import SwiftUI
import ExtensionFoundation
import ExtensionKit
import AntennaSwitchController

/// Antenna Switch controller as a sandboxed, crash-isolated ExtensionKit `.appex` for the
/// Amateur Radio Suite. Declares the suite's extension point (see Info.plist) and vends the
/// controller UI via `AntennaSwitchExtension.rootView()`; the suite embeds it with
/// `EXHostViewController`.
///
/// SwiftPM cannot build `.appex` bundles — this target is built by the Xcode project
/// (`App/Xcode/project.yml`). The standalone app and the in-process `AntennaSwitchPlugin`
/// are unchanged.
@main
struct AntennaSwitchPluginExtension: AppExtension {
    var configuration: AppExtensionSceneConfiguration {
        AppExtensionSceneConfiguration(
            PrimitiveAppExtensionScene(id: "primary") {
                AntennaSwitchExtension.rootView()
            }
        )
    }
}
