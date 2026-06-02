import SwiftUI

/// Public entry point for hosting the Antenna Switch controller **out-of-process** as an
/// ExtensionKit `.appex`. The extension target is a separate module, so this factory hands
/// it the same UI the in-process `AntennaSwitchPlugin` shows while every other type stays
/// `internal`. See `App/Xcode/Extension/AntennaSwitchPluginExtension.swift`.
public enum AntennaSwitchExtension {
    /// Build the controller root view for an out-of-process host. `defaults` backs the app's
    /// `@AppStorage`; the store is started here (mirroring `AntennaSwitchPlugin.activate()` —
    /// load saved controllers + begin Bonjour discovery).
    @MainActor
    public static func rootView(defaults: UserDefaults? = nil) -> AnyView {
        if let defaults { AppDefaults.store = defaults }
        let store = ControllersStore()
        store.start()
        return AnyView(ContentView().environmentObject(store))
    }
}
