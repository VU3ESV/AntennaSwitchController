# AntennaSwitchControllerApp — architecture notes

macOS SwiftUI app (and RadioPluginKit plugin) to manage multiple Antenna Switch
Controllers. Mirrors the conventions of `VU3ESV/BandPassFilterControllerApp`.

## Targets / products
- `AntennaSwitchController` (library target) → product **`AntennaSwitchControllerKit`**,
  consumed by the suite. Contains everything; only `AntennaSwitchPlugin` and the
  standalone `AntennaSwitchStandaloneApp` are `public`.
- `AntennaSwitchControllerMain` (executable) → product `AntennaSwitchController`
  (the standalone `.app`). Just calls `AntennaSwitchStandaloneApp.main()`.
- Depends on `RadioPluginKit` by **Git URL `from: "1.0.0"`** (same as the suite,
  so the whole graph resolves one identical RadioPluginKit — no path/URL clash).

## Layout
- `Models/` — `Band` (index order matches firmware), `Controller` (saved device,
  Codable), `DeviceIdentity` (`/discover`), `DeviceStatus` (`/status`),
  `DeviceConfig` (`/config` read + `formBody()` for `/save`).
- `Networking/` — `AntennaSwitchClient` (async HTTP), `DiscoveryService`
  (Bonjour `_antsw._tcp`, `NetServiceBrowser`).
- `ViewModels/` — `ControllersStore` (`@MainActor`, owns the persisted controller
  list + discovery, the sidebar's source of truth) and `ControllerViewModel`
  (per-controller: 2 s `/status` poll, load/save `/config`, relay override, reboot).
- `Views/` — `ContentView` (NavigationSplitView + sidebar + add-by-IP sheet),
  `ControllerDetailView` (Dashboard/Controls/Settings tabs, fresh VM per
  selection via `.id`), `DashboardView`, `ControlsView`, `SettingsView`.

## Key decisions
- **Multi-device:** `ControllersStore` persists `[Controller]` as JSON in
  `AppDefaults.store`; the plugin injects the host's namespaced defaults, the
  standalone app uses `.standard`.
- **Settings completeness:** every web-page option is in `SettingsView`. The app
  needs `/config` (added to firmware) to pre-fill the form; passwords are never
  read back and are only sent when the operator types a new value (blank = keep).
- `NSLocalNetworkUsageDescription` + `NSBonjourServices` in `Resources/Info.plist`
  are required for LAN/Bonjour access on macOS.

## Build quirk
Global git `safe.bareRepository=explicit` blocks SwiftPM. Prefix builds with
`GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.bareRepository GIT_CONFIG_VALUE_0=all`.
