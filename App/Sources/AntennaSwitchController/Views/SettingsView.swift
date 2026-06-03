import SwiftUI
import RadioPluginUI

/// Full configuration form — mirrors every option on the controller's web page:
/// WiFi, TCI server, band→relay map, hostname, OTA password, guard delay.
struct SettingsView: View {
    @ObservedObject var vm: ControllerViewModel

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
                TextField("Host / IP", text: $vm.config.tciHost)
                TextField("Port", value: $vm.config.tciPort, format: .number.grouping(.never))
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

            Section("Relay Names") {
                ForEach(0..<kRelayCount, id: \.self) { r in
                    HStack {
                        Text("R\(r + 1)")
                            .font(.callout.monospacedDigit())
                            .frame(width: 32, alignment: .leading)
                            .foregroundStyle(.secondary)
                        TextField("R\(r + 1) (GPIO\(kRelayGPIO[r]))", text: relayNameBinding(r))
                    }
                }
                Text("Name each relay’s antenna (e.g. “80m Dipole”). Blank uses “R<n>”. Shown on the dashboard, controls, and band map.")
                    .font(.caption).foregroundStyle(.secondary)
            }

            Section("Band → Relay Map") {
                ForEach(Band.allCases) { band in
                    Picker(band.label, selection: relayBinding(for: band.rawValue)) {
                        Text("None / bypass").tag(-1)
                        ForEach(0..<kRelayCount, id: \.self) { r in
                            Text("\(vm.config.relayLabel(r)) (GPIO\(kRelayGPIO[r]))").tag(r)
                        }
                    }
                }
                Text("Multiple bands may share one relay. Relays on GPIO0/15/16 may twitch at power-up.")
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
                    TextField("Master address (IP)", text: $vm.config.peerHost)
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

    /// Two-way binding into the `bands` array element for one band index.
    private func relayBinding(for index: Int) -> Binding<Int> {
        Binding(
            get: { index < vm.config.bands.count ? vm.config.bands[index] : -1 },
            set: { if index < vm.config.bands.count { vm.config.bands[index] = $0 } }
        )
    }

    /// Two-way binding into the `relayNames` array element for one relay index.
    private func relayNameBinding(_ r: Int) -> Binding<String> {
        Binding(
            get: { r < vm.config.relayNames.count ? vm.config.relayNames[r] : "" },
            set: { if r < vm.config.relayNames.count { vm.config.relayNames[r] = $0 } }
        )
    }
}
