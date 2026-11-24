import SwiftUI
import RadioPluginUI

/// Controls for radio-selector mode (MODE_SELECTOR): the board's 6×1
/// amplifier-input switch (at most one port on) and its 1×2 radio switches
/// (port 1 / port 2). Manual only — the firmware tracks no radio here.
///
/// Roles come from the firmware's live `/status` (`toggle_mask`), not the
/// Settings form, so an unsaved role edit can never send a command for a role
/// the board doesn't have yet.
struct SelectorControlsView: View {
    @ObservedObject var vm: ControllerViewModel
    @Environment(\.radioTheme) private var theme

    private let columns = [GridItem(.adaptive(minimum: 110), spacing: 10)]

    private var sel: SelectorStatus? { vm.status?.selector }

    /// Bit i = relay i is a 1×2. Live from the board; falls back to the config.
    private var toggleMask: Int {
        if let m = sel?.toggleMask { return m }
        return vm.config.oneByTwoRelays.reduce(0) { $0 | (1 << $1) }
    }
    private var sixByOne: [Int] { (0..<kRelayCount).filter { (toggleMask >> $0) & 1 == 0 } }
    private var oneByTwo: [Int] { (0..<kRelayCount).filter { (toggleMask >> $0) & 1 == 1 } }

    private var allOff: Bool { (sel?.port ?? -1) < 0 && (sel?.toggles ?? 0) == 0 }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("Radio Selector").font(.headline).foregroundStyle(theme.textPrimary)
                Text("Manual only — this mode has **no TX sensing**, so don’t switch while a radio is transmitting. The 6×1 connects at most one port (break-before-make).")
                    .font(.caption).foregroundStyle(theme.textSecondary)

                sixByOneSection
                oneByTwoSection

                HStack {
                    Button(role: .destructive) { Task { await vm.selectorAllOff() } } label: {
                        Label("All Off", systemImage: "poweroff")
                    }
                    .buttonStyle(.bordered).tint(theme.danger)
                    .disabled(allOff)
                    Text("6×1 open, every 1×2 on port 1 — also the state after power-up.")
                        .font(.caption).foregroundStyle(theme.textSecondary)
                }

                if let err = vm.errorMessage {
                    Text(err).font(.caption).foregroundStyle(theme.danger)
                }
            }
            .padding()
            .frame(maxWidth: .infinity, alignment: .topLeading)
        }
        .background(theme.background)
    }

    // MARK: - 6×1

    private var sixByOneSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("6×1 — amplifier input").font(.subheadline.weight(.semibold))
                .foregroundStyle(theme.textPrimary)
            if sixByOne.isEmpty {
                Text("No relay is a 6×1 port on this board (Settings → Antennas → Role).")
                    .font(.caption).foregroundStyle(theme.textSecondary)
            } else {
                LazyVGrid(columns: columns, spacing: 10) {
                    ForEach(sixByOne, id: \.self) { r in portButton(r) }
                }
                Button { Task { await vm.selectPort(nil) } } label: {
                    Label("Open 6×1", systemImage: "circle.slash")
                }
                .buttonStyle(.bordered)
                .disabled((sel?.port ?? -1) < 0)
            }
        }
    }

    /// A 6×1 port. The connected one is filled red with a bolt, drawn by our own
    /// style: system `.tint` / `.borderedProminent` go grey when the window isn't
    /// key (e.g. the suite in the background), and which radio is on the
    /// amplifier must be unmistakable regardless.
    private func portButton(_ r: Int) -> some View {
        let on = sel?.port == r
        return Button {
            Task { await vm.selectPort(on ? nil : r) }   // tap the connected port again to open
        } label: {
            HStack(spacing: 6) {
                if on { Image(systemName: "bolt.fill") }
                relayLabel(r, onFill: on)
            }
            .frame(maxWidth: .infinity).padding(.vertical, 10)
        }
        .buttonStyle(PortButtonStyle(on: on, theme: theme))
    }

    // MARK: - 1×2

    private var oneByTwoSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("1×2 — radio switches").font(.subheadline.weight(.semibold))
                .foregroundStyle(theme.textPrimary)
            if oneByTwo.isEmpty {
                Text("No relay is a 1×2 switch on this board (Settings → Antennas → Role).")
                    .font(.caption).foregroundStyle(theme.textSecondary)
            } else {
                ForEach(oneByTwo, id: \.self) { r in
                    HStack {
                        relayLabel(r)
                        Spacer()
                        Picker("", selection: portBinding(r)) {
                            Text("Port 1").tag(false)
                            Text("Port 2").tag(true)
                        }
                        .pickerStyle(.segmented).labelsHidden().fixedSize()
                    }
                    .padding(.vertical, 2)
                }
            }
        }
    }

    private func portBinding(_ r: Int) -> Binding<Bool> {
        Binding(
            get: { sel?.isOnPort2(r) ?? false },
            set: { port2 in Task { await vm.setSelectorToggle(r, port2: port2) } }
        )
    }

    private func relayLabel(_ r: Int, onFill: Bool = false) -> some View {
        let named = r < vm.config.relayNames.count && !vm.config.relayNames[r].isEmpty
        return VStack(alignment: .leading, spacing: 2) {
            Text(vm.config.relayLabel(r)).font(.headline).lineLimit(1).minimumScaleFactor(0.6)
            Text(named ? "R\(r + 1) · GPIO\(kRelayGPIO[r])" : "GPIO\(kRelayGPIO[r])")
                .font(.caption2)
                .foregroundStyle(onFill ? Color.white.opacity(0.85) : theme.textSecondary)
        }
    }
}

/// Fill + border drawn explicitly, so the connected 6×1 port looks the same in
/// an active or inactive window.
private struct PortButtonStyle: ButtonStyle {
    let on: Bool
    let theme: RadioTheme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(on ? Color.white : theme.textPrimary)
            .background(RoundedRectangle(cornerRadius: 8).fill(on ? theme.danger : theme.surface))
            .overlay(RoundedRectangle(cornerRadius: 8)
                .stroke(on ? theme.danger : theme.textSecondary.opacity(0.35), lineWidth: 1))
            .contentShape(RoundedRectangle(cornerRadius: 8))
            .opacity(configuration.isPressed ? 0.7 : 1)
    }
}
