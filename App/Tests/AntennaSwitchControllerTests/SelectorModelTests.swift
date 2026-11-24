import XCTest
@testable import AntennaSwitchController

/// Radio-selector mode (firmware MODE_SELECTOR, config v10): the app's models
/// must round-trip the firmware's `/config`, `/status` and `/save` shapes.
final class SelectorModelTests: XCTestCase {

    /// `/config` as firmware v10 sends it (trimmed to the fields the app reads).
    private func configJSON(mode: Int, roles: String?) -> Data {
        let rolesField = roles.map { #","relay_role":\#($0)"# } ?? ""
        return Data(#"""
        {"hostname":"ANT-SW-Controller-2F","ssid":"Shack","tci_host":"192.168.86.29","tci_port":60001,
         "radio_type":0,"cat_baud":9600,"civ_addr":148,"mode":\#(mode),"peer_host":"","interlock_policy":0,
         "on_peer_loss":0,"radio2_type":0,"radio2_host":"","radio2_port":50001,"switch_type":0,
         "radio_rx":0,"radio2_rx":0,"region":1,"guard_ms":50,
         "relay_names":["Radio 1","IN1 · Radio 1","IN1 · Radio 2","","","Radio 2","",""],
         "bands":[0,1,1,1,1,2,2,2,2,2,2],"bands2":[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],
         "relay_bands":[0,0,0,0,0,0,0,0],"relay_feed":[0,0,0,0,0,0,0,0],"relay_group":[0,0,0,0,0,0,0,0]\#(rolesField)}
        """#.utf8)
    }

    func testDecodesSelectorModeAndRoles() throws {
        let c = try JSONDecoder().decode(DeviceConfig.self,
                                         from: configJSON(mode: 4, roles: "[1,0,0,0,0,1,0,0]"))
        XCTAssertEqual(c.mode, .selector)
        XCTAssertFalse(c.mode.isSO2R)
        XCTAssertEqual(c.relayRoles, [1, 0, 0, 0, 0, 1, 0, 0])
        XCTAssertEqual(c.oneByTwoRelays, [0, 5])
        XCTAssertEqual(c.sixByOneRelays, [1, 2, 3, 4, 6, 7])
    }

    func testPreV10FirmwareGetsDefaultRoles() throws {
        let c = try JSONDecoder().decode(DeviceConfig.self, from: configJSON(mode: 0, roles: nil))
        XCTAssertEqual(c.mode, .standalone)
        XCTAssertEqual(c.relayRoles, RelayRole.defaults)       // matches the firmware default
    }

    func testFormBodySendsRoles() throws {
        var c = try JSONDecoder().decode(DeviceConfig.self,
                                         from: configJSON(mode: 4, roles: "[1,0,0,0,0,1,0,0]"))
        c.relayRoles = Array(repeating: RelayRole.oneByTwo.rawValue, count: kRelayCount)  // board 3
        let items = URLComponents(string: "?" + c.formBody())!.queryItems!
        let value = { (name: String) in items.first { $0.name == name }?.value }
        XCTAssertEqual(value("mode"), "4")
        for r in 0..<kRelayCount { XCTAssertEqual(value("rr\(r)"), "1", "rr\(r)") }
    }

    func testDecodesSelectorStatus() throws {
        let json = Data(#"""
        {"ap":0,"wifi":1,"ip":"192.168.86.62","tci":0,"freq":0,"band":"---","tx":0,"tune":0,
         "override":-2,"active_relay":2,"radio1_relay":2,"radio2_relay":-1,"switching":0,
         "relay_mask":37,"selector":{"port":2,"toggles":33,"toggle_mask":33},
         "interlock":{"role":"selector","peer_up":0,"beats_missed":0,"master_ant":-1,"slave_ant":-1}}
        """#.utf8)
        let s = try JSONDecoder().decode(DeviceStatus.self, from: json)
        XCTAssertTrue(s.isSelector)
        XCTAssertEqual(s.selector?.port, 2)
        XCTAssertEqual(s.selector?.toggleMask, 33)
        XCTAssertEqual(s.selector?.isOnPort2(0), true)
        XCTAssertEqual(s.selector?.isOnPort2(5), true)
        XCTAssertEqual(s.selector?.isOnPort2(2), false)
        XCTAssertEqual(s.interlock?.hasPeer, false)              // no "Peer down" badge
    }

    func testOtherModesAreNotSelector() throws {
        let json = Data(#"""
        {"ap":0,"wifi":1,"ip":"192.168.86.62","tci":1,"freq":3667100,"band":"80m","tx":0,"tune":0,
         "override":-2,"active_relay":1,"radio1_relay":1,"radio2_relay":-1,"switching":0,
         "interlock":{"role":"standalone","peer_up":0,"beats_missed":0,"master_ant":-1,"slave_ant":-1}}
        """#.utf8)
        let s = try JSONDecoder().decode(DeviceStatus.self, from: json)
        XCTAssertFalse(s.isSelector)
        XCTAssertNil(s.selector)
    }
}
