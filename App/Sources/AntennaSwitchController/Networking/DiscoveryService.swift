import Foundation

/// One controller found on the LAN via the firmware's `_antsw._tcp` mDNS
/// service. Identity comes from the Bonjour instance name, resolved host/IP,
/// and the TXT records the firmware publishes (version, product, host).
struct DiscoveredDevice: Identifiable, Equatable {
    let name: String          // Bonjour instance name
    var hostname: String?     // resolved mDNS host, e.g. "ANT-SW-Controller-7A.local"
    var ip: String?
    var port: Int
    var txt: [String: String]

    var id: String { name }

    /// Best address to talk to — prefer the mDNS hostname (stable across DHCP
    /// leases), fall back to the resolved IP.
    var address: String { hostname ?? ip ?? "" }

    var version: String? { txt["version"] }
    var product: String? { txt["product"] }
    var host: String?    { txt["host"] }

    /// Friendly title for the list row.
    var title: String { host ?? name }
}

/// Browses Bonjour for `_antsw._tcp` controllers and publishes resolved devices.
///
/// `NetServiceBrowser` delivers delegate callbacks on the run loop that started
/// the search. `start()` is always called from the main thread (SwiftUI
/// `onAppear` / button actions), so callbacks — and the `@Published` mutations
/// they make — stay on the main thread.
final class DiscoveryService: NSObject, ObservableObject {
    @Published private(set) var devices: [DiscoveredDevice] = []
    @Published private(set) var isBrowsing = false

    private let browser = NetServiceBrowser()
    private var resolving: [NetService] = []   // strong refs while resolving

    static let serviceType = "_antsw._tcp."

    override init() {
        super.init()
        browser.delegate = self
    }

    func start() {
        guard !isBrowsing else { return }
        devices = []
        resolving = []
        isBrowsing = true
        browser.searchForServices(ofType: Self.serviceType, inDomain: "local.")
    }

    func stop() {
        guard isBrowsing else { return }
        browser.stop()
        resolving.removeAll()
        isBrowsing = false
    }

    func restart() {
        stop()
        start()
    }

    private func makeDevice(from service: NetService) -> DiscoveredDevice {
        var ip: String?
        if let addresses = service.addresses {
            for data in addresses {
                if let s = Self.ipString(from: data), !s.contains("%"), s.contains(".") {
                    ip = s
                    break
                }
            }
            if ip == nil { ip = addresses.compactMap { Self.ipString(from: $0) }.first }
        }
        var host = service.hostName
        if host?.hasSuffix(".") == true { host = String(host!.dropLast()) }

        var txt: [String: String] = [:]
        if let data = service.txtRecordData() {
            for (k, v) in NetService.dictionary(fromTXTRecord: data) {
                txt[k] = String(data: v, encoding: .utf8) ?? ""
            }
        }
        return DiscoveredDevice(name: service.name, hostname: host, ip: ip,
                                port: service.port, txt: txt)
    }

    private func upsert(_ device: DiscoveredDevice) {
        if let i = devices.firstIndex(where: { $0.name == device.name }) {
            devices[i] = device
        } else {
            devices.append(device)
        }
        devices.sort { $0.title.localizedCaseInsensitiveCompare($1.title) == .orderedAscending }
    }

    private static func ipString(from data: Data) -> String? {
        data.withUnsafeBytes { raw -> String? in
            guard let sa = raw.baseAddress?.assumingMemoryBound(to: sockaddr.self) else { return nil }
            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            let result = getnameinfo(sa, socklen_t(data.count),
                                     &host, socklen_t(host.count),
                                     nil, 0, NI_NUMERICHOST)
            return result == 0 ? String(cString: host) : nil
        }
    }
}

extension DiscoveryService: NetServiceBrowserDelegate, NetServiceDelegate {
    func netServiceBrowser(_ browser: NetServiceBrowser, didFind service: NetService, moreComing: Bool) {
        service.delegate = self
        resolving.append(service)
        service.resolve(withTimeout: 5)
    }

    func netServiceBrowser(_ browser: NetServiceBrowser, didRemove service: NetService, moreComing: Bool) {
        devices.removeAll { $0.name == service.name }
        resolving.removeAll { $0.name == service.name }
    }

    func netServiceDidResolveAddress(_ service: NetService) {
        upsert(makeDevice(from: service))
        resolving.removeAll { $0 == service }
    }

    func netService(_ service: NetService, didNotResolve errorDict: [String: NSNumber]) {
        resolving.removeAll { $0 == service }
    }
}
