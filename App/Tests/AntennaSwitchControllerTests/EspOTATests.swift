import XCTest
@testable import AntennaSwitchController

/// Wire-format + validation tests for the ArduinoOTA ("espota") client. Expected
/// values come from the reference client (`tools/espota.py`, Python hashlib).
final class EspOTATests: XCTestCase {

    // MARK: - Messages

    func testMD5Hex() {
        XCTAssertEqual(EspOTA.md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e")
        XCTAssertEqual(EspOTA.md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72")
    }

    func testInvitationMatchesEspota() {
        // espota.py: '%d %d %d %s\n' % (command, localPort, content_size, file_md5)
        XCTAssertEqual(EspOTA.invitation(localPort: 54321, size: 498_380, md5: "34e3c114920cf45e3bfa650ced697a01"),
                       "0 54321 498380 34e3c114920cf45e3bfa650ced697a01\n")
    }

    func testAuthResponseMatchesEspota() {
        // espota.py: md5('%s:%s:%s' % (md5(password), nonce, cnonce))
        let nonce = "0123456789abcdef0123456789abcdef", cnonce = "fedcba9876543210fedcba9876543210"
        XCTAssertEqual(EspOTA.md5Hex("s3cret"), "33e1b232a4e6fa0028a6670753749a17")
        XCTAssertEqual(EspOTA.authResponse(password: "s3cret", nonce: nonce, cnonce: cnonce),
                       "58a42a589be7c06415c28c4c3cd6422e")
        XCTAssertEqual(EspOTA.authMessage(cnonce: cnonce, response: "58a42a589be7c06415c28c4c3cd6422e"),
                       "200 \(cnonce) 58a42a589be7c06415c28c4c3cd6422e\n")
    }

    func testClientNonceIs32Hex() {
        let n = EspOTA.makeClientNonce()
        XCTAssertEqual(n.count, 32)
        XCTAssertTrue(n.allSatisfy(\.isHexDigit))
        XCTAssertNotEqual(n, EspOTA.makeClientNonce())
    }

    // MARK: - Parsing

    func testParseReply() {
        XCTAssertEqual(EspOTA.parseReply("OK"), .ok)
        XCTAssertEqual(EspOTA.parseReply("AUTH 0123456789abcdef0123456789abcdef"),
                       .auth(nonce: "0123456789abcdef0123456789abcdef"))
        XCTAssertEqual(EspOTA.parseReply("AUTH short"), .unknown("AUTH short"))
        XCTAssertEqual(EspOTA.parseReply("Authentication Failed"), .authFailed)
        XCTAssertEqual(EspOTA.parseReply("ERR: Not Enough Space"), .error("Not Enough Space"))
        XCTAssertEqual(EspOTA.parseReply("huh"), .unknown("huh"))
    }

    func testParseOutcome() {
        XCTAssertEqual(EspOTA.parseOutcome("14601460292"), .pending)       // byte-count acks only
        XCTAssertEqual(EspOTA.parseOutcome("1460146014OK"), .ok)
        XCTAssertEqual(EspOTA.parseOutcome("1460ERROR[4]: MD5 Failed: expected:a, calculated:b\n"),
                       .error("ERROR[4]: MD5 Failed: expected:a, calculated:b"))
    }

    // MARK: - Firmware image

    func testValidImageReadsVersion() throws {
        let image = try FirmwareImage(data: Self.fakeFirmware(version: "0.1.33"), name: "fw.bin")
        XCTAssertEqual(image.version, "0.1.33")
        XCTAssertEqual(image.md5, EspOTA.md5Hex(image.data))
    }

    func testRejectsEmptyAndForeignImages() {
        XCTAssertThrowsError(try FirmwareImage(data: Data(), name: "x")) {
            XCTAssertEqual($0 as? FirmwareImageError, .empty)
        }
        var notESP = Self.fakeFirmware(version: "1.0"); notESP[0] = 0x7F  // e.g. an ELF
        XCTAssertThrowsError(try FirmwareImage(data: notESP, name: "x")) {
            XCTAssertEqual($0 as? FirmwareImageError, .notESP8266Image)
        }
        let otherProduct = Data([0xE9, 0x02, 0x03, 0x40]) + Data(#"{"device":"SO2R-BPF","version":"0.6.0"}"#.utf8)
        XCTAssertThrowsError(try FirmwareImage(data: otherProduct, name: "x")) {
            XCTAssertEqual($0 as? FirmwareImageError, .notAntennaSwitchFirmware)
        }
    }

    func testImageFromSliceUsesItsOwnBytes() throws {
        let full = Data([1, 2, 3]) + Self.fakeFirmware(version: "2.0")
        let image = try FirmwareImage(data: full.dropFirst(3), name: "slice.bin")
        XCTAssertEqual(image.version, "2.0")
        XCTAssertEqual(image.md5, EspOTA.md5Hex(Self.fakeFirmware(version: "2.0")))
    }

    // MARK: - Versions

    func testVersionCompare() {
        XCTAssertEqual(FirmwareVersion.compare("0.1.33", "0.1.26"), .orderedDescending)
        XCTAssertEqual(FirmwareVersion.compare("v0.1.26", "0.1.26"), .orderedSame)
        XCTAssertEqual(FirmwareVersion.compare("0.1.9", "0.1.10"), .orderedAscending)
        XCTAssertEqual(FirmwareVersion.compare("1.0", "1.0.0"), .orderedSame)
        XCTAssertNil(FirmwareVersion.compare(nil, "1.0"))
    }

    // MARK: - GitHub release

    func testPicksFirmwareAssetFromRelease() throws {
        let json = #"""
        {"tag_name":"v0.1.33","html_url":"https://github.com/VU3ESV/AntennaSwitchController/releases/tag/v0.1.33",
         "assets":[
          {"name":"Antenna-Switch-Controller-macOS.dmg","size":1,"browser_download_url":"https://example.invalid/a.dmg"},
          {"name":"AntennaSwitchController-firmware.bin","size":498380,"browser_download_url":"https://example.invalid/fw.bin"}]}
        """#
        let release = try JSONDecoder().decode(FirmwareReleaseClient.GitHubRelease.self, from: Data(json.utf8))
        let fw = try FirmwareReleaseClient.firmware(in: release)
        XCTAssertEqual(fw.tag, "v0.1.33")
        XCTAssertEqual(fw.version, "0.1.33")
        XCTAssertEqual(fw.size, 498_380)
        XCTAssertEqual(fw.downloadURL.absoluteString, "https://example.invalid/fw.bin")
    }

    func testReleaseWithoutFirmwareAssetThrows() throws {
        let json = #"{"tag_name":"v9.9.9","assets":[]}"#
        let release = try JSONDecoder().decode(FirmwareReleaseClient.GitHubRelease.self, from: Data(json.utf8))
        XCTAssertThrowsError(try FirmwareReleaseClient.firmware(in: release))
    }

    // MARK: - Fixtures

    /// A minimal image that passes validation: ESP magic byte, filler, and the
    /// `/discover` literal the real firmware embeds (WebPortal.h).
    static func fakeFirmware(version: String, size: Int = 10_000) -> Data {
        var d = Data([0xE9, 0x02, 0x03, 0x40])
        d += Data((0..<(size / 2)).map { UInt8(truncatingIfNeeded: $0 &* 31) })
        d += Data((FirmwareImage.identityMarker + version + #"","hostname":""#).utf8)
        while d.count < size { d.append(UInt8(truncatingIfNeeded: d.count)) }
        return d
    }
}
