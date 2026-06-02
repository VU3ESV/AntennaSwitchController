import Foundation
import Combine

/// Owns the operator's list of controllers (persisted) and merges in live
/// Bonjour discovery. The single source of truth for the sidebar.
@MainActor
final class ControllersStore: ObservableObject {
    @Published var controllers: [Controller] = []
    @Published var selection: Controller.ID?
    @Published private(set) var discovered: [DiscoveredDevice] = []
    @Published private(set) var isBrowsing = false

    private let discovery = DiscoveryService()
    private var bag = Set<AnyCancellable>()
    private let defaultsKey = "controllers"
    private var started = false

    init() {
        discovery.$devices.receive(on: RunLoop.main)
            .sink { [weak self] in self?.discovered = $0 }.store(in: &bag)
        discovery.$isBrowsing.receive(on: RunLoop.main)
            .sink { [weak self] in self?.isBrowsing = $0 }.store(in: &bag)
    }

    func start() {
        guard !started else { return }
        started = true
        load()
        discovery.start()
        if selection == nil { selection = controllers.first?.id }
    }

    func rescan() { discovery.restart() }

    // MARK: - Mutations

    @discardableResult
    func add(name: String, address: String) -> Controller {
        let label = name.trimmingCharacters(in: .whitespaces)
        let c = Controller(name: label.isEmpty ? address : label, address: address)
        controllers.append(c)
        save()
        selection = c.id
        return c
    }

    func addDiscovered(_ d: DiscoveredDevice) {
        guard !controllers.contains(where: { $0.address == d.address }) else { return }
        add(name: d.title, address: d.address)
    }

    func remove(_ id: Controller.ID) {
        controllers.removeAll { $0.id == id }
        save()
        if selection == id { selection = controllers.first?.id }
    }

    func controller(_ id: Controller.ID?) -> Controller? {
        guard let id else { return nil }
        return controllers.first { $0.id == id }
    }

    /// Discovered devices that aren't already in the saved list.
    var newlyDiscovered: [DiscoveredDevice] {
        discovered.filter { d in !controllers.contains { $0.address == d.address } }
    }

    // MARK: - Persistence

    private func load() {
        guard let data = AppDefaults.store.data(forKey: defaultsKey),
              let list = try? JSONDecoder().decode([Controller].self, from: data) else { return }
        controllers = list
    }

    private func save() {
        if let data = try? JSONEncoder().encode(controllers) {
            AppDefaults.store.set(data, forKey: defaultsKey)
        }
    }
}
