import SwiftUI
import RadioPluginUI

/// Manual override controls — force a relay, all-off, or return to auto/TCI.
struct ControlsView: View {
    @ObservedObject var vm: ControllerViewModel
    @Environment(\.radioTheme) private var theme

    private let columns = [GridItem(.adaptive(minimum: 96), spacing: 10)]

    /// Human summary of which relay(s) are energized — two in dual mode (one per
    /// radio), one otherwise.
    private func energizedSummary(_ s: DeviceStatus) -> String {
        func label(_ i: Int) -> String { i >= 0 ? vm.config.relayLabel(i) : "OFF" }
        // Override applies to Radio 1: -2 auto, -1 manual off, 0..7 manual relay.
        let r1Mode = s.overrideMode == -2 ? "auto" : "manual"
        if s.isDual {
            let r1 = s.radio1RelayIndex, r2 = s.radio2RelayIndex ?? -1
            return "Radio 1 → \(label(r1)) (\(r1Mode))   ·   Radio 2 → \(label(r2)) (auto)"
        }
        if s.overrideMode == -1 { return "Manually OFF — no relay energized." }
        if s.activeRelay < 0    { return "No relay energized (auto)." }
        return "\(vm.config.relayLabel(s.activeRelay)) energized (\(r1Mode))."
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Manual Override").font(.headline).foregroundStyle(theme.textPrimary)
            Text("Force a relay regardless of TCI — **tap an active (red) relay again to switch it off**. ‘Auto’ returns to band tracking; ‘All Off’ disconnects everything. Switching is break-before-make and deferred while transmitting.")
                .font(.caption).foregroundStyle(theme.textSecondary)

            HStack {
                Button { Task { await vm.setRelay("auto") } } label: {
                    Label("Auto (TCI)", systemImage: "wand.and.stars")
                }
                .buttonStyle(.borderedProminent).tint(theme.accent)
                .disabled(vm.status?.isAuto == true)

                Button { Task { await vm.setRelay("none") } } label: {
                    Label(vm.status?.isDual == true ? "Radio 1 Off" : "All Off", systemImage: "poweroff")
                }
                .buttonStyle(.bordered).tint(theme.danger)
                // Greyed when it's the current state (manual off), mirroring Auto.
                .disabled(vm.status?.overrideMode == -1)
            }

            // Mode B: a manual override drives Radio 1 only; Radio 2 stays automatic.
            if vm.status?.isDual == true {
                Text("Mode B: override applies to **Radio 1**; Radio 2 keeps tracking its band automatically.")
                    .font(.caption).foregroundStyle(theme.textSecondary)
            }

            LazyVGrid(columns: columns, spacing: 10) {
                ForEach(0..<kRelayCount, id: \.self) { r in
                    let active = vm.status?.energizedRelays.contains(r) ?? false
                    let forced = (vm.status?.overrideMode ?? -2) == r
                    let named = r < vm.config.relayNames.count && !vm.config.relayNames[r].isEmpty
                    Button {
                        // Toggle: tap the forced relay again to switch it off;
                        // otherwise force this relay on.
                        Task { await vm.setRelay(forced ? "none" : String(r)) }
                    } label: {
                        VStack(spacing: 2) {
                            HStack(spacing: 4) {
                                if forced { Image(systemName: "hand.raised.fill").font(.caption2) }
                                Text(vm.config.relayLabel(r)).font(.headline)
                                    .lineLimit(1).minimumScaleFactor(0.6)
                            }
                            Text(named ? "R\(r + 1) · GPIO\(kRelayGPIO[r])" : "GPIO\(kRelayGPIO[r])")
                                .font(.caption2).foregroundStyle(theme.textSecondary)
                        }
                        .frame(maxWidth: .infinity).padding(.vertical, 10)
                    }
                    .buttonStyle(.bordered)
                    // Active (energized) relays are red; idle relays grey. A forced
                    // relay also shows a hand icon — tap it again to switch it off.
                    .tint(active ? theme.danger : theme.textSecondary)
                }
            }

            if let s = vm.status {
                Text(energizedSummary(s))
                    .font(.callout).foregroundStyle(theme.textSecondary)
            }

            Spacer()
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(theme.background)
    }
}
