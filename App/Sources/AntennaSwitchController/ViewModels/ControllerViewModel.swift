import Foundation
import AppKit

/// Per-controller view model: polls `/status`, loads/saves `/config`, and issues
/// manual-override and reboot commands. One instance per selected controller.
@MainActor
final class ControllerViewModel: ObservableObject {
    let host: String

    @Published var status: DeviceStatus?
    @Published var identity: DeviceIdentity?
    @Published var config: DeviceConfig = .empty
    @Published var configLoaded = false
    @Published var connected = false
    @Published var errorMessage: String?
    @Published var isSaving = false
    @Published var saveResult: String?

    private var pollTask: Task<Void, Never>?
    private var client: AntennaSwitchClient { AntennaSwitchClient(host: host) }

    init(host: String) { self.host = host }

    // MARK: - Polling lifecycle

    func start() {
        stop()
        pollTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.refreshOnce()
                try? await Task.sleep(nanoseconds: 2_000_000_000)   // 2 s
            }
        }
    }

    func stop() {
        pollTask?.cancel()
        pollTask = nil
    }

    func refreshOnce() async {
        do {
            let s = try await client.fetchStatus()
            status = s
            connected = true
            errorMessage = nil
            if identity == nil { identity = try? await client.fetchDiscover() }
        } catch {
            connected = false
            errorMessage = message(error)
        }
    }

    // MARK: - Config

    func loadConfig() async {
        do {
            var c = try await client.fetchConfig()
            let want = Band.allCases.count
            if c.bands.count < want {
                c.bands += Array(repeating: -1, count: want - c.bands.count)
            } else if c.bands.count > want {
                c.bands = Array(c.bands.prefix(want))
            }
            config = c
            configLoaded = true
            errorMessage = nil
        } catch {
            errorMessage = message(error)
        }
    }

    func save() async {
        isSaving = true
        saveResult = nil
        defer { isSaving = false }
        do {
            try await client.saveConfig(config)
            config.wifiPassword = ""   // one-shot; firmware kept it if blank
            config.otaPassword = ""
            saveResult = "Saved. Device applies settings (reconnects TCI)."
        } catch {
            saveResult = message(error)
        }
    }

    // MARK: - Control

    func setRelay(_ set: String) async {
        do {
            try await client.setRelay(set)
            await refreshOnce()
        } catch {
            errorMessage = message(error)
        }
    }

    func reboot() async { try? await client.reboot() }

    func openWeb() {
        if let url = client.pageURL() { NSWorkspace.shared.open(url) }
    }

    private func message(_ error: Error) -> String {
        (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
    }
}
