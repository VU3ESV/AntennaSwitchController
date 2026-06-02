import Foundation

/// The controller's static `/discover` record — used to confirm a typed or
/// discovered address really is an Antenna Switch Controller and show identity.
struct DeviceIdentity: Codable, Equatable {
    let device: String
    let version: String?
    let hostname: String?
    let mdns: String?
    let ip: String?
    let relays: Int?

    var isAntennaSwitch: Bool { device == "AntennaSwitchController" }
}
