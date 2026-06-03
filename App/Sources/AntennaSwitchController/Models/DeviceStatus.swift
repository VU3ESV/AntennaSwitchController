import Foundation

/// Live operational state from the controller's `GET /status`.
///
/// Firmware shape:
/// `{"ap":1,"wifi":0,"ip":"...","tci":0,"freq":0,"band":"---","tx":0,"tune":0,
///   "override":-2,"active_relay":-1,"switching":0}`
/// SO2R interlock state, nested in `/status` as `"interlock"`.
/// `{"role":"master","peer_up":1,"master_ant":1,"slave_ant":2}`
struct InterlockStatus: Codable, Equatable {
    let role: String         // "standalone" | "master" | "slave" | "dual"
    let peerUp: Int
    let beatsMissed: Int?    // consecutive missed heartbeats (Mode A; nil = older fw)
    let masterAnt: Int       // antenna held by the master radio, -1 = none
    let slaveAnt: Int        // antenna held by the slave radio, -1 = none

    enum CodingKeys: String, CodingKey {
        case role
        case peerUp = "peer_up"
        case beatsMissed = "beats_missed"
        case masterAnt = "master_ant"
        case slaveAnt = "slave_ant"
    }

    var isStandalone: Bool { role == "standalone" }
    var isDual: Bool       { role == "dual" }
    var peerIsUp: Bool     { peerUp != 0 }
}

/// Radio 2 state in Mode B, nested in `/status` as `"radio2"`.
struct Radio2Status: Codable, Equatable {
    let tci: Int
    let freq: Int
    let band: String
    let tx: Int

    var tciUp: Bool        { tci != 0 }
    var transmitting: Bool { tx != 0 }
    var freqMHz: String {
        guard freq > 0 else { return "—" }
        return String(format: "%.3f MHz", Double(freq) / 1_000_000.0)
    }
}

struct DeviceStatus: Codable, Equatable {
    let ap: Int
    let wifi: Int
    let ip: String
    let tci: Int
    let freq: Int
    let band: String
    let tx: Int
    let tune: Int
    let overrideMode: Int    // -2 = auto/TCI, -1 = forced none, 0..7 = forced relay
    let activeRelay: Int     // -1 = none energized, else 0..7 (legacy; = radio 2 in dual)
    var radio1Relay: Int? = nil   // explicit per-radio energized relay (nil = older fw)
    var radio2Relay: Int? = nil   // dual only; -1 = none, nil = not dual / older fw
    let switching: Int
    let interlock: InterlockStatus?   // absent on pre-P2b firmware
    let radio2: Radio2Status?         // present only in Mode B (dual)

    enum CodingKeys: String, CodingKey {
        case ap, wifi, ip, tci, freq, band, tx, tune, switching, interlock, radio2
        case overrideMode = "override"
        case activeRelay = "active_relay"
        case radio1Relay = "radio1_relay"
        case radio2Relay = "radio2_relay"
    }

    var apMode: Bool       { ap != 0 }
    var wifiUp: Bool       { wifi != 0 }
    var tciUp: Bool        { tci != 0 }
    var transmitting: Bool { tx != 0 }
    var tuning: Bool       { tune != 0 }
    var isSwitching: Bool  { switching != 0 }
    var isAuto: Bool       { overrideMode == -2 }
    var isDual: Bool       { interlock?.isDual == true }

    /// Radio 1's energized relay index (-1 = none). Prefers the explicit
    /// `radio1_relay`, falling back for older firmware (interlock in dual, else
    /// `active_relay`).
    var radio1RelayIndex: Int {
        radio1Relay ?? (isDual ? (interlock?.masterAnt ?? -1) : activeRelay)
    }

    /// Radio 2's energized relay index in dual mode (-1 = none); nil when not dual.
    var radio2RelayIndex: Int? {
        if let r = radio2Relay { return r >= 0 ? r : -1 }
        return isDual ? (interlock?.slaveAnt ?? -1) : nil
    }

    /// All currently energized relay indices — one normally, up to two in dual.
    var energizedRelays: [Int] {
        var out: [Int] = []
        let r1 = radio1RelayIndex; if r1 >= 0 { out.append(r1) }
        if let r2 = radio2RelayIndex, r2 >= 0 { out.append(r2) }
        return out
    }

    /// Frequency formatted as MHz, or "—" when unknown.
    var freqMHz: String {
        guard freq > 0 else { return "—" }
        return String(format: "%.3f MHz", Double(freq) / 1_000_000.0)
    }
}
