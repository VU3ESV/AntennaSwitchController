import SwiftUI

/// Offscreen renderer for documentation screenshots of the Controls and Settings
/// panes (which can't be reached via screen capture without UI-scripting access).
/// Invoked by the standalone binary with `--snapshots <dir>`; not used at runtime.
@MainActor
public enum SnapshotTool {
    /// A view model pre-populated with representative data (no network), shared by
    /// the offscreen renderer and the `ASC_DEMO` window mode.
    static func demoViewModel() -> ControllerViewModel {
        let vm = ControllerViewModel(host: "192.168.86.50")
        vm.connected = true
        // Mode B (dual): one board tracking a SunSDR2's two receivers (RX1 = 20 m,
        // RX2 = 40 m) and driving an 8×2 switch — both relays energized, named.
        vm.status = DeviceStatus(
            ap: 0, wifi: 1, ip: "192.168.86.50", tci: 1,
            freq: 14_074_000, band: "20m", tx: 0, tune: 0,
            overrideMode: -2,
            activeRelay: 1,                       // legacy field (= radio 2 in dual)
            radio1Relay: 2, radio2Relay: 1,       // explicit per-radio antennas
            switching: 0,
            interlock: InterlockStatus(role: "dual", peerUp: 1, beatsMissed: nil,
                                       masterAnt: 2, slaveAnt: 1),
            radio2: Radio2Status(tci: 1, freq: 7_140_000, band: "40m", tx: 0))
        vm.identity = DeviceIdentity(device: "AntennaSwitchController", version: "0.1.15",
                                     hostname: "ANT-SW-Controller-1F",
                                     mdns: "ANT-SW-Controller-1F.local",
                                     ip: "192.168.86.50", relays: 8)
        var cfg = DeviceConfig.empty
        cfg.hostname = "ANT-SW-Controller-1F"
        cfg.ssid = "Shack-WiFi"
        cfg.radioType = .tci
        cfg.tciHost = "192.168.86.50"; cfg.tciPort = 50001; cfg.radioRx = 0
        cfg.region = 1; cfg.guardMs = 50
        cfg.mode = .dual
        cfg.radio2Type = .tci; cfg.radio2Host = "192.168.86.50"
        cfg.radio2Port = 50001; cfg.radio2Rx = 1
        cfg.switchType = .eightByTwo
        cfg.relayNames = ["80m Dipole", "40m Vertical", "20m Hex", "17m Dipole",
                          "15m Yagi", "10m Yagi", "6m Yagi", "Dummy Load"]
        cfg.bands = [-1, 0, -1, 1, -1, 2, 3, 4, -1, 5, 6]   // 80→R1,40→R2,20→R3,17→R4,15→R5,10→R6,6→R7
        vm.config = cfg
        vm.configLoaded = true
        return vm
    }

    public static func render(to dir: String) {
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        let vm = demoViewModel()
        let bg = Color(nsColor: .windowBackgroundColor)
        save(AnyView(DashboardContent(vm: vm).frame(width: 640, height: 470, alignment: .top).background(bg)),
             "app-dashboard.png", dir)
        save(AnyView(ControlsView(vm: vm).frame(width: 640, height: 470, alignment: .top).background(bg)),
             "app-controls.png", dir)
        // NOTE: SettingsView is a grouped Form, which the current ImageRenderer
        // doesn't lay out offscreen (renders blank). Regenerate by hand if needed;
        // the committed app-settings.png is a prior good render.
        save(AnyView(SettingsView(vm: vm).frame(width: 640, height: 900).background(bg)),
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
