import Foundation

/// The controller's stored settings. Read from `GET /config`, written via
/// `POST /save` as an `application/x-www-form-urlencoded` body.
///
/// `/config` never returns the Wi-Fi or OTA passwords, so those decode empty and
/// are only sent on save when the operator types a new value (blank = keep).
/// Which transport the controller uses to track the radio's band.
/// Matches the firmware's `RadioType` enum (Config.h).
enum RadioType: Int, Codable, CaseIterable {
    case tci  = 0     // TCI server (ExpertSDR / SunSDR)
    case flex = 1     // FlexRadio SmartSDR (TCP 4992)

    var label: String {
        switch self {
        case .tci:  return "TCI (ExpertSDR / SunSDR)"
        case .flex: return "FlexRadio (SmartSDR TCP)"
        }
    }

    /// Conventional default port for this transport.
    var defaultPort: Int {
        switch self {
        case .tci:  return 50001
        case .flex: return 4992
        }
    }
}

/// SO2R role of the unit. Matches the firmware `CtrlMode` enum (Config.h).
enum CtrlMode: Int, Codable, CaseIterable {
    case standalone = 0   // single radio, 8×1
    case master     = 1   // Mode A Radio 1, the LAN interlock arbiter
    case slave      = 2   // Mode A Radio 2, claims antennas from the master

    var label: String {
        switch self {
        case .standalone: return "Standalone (single radio)"
        case .master:     return "Master (Radio 1, arbiter)"
        case .slave:      return "Slave (Radio 2)"
        }
    }
}

/// Interlock arbitration when both radios want the same antenna.
enum InterlockPolicy: Int, Codable, CaseIterable {
    case firstCome = 0    // current holder keeps it (default)
    case priority  = 1    // master wins

    var label: String { self == .firstCome ? "First-come (holder keeps it)" : "Priority (master wins)" }
}

/// What a slave does when the master is unreachable.
enum PeerLoss: Int, Codable, CaseIterable {
    case safe = 0         // all off
    case hold = 1         // keep last granted antenna

    var label: String { self == .safe ? "Safe (all off)" : "Hold last" }
}

struct DeviceConfig: Codable, Equatable {
    var hostname: String
    var ssid: String
    var radioType: RadioType
    var tciHost: String
    var tciPort: Int
    var region: Int
    var guardMs: Int
    var bands: [Int]              // 11 entries, -1 = none/bypass else relay 0..7
    var mode: CtrlMode            // SO2R role (Mode A)
    var peerHost: String          // slave: master's address
    var interlockPolicy: InterlockPolicy
    var onPeerLoss: PeerLoss

    // Write-only, never present in /config — excluded from CodingKeys below.
    var wifiPassword: String = ""
    var otaPassword: String = ""

    enum CodingKeys: String, CodingKey {
        case hostname, ssid, region, bands, mode
        case radioType = "radio_type"
        case tciHost = "tci_host"
        case tciPort = "tci_port"
        case guardMs = "guard_ms"
        case peerHost = "peer_host"
        case interlockPolicy = "interlock_policy"
        case onPeerLoss = "on_peer_loss"
    }

    // Older firmware omits the newer fields — decode them as sensible defaults
    // (pre-P1 has no radio_type; pre-P2b has no mode/peer/interlock).
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        hostname  = try c.decode(String.self, forKey: .hostname)
        ssid      = try c.decode(String.self, forKey: .ssid)
        radioType = try c.decodeIfPresent(RadioType.self, forKey: .radioType) ?? .tci
        tciHost   = try c.decode(String.self, forKey: .tciHost)
        tciPort   = try c.decode(Int.self, forKey: .tciPort)
        region    = try c.decode(Int.self, forKey: .region)
        guardMs   = try c.decode(Int.self, forKey: .guardMs)
        bands     = try c.decode([Int].self, forKey: .bands)
        mode            = try c.decodeIfPresent(CtrlMode.self, forKey: .mode) ?? .standalone
        peerHost        = try c.decodeIfPresent(String.self, forKey: .peerHost) ?? ""
        interlockPolicy = try c.decodeIfPresent(InterlockPolicy.self, forKey: .interlockPolicy) ?? .firstCome
        onPeerLoss      = try c.decodeIfPresent(PeerLoss.self, forKey: .onPeerLoss) ?? .safe
    }

    init(hostname: String, ssid: String, radioType: RadioType = .tci,
         tciHost: String, tciPort: Int, region: Int, guardMs: Int, bands: [Int],
         mode: CtrlMode = .standalone, peerHost: String = "",
         interlockPolicy: InterlockPolicy = .firstCome, onPeerLoss: PeerLoss = .safe) {
        self.hostname = hostname; self.ssid = ssid; self.radioType = radioType
        self.tciHost = tciHost; self.tciPort = tciPort; self.region = region
        self.guardMs = guardMs; self.bands = bands
        self.mode = mode; self.peerHost = peerHost
        self.interlockPolicy = interlockPolicy; self.onPeerLoss = onPeerLoss
    }

    /// Build the `POST /save` form body matching the firmware's field names
    /// (ssid, pass, host, port, region, hostname, otapass, guard, b0..b10).
    func formBody() -> String {
        var items: [URLQueryItem] = [
            URLQueryItem(name: "ssid",     value: ssid),
            URLQueryItem(name: "rtype",    value: String(radioType.rawValue)),
            URLQueryItem(name: "host",     value: tciHost),
            URLQueryItem(name: "port",     value: String(tciPort)),
            URLQueryItem(name: "region",   value: String(region)),
            URLQueryItem(name: "hostname", value: hostname),
            URLQueryItem(name: "guard",    value: String(guardMs)),
            URLQueryItem(name: "mode",     value: String(mode.rawValue)),
            URLQueryItem(name: "peer",     value: peerHost),
            URLQueryItem(name: "ilk",      value: String(interlockPolicy.rawValue)),
            URLQueryItem(name: "ploss",    value: String(onPeerLoss.rawValue)),
        ]
        // Blank passwords mean "keep existing" — the firmware skips empty ones.
        if !wifiPassword.isEmpty { items.append(URLQueryItem(name: "pass",    value: wifiPassword)) }
        if !otaPassword.isEmpty  { items.append(URLQueryItem(name: "otapass", value: otaPassword)) }
        for (i, relay) in bands.enumerated() {
            items.append(URLQueryItem(name: "b\(i)", value: String(relay)))
        }
        var comps = URLComponents()
        comps.queryItems = items
        // application/x-www-form-urlencoded body.
        return comps.percentEncodedQuery ?? ""
    }

    static var empty: DeviceConfig {
        DeviceConfig(hostname: "", ssid: "", radioType: .tci, tciHost: "", tciPort: 50001,
                     region: 1, guardMs: 50,
                     bands: Array(repeating: -1, count: Band.allCases.count))
    }
}
