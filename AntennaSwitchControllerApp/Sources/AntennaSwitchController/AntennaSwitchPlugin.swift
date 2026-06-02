import SwiftUI
import RadioPluginKit

/// Plugin adapter for the Amateur Radio Suite container. Lives inside the
/// `AntennaSwitchController` module for internal access to `ContentView` /
/// `ControllersStore`. Only this type is `public`; all views/networking stay
/// private to the module.
@MainActor
public final class AntennaSwitchPlugin: RadioPlugin {
    public static let metadata = PluginMetadata(
        id: "antsw",
        title: "Antenna Switch",
        systemImage: "antenna.radiowaves.left.and.right",
        version: "1.0"
    )

    private let host: PluginHost
    private let store: ControllersStore
    private var started = false

    public init(host: PluginHost) {
        self.host = host
        // Isolated, namespaced defaults for this plugin (no collisions).
        AppDefaults.store = host.defaults(for: Self.metadata.id)
        self.store = ControllersStore()
    }

    public func makeRootView() -> AnyView {
        AnyView(ContentView().environmentObject(store))
    }

    public func activate() {
        guard !started else { return }
        started = true
        store.start()   // load saved controllers + begin Bonjour discovery
    }
}
