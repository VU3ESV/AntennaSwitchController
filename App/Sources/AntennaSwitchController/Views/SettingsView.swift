import SwiftUI
import RadioPluginUI

/// Full configuration form — mirrors every option on the controller's web page:
/// WiFi, TCI server, band→relay map, hostname, OTA password, guard delay.
struct SettingsView: View {
    @ObservedObject var vm: ControllerViewModel
    /// Controllers discovered on the LAN (`_antsw._tcp`), for the Slave's
    /// master-pick menu. Injected by the detail view from the shared store.
    var discoveredMasters: [DiscoveredDevice] = []

    var body: some View {
        Group {
            if vm.configLoaded {
                form
            } else {
                VStack(spacing: 12) {
                    ProgressView("Loading configuration…")
                    if let err = vm.errorMessage {
                        Banner(level: .warning, title: "Couldn’t load configuration", message: err)
                        Button("Retry") { Task { await vm.loadConfig() } }
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .task { if !vm.configLoaded { await vm.loadConfig() } }
    }

    private var form: some View {
        Form {
            Section("WiFi") {
                TextField("SSID", text: $vm.config.ssid)
                SecureField("Password (blank = keep)", text: $vm.config.wifiPassword)
            }

            Section("Radio") {
                Picker("Type", selection: radioTypeBinding) {
                    ForEach(RadioType.allCases, id: \.self) { type in
                        Text(type.label).tag(type)
                    }
                }
                if vm.config.radioType.isSerial {
                    // Serial CAT on the board's UART — band tracking only.
                    Picker("CAT baud", selection: $vm.config.catBaud) {
                        ForEach([4800, 9600, 19200, 38400, 57600, 115200], id: \.self) {
                            Text("\($0)").tag($0)
                        }
                    }
                    if vm.config.radioType == .catIcom {
                        HStack {
                            Text("CI-V address")
                            Spacer()
                            TextField("0x94", text: civAddrHexBinding)
                                .frame(width: 80).multilineTextAlignment(.trailing)
                        }
                    }
                    Text("Read-only serial CAT (one per board, UART0). Tracks band only — it can't inhibit switching during TX. See HARDWARE.md.")
                        .font(.caption).foregroundStyle(.secondary)
                } else {
                    TextField("Host / IP", text: $vm.config.tciHost)
                    TextField("Port", value: $vm.config.tciPort, format: .number.grouping(.never))
                }
                if vm.config.radioType == .tci {
                    Picker("Receiver", selection: $vm.config.radioRx) {
                        Text("RX1").tag(0); Text("RX2").tag(1)
                    }
                    .pickerStyle(.segmented)
                    Picker("IARU Region", selection: $vm.config.region) {
                        Text("1").tag(1); Text("2").tag(2); Text("3").tag(3)
                    }
                    .pickerStyle(.segmented)
                }
            }

            Section("Antennas") {
                ForEach(0..<kRelayCount, id: \.self) { r in
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text("R\(r + 1)")
                                .font(.callout.monospacedDigit())
                                .frame(width: 32, alignment: .leading)
                                .foregroundStyle(.secondary)
                            TextField("R\(r + 1) (GPIO\(kRelayGPIO[r]))", text: relayNameBinding(r))
                        }
                        HStack(spacing: 10) {
                            // Band coverage (multiband model) — multi-select menu.
                            Menu {
                                ForEach(Band.allCases) { band in
                                    Toggle(band.label, isOn: coverageBinding(r, band.rawValue))
                                }
                            } label: {
                                Label(coverageLabel(r), systemImage: "antenna.radiowaves.left.and.right")
                                    .font(.caption)
                            }
                            Spacer()
                            Picker("", selection: feedBinding(r)) {
                                Text("Single").tag(0); Text("Triplexed").tag(1)
                            }
                            .labelsHidden().fixedSize()
                            Stepper("Grp \(vm.config.relayGroup[r])", value: groupBinding(r), in: 0...kRelayCount)
                                .fixedSize().font(.caption)
                        }
                    }
                }
                Text("Name each antenna and tick the bands it covers (used to warn about mis-assignments below; leave empty to skip). Triplexed legs of one physical antenna sharing a group may serve both radios at once.")
                    .font(.caption).foregroundStyle(.secondary)
                if !AntennaCoverage.conflictingGroups(relayGroup: vm.config.relayGroup,
                                                      relayBands: vm.config.relayBands).isEmpty {
                    Label("A triplexer group has two legs covering the same band.",
                          systemImage: "exclamationmark.triangle.fill")
                        .font(.caption).foregroundStyle(.orange)
                }
            }

            Section("Band → Relay Map") {
                ForEach(Band.allCases) { band in
                    Picker(band.label, selection: relayBinding(for: band.rawValue)) {
                        Text("None / bypass").tag(-1)
                        ForEach(0..<kRelayCount, id: \.self) { r in
                            Text("\(vm.config.relayLabel(r)) (GPIO\(kRelayGPIO[r]))").tag(r)
                        }
                    }
                    // Coverage warning: assigned antenna's declared bands exclude this one.
                    if mismatchedBands.contains(band.rawValue) {
                        Label("Assigned antenna's coverage excludes \(band.label).",
                              systemImage: "exclamationmark.triangle.fill")
                            .font(.caption).foregroundStyle(.orange)
                    }
                    // SO2R fallback antenna — only meaningful when two radios share
                    // this controller (Master/Slave/Dual); hidden for standalone.
                    if vm.config.mode != .standalone {
                        Picker("↳ Fallback", selection: secondaryBinding(for: band.rawValue)) {
                            Text("None").tag(-1)
                            ForEach(0..<kRelayCount, id: \.self) { r in
                                Text(vm.config.relayLabel(r)).tag(r)
                            }
                        }
                        .font(.caption)
                    }
                }
                Text(vm.config.mode == .standalone
                     ? "Multiple bands may share one relay. Relays on GPIO0/15/16 may twitch at power-up."
                     : "Fallback is used in SO2R when the primary antenna is already in use by the other radio — e.g. a HexBeam shared across 20–6 m, with a wire dipole as the fallback.")
                    .font(.caption).foregroundStyle(.secondary)
            }

            Section("Device") {
                TextField("Hostname (mDNS / OTA)", text: $vm.config.hostname)
                SecureField("OTA password (blank = keep)", text: $vm.config.otaPassword)
                HStack {
                    Text("Break-before-make guard (ms)")
                    Spacer()
                    TextField("ms", value: $vm.config.guardMs, format: .number.grouping(.never))
                        .frame(width: 80).multilineTextAlignment(.trailing)
                }
            }

            Section("SO2R Role (Mode A)") {
                Picker("Role", selection: $vm.config.mode) {
                    ForEach(CtrlMode.allCases, id: \.self) { Text($0.label).tag($0) }
                }
                if vm.config.mode == .slave {
                    TextField("Master address (IP or .local)", text: $vm.config.peerHost)
                    if !masterCandidates.isEmpty {
                        Menu {
                            ForEach(masterCandidates) { dev in
                                Button { vm.config.peerHost = dev.address } label: {
                                    Text("\(dev.title) — \(dev.address)")
                                }
                            }
                        } label: {
                            Label("Pick master from network", systemImage: "antenna.radiowaves.left.and.right")
                                .font(.caption)
                        }
                    }
                }
                if vm.config.mode != .standalone {
                    Picker("Interlock policy", selection: $vm.config.interlockPolicy) {
                        ForEach(InterlockPolicy.allCases, id: \.self) { Text($0.label).tag($0) }
                    }
                }
                if vm.config.mode == .slave {
                    Picker("On master loss", selection: $vm.config.onPeerLoss) {
                        ForEach(PeerLoss.allCases, id: \.self) { Text($0.label).tag($0) }
                    }
                }
            }

            if vm.config.mode == .dual {
                Section("Radio 2 (Mode B)") {
                    Picker("Type", selection: $vm.config.radio2Type) {
                        ForEach(RadioType.allCases, id: \.self) { Text($0.label).tag($0) }
                    }
                    TextField("Host / IP", text: $vm.config.radio2Host)
                    TextField("Port", value: $vm.config.radio2Port, format: .number.grouping(.never))
                    // The RX1/RX2 picker only makes sense when both radios are the
                    // SAME TCI server (one 2-receiver radio). For two separate radios
                    // each uses its own RX1, so hide the picker and force RX1 (see
                    // normalizeRadio2Rx, applied on save).
                    if radio2SharesRadio1 {
                        Picker("Receiver", selection: $vm.config.radio2Rx) {
                            Text("RX1").tag(0); Text("RX2").tag(1)
                        }
                        .pickerStyle(.segmented)
                    }
                    Picker("External switch", selection: $vm.config.switchType) {
                        ForEach(SwitchType.allCases, id: \.self) { Text($0.label).tag($0) }
                    }
                    if radio2SharesRadio1 {
                        Text("One 2-receiver radio (e.g. SunSDR2): radio 1 + radio 2 share this Host/Port — radio 1 = RX1, radio 2 = RX2.")
                            .font(.caption).foregroundStyle(.secondary)
                    } else {
                        Text("Separate radio — tracks its own RX1. (RX2 only applies when both radios share one Host/Port, e.g. a 2-receiver SunSDR2.)")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }
            }

            Section {
                HStack {
                    Button { Task { normalizeRadio2Rx(); await vm.save() } } label: {
                        if vm.isSaving { ProgressView().controlSize(.small) }
                        else { Text("Save & Apply") }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(vm.isSaving)

                    Button("Reload") { Task { await vm.loadConfig() } }
                        .disabled(vm.isSaving)

                    Spacer()
                    if let r = vm.saveResult {
                        Text(r).font(.caption).foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
    }

    /// True when Radio 2 is the SAME TCI endpoint as Radio 1 — i.e. one physical
    /// radio exposing two receivers (RX1/RX2). Only then is the Radio 2 RX picker
    /// meaningful; two separate radios each use their own RX1.
    private var radio2SharesRadio1: Bool {
        vm.config.radioType == .tci && vm.config.radio2Type == .tci &&
        vm.config.radio2Host == vm.config.tciHost &&
        vm.config.radio2Port == vm.config.tciPort
    }

    /// Force Radio 2 to RX1 unless it shares Radio 1's endpoint, so a stale RX2
    /// from an earlier single-2-receiver-radio setup can't break a separate radio.
    private func normalizeRadio2Rx() {
        if !radio2SharesRadio1 { vm.config.radio2Rx = 0 }
    }

    /// Binding for the radio type. Switching transports retunes the port to the
    /// new transport's conventional default, but only if the current port is
    /// still the old transport's default (so a custom port is never clobbered).
    private var radioTypeBinding: Binding<RadioType> {
        Binding(
            get: { vm.config.radioType },
            set: { newType in
                let old = vm.config.radioType
                if vm.config.tciPort == old.defaultPort { vm.config.tciPort = newType.defaultPort }
                vm.config.radioType = newType
            }
        )
    }

    /// CI-V address edited as hex text ("0x94"); accepts hex with/without 0x.
    private var civAddrHexBinding: Binding<String> {
        Binding(
            get: { String(format: "0x%02X", vm.config.civAddr) },
            set: { txt in
                let s = txt.lowercased().replacingOccurrences(of: "0x", with: "")
                if let v = Int(s, radix: 16), (0...255).contains(v) { vm.config.civAddr = v }
            }
        )
    }

    /// Two-way binding into the `bands` array element for one band index.
    private func relayBinding(for index: Int) -> Binding<Int> {
        Binding(
            get: { index < vm.config.bands.count ? vm.config.bands[index] : -1 },
            set: { if index < vm.config.bands.count { vm.config.bands[index] = $0 } }
        )
    }

    /// Two-way binding into the `bands2` (SO2R fallback) array element.
    private func secondaryBinding(for index: Int) -> Binding<Int> {
        Binding(
            get: { index < vm.config.bands2.count ? vm.config.bands2[index] : -1 },
            set: { if index < vm.config.bands2.count { vm.config.bands2[index] = $0 } }
        )
    }

    /// Two-way binding into the `relayNames` array element for one relay index.
    private func relayNameBinding(_ r: Int) -> Binding<String> {
        Binding(
            get: { r < vm.config.relayNames.count ? vm.config.relayNames[r] : "" },
            set: { if r < vm.config.relayNames.count { vm.config.relayNames[r] = $0 } }
        )
    }

    // MARK: - Antenna metadata bindings

    /// Toggle binding for one band bit in relay `r`'s coverage bitmask.
    private func coverageBinding(_ r: Int, _ bandIndex: Int) -> Binding<Bool> {
        Binding(
            get: { r < vm.config.relayBands.count && (vm.config.relayBands[r] & (1 << bandIndex)) != 0 },
            set: { on in
                guard r < vm.config.relayBands.count else { return }
                if on { vm.config.relayBands[r] |=  (1 << bandIndex) }
                else  { vm.config.relayBands[r] &= ~(1 << bandIndex) }
            }
        )
    }

    private func feedBinding(_ r: Int) -> Binding<Int> {
        Binding(get: { r < vm.config.relayFeed.count ? vm.config.relayFeed[r] : 0 },
                set: { if r < vm.config.relayFeed.count { vm.config.relayFeed[r] = $0 } })
    }

    private func groupBinding(_ r: Int) -> Binding<Int> {
        Binding(get: { r < vm.config.relayGroup.count ? vm.config.relayGroup[r] : 0 },
                set: { if r < vm.config.relayGroup.count { vm.config.relayGroup[r] = $0 } })
    }

    /// Compact label for a relay's coverage menu: "All bands", "None", or a count.
    private func coverageLabel(_ r: Int) -> String {
        let mask = r < vm.config.relayBands.count ? vm.config.relayBands[r] : 0
        let n = AntennaCoverage.coveredLabels(mask: mask).count
        if n == 0 { return "Any band" }
        if n == Band.allCases.count { return "All bands" }
        return "\(n) band\(n == 1 ? "" : "s")"
    }

    /// Discovered controllers eligible to be this slave's master — everything on
    /// the LAN except this unit itself (matched by hostname or current address).
    private var masterCandidates: [DiscoveredDevice] {
        let selfHost = vm.config.hostname.lowercased()
        let selfAddr = vm.host.lowercased()
        return discoveredMasters.filter { dev in
            let host = (dev.host ?? "").lowercased()
            let addr = dev.address.lowercased()
            let name = dev.name.lowercased()
            let isSelf = (!host.isEmpty && host == selfHost)
                || (!addr.isEmpty && addr == selfAddr)
                || (!selfHost.isEmpty && name.contains(selfHost))
            return !isSelf && !dev.address.isEmpty
        }
    }

    /// Band indices the current map assigns to an antenna that doesn't cover them.
    private var mismatchedBands: Set<Int> {
        AntennaCoverage.mismatchedBands(bands: vm.config.bands, bands2: vm.config.bands2,
                                        relayBands: vm.config.relayBands)
    }
}
