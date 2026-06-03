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
                                 value: s.activeRelay < 0 ? "None" : "\(vm.config.relayLabel(s.activeRelay)) (GPIO\(kRelayGPIO[s.activeRelay]))",
                                 systemImage: "switch.2")
                        StatCard(title: "Mode",
                                 value: s.isAuto ? "Auto (TCI)" : (s.overrideMode == -1 ? "Forced Off" : "Forced \(vm.config.relayLabel(s.overrideMode))"),
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

                    // SO2R interlock (hidden for standalone units). For Mode A the
                    // master_ant/slave_ant are the two boards; for Mode B (dual)
                    // they are Radio 1 / Radio 2 on this one board.
                    if let ilk = s.interlock, !ilk.isStandalone {
                        HStack(spacing: 10) {
                            StatusBadge(ilk.isDual ? "Dual (Mode B)" : ilk.role.capitalized, kind: .neutral)
                            if !ilk.isDual {
                                StatusBadge(ilk.peerIsUp ? "Peer up" : "Peer down",
                                            kind: ilk.peerIsUp ? .success : .danger)
                                // Heartbeat health: flag tolerated misses before loss.
                                if let m = ilk.beatsMissed, m > 0, ilk.peerIsUp {
                                    StatusBadge("♥ \(m) missed", kind: .warning)
                                }
                            }
                            if ilk.masterAnt >= 0 { StatusBadge("Radio 1 → \(vm.config.relayLabel(ilk.masterAnt))", kind: .neutral) }
                            if ilk.slaveAnt >= 0  { StatusBadge("Radio 2 → \(vm.config.relayLabel(ilk.slaveAnt))",  kind: .neutral) }
                        }
                    }

                    // Mode B: Radio 2's band/frequency on this board.
                    if let r2 = s.radio2 {
                        LazyVGrid(columns: columns, alignment: .leading, spacing: 12) {
                            StatCard(title: "Radio 2 Band", value: r2.band, systemImage: "waveform")
                            StatCard(title: "Radio 2 Freq", value: r2.freqMHz, systemImage: "dot.radiowaves.right")
                        }
                        HStack(spacing: 10) {
                            StatusBadge("Radio 2 TCI", kind: r2.tciUp ? .success : .neutral)
                            StatusBadge("Radio 2 TX", kind: r2.transmitting ? .danger : .neutral)
                        }
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
