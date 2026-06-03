import SwiftUI
import RadioPluginUI

/// Live operational view of a controller's `/status`. Uses the host-injected
/// `RadioTheme` (via RadioPluginUI components + tokens) so it matches the suite.
struct DashboardView: View {
    @ObservedObject var vm: ControllerViewModel
    @Environment(\.radioTheme) private var theme

    var body: some View {
        ScrollView { DashboardContent(vm: vm) }
            .background(theme.background)
    }
}

/// The dashboard's scrollable content as a standalone view, so it can be rendered
/// for the documentation screenshots (ImageRenderer doesn't lay out the content
/// of a ScrollView offscreen).
struct DashboardContent: View {
    @ObservedObject var vm: ControllerViewModel
    @Environment(\.radioTheme) private var theme

    private let columns = [GridItem(.adaptive(minimum: 180), spacing: 12)]

    /// "None", or the relay's name + GPIO (e.g. "80m Dipole (GPIO14)").
    private func relayValue(_ idx: Int) -> String {
        guard idx >= 0, idx < kRelayCount else { return "None" }
        return "\(vm.config.relayLabel(idx)) (GPIO\(kRelayGPIO[idx]))"
    }

    @ViewBuilder
    private func contentionBadge(bandLabel: String, granted: Int) -> some View {
        switch AntennaContention.evaluate(bandLabel: bandLabel, granted: granted,
                                          bands: vm.config.bands, fallback: vm.config.bands2) {
        case .onFallback: StatusBadge("On fallback", kind: .warning)
        case .blocked:    StatusBadge("Primary busy", kind: .danger)
        case .none:       EmptyView()
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
                if let err = vm.errorMessage, !vm.connected {
                    Banner(level: .error, title: "Disconnected", message: err)
                }

                if let s = vm.status {
                    // In dual mode (Mode B) the top row is Radio 1. The firmware's
                    // active_relay is actually Radio 2's antenna, so use the interlock
                    // fields for unambiguous per-radio relays (master = R1, slave = R2).
                    let dual = s.isDual
                    let r1Relay = s.radio1RelayIndex
                    LazyVGrid(columns: columns, alignment: .leading, spacing: 12) {
                        StatCard(title: dual ? "Radio 1 Band" : "Band", value: s.band, systemImage: "waveform")
                        StatCard(title: dual ? "Radio 1 Freq" : "Frequency", value: s.freqMHz, systemImage: "dot.radiowaves.right")
                        StatCard(title: dual ? "Radio 1 Relay" : "Active Relay",
                                 value: relayValue(r1Relay),
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
                        // SO2R: flag when this radio lost its primary to the other.
                        contentionBadge(bandLabel: s.band, granted: r1Relay)
                    }

                    // SO2R interlock (hidden for standalone units). For Mode A the
                    // master_ant/slave_ant are the two boards; for Mode B (dual)
                    // they are Radio 1 / Radio 2 on this one board.
                    if let ilk = s.interlock, !ilk.isStandalone {
                        HStack(spacing: 10) {
                            StatusBadge(ilk.isDual ? "Dual (Mode B)" : ilk.role.capitalized, kind: .neutral)
                            // Mode A only: peer health + the two boards' antennas as
                            // badges. In dual mode each radio's relay is shown as a
                            // card (below), so the badges would be redundant.
                            if !ilk.isDual {
                                StatusBadge(ilk.peerIsUp ? "Peer up" : "Peer down",
                                            kind: ilk.peerIsUp ? .success : .danger)
                                if let m = ilk.beatsMissed, m > 0, ilk.peerIsUp {
                                    StatusBadge("♥ \(m) missed", kind: .warning)
                                }
                                if ilk.masterAnt >= 0 { StatusBadge("Radio 1 → \(vm.config.relayLabel(ilk.masterAnt))", kind: .neutral) }
                                if ilk.slaveAnt >= 0  { StatusBadge("Radio 2 → \(vm.config.relayLabel(ilk.slaveAnt))",  kind: .neutral) }
                            }
                        }
                    }

                    // Mode B: Radio 2's band / frequency / relay on this board,
                    // symmetric with the Radio 1 row above (slave_ant = Radio 2).
                    if let r2 = s.radio2 {
                        let r2Relay = s.radio2RelayIndex ?? -1
                        LazyVGrid(columns: columns, alignment: .leading, spacing: 12) {
                            StatCard(title: "Radio 2 Band", value: r2.band, systemImage: "waveform")
                            StatCard(title: "Radio 2 Freq", value: r2.freqMHz, systemImage: "dot.radiowaves.right")
                            StatCard(title: "Radio 2 Relay", value: relayValue(r2Relay), systemImage: "switch.2")
                        }
                        HStack(spacing: 10) {
                            StatusBadge("Radio 2 TCI", kind: r2.tciUp ? .success : .neutral)
                            StatusBadge("Radio 2 TX", kind: r2.transmitting ? .danger : .neutral)
                            contentionBadge(bandLabel: r2.band, granted: r2Relay)
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
