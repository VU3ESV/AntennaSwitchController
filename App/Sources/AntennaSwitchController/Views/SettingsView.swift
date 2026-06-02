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
                    Picker("IARU Region", selection: $vm.config.region) {
                        Text("1").tag(1); Text("2").tag(2); Text("3").tag(3)
                    }
                    .pickerStyle(.segmented)
                }
            }

            Section("Band → Relay Map") {
                ForEach(Band.allCases) { band in
                    Picker(band.label, selection: relayBinding(for: band.rawValue)) {
                        Text("None / bypass").tag(-1)
                        ForEach(0..<kRelayCount, id: \.self) { r in
                            Text("Relay \(r + 1) (GPIO\(kRelayGPIO[r]))").tag(r)
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

            Section {
                HStack {
                    Button { Task { await vm.save() } } label: {
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
}
