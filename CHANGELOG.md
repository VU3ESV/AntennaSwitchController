# Changelog

All notable changes to the **Antenna Switch Controller** — an ESP8266 8-channel relay board
(`Controller/`) plus a macOS app (`App/`) that also ships as an
[Amateur Radio Suite](https://github.com/VU3ESV/AmateurRadioSuite) plugin. Format follows
[Keep a Changelog](https://keepachangelog.com/); a release (macOS app + firmware + plugin)
is cut on every merge to `main` (tags `vX.Y.Z`).

## [Unreleased]

### Added
- **Out-of-process plugin** ([CONVERTING-A-PLUGIN.md](https://github.com/VU3ESV/AmateurRadioSuite/blob/main/docs/CONVERTING-A-PLUGIN.md)):
  an ExtensionKit `.appex` target (`App/Xcode/`) + `App/scripts/make-radioplugin.sh` packaging
  `AntennaSwitch.radioplugin`, so the suite can browse/install the controller and host it
  sandboxed via `EXHostViewController`. Adds a public `AntennaSwitchExtension.rootView()`
  factory; the standalone app and in-process `AntennaSwitchPlugin` are unchanged. CI builds the
  `.appex` on every PR; the release attaches the `.radioplugin` alongside the app zip + firmware.

### Changed
- **Release on merge**: the release was tag-only; it now also **auto-cuts a release on every
  merge to `main`** (auto patch-bump of the latest `vX.Y.Z` tag). Tag-push and manual dispatch
  still work. The release still builds the ESP8266 firmware and the universal macOS app.

## [0.1.0] — 2026-06-02
### Added
- **Plugin support**: a `public AntennaSwitchPlugin` adopting the RadioPluginKit 1.2 contract
  (declarative manifest, `persistState`/`restoreState`, and connectivity/notification/badge
  routing to the host context); the app adopts the `RadioPluginUI` dark-LCD design system.
- **CI + Release pipelines**: build/test the macOS app and compile the ESP8266 firmware; a
  GitHub Release publishes the universal `.app` (zip) + firmware `.bin`.
- **Repo restructure**: split into `Controller/` (ESP8266 sketch) and `App/` (macOS SwiftUI
  app) with docs merged to the repo root.
- Initial ESP8266 8-channel Antenna Switch Controller with mDNS discovery and a macOS
  client (controllers list, live state, web portal).
