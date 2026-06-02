import Foundation

/// A controller the operator has added — by typed IP/hostname or pinned from
/// Bonjour discovery. Persisted (as JSON) in the plugin's isolated defaults.
struct Controller: Identifiable, Codable, Equatable, Hashable {
    var id: UUID
    var name: String        // user-facing label
    var address: String     // IP or mDNS host, e.g. "192.168.1.42" or "ANT-SW-Controller-7A.local"

    init(id: UUID = UUID(), name: String, address: String) {
        self.id = id
        self.name = name
        self.address = address
    }
}
