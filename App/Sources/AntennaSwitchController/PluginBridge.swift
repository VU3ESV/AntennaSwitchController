import Foundation

/// Bridge the module's view models use to surface events to a RadioPluginKit host
/// (errors / notifications / sidebar badge) WITHOUT depending on RadioPluginKit
/// directly. `AntennaSwitchPlugin` provides the implementation and maps these to
/// `PluginHost.report/notify/setBadge`; the standalone app leaves it `nil` and the
/// views just show their own inline state.
@MainActor
protocol PluginHostBridge: AnyObject {
    /// A controller that was reachable has dropped (connectivity error + badge).
    func controllerWentOffline(name: String, address: String)
    /// A previously-offline controller is reachable again (clears badge).
    func controllerCameOnline(name: String)
    /// Result of a `POST /save` (host notification).
    func configSaved(name: String, ok: Bool, detail: String?)
}
