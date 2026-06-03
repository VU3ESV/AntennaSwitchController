import Foundation

/// Multiband-antenna validation (docs/MULTI-RADIO-SO2R-PLAN.md §11). Each relay
/// (antenna) may declare which bands it covers as a bitmask (`relay_bands`); the
/// app uses it to warn when a band is assigned to an antenna that can't work it,
/// and to flag a triplexer group asked to put one band on two legs at once.
///
/// Pure value logic — no SwiftUI — so it's unit-tested without hardware.
enum AntennaCoverage {
    /// Is band `bandIndex` assigned to `relay` whose declared coverage excludes
    /// it? `false` when the relay is none/out-of-range or coverage is undeclared
    /// (mask 0 → the operator opted out of the check).
    static func isMismatch(relay: Int, bandIndex: Int, relayBands: [Int]) -> Bool {
        guard relay >= 0, relay < relayBands.count else { return false }
        let mask = relayBands[relay]
        guard mask != 0 else { return false }
        return (mask & (1 << bandIndex)) == 0
    }

    /// Band labels a coverage `mask` includes, e.g. "20m, 17m, 15m" (for display).
    static func coveredLabels(mask: Int) -> [String] {
        Band.allCases.filter { (mask & (1 << $0.rawValue)) != 0 }.map(\.label)
    }

    /// Band indices where the primary or fallback antenna's declared coverage
    /// excludes that band — the set the UI flags in the map.
    static func mismatchedBands(bands: [Int], bands2: [Int], relayBands: [Int]) -> Set<Int> {
        var out = Set<Int>()
        for b in 0..<bands.count {
            if isMismatch(relay: bands[b], bandIndex: b, relayBands: relayBands) { out.insert(b) }
            if b < bands2.count,
               isMismatch(relay: bands2[b], bandIndex: b, relayBands: relayBands) { out.insert(b) }
        }
        return out
    }

    /// Triplexer sanity: relays sharing a non-zero group must not both cover the
    /// same band (one band can't be on two legs of one antenna at once). Returns
    /// the offending group ids.
    static func conflictingGroups(relayGroup: [Int], relayBands: [Int]) -> Set<Int> {
        var out = Set<Int>()
        let groups = Set(relayGroup.filter { $0 != 0 })
        for g in groups {
            var seen = 0
            for r in 0..<relayGroup.count where relayGroup[r] == g {
                let mask = r < relayBands.count ? relayBands[r] : 0
                if seen & mask != 0 { out.insert(g); break }
                seen |= mask
            }
        }
        return out
    }
}
