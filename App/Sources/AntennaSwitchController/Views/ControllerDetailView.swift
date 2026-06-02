import SwiftUI

/// Detail pane for one controller: Dashboard / Controls / Settings tabs, plus
/// toolbar actions (open web portal, reboot). Owns the polling view model.
struct ControllerDetailView: View {
    let controller: Controller
    @EnvironmentObject private var store: ControllersStore
    @StateObject private var vm: ControllerViewModel
    @State private var confirmReboot = false

    init(controller: Controller) {
        self.controller = controller
        _vm = StateObject(wrappedValue: ControllerViewModel(host: controller.address,
                                                            name: controller.name))
    }

    var body: some View {
        TabView {
            DashboardView(vm: vm).tabItem { Label("Dashboard", systemImage: "gauge.with.dots.needle.bottom.50percent") }
            ControlsView(vm: vm).tabItem { Label("Controls", systemImage: "dial.medium") }
            SettingsView(vm: vm).tabItem { Label("Settings", systemImage: "gearshape") }
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
    let connected: Bool
    var body: some View {
        HStack(spacing: 5) {
            Circle().fill(connected ? Color.green : Color.secondary).frame(width: 9, height: 9)
            Text(connected ? "Online" : "Offline").font(.caption).foregroundStyle(.secondary)
        }
    }
}
