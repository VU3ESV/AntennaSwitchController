import Foundation

/// The controller's stored settings. Read from `GET /config`, written via
/// `POST /save` as an `application/x-www-form-urlencoded` body.
///
/// `/config` never returns the Wi-Fi or OTA passwords, so those decode empty and
/// are only sent on save when the operator types a new value (blank = keep).
struct DeviceConfig: Codable, Equatable {
    var hostname: String
    var ssid: String
    var tciHost: String
    var tciPort: Int
    var region: Int
    var guardMs: Int
    var bands: [Int]              // 11 entries, -1 = none/bypass else relay 0..7

    // Write-only, never present in /config — excluded from CodingKeys below.
    var wifiPassword: String = ""
    var otaPassword: String = ""

    enum CodingKeys: String, CodingKey {
        case hostname, ssid, region, bands
        case tciHost = "tci_host"
        case tciPort = "tci_port"
        case guardMs = "guard_ms"
    }

    /// Build the `POST /save` form body matching the firmware's field names
    /// (ssid, pass, host, port, region, hostname, otapass, guard, b0..b10).
    func formBody() -> String {
        var items: [URLQueryItem] = [
            URLQueryItem(name: "ssid",     value: ssid),
            URLQueryItem(name: "host",     value: tciHost),
            URLQueryItem(name: "port",     value: String(tciPort)),
            URLQueryItem(name: "region",   value: String(region)),
            URLQueryItem(name: "hostname", value: hostname),
            URLQueryItem(name: "guard",    value: String(guardMs)),
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
        DeviceConfig(hostname: "", ssid: "", tciHost: "", tciPort: 50001,
                     region: 1, guardMs: 50,
                     bands: Array(repeating: -1, count: Band.allCases.count))
    }
}
