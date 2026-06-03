import XCTest
@testable import AntennaSwitchController

/// Hardware-free unit tests for the dashboard's SO2R contention indicator.
final class AntennaContentionTests: XCTestCase {
    // 20 m (index 5) → primary relay 2, fallback relay 5; everything else unmapped.
    private let bands:    [Int] = [-1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1]
    private let fallback: [Int] = [-1, -1, -1, -1, -1, 5, -1, -1, -1, -1, -1]

    func testOnPrimaryIsNotFlagged() {
        XCTAssertNil(AntennaContention.evaluate(bandLabel: "20m", granted: 2,
                                                bands: bands, fallback: fallback))
    }

    func testOnFallbackIsFlagged() {
        XCTAssertEqual(AntennaContention.evaluate(bandLabel: "20m", granted: 5,
                                                  bands: bands, fallback: fallback), .onFallback)
    }

    func testBlockedWhenNoAntenna() {
        XCTAssertEqual(AntennaContention.evaluate(bandLabel: "20m", granted: -1,
                                                  bands: bands, fallback: fallback), .blocked)
    }

    func testUnmappedBandIsNotFlagged() {
        // 40 m is unmapped (primary -1) → nothing to contend for.
        XCTAssertNil(AntennaContention.evaluate(bandLabel: "40m", granted: -1,
                                                bands: bands, fallback: fallback))
    }

    func testGrantedSomeOtherRelayWithoutFallback() {
        // On a relay that's neither primary nor the configured fallback: not the
        // fallback case, and not "none" → no specific contention label.
        XCTAssertNil(AntennaContention.evaluate(bandLabel: "20m", granted: 3,
                                                bands: bands, fallback: [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1]))
    }

    func testUnknownBandLabelIsSafe() {
        XCTAssertNil(AntennaContention.evaluate(bandLabel: "---", granted: -1,
                                                bands: bands, fallback: fallback))
    }
}
