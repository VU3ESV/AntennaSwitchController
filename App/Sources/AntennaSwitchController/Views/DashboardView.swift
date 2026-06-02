import SwiftUI

/// Live operational view of a controller's `/status`.
struct DashboardView: View {
    @ObservedObject var vm: ControllerViewModel

    private let columns = [GridItem(.adaptive(minimum: 180), spacing: 12)]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if let err = vm.errorMessage, !vm.connected {
                    Banner(text: err, systemImage: "exclamationmark.triangle.fill", tint: .orange)
                }

                if let s = vm.status {
                    LazyVGrid(columns: columns, alignment: .leading, spacing: 12) {
                        StatCard(title: "Band", value: s.band, systemImage: "waveform")
                        StatCard(title: "Frequency", value: s.freqMHz, systemImage: "dot.radiowaves.right")
                        StatCard(title: "Active Relay",
                                 value: s.activeRelay < 0 ? "None" : "R\(s.activeRelay + 1) (GPIO\(kRelayGPIO[s.activeRelay]))",
                                 systemImage: "switch.2")
                        StatCard(title: "Mode",
                                 value: s.isAuto ? "Auto (TCI)" : (s.overrideMode == -1 ? "Forced Off" : "Forced R\(s.overrideMode + 1)"),
                                 systemImage: "slider.horizontal.3")
                    }

                    HStack(spacing: 10) {
                        StatusBadge(label: s.apMode ? "Setup AP" : "WiFi", on: s.apMode ? true : s.wifiUp, onColor: s.apMode ? .orange : .green)
                        StatusBadge(label: "TCI", on: s.tciUp, onColor: .green)
                        StatusBadge(label: "TX", on: s.transmitting, onColor: .red)
                        StatusBadge(label: "Tune", on: s.tuning, onColor: .yellow)
                        if s.isSwitching { StatusBadge(label: "Switching", on: true, onColor: .blue) }
                    }

                    if let ident = vm.identity {
                        Text("\(ident.device) • firmware \(ident.version ?? "?") • \(ident.relays ?? kRelayCount) relays")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    Text("IP \(s.ip)").font(.caption).foregroundStyle(.secondary)
                } else {
                    ProgressView("Connecting to \(vm.host)…")
                        .frame(maxWidth: .infinity, minHeight: 200)
                }
            }
            .padding()
        }
    }
}

struct StatCard: View {
    let title: String
    let value: String
    let systemImage: String
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(title, systemImage: systemImage).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title3).bold().lineLimit(1).minimumScaleFactor(0.6)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(.quaternary.opacity(0.4), in: RoundedRectangle(cornerRadius: 10))
    }
}

struct StatusBadge: View {
    let label: String
    let on: Bool
    var onColor: Color = .green
    var body: some View {
        HStack(spacing: 5) {
            Circle().fill(on ? onColor : Color.secondary.opacity(0.4)).frame(width: 8, height: 8)
            Text(label).font(.caption)
        }
        .padding(.horizontal, 10).padding(.vertical, 5)
        .background(.quaternary.opacity(0.4), in: Capsule())
    }
}

struct Banner: View {
    let text: String
    var systemImage: String = "info.circle"
    var tint: Color = .accentColor
    var body: some View {
        Label(text, systemImage: systemImage)
            .font(.callout)
            .padding(10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(tint.opacity(0.15), in: RoundedRectangle(cornerRadius: 8))
    }
}
