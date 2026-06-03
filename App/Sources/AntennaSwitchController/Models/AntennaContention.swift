import Foundation

/// SO2R contention state for one radio, derived from `/status` + `/config`:
/// the radio wanted its band's **primary** antenna but the other radio holds it,
/// so the controller put it on the per-band **fallback** (or left it with none).
///
/// This is presentation logic — the firmware already made and applied the
/// decision (verified by the resolver/integration tests); the app just compares
/// what the radio *got* against what its band maps to, to label *why*.
enum AntennaContention: Equatable {
    case onFallback   // primary in use by the other radio → on the configured fallback
    case blocked      // primary in use and no fallback free → no antenna

    /// `nil` when the radio is on its primary, its band is unmapped, or the band
    /// label doesn't resolve — i.e. nothing to flag.
    ///
    /// - Parameters:
    ///   - bandLabel: the radio's current band, e.g. "20m" (matches `Band.label`).
    ///   - granted:   the relay the controller actually energized (-1 = none).
    ///   - bands:     primary band→relay map (`config.bands`).
    ///   - fallback:  per-band fallback map (`config.bands2`).
    static func evaluate(bandLabel: String, granted: Int,
                         bands: [Int], fallback: [Int]) -> AntennaContention? {
        guard let band = Band.allCases.first(where: { $0.label == bandLabel }) else { return nil }
        let i = band.rawValue
        guard i < bands.count else { return nil }
        let primary = bands[i]
        guard primary >= 0, granted != primary else { return nil }   // unmapped, or on primary
        let fb = i < fallback.count ? fallback[i] : -1
        if granted >= 0 && granted == fb { return .onFallback }
        if granted < 0 { return .blocked }
        return nil
    }
}
