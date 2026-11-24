import XCTest
import Network
@testable import AntennaSwitchController

/// End-to-end tests of `EspOTAUploader` against `FakeOTADevice`, a loopback
/// stand-in that follows the ESP8266 core's `ArduinoOTA.cpp` (no hardware needed).
@MainActor
final class EspOTAUploaderTests: XCTestCase {
    private let fast = EspOTAUploader.Timeouts(reply: 1, connect: 1, ack: 2, result: 3)
    private let imageSize = 10_000          // ~7 chunks, last one partial

    func testUploadWithoutPassword() async throws {
        let device = FakeOTADevice()
        let phases = try await upload(to: device)
        XCTAssertEqual(device.received, try image().data)
        XCTAssertEqual(phases.first, .inviting)
        XCTAssertFalse(phases.contains(.authenticating))
        XCTAssertTrue(phases.contains(.waitingForDevice))
        XCTAssertTrue(phases.contains(.uploading(sent: imageSize, total: imageSize)))
        XCTAssertEqual(phases.last, .verifying)
    }

    func testUploadWithPassword() async throws {
        let device = FakeOTADevice(password: "s3cret")
        let phases = try await upload(to: device, password: "s3cret")
        XCTAssertTrue(phases.contains(.authenticating))
        XCTAssertEqual(device.received, try image().data)
    }

    func testWrongPassword() async throws {
        let device = FakeOTADevice(password: "s3cret")
        await assertUpload(to: device, password: "wrong", failsWith: .authFailed)
        XCTAssertTrue(device.received.isEmpty)
    }

    func testPasswordRequired() async throws {
        await assertUpload(to: FakeOTADevice(password: "s3cret"), failsWith: .passwordRequired)
    }

    func testDeviceRefusesToBegin() async throws {
        await assertUpload(to: FakeOTADevice(behavior: .rejectBegin("Not Enough Space")),
                           failsWith: .deviceRejected("Not Enough Space"))
    }

    func testNoAnswer() async throws {
        await assertUpload(to: FakeOTADevice(behavior: .silent), failsWith: .noAnswer)
    }

    func testDeviceNeverConnectsBack() async throws {
        await assertUpload(to: FakeOTADevice(behavior: .neverConnect), failsWith: .deviceDidNotConnect)
    }

    func testDeviceRejectsImage() async throws {
        let device = FakeOTADevice(behavior: .failVerify)
        do {
            _ = try await upload(to: device)
            XCTFail("expected the device to reject the image")
        } catch OTAUploadError.deviceError(let message) {
            XCTAssertTrue(message.hasPrefix("ERROR[4]: MD5 Failed"), message)
        }
    }

    func testUploadsFullSizeImage() async throws {
        // Real firmware is ~490 KB: ~340 chunk/ack round trips.
        let big = try FirmwareImage(data: EspOTATests.fakeFirmware(version: "9.9.9", size: 500_000), name: "big.bin")
        let device = FakeOTADevice()
        _ = try await upload(to: device, image: big)
        XCTAssertEqual(device.received, big.data)
    }

    /// Flashes a REAL controller. Skipped unless ANTSW_OTA_HOST (board IP) and
    /// ANTSW_OTA_BIN (path to a firmware .bin) are set; ANTSW_OTA_PASSWORD is the
    /// board's OTA password if it has one. The board reboots into the new image.
    func testLiveUploadToBoard() async throws {
        let env = ProcessInfo.processInfo.environment
        guard let host = env["ANTSW_OTA_HOST"], let path = env["ANTSW_OTA_BIN"] else {
            throw XCTSkip("Set ANTSW_OTA_HOST + ANTSW_OTA_BIN to flash a real board.")
        }
        let image = try FirmwareImage(data: Data(contentsOf: URL(fileURLWithPath: path)),
                                      name: (path as NSString).lastPathComponent)
        let log = PhaseLog()
        let started = Date()
        let uploader = EspOTAUploader(host: host, password: env["ANTSW_OTA_PASSWORD"] ?? "", image: image)
        try await uploader.run { log.phases.append($0) }
        print("live OTA: \(image.size) bytes (firmware \(image.version ?? "?")) to \(host) in "
              + String(format: "%.1f s", Date().timeIntervalSince(started)))
        XCTAssertEqual(log.phases.last, .verifying)
    }

    // MARK: - Helpers

    private func image() throws -> FirmwareImage {
        try FirmwareImage(data: EspOTATests.fakeFirmware(version: "9.9.9", size: imageSize), name: "test.bin")
    }

    private func upload(to device: FakeOTADevice, password: String = "",
                        image custom: FirmwareImage? = nil) async throws -> [EspOTAUploader.Phase] {
        let port = try await device.start()
        addTeardownBlock { device.stop() }
        let log = PhaseLog()
        let uploader = EspOTAUploader(host: "127.0.0.1", port: port, password: password,
                                      image: try custom ?? image(), timeouts: fast)
        try await uploader.run { log.phases.append($0) }
        return log.phases
    }

    private func assertUpload(to device: FakeOTADevice, password: String = "",
                              failsWith expected: OTAUploadError,
                              file: StaticString = #filePath, line: UInt = #line) async {
        do {
            _ = try await upload(to: device, password: password)
            XCTFail("expected \(expected)", file: file, line: line)
        } catch {
            XCTAssertEqual(error as? OTAUploadError, expected, file: file, line: line)
        }
    }
}

@MainActor
private final class PhaseLog {
    var phases: [EspOTAUploader.Phase] = []
}

/// The device half of ArduinoOTA on 127.0.0.1: answers the UDP invitation
/// (optionally with the MD5 challenge), connects back over TCP, acks every read
/// with its byte count, then checks the MD5 and replies "OK" or an Updater error.
final class FakeOTADevice: @unchecked Sendable {
    enum Behavior: Equatable {
        case accept
        case rejectBegin(String)   // "ERR: …" instead of "OK" (Update.begin failed)
        case silent                // never answers the invitation
        case neverConnect          // says "OK" but never connects back
        case failVerify            // takes the image, then reports an MD5 failure
    }

    let password: String?
    let behavior: Behavior
    private let nonce = "0123456789abcdef0123456789abcdef"
    private let queue = DispatchQueue(label: "fake-ota-device")
    private let lock = NSLock()
    private var listener: NWListener?
    private var connections: [NWConnection] = []
    private var buffer = Data()

    var received: Data { lock.lock(); defer { lock.unlock() }; return buffer }

    init(password: String? = nil, behavior: Behavior = .accept) {
        self.password = password
        self.behavior = behavior
    }

    func start() async throws -> UInt16 {
        let params = NWParameters.udp
        params.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: .any)
        let listener = try NWListener(using: params)
        self.listener = listener
        listener.newConnectionHandler = { [weak self] c in self?.serve(c) }
        let once = Once()
        return try await withCheckedThrowingContinuation { cont in
            listener.stateUpdateHandler = { state in
                switch state {
                case .ready: if once.claim() { cont.resume(returning: listener.port!.rawValue) }
                case .failed(let e): if once.claim() { cont.resume(throwing: e) }
                default: break
                }
            }
            listener.start(queue: queue)
        }
    }

    func stop() {
        listener?.cancel()
        lock.lock(); let all = connections; connections = []; lock.unlock()
        all.forEach { $0.cancel() }
    }

    private func track(_ c: NWConnection) {
        lock.lock(); connections.append(c); lock.unlock()
    }

    private func serve(_ udp: NWConnection) {
        track(udp)
        udp.start(queue: queue)
        udp.receiveMessage { [weak self] data, _, _, _ in
            guard let self, let data, let text = String(data: data, encoding: .utf8) else { return }
            // "<cmd> <port> <size> <md5>\n"
            let f = text.trimmingCharacters(in: .whitespacesAndNewlines).split(separator: " ")
            guard f.count == 4, f[0] == "0", let port = UInt16(f[1]), let size = Int(f[2]) else { return }
            let md5 = String(f[3])
            switch self.behavior {
            case .silent: return
            case .rejectBegin(let m): self.reply(udp, "ERR: \(m)"); return
            default: break
            }
            if self.password == nil {
                self.accept(udp, port: port, size: size, md5: md5)
            } else {
                self.reply(udp, "AUTH \(self.nonce)")
                self.checkAuth(udp, port: port, size: size, md5: md5)
            }
        }
    }

    private func checkAuth(_ udp: NWConnection, port: UInt16, size: Int, md5: String) {
        udp.receiveMessage { [weak self] data, _, _, _ in
            guard let self, let password = self.password, let data,
                  let text = String(data: data, encoding: .utf8) else { return }
            // "200 <cnonce> <response>\n"; device keeps md5(password) and checks
            // md5("<md5(password)>:<nonce>:<cnonce>") — ArduinoOTA.cpp.
            let f = text.trimmingCharacters(in: .whitespacesAndNewlines).split(separator: " ")
            let challenge = f.count == 3 ? "\(EspOTA.md5Hex(password)):\(self.nonce):\(f[1])" : ""
            if f.count == 3, f[0] == "200", EspOTA.md5Hex(challenge) == f[2] {
                self.accept(udp, port: port, size: size, md5: md5)
            } else {
                self.reply(udp, "Authentication Failed")
            }
        }
    }

    private func accept(_ udp: NWConnection, port: UInt16, size: Int, md5: String) {
        reply(udp, "OK")
        guard behavior != .neverConnect, case .hostPort(let host, _) = udp.endpoint else { return }
        queue.asyncAfter(deadline: .now() + 0.1) {            // ArduinoOTA: delay(100)
            let tcp = NWConnection(host: host, port: NWEndpoint.Port(rawValue: port)!, using: .tcp)
            self.track(tcp)
            tcp.start(queue: self.queue)
            self.read(tcp, size: size, md5: md5)
        }
    }

    private func read(_ tcp: NWConnection, size: Int, md5: String) {
        tcp.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            if let data, !data.isEmpty {
                self.lock.lock(); self.buffer.append(data); let total = self.buffer.count; self.lock.unlock()
                tcp.send(content: Data(String(data.count).utf8), completion: .idempotent)  // client.print(written)
                if total >= size { self.verdict(tcp, md5: md5); return }
            }
            if isComplete || error != nil { return }
            self.read(tcp, size: size, md5: md5)
        }
    }

    private func verdict(_ tcp: NWConnection, md5: String) {
        let actual = EspOTA.md5Hex(received)
        let ok = behavior != .failVerify && actual == md5
        let text = ok ? "OK" : "ERROR[4]: MD5 Failed: expected:\(md5), calculated:\(actual)\n"
        queue.asyncAfter(deadline: .now() + 0.05) {
            tcp.send(content: Data(text.utf8), completion: .contentProcessed { _ in
                self.queue.asyncAfter(deadline: .now() + 0.2) { tcp.cancel() }
            })
        }
    }

    private func reply(_ c: NWConnection, _ text: String) {
        c.send(content: Data(text.utf8), completion: .idempotent)
    }
}

private final class Once: @unchecked Sendable {
    private let lock = NSLock()
    private var done = false
    func claim() -> Bool { lock.lock(); defer { lock.unlock() }; if done { return false }; done = true; return true }
}
