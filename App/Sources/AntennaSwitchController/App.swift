import SwiftUI
import RadioPluginUI

/// Standalone-app entry. In the suite this type is unused (the container owns
/// the process); the plugin path is `AntennaSwitchPlugin`. Kept `public` so the
/// thin `AntennaSwitchControllerMain` executable can call `.main()` on it.
public struct AntennaSwitchStandaloneApp: App {
    @StateObject private var store = ControllersStore()

    /// Set via the `ASC_DEMO` env var to show a single pane with mock data, for
    /// documentation screenshots (e.g. `ASC_DEMO=settings`). Empty in normal use.
    private let demoPane = ProcessInfo.processInfo.environment["ASC_DEMO"]

    public init() {}

    public var body: some Scene {
        WindowGroup("Antenna Switch Controller") {
            Group {
                if let pane = demoPane {
                    demoView(pane).frame(minWidth: 600, minHeight: 700)
                } else {
                    ContentView()
                        .environmentObject(store)
                        .frame(minWidth: 900, minHeight: 600)
                        .onAppear { store.start() }
                }
            }
            // Standalone owns its chrome: adopt the suite's dark-LCD theme + a dark
            // appearance so standard controls match. In the suite the host injects
            // the theme/appearance around the plugin's root instead.
            .radioTheme(.dark)
            .preferredColorScheme(.dark)
        }
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }

    @ViewBuilder
    private func demoView(_ pane: String) -> some View {
        let vm = SnapshotTool.demoViewModel()
        switch pane {
        case "controls":  ControlsView(vm: vm)
        case "dashboard": DashboardView(vm: vm)
        default:          SettingsView(vm: vm)
        }
    }
}
