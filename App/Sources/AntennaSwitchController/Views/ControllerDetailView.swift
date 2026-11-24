import SwiftUI
import RadioPluginUI

/// Detail pane for one controller: Dashboard / Controls / Settings / Firmware tabs,
/// plus toolbar actions (open web portal, reboot). Owns the polling view model.
///
/// Anything the operator must reach in the Amateur Radio Suite belongs in a tab,
/// not the toolbar: the suite hosts this view out-of-process (ExtensionKit) inside
/// its own window, and plugin toolbar items are not shown there.
struct ControllerDetailView: View {
    let controller: Controller
    @EnvironmentObject private var store: ControllersStore
    @StateObject private var vm: ControllerViewModel
    /// Owned here (not by the tab) so an update in progress survives tab switches.
    @StateObject private var firmware: FirmwareUpdateModel
    @State private var confirmReboot = false

    init(controller: Controller) {
        self.controller = controller
        // Both autoclosures run once, together, when the view is first installed,
        // so the firmware model always drives this view's own `vm`.
        let vm = ControllerViewModel(host: controller.address, name: controller.name)
        _vm = StateObject(wrappedValue: vm)
        _firmware = StateObject(wrappedValue: FirmwareUpdateModel(controller: vm))
    }

    var body: some View {
        TabView {
            DashboardView(vm: vm).tabItem { Label("Dashboard", systemImage: "gauge.with.dots.needle.bottom.50percent") }
            ControlsView(vm: vm).tabItem { Label("Controls", systemImage: "dial.medium") }
            SettingsView(vm: vm, discoveredMasters: store.discovered)
                .tabItem { Label("Settings", systemImage: "gearshape") }
            FirmwareUpdateView(model: firmware)
                .tabItem { Label("Firmware", systemImage: "arrow.down.circle") }
        }
        .navigationTitle(controller.name)
        .navigationSubtitle(controller.address)
        .toolbar {
            ToolbarItem {
                ConnectionDot(connected: vm.connected)
            }
            ToolbarItem {
                Button { vm.openWeb() } label: { Label("Open Web Portal", systemImage: "safari") }
            }
            ToolbarItem {
                Button { confirmReboot = true } label: { Label("Reboot", systemImage: "arrow.clockwise.circle") }
            }
        }
        .confirmationDialog("Reboot \(controller.name)?", isPresented: $confirmReboot, titleVisibility: .visible) {
            Button("Reboot", role: .destructive) { Task { await vm.reboot() } }
            Button("Cancel", role: .cancel) {}
        }
        .onAppear { vm.hostBridge = store.host; vm.start() }
        .onDisappear { vm.stop() }
    }
}

struct ConnectionDot: View {
    @Environment(\.radioTheme) private var theme
    let connected: Bool
    var body: some View {
        HStack(spacing: 5) {
            Circle().fill(connected ? theme.success : theme.textSecondary).frame(width: 9, height: 9)
            Text(connected ? "Online" : "Offline").font(.caption).foregroundStyle(theme.textSecondary)
        }
    }
}
