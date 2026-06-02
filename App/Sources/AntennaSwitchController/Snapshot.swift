import SwiftUI

/// Offscreen renderer for documentation screenshots of the Controls and Settings
/// panes (which can't be reached via screen capture without UI-scripting access).
/// Invoked by the standalone binary with `--snapshots <dir>`; not used at runtime.
@MainActor
public enum SnapshotTool {
    /// A view model pre-populated with representative data (no network), shared by
    /// the offscreen renderer and the `ASC_DEMO` window mode.
    static func demoViewModel() -> ControllerViewModel {
        let vm = ControllerViewModel(host: "192.168.86.52")
        vm.connected = true
        vm.status = DeviceStatus(ap: 0, wifi: 1, ip: "192.168.86.52", tci: 1,
                                 freq: 7_140_000, band: "40m", tx: 0, tune: 0,
                                 overrideMode: -2, activeRelay: 1, switching: 0,
                                 interlock: nil, radio2: nil)
        vm.identity = DeviceIdentity(device: "AntennaSwitchController", version: "1.0",
                                     hostname: "ANT-SW-Controller-2F",
                                     mdns: "ANT-SW-Controller-2F.local",
                                     ip: "192.168.86.52", relays: 8)
        var cfg = DeviceConfig.empty
        cfg.hostname = "ANT-SW-Controller-2F"
        cfg.ssid = "Shack-WiFi"
        cfg.tciHost = "192.168.86.10"
        cfg.tciPort = 50001
        cfg.region = 1
        cfg.guardMs = 50
        cfg.bands = [0, 1, 2, 3, -1, 4, 5, 6, -1, 7, -1]   // sample assignments
        vm.config = cfg
        vm.configLoaded = true
        return vm
    }

    public static func render(to dir: String) {
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        let vm = demoViewModel()
        let bg = Color(nsColor: .windowBackgroundColor)
        save(AnyView(ControlsView(vm: vm).frame(width: 620, height: 540).background(bg)),
             "app-controls.png", dir)
        save(AnyView(SettingsView(vm: vm).frame(width: 620, height: 760).background(bg)),
             "app-settings.png", dir)
    }

    private static func save(_ view: AnyView, _ name: String, _ dir: String) {
        let renderer = ImageRenderer(content: view)
        renderer.scale = 2.0
        guard let img = renderer.nsImage,
              let tiff = img.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else {
            print("✗ \(name)"); return
        }
        try? png.write(to: URL(fileURLWithPath: "\(dir)/\(name)"))
        print("✓ \(name)")
    }
}
