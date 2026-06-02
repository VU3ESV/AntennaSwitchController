import SwiftUI
import RadioPluginUI

/// Manual override controls — force a relay, all-off, or return to auto/TCI.
struct ControlsView: View {
    @ObservedObject var vm: ControllerViewModel
    @Environment(\.radioTheme) private var theme

    private let columns = [GridItem(.adaptive(minimum: 96), spacing: 10)]

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

            LazyVGrid(columns: columns, spacing: 10) {
                ForEach(0..<kRelayCount, id: \.self) { r in
                    let active = vm.status?.activeRelay == r
                    Button { Task { await vm.setRelay(String(r)) } } label: {
                        VStack(spacing: 2) {
                            Text("R\(r + 1)").font(.headline)
                            Text("GPIO\(kRelayGPIO[r])").font(.caption2).foregroundStyle(theme.textSecondary)
                        }
                        .frame(maxWidth: .infinity).padding(.vertical, 10)
                    }
                    .buttonStyle(.bordered)
                    .tint(active ? theme.success : theme.textSecondary)
                }
            }

            if let s = vm.status {
                Text(s.activeRelay < 0
                     ? "No relay energized."
                     : "Relay R\(s.activeRelay + 1) energized.")
                    .font(.callout).foregroundStyle(theme.textSecondary)
            }

            Spacer()
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(theme.background)
    }
}
