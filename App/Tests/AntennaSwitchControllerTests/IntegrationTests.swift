import XCTest
@testable import AntennaSwitchController

/// End-to-end integration tests that drive **live** controllers over HTTP using
/// the app's own `AntennaSwitchClient` (to configure boards, "use the app to
/// configure") plus the firmware's `/test/inject` API (to put a board into any
/// band/TX scenario without tuning real radios). The board must run a
/// `-DANTSW_TEST` build (the `/test/*` routes are absent otherwise).
///
/// These are **skipped automatically** unless the board addresses are supplied,
/// so `swift test` is a no-op on a machine with no hardware / in CI:
///
///   ANTSW_TEST_MASTER=192.168.86.52 ANTSW_TEST_SLAVE=192.168.86.49 swift test
///
/// The pure resolver logic is covered exhaustively and hardware-free by the C++
/// host tests in `Controller/test/`; this suite proves the firmware *wiring*
/// (config save/read → resolver → relay → status) on real boards.
final class IntegrationTests: XCTestCase {

    // Band indices (must match the firmware Band enum / BandPlan.h).
    private enum B { static let m160 = 0, m80 = 1, m40 = 3, m20 = 5 }

    private var master: AntennaSwitchClient?
    private var slave: AntennaSwitchClient?
    private var dual: AntennaSwitchClient?
    private var savedMaster: DeviceConfig?
    private var savedSlave: DeviceConfig?
    private var savedDual: DeviceConfig?

    override func setUp() {
        // Each test guards on the boards it needs; nothing is global so the
        // Mode A and Mode B tests skip independently.
        let env = ProcessInfo.processInfo.environment
        master = env["ANTSW_TEST_MASTER"].map { AntennaSwitchClient(host: $0, timeout: 5) }
        slave  = env["ANTSW_TEST_SLAVE"].map  { AntennaSwitchClient(host: $0, timeout: 5) }
        dual   = env["ANTSW_TEST_DUAL"].map   { AntennaSwitchClient(host: $0, timeout: 5) }
    }

    override func tearDown() async throws {
        // Always return the boards to live radios and restore their saved config.
        try? await master?.clearTestRig()
        try? await slave?.clearTestRig()
        try? await dual?.clearTestRig()
        if let cfg = savedMaster { try? await master?.saveConfig(cfg) }
        if let cfg = savedSlave  { try? await slave?.saveConfig(cfg) }
        if let cfg = savedDual   { try? await dual?.saveConfig(cfg) }
    }

    private func requireMasterSlave() throws -> (AntennaSwitchClient, AntennaSwitchClient) {
        guard let m = master, let s = slave else {
            throw XCTSkip("Set ANTSW_TEST_MASTER + ANTSW_TEST_SLAVE (board IPs, -DANTSW_TEST build).")
        }
        return (m, s)
    }

    /// Mode A: a band's primary antenna held by the master forces the slave onto
    /// its per-band fallback; when the master leaves, the slave reclaims primary.
    func testModeASlaveFallbackAndRecovery() async throws {
        let (master, slave) = try requireMasterSlave()
        // Snapshot for restore, then program a deterministic 20 m collision:
        // master 20 m → relay 2; slave 20 m → relay 2 primary, relay 5 fallback.
        savedMaster = try await master.fetchConfig()
        savedSlave  = try await slave.fetchConfig()
        XCTAssertEqual(savedMaster?.mode, .master, "ANTSW_TEST_MASTER must be a master board")
        XCTAssertEqual(savedSlave?.mode,  .slave,  "ANTSW_TEST_SLAVE must be a slave board")

        var m = savedMaster!; m.bands[B.m20] = 2
        var s = savedSlave!;  s.bands[B.m20] = 2; s.bands2[B.m20] = 5
        try await master.saveConfig(m)
        try await slave.saveConfig(s)

        // Park the slave on 160 m (relay 0), put the master on 20 m (relay 2).
        // The slave's claim lands a heartbeat beat after the master settles, so
        // wait for each side to reach its antenna (don't assert instantly).
        try await slave.injectTestRig(radio: 0, band: B.m160, tx: false)
        try await master.injectTestRig(radio: 0, band: B.m20, tx: false)
        try await waitUntil("master holds relay 2") { try await master.fetchStatus().interlock?.masterAnt == 2 }
        try await waitUntil("slave parks on its 160 m antenna (relay 0)") { try await slave.fetchStatus().radio1Relay == 0 }

        // Slave moves onto 20 m too → primary (relay 2) is taken → fallback relay 5.
        try await slave.injectTestRig(radio: 0, band: B.m20, tx: false)
        try await waitUntil("slave falls back to relay 5") { try await slave.fetchStatus().radio1Relay == 5 }
        let mStatus = try await master.fetchStatus()
        XCTAssertEqual(mStatus.interlock?.masterAnt, 2, "master keeps its primary")
        XCTAssertEqual(mStatus.interlock?.slaveAnt, 5, "master sees the slave on the fallback")

        // Master leaves 20 m (→ 80 m, relay 1); the freed primary is reclaimed.
        try await master.injectTestRig(radio: 0, band: B.m80, tx: false)
        try await waitUntil("slave reclaims primary relay 2") { try await slave.fetchStatus().radio1Relay == 2 }
    }

    /// Sanity: the fallback map round-trips through `/save` → `/config`.
    func testFallbackMapPersists() async throws {
        let (_, slave) = try requireMasterSlave()
        savedSlave = try await slave.fetchConfig()
        var s = savedSlave!
        s.bands[B.m40] = 3; s.bands2[B.m40] = 6
        try await slave.saveConfig(s)
        let read = try await slave.fetchConfig()
        XCTAssertEqual(read.bands[B.m40], 3)
        XCTAssertEqual(read.bands2[B.m40], 6)
    }

    /// Mode B (Dual): one board tracks both radios. A 20 m collision puts Radio 2
    /// on the band's fallback while Radio 1 keeps the primary; a TX-keyed radio is
    /// never moved. Temporarily reconfigures the board to dual and restores it.
    ///
    /// Needs a single board (`ANTSW_TEST_DUAL`) on a `-DANTSW_TEST` build.
    func testModeBDualFallback() async throws {
        guard let dual = dual else {
            throw XCTSkip("Set ANTSW_TEST_DUAL (one board IP, -DANTSW_TEST build) to run.")
        }
        savedDual = try await dual.fetchConfig()

        // Put the board in Dual mode with a shared map: 20 m → relay 2 (primary),
        // fallback relay 5; 80 m → relay 1. Both radios use this one map.
        var cfg = savedDual!
        cfg.mode = .dual
        cfg.switchType = .eightByTwo
        cfg.bands[B.m20] = 2; cfg.bands2[B.m20] = 5
        cfg.bands[B.m80] = 1
        try await dual.saveConfig(cfg)

        // Both radios on 20 m → collision. Radio 1 keeps relay 2, Radio 2 → relay 5.
        try await dual.injectTestRig(radio: 0, band: B.m20, tx: false)
        try await dual.injectTestRig(radio: 1, band: B.m20, tx: false)
        try await waitUntil("R1=relay2, R2=fallback relay5") {
            let s = try await dual.fetchStatus()
            return s.radio1Relay == 2 && s.radio2Relay == 5
        }

        // Radio 2 moves to 80 m (relay 1) → no collision, both on their primaries.
        try await dual.injectTestRig(radio: 1, band: B.m80, tx: false)
        try await waitUntil("R1=relay2, R2=relay1 (no collision)") {
            let s = try await dual.fetchStatus()
            return s.radio1Relay == 2 && s.radio2Relay == 1
        }

        // TX-safety: Radio 2 keys on 80 m, then Radio 1 also tunes to 80 m (relay 1,
        // a collision). The transmitting Radio 2 must keep relay 1; Radio 1 yields.
        try await dual.injectTestRig(radio: 1, band: B.m80, tx: true)
        try await dual.injectTestRig(radio: 0, band: B.m80, tx: false)
        try await waitUntil("TX R2 keeps relay 1; R1 yields") {
            let s = try await dual.fetchStatus()
            return s.radio2Relay == 1 && s.radio1Relay != 1
        }
    }

    // Poll `condition` (which reads live status) until true or ~12 s elapse.
    private func waitUntil(_ what: String,
                           timeout: TimeInterval = 12,
                           _ condition: @escaping () async throws -> Bool) async throws {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if try await condition() { return }
            try await Task.sleep(nanoseconds: 500_000_000)   // 0.5 s
        }
        XCTFail("timed out waiting for: \(what)")
    }
}
