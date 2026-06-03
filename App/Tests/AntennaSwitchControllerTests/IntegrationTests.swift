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

    private var master: AntennaSwitchClient!
    private var slave: AntennaSwitchClient!
    private var savedMaster: DeviceConfig?
    private var savedSlave: DeviceConfig?

    override func setUpWithError() throws {
        let env = ProcessInfo.processInfo.environment
        guard let m = env["ANTSW_TEST_MASTER"], let s = env["ANTSW_TEST_SLAVE"] else {
            throw XCTSkip("Set ANTSW_TEST_MASTER and ANTSW_TEST_SLAVE (board IPs, -DANTSW_TEST build) to run.")
        }
        master = AntennaSwitchClient(host: m, timeout: 5)
        slave  = AntennaSwitchClient(host: s, timeout: 5)
    }

    override func tearDown() async throws {
        // Always return the boards to live radios and restore their saved config.
        try? await master?.clearTestRig()
        try? await slave?.clearTestRig()
        if let cfg = savedMaster { try? await master?.saveConfig(cfg) }
        if let cfg = savedSlave  { try? await slave?.saveConfig(cfg) }
    }

    /// Mode A: a band's primary antenna held by the master forces the slave onto
    /// its per-band fallback; when the master leaves, the slave reclaims primary.
    func testModeASlaveFallbackAndRecovery() async throws {
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
        try await waitUntil("master holds relay 2") { try await self.master.fetchStatus().interlock?.masterAnt == 2 }
        try await waitUntil("slave parks on its 160 m antenna (relay 0)") { try await self.slave.fetchStatus().radio1Relay == 0 }

        // Slave moves onto 20 m too → primary (relay 2) is taken → fallback relay 5.
        try await slave.injectTestRig(radio: 0, band: B.m20, tx: false)
        try await waitUntil("slave falls back to relay 5") { try await self.slave.fetchStatus().radio1Relay == 5 }
        let mStatus = try await master.fetchStatus()
        XCTAssertEqual(mStatus.interlock?.masterAnt, 2, "master keeps its primary")
        XCTAssertEqual(mStatus.interlock?.slaveAnt, 5, "master sees the slave on the fallback")

        // Master leaves 20 m (→ 80 m, relay 1); the freed primary is reclaimed.
        try await master.injectTestRig(radio: 0, band: B.m80, tx: false)
        try await waitUntil("slave reclaims primary relay 2") { try await self.slave.fetchStatus().radio1Relay == 2 }
    }

    /// Sanity: the v7 fallback map round-trips through `/save` → `/config`.
    func testFallbackMapPersists() async throws {
        savedSlave = try await slave.fetchConfig()
        var s = savedSlave!
        s.bands[B.m40] = 3; s.bands2[B.m40] = 6
        try await slave.saveConfig(s)
        let read = try await slave.fetchConfig()
        XCTAssertEqual(read.bands[B.m40], 3)
        XCTAssertEqual(read.bands2[B.m40], 6)
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
