import SwiftUI

/// Manual override controls — force a relay, all-off, or return to auto/TCI.
struct ControlsView: View {
    @ObservedObject var vm: ControllerViewModel

    private let columns = [GridItem(.adaptive(minimum: 96), spacing: 10)]

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Manual Override").font(.headline)
            Text("Force a specific relay regardless of TCI. ‘Auto’ returns to band tracking. Switching is break-before-make and is deferred while the radio is transmitting.")
                .font(.caption).foregroundStyle(.secondary)

            HStack {
                Button { Task { await vm.setRelay("auto") } } label: {
                    Label("Auto (TCI)", systemImage: "wand.and.stars")
                }
                .buttonStyle(.borderedProminent)
                .disabled(vm.status?.isAuto == true)

                Button { Task { await vm.setRelay("none") } } label: {
                    Label("All Off", systemImage: "poweroff")
                }
                .disabled(vm.status?.activeRelay == -1 && vm.status?.isAuto == false)
            }

            LazyVGrid(columns: columns, spacing: 10) {
                ForEach(0..<kRelayCount, id: \.self) { r in
                    Button { Task { await vm.setRelay(String(r)) } } label: {
                        VStack(spacing: 2) {
                            Text("R\(r + 1)").font(.headline)
                            Text("GPIO\(kRelayGPIO[r])").font(.caption2).foregroundStyle(.secondary)
                        }
                        .frame(maxWidth: .infinity).padding(.vertical, 10)
                    }
                    .buttonStyle(.bordered)
                    .tint(vm.status?.activeRelay == r ? .green : nil)
                }
            }

            if let s = vm.status {
                Text(s.activeRelay < 0
                     ? "No relay energized."
                     : "Relay R\(s.activeRelay + 1) energized.")
                    .font(.callout).foregroundStyle(.secondary)
            }

            Spacer()
        }
        .padding()
    }
}
