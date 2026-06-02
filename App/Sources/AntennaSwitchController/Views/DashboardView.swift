import SwiftUI
import RadioPluginUI

/// Live operational view of a controller's `/status`. Uses the host-injected
/// `RadioTheme` (via RadioPluginUI components + tokens) so it matches the suite.
struct DashboardView: View {
    @ObservedObject var vm: ControllerViewModel
    @Environment(\.radioTheme) private var theme

    private let columns = [GridItem(.adaptive(minimum: 180), spacing: 12)]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if let err = vm.errorMessage, !vm.connected {
                    Banner(level: .error, title: "Disconnected", message: err)
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
                        StatusBadge(s.apMode ? "Setup AP" : "WiFi",
                                    kind: s.apMode ? .warning : (s.wifiUp ? .success : .neutral))
                        StatusBadge("TCI", kind: s.tciUp ? .success : .neutral)
                        StatusBadge("TX", kind: s.transmitting ? .danger : .neutral)
                        StatusBadge("Tune", kind: s.tuning ? .warning : .neutral)
                        if s.isSwitching { StatusBadge("Switching", kind: .neutral) }
                    }

                    if let ident = vm.identity {
                        Text("\(ident.device) • firmware \(ident.version ?? "?") • \(ident.relays ?? kRelayCount) relays")
                            .font(.caption).foregroundStyle(theme.textSecondary)
                    }
                    Text("IP \(s.ip)").font(.caption).foregroundStyle(theme.textSecondary)
                } else {
                    ProgressView("Connecting to \(vm.host)…")
                        .frame(maxWidth: .infinity, minHeight: 200)
                }
            }
            .padding()
        }
        .background(theme.background)
    }
}

/// One metric tile, themed via the host palette.
struct StatCard: View {
    @Environment(\.radioTheme) private var theme
    let title: String
    let value: String
    let systemImage: String

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(title, systemImage: systemImage)
                .font(.caption).foregroundStyle(theme.textSecondary)
            Text(value).font(.title3).bold().lineLimit(1).minimumScaleFactor(0.6)
                .foregroundStyle(theme.textPrimary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(theme.surface, in: RoundedRectangle(cornerRadius: theme.cornerRadius))
    }
}
