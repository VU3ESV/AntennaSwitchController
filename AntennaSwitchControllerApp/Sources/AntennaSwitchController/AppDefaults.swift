import Foundation

/// Indirection over `UserDefaults` so the plugin can inject the host-provided,
/// per-plugin namespaced store (`PluginHost.defaults(for:)`), while the
/// standalone app falls back to `.standard`. Plugins must never touch
/// `UserDefaults.standard` directly (keys would collide across the suite).
enum AppDefaults {
    static var store: UserDefaults = .standard
}
