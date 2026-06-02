import SwiftUI
import RadioPluginKit

/// Plugin adapter for the Amateur Radio Suite container. Lives inside the
/// `AntennaSwitchController` module for internal access to `ContentView` /
/// `ControllersStore`. Only this type is `public`; all views/networking stay
/// private to the module.
///
/// Implements the RadioPluginKit 1.2 contract: a declarative `manifest`
/// (capabilities/isolation/versions), `persistState`/`restoreState` for the
/// selected controller, and it routes connectivity/save events to the host's
/// error, notification, and badge surfaces (via `PluginHostBridge`).
@MainActor
public final class AntennaSwitchPlugin: RadioPlugin, PluginHostBridge {
    public static let manifest: RadioPluginManifest? = RadioPluginManifest(
        id: "antsw",
        name: "Antenna Switch",
        version: "1.0",
        isolation: .inProcess,                              // first-party, linked into the host
        capabilities: [.networkClient, .bonjour, .notifications],
        systemImage: "antenna.radiowaves.left.and.right",
        author: "VU3ESV",
        homepage: "https://github.com/VU3ESV/AntennaSwitchController"
    )
    public static var metadata: PluginMetadata { manifest!.metadata }

    private let host: PluginHost
    private let store: ControllersStore
    private var started = false

    public init(host: PluginHost) {
        self.host = host
        // Isolated, namespaced defaults for this plugin (no collisions).
        AppDefaults.store = host.defaults(for: Self.metadata.id)
        self.store = ControllersStore()
        self.store.host = self          // route view-model events to the host context
    }

    public func makeRootView() -> AnyView {
        AnyView(ContentView().environmentObject(store))
    }

    public func activate() {
        host.setBadge(nil, for: Self.metadata.id)   // attention cleared on activation
        guard !started else { return }
        started = true
        store.start()   // load saved controllers + begin Bonjour discovery
    }

    // MARK: - State restoration (RadioPlugin)

    /// Persist the selected controller so a relaunch/crash-restart reopens it.
    public func persistState() -> Data? {
        store.selection?.uuidString.data(using: .utf8)
    }

    public func restoreState(_ data: Data) {
        if let s = String(data: data, encoding: .utf8), let id = UUID(uuidString: s) {
            store.restore(selection: id)
        }
    }

    // MARK: - PluginHostBridge → host context

    func controllerWentOffline(name: String, address: String) {
        host.report(PluginError(severity: .connectivity,
                                title: "\(name) is offline",
                                message: "Lost connection to \(address). Retrying…"),
                    from: Self.metadata.id)
        host.setBadge(.dot, for: Self.metadata.id)
    }

    func controllerCameOnline(name: String) {
        host.setBadge(nil, for: Self.metadata.id)
        host.notify(PluginNotification(level: .success, title: "\(name) reconnected"),
                    from: Self.metadata.id)
    }

    func configSaved(name: String, ok: Bool, detail: String?) {
        host.notify(PluginNotification(level: ok ? .success : .error,
                                       title: ok ? "Saved \(name)" : "Save failed — \(name)",
                                       body: detail),
                    from: Self.metadata.id)
    }
}
