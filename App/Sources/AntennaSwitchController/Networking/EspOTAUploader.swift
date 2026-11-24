import Foundation
import Network

enum OTAUploadError: LocalizedError, Equatable {
    case network(String)
    case listenerFailed(String)
    case noAnswer
    case passwordRequired
    case authFailed
    case deviceRejected(String)
    case unexpectedReply(String)
    case deviceDidNotConnect
    case uploadFailed(String)
    case deviceError(String)
    case noResult

    var errorDescription: String? {
        switch self {
        case .network(let m):
            return "Network error: \(m)"
        case .listenerFailed(let m):
            return "Couldn’t open a port for the controller to connect back to: \(m)"
        case .noAnswer:
            return "The controller didn’t answer the update request (UDP port 8266). Check that it’s online on your WiFi — OTA isn’t available while it’s in setup (AP) mode."
        case .passwordRequired:
            return "This controller has an OTA password. Enter it and try again."
        case .authFailed:
            return "Wrong OTA password."
        case .deviceRejected(let m):
            return "The controller refused the update: \(m)"
        case .unexpectedReply(let m):
            return "Unexpected reply from the controller: “\(m)”"
        case .deviceDidNotConnect:
            return "The controller accepted the update but couldn’t connect back to this Mac. The macOS firewall may be blocking incoming connections for this app (System Settings → Network → Firewall)."
        case .uploadFailed(let m):
            return "Upload interrupted: \(m)"
        case .deviceError(let m):
            return "The controller rejected the image: \(m)"
        case .noResult:
            return "The image was sent, but the controller never confirmed it was written."
        }
    }
}

/// Uploads a firmware image to an ESP8266 running ArduinoOTA, speaking the same
/// protocol as `espota.py` (see `EspOTA`). One instance per upload.
///
/// The device connects *back* to the host, so this opens a TCP listener on an
/// ephemeral port — a sandboxed host needs `com.apple.security.network.server`.
/// Every wait has a deadline; cancelling the calling task tears the sockets down.
final class EspOTAUploader: @unchecked Sendable {
    enum Phase: Equatable {
        case inviting
        case authenticating
        /// The device accepted: it has entered OTA (relays forced safe, radio link
        /// dropped) and is about to connect back.
        case waitingForDevice
        case uploading(sent: Int, total: Int)
        /// Whole image sent; the device is checking the MD5 and finishing the write.
        case verifying
    }

    struct Timeouts {
        var reply: TimeInterval = 10      // espota: invitation / auth answer
        var connect: TimeInterval = 10    // espota: device connect-back
        var ack: TimeInterval = 10        // espota: per-chunk ack
        var result: TimeInterval = 60     // espota: final OK / error
    }

    let host: String
    let port: UInt16
    let password: String
    let image: FirmwareImage
    let timeouts: Timeouts

    private let queue = DispatchQueue(label: "antsw.ota-upload")
    private let lock = NSLock()
    private var listener: NWListener?
    private var udp: NWConnection?
    private var tcp: NWConnection?
    private var pendingInbound: NWConnection?
    private var inboundWaiter: ((Result<NWConnection, Error>) -> Void)?
    private var inboundTaken = false

    init(host: String, port: UInt16 = EspOTA.defaultPort, password: String,
         image: FirmwareImage, timeouts: Timeouts = Timeouts()) {
        self.host = host
        self.port = port
        self.password = password
        self.image = image
        self.timeouts = timeouts
    }

    /// Runs the whole upload. Returns once the device has confirmed the image
    /// (it reboots by itself right after). `onPhase` is called in order on the
    /// main actor.
    func run(onPhase: @escaping @MainActor @Sendable (Phase) -> Void) async throws {
        try await withTaskCancellationHandler {
            defer { teardown() }
            try await perform(onPhase)
        } onCancel: {
            teardown()
        }
    }

    // MARK: - Protocol

    private func perform(_ onPhase: @MainActor @Sendable (Phase) -> Void) async throws {
        let localPort = try await startListener()

        // 1–3. Invitation (+ challenge/response) over UDP.
        await onPhase(.inviting)
        guard let devicePort = NWEndpoint.Port(rawValue: port) else {
            throw OTAUploadError.network("invalid port \(port)")
        }
        let udp = NWConnection(host: NWEndpoint.Host(host), port: devicePort, using: .udp)
        locked { self.udp = udp }
        try await ready(udp, timeout: timeouts.reply, unreachable: .noAnswer)
        try await send(udp, Data(EspOTA.invitation(localPort: localPort, size: image.size,
                                                   md5: image.md5).utf8))
        var reply = try await receiveReply(udp)
        if case .auth(let nonce) = reply {
            guard !password.isEmpty else { throw OTAUploadError.passwordRequired }
            await onPhase(.authenticating)
            let cnonce = EspOTA.makeClientNonce()
            let response = EspOTA.authResponse(password: password, nonce: nonce, cnonce: cnonce)
            try await send(udp, Data(EspOTA.authMessage(cnonce: cnonce, response: response).utf8))
            reply = try await receiveReply(udp)
        }
        switch reply {
        case .ok:                  break
        case .authFailed:          throw OTAUploadError.authFailed
        case .error(let m):        throw OTAUploadError.deviceRejected(m)
        case .auth:                throw OTAUploadError.unexpectedReply("AUTH")
        case .unknown(let s):      throw OTAUploadError.unexpectedReply(s)
        }
        udp.cancel()

        // 4. The device connects back; stream the image, one ack per chunk.
        await onPhase(.waitingForDevice)
        let tcp = try await acceptInbound()
        locked { self.tcp = tcp }
        try await ready(tcp, timeout: timeouts.connect, unreachable: .deviceDidNotConnect)

        let total = image.size
        var sent = 0
        var stream = ""
        await onPhase(.uploading(sent: 0, total: total))
        while sent < total {
            let end = min(sent + EspOTA.chunkSize, total)
            let base = image.data.startIndex
            try await send(tcp, image.data.subdata(in: (base + sent)..<(base + end)))
            sent = end
            guard let ack = try await receive(tcp, timeout: timeouts.ack,
                                              onTimeout: .uploadFailed("the controller stopped acknowledging data"))
            else { throw OTAUploadError.uploadFailed("the controller closed the connection") }
            stream += String(decoding: ack, as: UTF8.self)
            if case .error(let m) = EspOTA.parseOutcome(stream) { throw OTAUploadError.deviceError(m) }
            await onPhase(.uploading(sent: sent, total: total))
        }

        // 5. Wait for the device's verdict.
        await onPhase(.verifying)
        let deadline = Date().addingTimeInterval(timeouts.result)
        while true {
            switch EspOTA.parseOutcome(stream) {
            case .ok:              return
            case .error(let m):    throw OTAUploadError.deviceError(m)
            case .pending:         break
            }
            let remaining = deadline.timeIntervalSinceNow
            guard remaining > 0,
                  let more = try await receive(tcp, timeout: remaining, onTimeout: .noResult)
            else { throw OTAUploadError.noResult }
            stream += String(decoding: more, as: UTF8.self)
        }
    }

    // MARK: - Socket helpers (Network.framework → async, each with a deadline)

    private func startListener() async throws -> UInt16 {
        let listener: NWListener
        do { listener = try NWListener(using: .tcp) } catch {
            throw OTAUploadError.listenerFailed(error.localizedDescription)
        }
        locked { self.listener = listener }
        listener.newConnectionHandler = { [weak self] c in self?.offerInbound(c) }
        return try await withDeadline(5, .listenerFailed("timed out")) { finish in
            listener.stateUpdateHandler = { state in
                switch state {
                case .ready:
                    if let p = listener.port?.rawValue { finish(.success(p)) }
                    else { finish(.failure(OTAUploadError.listenerFailed("no port assigned"))) }
                case .waiting(let e), .failed(let e):
                    finish(.failure(OTAUploadError.listenerFailed(e.localizedDescription)))
                case .cancelled:
                    finish(.failure(CancellationError()))
                default: break
                }
            }
            listener.start(queue: queue)
        }
    }

    private func offerInbound(_ c: NWConnection) {
        lock.lock()
        if inboundTaken { lock.unlock(); c.cancel(); return }
        if let waiter = inboundWaiter {
            inboundWaiter = nil
            inboundTaken = true
            lock.unlock()
            waiter(.success(c))
            return
        }
        if pendingInbound == nil { pendingInbound = c; lock.unlock(); return }
        lock.unlock()
        c.cancel()
    }

    private func acceptInbound() async throws -> NWConnection {
        try await withDeadline(timeouts.connect, .deviceDidNotConnect) { finish in
            lock.lock()
            if let c = pendingInbound {
                pendingInbound = nil
                inboundTaken = true
                lock.unlock()
                finish(.success(c))
            } else {
                inboundWaiter = finish
                lock.unlock()
            }
        }
    }

    private func ready(_ c: NWConnection, timeout: TimeInterval,
                       unreachable: OTAUploadError) async throws {
        try await withDeadline(timeout, unreachable) { (finish: @escaping (Result<Void, Error>) -> Void) in
            c.stateUpdateHandler = { state in
                switch state {
                case .ready:
                    finish(.success(()))
                case .waiting(let e), .failed(let e):
                    finish(.failure(OTAUploadError.network(e.localizedDescription)))
                case .cancelled:
                    finish(.failure(CancellationError()))
                default: break
                }
            }
            c.start(queue: queue)
        }
    }

    private func send(_ c: NWConnection, _ data: Data) async throws {
        try await withDeadline(timeouts.ack, .uploadFailed("sending timed out")) { (finish: @escaping (Result<Void, Error>) -> Void) in
            c.send(content: data, completion: .contentProcessed { error in
                if let error { finish(.failure(OTAUploadError.network(error.localizedDescription))) }
                else { finish(.success(())) }
            })
        }
    }

    private func receiveReply(_ c: NWConnection) async throws -> EspOTA.Reply {
        let data: Data = try await withDeadline(timeouts.reply, .noAnswer) { finish in
            c.receiveMessage { data, _, _, error in
                if case .posix(let code)? = error, code == .ECONNREFUSED {
                    finish(.failure(OTAUploadError.noAnswer))      // nothing listening on :8266
                } else if let error {
                    finish(.failure(OTAUploadError.network(error.localizedDescription)))
                } else {
                    finish(.success(data ?? Data()))
                }
            }
        }
        return EspOTA.parseReply(String(decoding: data, as: UTF8.self))
    }

    /// Next bytes from a stream connection, or nil at end of stream. A timeout is
    /// always fatal to the upload, so an abandoned receive never loses data.
    private func receive(_ c: NWConnection, timeout: TimeInterval,
                         onTimeout: OTAUploadError) async throws -> Data? {
        try await withDeadline(timeout, onTimeout) { finish in
            c.receive(minimumIncompleteLength: 1, maximumLength: 512) { data, _, isComplete, error in
                if let data, !data.isEmpty { finish(.success(data)) }
                else if let error { finish(.failure(OTAUploadError.uploadFailed(error.localizedDescription))) }
                else if isComplete { finish(.success(nil)) }
                else { finish(.success(Data())) }
            }
        }
    }

    /// Bridges one callback to async, resolving exactly once: with the callback's
    /// result or with `timeoutError` after `seconds`, whichever comes first.
    private func withDeadline<T>(_ seconds: TimeInterval, _ timeoutError: OTAUploadError,
                                 _ body: (@escaping (Result<T, Error>) -> Void) -> Void) async throws -> T {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<T, Error>) in
            let once = OneShot()
            let finish: (Result<T, Error>) -> Void = { r in if once.claim() { cont.resume(with: r) } }
            queue.asyncAfter(deadline: .now() + seconds) { finish(.failure(timeoutError)) }
            body(finish)
        }
    }

    private func locked(_ mutate: () -> Void) {
        lock.lock(); defer { lock.unlock() }
        mutate()
    }

    private func teardown() {
        lock.lock()
        let objects = (listener, udp, tcp, pendingInbound, inboundWaiter)
        listener = nil; udp = nil; tcp = nil; pendingInbound = nil; inboundWaiter = nil
        inboundTaken = true
        lock.unlock()
        objects.0?.cancel()
        objects.1?.cancel()
        objects.2?.cancel()
        objects.3?.cancel()
        objects.4?(.failure(CancellationError()))
    }
}

private final class OneShot: @unchecked Sendable {
    private let lock = NSLock()
    private var claimed = false
    func claim() -> Bool {
        lock.lock(); defer { lock.unlock() }
        if claimed { return false }
        claimed = true
        return true
    }
}
