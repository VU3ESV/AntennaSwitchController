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
    case dual       = 3   // Mode B: both radios on one board → external 8×2

    var label: String {
        switch self {
        case .standalone: return "Standalone (single radio)"
        case .master:     return "Master (Mode A — Radio 1)"
        case .slave:      return "Slave (Mode A — Radio 2)"
        case .dual:       return "Dual (Mode B — both radios, 8×2)"
        }
    }
}

/// External antenna switch wiring. Matches the firmware `SwitchType` enum.
enum SwitchType: Int, Codable, CaseIterable {
    case eightByOne = 0   // exclusive 1-of-8
    case eightByTwo = 1   // per-antenna A/B select, 8× SPDT (Mode B)

    var label: String { self == .eightByOne ? "8×1 (single radio)" : "8×2 (per-antenna A/B)" }
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
    var bands2: [Int]             // SO2R per-band fallback relay; -1 = none
    var mode: CtrlMode            // SO2R role (Mode A/B)
    var peerHost: String          // slave: master's address
    var interlockPolicy: InterlockPolicy
    var onPeerLoss: PeerLoss
    var radio2Type: RadioType     // Mode B: radio 2 transport
    var radio2Host: String
    var radio2Port: Int
    var switchType: SwitchType    // external switch wiring (8x1 / 8x2)
    var radioRx: Int              // radio 1 TCI receiver index (0=RX1, 1=RX2)
    var radio2Rx: Int             // radio 2 TCI receiver index (0=RX1, 1=RX2)
    var relayNames: [String]      // kRelayCount entries; "" = default "R<n>"
    var relayBands: [Int]         // per-relay band-coverage bitmask (bit b = Band b); 0 = undeclared
    var relayFeed: [Int]          // per-relay FeedType: 0 = single feedline, 1 = triplexed leg
    var relayGroup: [Int]         // per-relay triplexer group id; 0 = none

    /// Display name for relay index `r` — the operator's name, or "R<n>" if blank.
    func relayLabel(_ r: Int) -> String {
        guard r >= 0, r < relayNames.count, !relayNames[r].isEmpty else { return "R\(r + 1)" }
        return relayNames[r]
    }

    // Write-only, never present in /config — excluded from CodingKeys below.
    var wifiPassword: String = ""
    var otaPassword: String = ""

    enum CodingKeys: String, CodingKey {
        case hostname, ssid, region, bands, bands2, mode
        case radioType = "radio_type"
        case tciHost = "tci_host"
        case tciPort = "tci_port"
        case guardMs = "guard_ms"
        case peerHost = "peer_host"
        case interlockPolicy = "interlock_policy"
        case onPeerLoss = "on_peer_loss"
        case radio2Type = "radio2_type"
        case radio2Host = "radio2_host"
        case radio2Port = "radio2_port"
        case switchType = "switch_type"
        case radioRx = "radio_rx"
        case radio2Rx = "radio2_rx"
        case relayNames = "relay_names"
        case relayBands = "relay_bands"
        case relayFeed = "relay_feed"
        case relayGroup = "relay_group"
    }

    /// Normalize a names array to exactly `kRelayCount` entries (pad/truncate).
    private static func normalizedNames(_ raw: [String]?) -> [String] {
        var names = raw ?? []
        if names.count < kRelayCount { names += Array(repeating: "", count: kRelayCount - names.count) }
        else if names.count > kRelayCount { names = Array(names.prefix(kRelayCount)) }
        return names
    }

    /// Normalize an int array to `count` entries, padding/truncating with `pad`.
    private static func normalizedInts(_ raw: [Int]?, count: Int, pad: Int = -1) -> [Int] {
        var v = raw ?? []
        if v.count < count { v += Array(repeating: pad, count: count - v.count) }
        else if v.count > count { v = Array(v.prefix(count)) }
        return v
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
        // Older firmware (pre-v7) omits the fallback map — default all-none, same length as bands.
        bands2    = DeviceConfig.normalizedInts(try c.decodeIfPresent([Int].self, forKey: .bands2), count: bands.count)
        mode            = try c.decodeIfPresent(CtrlMode.self, forKey: .mode) ?? .standalone
        peerHost        = try c.decodeIfPresent(String.self, forKey: .peerHost) ?? ""
        interlockPolicy = try c.decodeIfPresent(InterlockPolicy.self, forKey: .interlockPolicy) ?? .firstCome
        onPeerLoss      = try c.decodeIfPresent(PeerLoss.self, forKey: .onPeerLoss) ?? .safe
        radio2Type      = try c.decodeIfPresent(RadioType.self, forKey: .radio2Type) ?? .tci
        radio2Host      = try c.decodeIfPresent(String.self, forKey: .radio2Host) ?? ""
        radio2Port      = try c.decodeIfPresent(Int.self, forKey: .radio2Port) ?? 50001
        switchType      = try c.decodeIfPresent(SwitchType.self, forKey: .switchType) ?? .eightByOne
        radioRx         = try c.decodeIfPresent(Int.self, forKey: .radioRx) ?? 0
        radio2Rx        = try c.decodeIfPresent(Int.self, forKey: .radio2Rx) ?? 0
        relayNames      = DeviceConfig.normalizedNames(try c.decodeIfPresent([String].self, forKey: .relayNames))
        // Antenna metadata (pre-v8 firmware omits these → undeclared/single/no group).
        relayBands      = DeviceConfig.normalizedInts(try c.decodeIfPresent([Int].self, forKey: .relayBands), count: kRelayCount)
        relayFeed       = DeviceConfig.normalizedInts(try c.decodeIfPresent([Int].self, forKey: .relayFeed),  count: kRelayCount, pad: 0)
        relayGroup      = DeviceConfig.normalizedInts(try c.decodeIfPresent([Int].self, forKey: .relayGroup), count: kRelayCount, pad: 0)
    }

    init(hostname: String, ssid: String, radioType: RadioType = .tci,
         tciHost: String, tciPort: Int, region: Int, guardMs: Int, bands: [Int],
         bands2: [Int]? = nil,
         mode: CtrlMode = .standalone, peerHost: String = "",
         interlockPolicy: InterlockPolicy = .firstCome, onPeerLoss: PeerLoss = .safe,
         radio2Type: RadioType = .tci, radio2Host: String = "", radio2Port: Int = 50001,
         switchType: SwitchType = .eightByOne, radioRx: Int = 0, radio2Rx: Int = 0,
         relayNames: [String] = [],
         relayBands: [Int]? = nil, relayFeed: [Int]? = nil, relayGroup: [Int]? = nil) {
        self.hostname = hostname; self.ssid = ssid; self.radioType = radioType
        self.tciHost = tciHost; self.tciPort = tciPort; self.region = region
        self.guardMs = guardMs; self.bands = bands
        self.bands2 = DeviceConfig.normalizedInts(bands2, count: bands.count)
        self.mode = mode; self.peerHost = peerHost
        self.interlockPolicy = interlockPolicy; self.onPeerLoss = onPeerLoss
        self.radio2Type = radio2Type; self.radio2Host = radio2Host
        self.radio2Port = radio2Port; self.switchType = switchType
        self.radioRx = radioRx; self.radio2Rx = radio2Rx
        self.relayNames = DeviceConfig.normalizedNames(relayNames)
        self.relayBands = DeviceConfig.normalizedInts(relayBands, count: kRelayCount, pad: 0)
        self.relayFeed  = DeviceConfig.normalizedInts(relayFeed,  count: kRelayCount, pad: 0)
        self.relayGroup = DeviceConfig.normalizedInts(relayGroup, count: kRelayCount, pad: 0)
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
            URLQueryItem(name: "r2type",   value: String(radio2Type.rawValue)),
            URLQueryItem(name: "r2host",   value: radio2Host),
            URLQueryItem(name: "r2port",   value: String(radio2Port)),
            URLQueryItem(name: "swtype",   value: String(switchType.rawValue)),
            URLQueryItem(name: "rrx",      value: String(radioRx)),
            URLQueryItem(name: "r2rx",     value: String(radio2Rx)),
        ]
        // Blank passwords mean "keep existing" — the firmware skips empty ones.
        if !wifiPassword.isEmpty { items.append(URLQueryItem(name: "pass",    value: wifiPassword)) }
        if !otaPassword.isEmpty  { items.append(URLQueryItem(name: "otapass", value: otaPassword)) }
        for (i, relay) in bands.enumerated() {
            items.append(URLQueryItem(name: "b\(i)", value: String(relay)))
        }
        for (i, relay) in bands2.enumerated() {
            items.append(URLQueryItem(name: "s\(i)", value: String(relay)))
        }
        for (i, name) in relayNames.enumerated() {
            items.append(URLQueryItem(name: "rn\(i)", value: name))
        }
        // Antenna metadata. The firmware sums every arg named rb<i>; sending the
        // single combined mask is sufficient (no hidden-zero baseline needed).
        for (i, mask)  in relayBands.enumerated() { items.append(URLQueryItem(name: "rb\(i)", value: String(mask))) }
        for (i, feed)  in relayFeed.enumerated()  { items.append(URLQueryItem(name: "rf\(i)", value: String(feed))) }
        for (i, group) in relayGroup.enumerated() { items.append(URLQueryItem(name: "rg\(i)", value: String(group))) }
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
