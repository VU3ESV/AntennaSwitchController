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
        if s.energizedRelays.isEmpty { return "No relay energized." }
        if s.isDual {
            let r1 = s.radio1RelayIndex, r2 = s.radio2RelayIndex ?? -1
            let a = r1 >= 0 ? vm.config.relayLabel(r1) : "none"
            let b = r2 >= 0 ? vm.config.relayLabel(r2) : "none"
            return "Radio 1 → \(a) · Radio 2 → \(b)  (both energized)."
        }
        return "\(vm.config.relayLabel(s.activeRelay)) energized."
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Manual Override").font(.headline).foregroundStyle(theme.textPrimary)
            Text("Force a specific relay regardless of TCI. ‘Auto’ returns to band tracking. Switching is break-before-make and is deferred while the radio is transmitting.")
                .font(.caption).foregroundStyle(theme.textSecondary)

            HStack {
                Button { Task { await vm.setRelay("auto") } } label: {
                    Label("Auto (TCI)", systemImage: "wand.and.stars")
                }
                .buttonStyle(.borderedProminent).tint(theme.accent)
                .disabled(vm.status?.isAuto == true)

                Button { Task { await vm.setRelay("none") } } label: {
                    Label("All Off", systemImage: "poweroff")
                }
                .disabled(vm.status?.activeRelay == -1 && vm.status?.isAuto == false)
            }

            // Mode B: a manual override drives Radio 1 only; Radio 2 stays automatic.
            if vm.status?.isDual == true {
                Text("Mode B: override applies to **Radio 1**; Radio 2 keeps tracking its band automatically.")
                    .font(.caption).foregroundStyle(theme.textSecondary)
            }

            LazyVGrid(columns: columns, spacing: 10) {
                ForEach(0..<kRelayCount, id: \.self) { r in
                    let active = vm.status?.energizedRelays.contains(r) ?? false
                    let named = r < vm.config.relayNames.count && !vm.config.relayNames[r].isEmpty
                    Button { Task { await vm.setRelay(String(r)) } } label: {
                        VStack(spacing: 2) {
                            Text(vm.config.relayLabel(r)).font(.headline)
                                .lineLimit(1).minimumScaleFactor(0.6)
                            Text(named ? "R\(r + 1) · GPIO\(kRelayGPIO[r])" : "GPIO\(kRelayGPIO[r])")
                                .font(.caption2).foregroundStyle(theme.textSecondary)
                        }
                        .frame(maxWidth: .infinity).padding(.vertical, 10)
                    }
                    .buttonStyle(.bordered)
                    .tint(active ? theme.success : theme.textSecondary)
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
