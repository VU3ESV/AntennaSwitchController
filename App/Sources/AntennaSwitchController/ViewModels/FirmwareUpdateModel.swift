import Foundation

/// Drives one OTA firmware update of one controller: pick an image (latest GitHub
/// release or a local `.bin`), upload it with `EspOTAUploader`, then wait for the
/// controller to reboot and report its new version.
@MainActor
final class FirmwareUpdateModel: ObservableObject {
    enum Source: String, CaseIterable, Identifiable {
        case release = "Latest release"
        case file = "Local file"
        var id: String { rawValue }
    }

    enum Stage: Equatable {
        case idle
        case loadingImage(String)
        case uploading(EspOTAUploader.Phase)
        case rebooting
        case succeeded(String)
        case failed(String)
    }

    @Published var source: Source = .release {
        didSet {
            guard source != oldValue else { return }
            // Drop an in-flight release download so it can't land under "Local file".
            if case .loadingImage = stage { task?.cancel(); stage = .idle }
            image = nil
            sourceError = nil
        }
    }
    @Published var password = ""
    @Published private(set) var release: FirmwareRelease?
    @Published private(set) var image: FirmwareImage?
    @Published private(set) var sourceError: String?
    @Published private(set) var stage: Stage = .idle
    /// A failed attempt got far enough that the controller entered OTA, which forces
    /// every relay off and drops the radio link — it stays that way until rebooted.
    @Published private(set) var needsReboot = false

    let controller: ControllerViewModel
    private let releases = FirmwareReleaseClient()
    private var task: Task<Void, Never>?
    private var enteredOTA = false

    init(controller: ControllerViewModel) {
        self.controller = controller
    }

    // MARK: - Derived state

    var deviceVersion: String? { controller.identity?.version }

    var isBusy: Bool {
        switch stage {
        case .loadingImage, .uploading, .rebooting: return true
        default: return false
        }
    }

    var isUpdating: Bool {
        switch stage {
        case .uploading, .rebooting: return true
        default: return false
        }
    }

    /// Why the update can't start right now, or nil when it can.
    var blockReason: String? {
        guard controller.connected, let s = controller.status else {
            return "The controller is offline."
        }
        if s.apMode {
            return "OTA needs the controller on your WiFi — it’s in setup (AP) mode."
        }
        if s.transmitting || s.tuning || s.radio2?.transmitting == true {
            return "A radio is transmitting. Starting an update forces every relay off, so wait until it’s back on receive."
        }
        if image == nil { return "Choose a firmware image." }
        return nil
    }

    /// The controller's own IPv4 address when known (skips mDNS for the UDP
    /// invitation), else the saved address.
    var target: String {
        if let ip = controller.status?.ip, !ip.isEmpty, ip != "0.0.0.0" { return ip }
        return controller.host
    }

    // MARK: - Choosing an image

    func prepare() {
        if source == .release, image == nil, !isBusy { fetchLatestRelease() }
    }

    func fetchLatestRelease() {
        task?.cancel()
        sourceError = nil
        image = nil
        task = Task {
            do {
                stage = .loadingImage("Checking GitHub for the latest release…")
                let r = try await releases.latest()
                release = r
                stage = .loadingImage("Downloading \(r.tag)…")
                let data = try await releases.download(r)
                try Task.checkCancellation()
                image = try FirmwareImage(data: data, name: r.assetName)
            } catch {
                if !Task.isCancelled { sourceError = Self.message(error) }
            }
            if case .loadingImage = stage { stage = .idle }
        }
    }

    func loadFile(_ url: URL) {
        sourceError = nil
        image = nil
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            image = try FirmwareImage(data: try Data(contentsOf: url), name: url.lastPathComponent)
        } catch {
            sourceError = Self.message(error)
        }
    }

    func fileImportFailed(_ error: Error) {
        sourceError = Self.message(error)
    }

    // MARK: - Updating

    func startUpdate() {
        guard blockReason == nil, let image, !isBusy else { return }
        task = Task { await runUpdate(image) }
    }

    func cancel() { task?.cancel() }

    func rebootController() async {
        await controller.reboot()
        needsReboot = false
        stage = .idle
    }

    private func runUpdate(_ image: FirmwareImage) async {
        let before = deviceVersion
        needsReboot = false
        enteredOTA = false
        // The controller can't serve HTTP while it's flashing — pause polling so the
        // dashboard (and the suite) don't report a spurious outage.
        controller.stop()
        defer { controller.start() }

        let uploader = EspOTAUploader(host: target, password: password, image: image)
        do {
            try await uploader.run { [weak self] phase in
                guard let self else { return }
                switch phase {
                case .waitingForDevice, .uploading, .verifying: self.enteredOTA = true
                default: break
                }
                self.stage = .uploading(phase)
            }
        } catch {
            needsReboot = enteredOTA
            stage = .failed(Task.isCancelled ? "Update cancelled." : Self.message(error))
            return
        }

        stage = .rebooting
        guard let after = await waitForController() else {
            stage = .failed(Task.isCancelled
                ? "Stopped waiting. The firmware was already written; the controller reboots into it by itself."
                : "The firmware was written, but the controller didn’t come back within 90 seconds. Check its power and WiFi; it may have rejoined with a different address.")
            return
        }
        controller.identity = after
        let now = after.version ?? "?"
        if let expected = image.version, FirmwareVersion.compare(now, expected) != .orderedSame {
            stage = .failed("The controller rebooted but reports firmware \(now), not \(expected). The new image may not have been activated.")
        } else if let before, FirmwareVersion.compare(before, now) != .orderedSame {
            stage = .succeeded("Updated from firmware \(before) to \(now).")
        } else {
            stage = .succeeded("Firmware \(now) installed.")
        }
    }

    /// Polls `/discover` until the rebooted controller answers.
    private func waitForController(timeout: TimeInterval = 90) async -> DeviceIdentity? {
        // The device restarts ~1 s after confirming; give it time to go down first
        // so the old firmware can't answer the first poll.
        try? await Task.sleep(nanoseconds: 4_000_000_000)
        let client = AntennaSwitchClient(host: controller.host, timeout: 3)
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline, !Task.isCancelled {
            if let id = try? await client.fetchDiscover() { return id }
            try? await Task.sleep(nanoseconds: 2_000_000_000)
        }
        return nil
    }

    private static func message(_ error: Error) -> String {
        (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
    }
}
