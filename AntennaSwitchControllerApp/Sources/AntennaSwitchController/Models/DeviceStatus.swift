import Foundation

/// Live operational state from the controller's `GET /status`.
///
/// Firmware shape:
/// `{"ap":1,"wifi":0,"ip":"...","tci":0,"freq":0,"band":"---","tx":0,"tune":0,
///   "override":-2,"active_relay":-1,"switching":0}`
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
    let activeRelay: Int     // -1 = none energized, else 0..7
    let switching: Int

    enum CodingKeys: String, CodingKey {
        case ap, wifi, ip, tci, freq, band, tx, tune, switching
        case overrideMode = "override"
        case activeRelay = "active_relay"
    }

    var apMode: Bool       { ap != 0 }
    var wifiUp: Bool       { wifi != 0 }
    var tciUp: Bool        { tci != 0 }
    var transmitting: Bool { tx != 0 }
    var tuning: Bool       { tune != 0 }
    var isSwitching: Bool  { switching != 0 }
    var isAuto: Bool       { overrideMode == -2 }

    /// Frequency formatted as MHz, or "—" when unknown.
    var freqMHz: String {
        guard freq > 0 else { return "—" }
        return String(format: "%.3f MHz", Double(freq) / 1_000_000.0)
    }
}
