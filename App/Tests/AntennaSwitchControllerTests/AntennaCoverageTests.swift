import XCTest
@testable import AntennaSwitchController

/// Hardware-free unit tests for the multiband-antenna coverage validation.
final class AntennaCoverageTests: XCTestCase {
    // Band indices: 20m = 5, 17m = 6, 15m = 7, 40m = 3, 80m = 1.
    private func mask(_ bands: [Int]) -> Int { bands.reduce(0) { $0 | (1 << $1) } }

    func testUndeclaredCoverageNeverMismatches() {
        // relay 2 coverage = 0 (undeclared) → opted out, no warning.
        XCTAssertFalse(AntennaCoverage.isMismatch(relay: 2, bandIndex: 5,
                                                  relayBands: [0, 0, 0, 0, 0, 0, 0, 0]))
    }

    func testCoveredBandIsNotFlagged() {
        var rb = Array(repeating: 0, count: 8)
        rb[2] = mask([5, 6, 7])                       // HexBeam on relay 2 covers 20/17/15
        XCTAssertFalse(AntennaCoverage.isMismatch(relay: 2, bandIndex: 5, relayBands: rb))
    }

    func testUncoveredBandIsFlagged() {
        var rb = Array(repeating: 0, count: 8)
        rb[2] = mask([5, 6, 7])                       // covers 20/17/15, not 40
        XCTAssertTrue(AntennaCoverage.isMismatch(relay: 2, bandIndex: 3, relayBands: rb))
    }

    func testNoneRelayIsSafe() {
        XCTAssertFalse(AntennaCoverage.isMismatch(relay: -1, bandIndex: 5, relayBands: [mask([0])]))
    }

    func testMismatchedBandsAcrossPrimaryAndFallback() {
        // 20m(5)→relay2 (covers 20 ✓); 40m(3)→relay2 (✗); fallback 80m(1)→relay3 (✗).
        var rb = Array(repeating: 0, count: 8)
        rb[2] = mask([5])          // relay 2 covers only 20m
        rb[3] = mask([3])          // relay 3 covers only 40m
        var bands  = Array(repeating: -1, count: 11)
        var bands2 = Array(repeating: -1, count: 11)
        bands[5] = 2; bands[3] = 2                     // 20m ok on r2, 40m NOT on r2
        bands2[1] = 3                                   // 80m fallback on r3 (covers 40m only)
        let bad = AntennaCoverage.mismatchedBands(bands: bands, bands2: bands2, relayBands: rb)
        XCTAssertEqual(bad, [3, 1])
    }

    func testTriplexerSameBandOnTwoLegsConflicts() {
        // relays 5 & 6 both in group 1, both covering 20m → conflict.
        var rb = Array(repeating: 0, count: 8)
        rb[5] = mask([5]); rb[6] = mask([5])
        let grp = [0, 0, 0, 0, 0, 1, 1, 0]
        XCTAssertEqual(AntennaCoverage.conflictingGroups(relayGroup: grp, relayBands: rb), [1])
    }

    func testTriplexerDistinctBandsNoConflict() {
        // Same group, different bands (20 / 15) → legitimate triplexer, no conflict.
        var rb = Array(repeating: 0, count: 8)
        rb[5] = mask([5]); rb[6] = mask([7])
        let grp = [0, 0, 0, 0, 0, 1, 1, 0]
        XCTAssertTrue(AntennaCoverage.conflictingGroups(relayGroup: grp, relayBands: rb).isEmpty)
    }
}
