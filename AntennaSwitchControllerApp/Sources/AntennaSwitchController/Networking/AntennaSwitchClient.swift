import Foundation

enum AntennaSwitchClientError: LocalizedError {
    case invalidHost
    case badResponse(Int)
    case transport(String)
    case decoding(String)

    var errorDescription: String? {
        switch self {
        case .invalidHost:        return "The device address is not a valid URL."
        case .badResponse(let c): return "Device returned HTTP \(c)."
        case .transport(let m):   return m
        case .decoding(let m):    return "Could not read device response: \(m)"
        }
    }
}

/// Thin async client over the controller firmware's HTTP API.
///
/// Routes (from ESP8266_ANT_SW/WebPortal.h):
///   GET  /discover                 -> JSON DeviceIdentity
///   GET  /status                   -> JSON DeviceStatus
///   GET  /config                   -> JSON DeviceConfig (no passwords)
///   POST /save        (form body)  -> persist settings
///   POST /relay?set=auto|none|0..7 -> manual override
///   POST /reboot                   -> soft reboot
struct AntennaSwitchClient {
    /// Base host as the user typed it, e.g. "ANT-SW-Controller-7A.local" or "192.168.1.42".
    let host: String
    let timeout: TimeInterval

    init(host: String, timeout: TimeInterval = 4.0) {
        self.host = host
        self.timeout = timeout
    }

    private func baseURL() throws -> URL {
        var raw = host.trimmingCharacters(in: .whitespaces)
        guard !raw.isEmpty else { throw AntennaSwitchClientError.invalidHost }
        if !raw.lowercased().hasPrefix("http://") && !raw.lowercased().hasPrefix("https://") {
            raw = "http://" + raw
        }
        guard let url = URL(string: raw) else { throw AntennaSwitchClientError.invalidHost }
        return url
    }

    /// URL of a device web page for opening in an external browser.
    func pageURL(_ path: String = "") -> URL? {
        guard let base = try? baseURL() else { return nil }
        return path.isEmpty ? base : base.appendingPathComponent(path)
    }

    private func session() -> URLSession {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = timeout
        config.waitsForConnectivity = false
        config.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: config)
    }

    // MARK: - GET helpers

    private func get<T: Decodable>(_ path: String, as type: T.Type) async throws -> T {
        let url = try baseURL().appendingPathComponent(path)
        var request = URLRequest(url: url)
        request.httpMethod = "GET"
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await session().data(for: request)
        } catch {
            throw AntennaSwitchClientError.transport(error.localizedDescription)
        }
        try Self.validate(response)
        do {
            return try JSONDecoder().decode(T.self, from: data)
        } catch {
            throw AntennaSwitchClientError.decoding(error.localizedDescription)
        }
    }

    func fetchDiscover() async throws -> DeviceIdentity { try await get("discover", as: DeviceIdentity.self) }
    func fetchStatus()   async throws -> DeviceStatus   { try await get("status",   as: DeviceStatus.self) }
    func fetchConfig()   async throws -> DeviceConfig   { try await get("config",   as: DeviceConfig.self) }

    // MARK: - Mutations

    func saveConfig(_ config: DeviceConfig) async throws {
        let url = try baseURL().appendingPathComponent("save")
        try await postForm(url: url, body: config.formBody())
    }

    /// Manual override. `set` is "auto", "none", or "0".."7".
    func setRelay(_ set: String) async throws {
        var comps = URLComponents(url: try baseURL().appendingPathComponent("relay"),
                                  resolvingAgainstBaseURL: false)
        comps?.queryItems = [URLQueryItem(name: "set", value: set)]
        guard let url = comps?.url else { throw AntennaSwitchClientError.invalidHost }
        try await postForm(url: url, body: "")
    }

    func reboot() async throws {
        let url = try baseURL().appendingPathComponent("reboot")
        // The device restarts and may drop the connection; tolerate that.
        try? await postForm(url: url, body: "")
    }

    // MARK: - Helpers

    private func postForm(url: URL, body: String) async throws {
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.httpBody = body.data(using: .utf8)
        let response: URLResponse
        do {
            (_, response) = try await session().data(for: request)
        } catch {
            throw AntennaSwitchClientError.transport(error.localizedDescription)
        }
        try Self.validate(response)
    }

    private static func validate(_ response: URLResponse) throws {
        guard let http = response as? HTTPURLResponse else { return }
        guard (200..<400).contains(http.statusCode) else {
            throw AntennaSwitchClientError.badResponse(http.statusCode)
        }
    }
}
