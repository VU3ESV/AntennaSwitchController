import Foundation
import CryptoKit

/// The ArduinoOTA ("espota") wire protocol for the ESP8266 core 3.x, as implemented
/// by the device (`libraries/ArduinoOTA/ArduinoOTA.cpp`) and the reference host
/// client (`tools/espota.py`). Pure functions only — the sockets live in
/// `EspOTAUploader`.
///
///  1. Host → device UDP :8266   `"<cmd> <hostTcpPort> <size> <md5>\n"`  (cmd 0 = flash)
///  2. Device → host UDP         `"OK"`, `"AUTH <nonce>"` (password set) or `"ERR: …"`
///  3. (auth) Host → device      `"200 <cnonce> <md5(md5(pw):nonce:cnonce)>\n"`
///     Device → host             `"OK"` or `"Authentication Failed"`
///  4. Device connects back over TCP to the host's port; the host streams the image
///     and the device acks each write with its byte count in ASCII decimal.
///  5. Device verifies the MD5, replies `"OK"` (then reboots) or an Updater
///     `"ERROR[n]: …"` line.
enum EspOTA {
    static let defaultPort: UInt16 = 8266
    static let flashCommand = 0      // U_FLASH
    static let authCommand = 200     // U_AUTH
    /// Same chunk size as espota.py: one Ethernet-MTU TCP segment.
    static let chunkSize = 1460

    // MARK: - Hashing

    static func md5Hex(_ data: Data) -> String {
        Insecure.MD5.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    static func md5Hex(_ string: String) -> String { md5Hex(Data(string.utf8)) }

    /// A fresh 32-hex client nonce (espota derives one from file metadata; any
    /// 32-character value is accepted by the device).
    static func makeClientNonce() -> String {
        md5Hex(Data((0..<16).map { _ in UInt8.random(in: .min ... .max) }))
    }

    // MARK: - Messages

    static func invitation(localPort: UInt16, size: Int, md5: String,
                           command: Int = flashCommand) -> String {
        "\(command) \(localPort) \(size) \(md5)\n"
    }

    /// The device stores `md5(password)` and checks
    /// `md5("<md5(password)>:<nonce>:<cnonce>")`.
    static func authResponse(password: String, nonce: String, cnonce: String) -> String {
        md5Hex("\(md5Hex(password)):\(nonce):\(cnonce)")
    }

    static func authMessage(cnonce: String, response: String) -> String {
        "\(authCommand) \(cnonce) \(response)\n"
    }

    // MARK: - Parsing

    /// A UDP reply from the device to the invitation or to the auth message.
    enum Reply: Equatable {
        case ok
        case auth(nonce: String)
        case authFailed
        case error(String)          // "ERR: <Updater error>", e.g. not enough space
        case unknown(String)
    }

    static func parseReply(_ text: String) -> Reply {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if t == "OK" { return .ok }
        if t.hasPrefix("AUTH") {
            let nonce = t.dropFirst(4).trimmingCharacters(in: .whitespaces)
            return nonce.count == 32 ? .auth(nonce: nonce) : .unknown(t)
        }
        if t.hasPrefix("Authentication Failed") { return .authFailed }
        if t.hasPrefix("ERR") {
            var msg = t.dropFirst(3)
            if msg.hasPrefix(":") { msg = msg.dropFirst() }
            return .error(msg.trimmingCharacters(in: .whitespaces))
        }
        return .unknown(t)
    }

    /// Outcome of the device's TCP stream so far. While uploading, the device only
    /// sends decimal byte counts; at the end it sends `"OK"` or an Updater error
    /// (`Update.printError` → `"ERROR[n]: …"`). Pass the whole accumulated text.
    enum Outcome: Equatable {
        case pending
        case ok
        case error(String)
    }

    static func parseOutcome(_ text: String) -> Outcome {
        if let r = text.range(of: "ERROR") {
            return .error(text[r.lowerBound...].trimmingCharacters(in: .whitespacesAndNewlines))
        }
        if text.contains("OK") { return .ok }
        return .pending
    }
}

// MARK: - Firmware image

enum FirmwareImageError: LocalizedError, Equatable {
    case empty
    case notESP8266Image
    case notAntennaSwitchFirmware

    var errorDescription: String? {
        switch self {
        case .empty:
            return "The firmware file is empty."
        case .notESP8266Image:
            return "This isn’t an ESP8266 firmware image (.bin). Use the AntennaSwitchController-firmware.bin from a release, or Controller.ino.bin from an arduino-cli build."
        case .notAntennaSwitchFirmware:
            return "This is ESP8266 firmware, but not Antenna Switch Controller firmware — flashing it would replace the controller with a different program."
        }
    }
}

/// A validated Antenna Switch Controller firmware image, ready to upload.
struct FirmwareImage: Equatable {
    let data: Data
    let name: String          // file or release-asset name, for display
    let md5: String
    /// `FW_VERSION` baked into the image, when it can be read (see `identityMarker`).
    let version: String?

    var size: Int { data.count }

    /// First byte of every (uncompressed) ESP8266 application image.
    static let espImageMagic: UInt8 = 0xE9

    /// The firmware's `/discover` handler concatenates this literal with
    /// `FW_VERSION` at compile time (WebPortal.h), so it appears verbatim in every
    /// image: it identifies the product and carries the version.
    static let identityMarker = #"{"device":"AntennaSwitchController","version":""#

    init(data: Data, name: String) throws {
        guard !data.isEmpty else { throw FirmwareImageError.empty }
        guard data.first == Self.espImageMagic else { throw FirmwareImageError.notESP8266Image }
        let marker = Data(Self.identityMarker.utf8)
        guard let hit = data.range(of: marker) else { throw FirmwareImageError.notAntennaSwitchFirmware }

        // Version runs from the end of the marker to the next quote (bounded).
        let tail = data[hit.upperBound...].prefix(32)
        if let quote = tail.firstIndex(of: UInt8(ascii: "\"")),
           let v = String(data: data[hit.upperBound..<quote], encoding: .utf8), !v.isEmpty {
            version = v
        } else {
            version = nil
        }
        self.data = data
        self.name = name
        self.md5 = EspOTA.md5Hex(data)
    }
}

// MARK: - Versions

enum FirmwareVersion {
    /// Numeric dotted-version comparison (`"v0.1.33"` == `"0.1.33"`); non-numeric
    /// parts compare as 0. Returns nil when either side is missing.
    static func compare(_ a: String?, _ b: String?) -> ComparisonResult? {
        guard let a, let b else { return nil }
        let pa = parts(a), pb = parts(b)
        for i in 0..<max(pa.count, pb.count) {
            let x = i < pa.count ? pa[i] : 0, y = i < pb.count ? pb[i] : 0
            if x != y { return x < y ? .orderedAscending : .orderedDescending }
        }
        return .orderedSame
    }

    static func normalized(_ v: String) -> String {
        v.hasPrefix("v") || v.hasPrefix("V") ? String(v.dropFirst()) : v
    }

    private static func parts(_ v: String) -> [Int] {
        normalized(v).split(separator: ".").map { Int($0) ?? 0 }
    }
}
