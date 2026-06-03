# Changelog

All notable changes to the **Antenna Switch Controller** — an ESP8266 8-channel
relay board (`Controller/`) plus a macOS app (`App/`) that also ships as an
[Amateur Radio Suite](https://github.com/VU3ESV/AmateurRadioSuite) plugin. Format
follows [Keep a Changelog](https://keepachangelog.com/). A release (macOS app
`.dmg` + suite `.radioplugin` + firmware `.bin`) is cut on every merge to `main`
(auto patch-bump of the latest `vX.Y.Z` tag); tag-push and manual dispatch also
work. See the [GitHub Releases] for downloads.

[GitHub Releases]: https://github.com/VU3ESV/AntennaSwitchController/releases

## [Unreleased]
### Added
- **Per-relay (antenna) names.** Name each relay — e.g. "80m Dipole", "Hexbeam" —
  in the app's Settings or the controller's web page. The names replace the
  generic "R1–R8" everywhere (dashboard active relay, controls buttons, band→relay
  map, manual-override buttons, and the dual-mode interlock badges, now "Radio 1 →
  80m Dipole"), removing the clash with the radio's own RX1/RX2 receiver labels.
  Stored on the controller in EEPROM (config **v6**, exposed via `/config` +
  `/save`); a blank name falls back to "R<n>". v5→v6 migration preserves all
  existing settings.
- **Single shared TCI client in Mode B (dual)** — when both radios are TCI on the
  *same* `host:port` (a SunSDR2's two receivers, RX1/RX2), the controller reads
  both receivers from **one** WebSocket instead of opening a redundant second one
  (the bundled library already demultiplexes per-receiver events into
  `rtx[0]`/`rtx[1]`). Halves inbound traffic and removes the band-switch lag seen
  with two sockets to one radio; `TCI::process()` also drains a small bounded
  batch of queued frames per call (was one), so a busy radio's events don't back up.

### Fixed
- **Firmware version reported correctly.** The controller's `/discover` (and mDNS
  TXT) reported a hardcoded `"1.0"` regardless of the build — the release pipeline
  only stamped the version into the macOS app, not the firmware. The release now
  injects the tag into `FW_VERSION` at compile time, so the app's dashboard shows
  the real firmware version (e.g. `0.1.15`). Local builds still fall back to `1.0`.
- **Band-change reboot in Mode B (dangerous: dropped a live antenna).** Tuning one
  receiver to a new band could reset the ESP — a CPU exception (LoadStoreError)
  that de-energized **every** relay for ~6 s before reconnecting. Root cause: the
  bundled TCI parser dispatches by substring (`strstr`) but parses from the start
  (`sscanf`), so a substring/format mismatch left the receiver index `rtxId`
  unparsed (garbage) and `rtx[rtxId]` stored through a wild pointer. All
  receiver-array accesses now go through a bounds-checked accessor; the
  `modulation` handler's unbounded `%s` + `modulation[strlen-1]` underflow is
  rewritten as a bounded read, and every `sscanf("%s")` is width-limited.
  Validated on a SunSDR2: RX1 cycled across bands repeatedly, zero exceptions,
  RX2's relay held throughout.
- **In-use antenna dropped on a brief TCI flap.** Relay decisions now debounce
  link loss (hold the last antenna for up to 5 s) instead of de-energizing the
  instant `connected()` goes false, so a momentary reconnect no longer drops a
  live antenna. Sustained loss still fails safe (R2.10).
- **TCI ring-buffer overflow.** `put_messages()` did an unbounded `sprintf` into a
  90-byte slot; oversized frames are now dropped and the copy is bounded.

## [v0.1.7] — 2026-06-03
### Added
- **Per-radio TCI receiver index** (`radio_rx` / `radio2_rx`): a radio that
  exposes two receivers over TCI (e.g. **SunSDR2 PRO**) can drive **SO2R from a
  single board** — point both radios at the same TCI host/port, Radio 1 = RX1,
  Radio 2 = RX2. App + web UI gain RX1/RX2 selectors.
### Changed
- **8×2 output corrected** to the verified switch wiring: relay _i_ = antenna
  _i_, and the relay for **each** receiver's antenna is energized — up to **two**
  relays HIGH at once (one per radio), with per-line break-before-make.
  `/status` adds `relay_mask`. Supersedes the earlier single-relay A/B model.
- EEPROM config migrated to v5 (receiver indices); prior settings preserved.

## [v0.1.6] — 2026-06-02
### Added
- **Debounced master/slave heartbeat** for SO2R Mode A: the slave's claim/release
  doubles as a 2 s heartbeat; peer loss is declared only after 3 consecutive
  missed beats (~6 s), so a single dropped LAN packet never drops an antenna.
  `/status` exposes `beats_missed`; the app shows a heartbeat badge.

## [v0.1.5] — 2026-06-02
### Added
- **SO2R Mode B** (`mode = dual`): one board tracks **both radios** and drives an
  external **8×2** antenna switch, with an in-firmware first-come interlock and
  per-radio TX-safety. App gains a Radio 2 settings section + dual dashboard.
- EEPROM config migrated to v4 (second radio + switch type).

## [v0.1.4] — 2026-06-02
### Added
- **FlexRadio SmartSDR** band source over TCP 4992 (`radio_type`), selectable per
  unit alongside TCI; app radio-type picker.
- **SO2R Mode A** (master / slave): two 8×1 boards coordinate over the LAN so the
  two radios never share an antenna — `/interlock` API, role selector, app
  interlock view. First-come / current-holder-keeps-it policy.
### Changed
- Firmware refactored behind `RadioSource` and `OutputStage` abstractions (no
  behaviour change for existing standalone units); EEPROM config migrated to
  v2/v3 with the band map preserved.

## [v0.1.3] — 2026-06-02
### Changed
- The release version is now stamped into the app bundle
  (`CFBundleShortVersionString`); hardened the local `git describe` fallback.

## [v0.1.2] — 2026-06-02
### Changed
- Releases publish the macOS app as a **`.dmg`** (was a `.zip`).
- **Release on merge**: in addition to tag-push and manual dispatch, the workflow
  now auto-cuts a release on every merge to `main` (auto patch-bump).

## [v0.1.1] — 2026-06-02
### Added
- **Out-of-process plugin**: an ExtensionKit `.appex` target (`App/Xcode/`) +
  `App/scripts/make-radioplugin.sh` packaging `AntennaSwitch.radioplugin`, so the
  suite can browse/install the controller and host it sandboxed via
  `EXHostViewController`. The standalone app and in-process plugin are unchanged;
  CI builds the `.appex` and the release attaches the `.radioplugin`.

## [v0.1.0] — 2026-06-02
### Added
- Initial pre-release. ESP8266 **single-radio TCI** antenna switch: band → one of
  8 relays, exclusive break-before-make, TX-safe, failsafe de-energized; web
  config portal, EEPROM + CRC32 persistence, mDNS / `_antsw._tcp`, SoftAP
  first-run setup, ArduinoOTA.
- **macOS app** to manage multiple controllers (add by IP or Bonjour discovery),
  with dashboard / controls / settings.
- **RadioPluginKit 1.2** in-process plugin (`AntennaSwitchPlugin`: declarative
  manifest, `persistState`/`restoreState`, host connectivity/notification/badge
  routing) + `RadioPluginUI` dark-LCD theming.
- CI + Release pipelines: universal (Apple Silicon + Intel) app build, ESP8266
  firmware compile, and a GitHub Release publishing both.
- Repo restructured into `Controller/` + `App/` with docs at the root.
