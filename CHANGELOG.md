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
_Nothing yet._

## [v0.1.25] — 2026-06-03
### Added
- **Serial CAT radio support (read-only) — P1.** A radio can now be tracked over
  **serial CAT** on the board's UART, in addition to TCI and FlexRadio: **Kenwood
  / Elecraft** and **modern Yaesu** (FT-991A / FT-DX10) via the ASCII `IF;` poll,
  and **Icom CI-V** (IC-7300/9700/705/7610) via the binary frequency frame. New
  `radio_type` options plus `cat_baud` and an Icom `civ_addr`, in the app's Radio
  settings and the web page (config **v9**; v8→v9 migration). It takes over UART0
  (the serial console is disabled while a CAT radio is selected) and tracks
  **band only** — unlike TCI/Flex it can't inhibit switching during TX, so don't
  change bands while transmitting and sequence any amplifier externally. Level
  converters are required — see [docs/HARDWARE.md §3](docs/HARDWARE.md). The CAT
  frequency parsers have host unit tests; **the serial path is build-verified
  only (no live rig tested).**

## [v0.1.24] — 2026-06-03
### Added
- **Pick the SO2R master from mDNS discovery.** In Slave mode, Settings offers a
  "Pick master from network" menu of the controllers found via Bonjour
  (`_antsw._tcp`), excluding this unit; selecting one fills in the master address
  (preferring the stable `.local` hostname over the DHCP IP). Manual entry still
  works.

## [v0.1.23] — 2026-06-03
### Changed
- **Tests:** added a live **Mode B (Dual)** integration test (collision → fallback,
  clear-band, TX-safety) driven via the app client + `/test/inject`; the Mode A
  and Mode B integration tests now skip independently on their own env vars.

## [v0.1.22] — 2026-06-03
### Added
- **Antenna metadata + coverage validation (P4 / M1, M3).** Each relay (antenna)
  can now declare the **bands it covers**, a **feed type** (single feedline vs a
  **triplexed** leg), and a **group** id. The app's new *Antennas* section (and
  the web page) edit these; the band→relay map then **warns** when a band is
  assigned to an antenna whose coverage excludes it, and flags a triplexer group
  asked to put one band on two legs. Triplexed legs sharing a group are distinct
  relay ports, so the SO2R interlock already lets both radios use one physical
  multiband antenna at once (different legs) — see
  [docs/HARDWARE.md](docs/HARDWARE.md) for the triplexer + BPF + stub isolation
  it requires. Metadata only — switching behaviour is unchanged. Config **v8**
  (`relay_bands` / `relay_feed` / `relay_group` via `/config` + `/save`); v7→v8
  migration preserves all settings.

## [v0.1.21] — 2026-06-03
### Added
- **SO2R contention indicator** in the app dashboard. When a radio loses its
  band's primary antenna to the other radio, its card shows an **"On fallback"**
  (amber) or **"Primary busy"** (red, no antenna free) badge — so the operator
  can see *why* a radio is on a different antenna or none. Derived entirely from
  `/status` + `/config` (no firmware change); standalone units never show it.

## [v0.1.20] — 2026-06-03
### Added
- **Per-band fallback antenna for SO2R (P4 / multiband antennas).** Each band now
  has an optional **secondary** antenna in addition to its primary. In SO2R
  (Master/Slave or Dual) when the primary antenna is already in use by the other
  radio, the controller switches that radio to the band's fallback instead of
  dropping it to none — e.g. a **HexBeam** shared across 20–6 m with a wire dipole
  as the fallback. The fallback is honoured by the Mode A master, the Mode A slave
  (claims the secondary when the master denies the primary), and the Mode B
  `DualResolver`; a transmitting radio is never moved or preempted (R2.9).
  Standalone units ignore it (no contention). Config **v7** (`band_relay2[]`,
  exposed via `/config` `bands2` + `/save` `s0..s10`); v6→v7 migration preserves
  all settings. App + web band map gain a "Fallback" column (shown only for SO2R
  modes). **Live-validated** on the two-board Mode A pair. See
  [docs/MULTI-RADIO-SO2R-PLAN.md §11](docs/MULTI-RADIO-SO2R-PLAN.md).
- **Test infrastructure** for the SO2R decision logic (plan §11.9): host unit
  tests of the resolvers (`Controller/test/`, real firmware headers + desktop
  shims, no hardware) and a live integration suite (`App/Tests/`) that configures
  real boards with the app's HTTP client and drives scenarios via a new gated
  **`/test/inject`** firmware API (compiled only with `-DANTSW_TEST`; absent from
  production images). Both wired into CI; the integration suite self-skips without
  board addresses.

## [v0.1.19] — 2026-06-03
### Changed
- **Docs:** added the multiband-antenna (HexBeam) support plan —
  [docs/MULTI-RADIO-SO2R-PLAN.md §11](docs/MULTI-RADIO-SO2R-PLAN.md) + a P4
  roadmap entry.

## [v0.1.18] — 2026-06-03
### Fixed
- **Standalone app quits when its window is closed.** Closing the window left a
  windowless process in the Dock that wouldn't reopen on click (the app removes
  the "New Window" command, which also disables SwiftUI's dock-reopen). It now
  terminates on last-window-close. The suite plugin's lifecycle is host-owned and
  unaffected.

## [v0.1.17] — 2026-06-03
### Changed
- **JSON responses pre-reserve their buffers.** `/status`, `/config`, the
  interlock payload, and the HTML-escape helper now `String::reserve()` up front
  instead of growing append-by-append, so building a response no longer churns
  the heap with repeated reallocations. Behaviour is unchanged.
### Removed
- **Temporary `/status` crash diagnostics** (`reset` / `resetinfo` / `heap`).
  Added to chase the Mode-B band-change reboot; that bug is fixed and
  field-confirmed, so the fields are gone (~0.5 KB flash). The app/web never read
  them.

## [v0.1.16] — 2026-06-03
### Changed
- **Docs:** refreshed the README and regenerated the app screenshots
  (dual-radio dashboard, named antennas, red/tap-to-deactivate Controls).

## [v0.1.15] — 2026-06-03
### Fixed
- **Firmware version reported correctly.** `/discover` (and the mDNS TXT) reported
  a hardcoded `"1.0"` regardless of the build — the pipeline only stamped the app.
  The release now injects the tag into `FW_VERSION` at compile time, so the
  dashboard shows the real firmware version. Local builds still fall back to `1.0`.

## [v0.1.14] — 2026-06-03
### Changed
- **Settings: the Radio 2 RX picker appears only when it can apply** — i.e. when
  Radio 2 shares Radio 1's TCI host/port (the one 2-receiver-radio case). Two
  *separate* radios both use RX1; the app forces `radio2_rx = 0` on save for that
  case, fixing a board that wasn't switching on a separate Radio 2's band.

## [v0.1.13] — 2026-06-03
### Changed
- **Controls: clearer manual-off state in Mode B** — a per-radio mode summary and
  an explicit "Radio 1 Off" button, with natural wording ("Radio 1 off (manual)").

## [v0.1.12] — 2026-06-03
### Changed
- **Controls: active relays are red and tap-to-deactivate.** Tapping an energized
  relay returns to Auto; the active relay is shown in red.
### Fixed
- Release badge tracked a stale dispatch run; now follows the merge event.

## [v0.1.11] — 2026-06-03
### Added
- **Per-relay (antenna) names.** Name each relay — e.g. "80m Dipole", "HexBeam" —
  in the app's Settings or the controller's web page. The names replace the
  generic "R1–R8" everywhere (dashboard, controls, band→relay map, manual-override
  buttons, dual-mode interlock badges), removing the clash with the radio's own
  RX1/RX2 receiver labels. Stored in EEPROM (config **v6**, via `/config` +
  `/save`); a blank name falls back to "R<n>". v5→v6 migration preserves settings.
- **Symmetric dual dashboard + Controls (Mode B).** The dashboard shows both
  radios' band / frequency / relay cards; Controls shows both energized relays.
  `/status` gains explicit `radio1_relay` / `radio2_relay` (preferred over the
  legacy `active_relay`, kept for back-compat).

## [v0.1.10] — 2026-06-03
### Fixed
- **Band-change reboot in Mode B (dangerous: dropped a live antenna).** Tuning one
  receiver to a new band could reset the ESP — a CPU exception (LoadStoreError)
  that de-energized **every** relay for ~6 s. Root cause: the bundled TCI parser
  dispatches by substring (`strstr`) but parses from the start (`sscanf`), so a
  mismatch left the receiver index `rtxId` unparsed (garbage) and `rtx[rtxId]`
  stored through a wild pointer. All receiver-array accesses now go through a
  bounds-checked accessor; the `modulation` handler's unbounded `%s` underflow is
  a bounded read, and every `sscanf("%s")` is width-limited. Validated on a
  SunSDR2: RX1 cycled across bands repeatedly, zero exceptions, RX2 held.
- **In-use antenna dropped on a brief TCI flap.** Relay decisions now debounce
  link loss (hold the last antenna for up to 5 s) instead of de-energizing the
  instant `connected()` goes false. Sustained loss still fails safe (R2.10).
- **TCI ring-buffer overflow.** `put_messages()` did an unbounded `sprintf` into a
  90-byte slot; oversized frames are now dropped and the copy is bounded.

## [v0.1.9] — 2026-06-03
### Changed
- **Docs:** updated the README and CHANGELOG for the multi-radio / SO2R features.

## [v0.1.8] — 2026-06-03
### Added
- **Single shared TCI client in Mode B (dual)** — when both radios are TCI on the
  *same* `host:port` (a SunSDR2's two receivers, RX1/RX2), the controller reads
  both receivers from **one** WebSocket instead of a redundant second one (the
  bundled library already demultiplexes per-receiver events into `rtx[0]`/`rtx[1]`).
  Halves inbound traffic and removes the band-switch lag seen with two sockets;
  `TCI::process()` also drains a small bounded batch of queued frames per call.

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
