# Antenna Switch Controller — Architecture & Requirements

An ESP8266 antenna-switch controller for amateur radio, plus a macOS app to
manage it. The controller listens to a transceiver's **TCI** server, reads the
active band, and switches the matching antenna relay automatically (exclusive,
break-before-make). It has a built-in web server and supports **OTA** updates.
The macOS app manages **multiple** controllers (add by IP or Bonjour discovery)
and exposes every option the web page does.

Implementation mirrors the conventions of the companion projects
[VU3ESV/BandPassFilterController](https://github.com/VU3ESV/BandPassFilterController)
(firmware) and [VU3ESV/BandPassFilterControllerApp](https://github.com/VU3ESV/BandPassFilterControllerApp)
(app): bundled IW7DMH TCI library, ArduinoOTA, web-config + EEPROM persistence,
arduino-cli build flow; and a RadioPluginKit plugin hostable in AmateurRadioApps.

## Repository layout

```
Controller/   ESP8266 firmware (Arduino sketch — folder name == Controller.ino)
App/          macOS SwiftUI app + RadioPluginKit plugin (Swift package)
CLAUDE.md     this file (firmware requirements + app architecture)
README.md     project overview, screenshots, build/flash, suite integration
```

---

# Part 1 — Controller firmware (`Controller/`)

This part is the authoritative requirements specification for the firmware.

## 1. Hardware

**Target board:** ESP8266 ESP-12F 8-Channel Relay Board
([reference](https://werner.rothschopf.net/microcontroller/202108_esp8266_esp12f_relay_x8_en.htm)).

![ESP8266 ESP-12F 8-channel relay board](App/docs/images/relay-board.jpg)

- **MCU:** ESP8266 (ESP-12F module), typ. 4 MB flash.
- **Relays:** 8, driven **directly from GPIO, active HIGH** (GPIO HIGH = relay
  energized). No shift register / I²C expander.
- **Power:** 5 V, or 7–28 V DC via the onboard LM2596S buck converter
  (~700 mA @ 9.6 V with all relays active). A second regulator supplies 3.3 V to
  the ESP8266.
- **Programming:** USB-TTL adapter; pull GPIO0 → GND and reset to enter flash
  mode (NodeMCU/generic ESP8266 settings).

### 1.1 GPIO → Relay map (fixed by the board)

| Relay | GPIO  | Boot-state caveat                                  |
|-------|-------|----------------------------------------------------|
| 1     | GPIO16| Briefly pulsed/active at startup                   |
| 2     | GPIO14| —                                                  |
| 3     | GPIO12| —                                                  |
| 4     | GPIO13| —                                                  |
| 5     | GPIO15| Must be LOW at boot; briefly active at startup     |
| 6     | GPIO0 | Boot/flash-mode strap; must be HIGH at boot        |
| 7     | GPIO4 | —                                                  |
| 8     | GPIO5 | —                                                  |

- GPIO6–11 are not connected (internal flash).
- GPIO2 drives the onboard blue LED.

**R1: Boot-glitch safety.** Because GPIO0/GPIO15/GPIO16 glitch during reset, the
firmware MUST treat any relay activity before the controller reaches a known
state as undefined. On boot it MUST drive all relays to the safe/de-energized
state (all GPIO LOW) as early as possible, before connecting WiFi or TCI.
Operators MUST be warned (README + web UI) that relays on GPIO0/15/16 may twitch
at power-up, and SHOULD prefer GPIO14/12/13/4/5 for the most-used bands.

## 2. Functional requirements

### 2.1 TCI client

- **R2.1** The controller SHALL act as a **TCI client** and connect to the
  radio's TCI server (WebSocket). It SHALL use **IW7DMH's TCI library
  (v1.0.1)**, bundled inside the sketch folder (`TCI.h`, `TCI.cpp`, `RTX.h`,
  `RTX.cpp`). The upstream library is ESP32/FreeRTOS-only; this copy is **ported
  to ESP8266** by guarding all FreeRTOS/`<WiFi.h>` code behind `#if !defined(ESP8266)`
  and adding a cooperative `TCI::process()` (pumps `webSocket.loop()` + drains
  a small bounded batch of queued frames) called from the Arduino `loop()`. A
  single TCI link demultiplexes both receivers into `rtx[0]`/`rtx[1]`, so in
  Mode B with two TCI radios on the **same host:port** (SunSDR2) the second
  `TciSource` shares the first's client (`shareWith()`) rather than opening a
  redundant WebSocket.
- **R2.2** TCI server `host` and `port` SHALL be configurable via the web UI and
  persisted. Default port `50001` (ExpertSDR3); SHALL also work with SunSDR2 PRO
  and other TCI-compliant servers.
- **R2.3** The controller SHALL track the active **band** from the radio's
  RX-1 VFO A frequency (single-radio operation) and derive the band from the
  frequency.
- **R2.4** On a band change, the controller SHALL select the relay mapped to the
  new band per the operator's configuration (§2.2).
- **R2.5** The controller SHOULD auto-reconnect to the TCI server on connection
  loss (handled by `WebSocketsClient` reconnect while `process()` is pumped).

### 2.2 Band → relay mapping & switching

- **R2.6** The firmware SHALL support the standard HF/6 m bands: 160, 80, 60,
  40, 30, 20, 17, 15, 12, 10, 6 m (60 m included).
- **R2.7** The operator SHALL be able to assign **each band to one of the 8
  relays, or to "none/bypass"**, via the web UI. Multiple bands MAY map to the
  same relay (one antenna serving several bands).
- **R2.8 Exclusive switching (antenna switch semantics).** At most **one relay**
  SHALL be energized at any time. When switching bands the firmware SHALL use
  **break-before-make**: de-energize the current relay, observe a short guard
  delay (configurable, default ~50 ms), then energize the new relay. This
  prevents momentarily connecting two antennas together.
- **R2.9 TX-safety.** When the radio reports a tune/transmit state (TCI tune/TX
  events), the firmware SHALL NOT change relays mid-transmission. Switching is
  deferred until the radio returns to RX. (No hot-switching an antenna relay
  under RF.)
- **R2.10 Failsafe.** On boot, WiFi loss, TCI disconnection, an unmapped band, or
  an out-of-range frequency, the firmware SHALL enter a defined safe state
  (default: all relays de-energized), reflected in the status JSON.
- **R2.11** Manual override: the operator SHALL be able to force a specific relay
  (or all-off) from the web UI / app, overriding TCI until cleared (Auto).

### 2.3 Web server / configuration

- **R3.1** The controller SHALL run an HTTP server exposing a configuration
  portal and a JSON API:

  | Route      | Method | Purpose                                          |
  |------------|--------|--------------------------------------------------|
  | `/`        | GET    | Config form: WiFi, TCI host/port, band→relay map, manual override |
  | `/save`    | POST   | Persist settings                                 |
  | `/status`  | GET    | JSON: current band, active relay, TCI/WiFi/TX/Tune, mode |
  | `/config`  | GET    | JSON: stored settings (band→relay map + per-band SO2R fallback map, relay names, radio_type, radio host/port, receiver index, mode/peer/interlock, radio2_*, switch_type, region, hostname, guard, SSID — **never** passwords) for the macOS app |
  | `/relay`   | POST   | Manual override: `set=auto\|none\|0..7`          |
  | `/discover`| GET    | Device identity + mDNS metadata + firmware version |
  | `/reboot`  | POST   | Soft reboot                                      |
  | `/interlock`| GET   | SO2R Mode A: `{role, peer_up, beats_missed, master_ant, slave_ant}` |
  | `/interlock/claim`  | POST | SO2R: slave claims antenna `ant=i` → `1`/`0`; doubles as the heartbeat (master only) |
  | `/interlock/release`| POST | SO2R: slave releases its hold / idle heartbeat (master only) |

- **R3.2** The band→relay assignment UI SHALL present each supported band with a
  dropdown of relays 1–8 plus "none/bypass", and SHALL show the live current
  band and active relay.
- **R3.3** Settings (WiFi, TCI host/port, band→relay map, hostname, OTA
  password, guard delay) SHALL be persisted in EEPROM with a CRC32 integrity
  check; corrupt/blank config falls back to safe defaults.
- **R3.4** The device SHALL be reachable via **mDNS** at `<hostname>.local`.
  Default hostname is `ANT-SW-Controller-xx`, where `xx` is a per-device suffix
  derived from the ESP8266 MAC (so multiple units coexist on one LAN); the
  operator MAY override the full hostname in the web UI. The device also
  advertises a dedicated **`_antsw._tcp`** service (TXT: version/product/host)
  for the macOS app's discovery.
- **R3.5** First-boot / no-WiFi behavior provides a SoftAP captive setup
  (`ANT-SW-Setup`, `http://192.168.4.1/`) so the operator can enter credentials.

### 2.4 OTA updates

- **R4.1** After the initial USB flash, the firmware SHALL support **OTA updates
  via ArduinoOTA over WiFi / mDNS** (STA mode only).
- **R4.2** The OTA password SHALL be configurable (default empty), settable in
  the web UI; changes take effect after reboot without USB.
- **R4.3** During an OTA update the firmware SHALL force the **safe state (all
  relays de-energized)** and disconnect the TCI client to avoid WebSocket reader
  contention with the update stack.
- **R4.4** Flash layout SHALL leave OTA headroom: built with
  `eesz=4M1M` (4 MB flash, 1 MB FS), giving a ~1.5 MB OTA slot; the sketch is
  ~41% of it.

## 3. Non-functional requirements

- **R5.1** Single firmware image, no external config files required at runtime.
- **R5.2** Responsive switching: band-change → relay-settled within a few hundred
  ms (excluding the configured guard delay).
- **R5.3** Robust: survives WiFi/TCI flaps and radio restarts without a manual
  reboot.
- **R5.4** Onboard LED (GPIO2) indicates link status (on when TCI connected).

## 4. Build & flash (firmware)

```bash
# Core + library (TCI is bundled in Controller/, not via lib manager)
arduino-cli core install esp8266:esp8266
arduino-cli lib install WebSockets

# Initial USB flash (board in flash mode: GPIO0->GND + reset)
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=4M1M \
  --upload --port /dev/cu.usbserial-XXXX Controller

# OTA update over WiFi (board already on the LAN, STA mode)
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=4M1M \
  --output-dir /tmp/antsw_build Controller
python3 ~/Library/Arduino15/packages/esp8266/hardware/esp8266/*/tools/espota.py \
  -i <board-ip> -p 8266 -f /tmp/antsw_build/Controller.ino.bin -r
```

> Note: many bare USB-TTL adapters have no DTR/RTS auto-reset wiring, so flash
> mode is **manual** (hold GPIO0→GND, tap reset) and the board must be taken out
> of flash mode (release GPIO0, reset) to run the sketch. `arduino-cli`'s
> `--port <hostname>.local` treats the name as a serial port for ESP8266, so use
> `espota.py` with the IP for OTA.

## 5. Firmware source map (`Controller/`)

```
Controller.ino     setup/loop, OTA, failsafe, serial console (wires the parts)
BandPlan.h         band list (160–6 m incl. 60 m) + frequency→band resolver
Config.h           EEPROM settings struct + CRC32 load/save, defaults, MAC hostname
RadioSource.h      abstract "band + TX" source (poll-based)
TciSource.h        RadioSource over the bundled TCI client; setRig() picks RX1/RX2
FlexSource.h       RadioSource over FlexRadio SmartSDR (TCP 4992); P1, build-verified
OutputStage.h      relay map; Relay8x1 (8×1 break-before-make) + Relay8x2 (8×2: relay i=antenna i, one HIGH per radio)
Interlock.h        SO2R MasterArbiter + SlaveClient (Mode A, debounced heartbeat) + DualResolver (Mode B)
WebPortal.h        HTTP routes + HTML config page (+ /config, /discover)
TCI.h TCI.cpp      bundled IW7DMH TCI v1.0.1, ESP8266-ported (see R2.1); parse
                   hardened: bounds-checked rtxAt() for all rtx[] access + bounded
                   sscanf reads (a garbage rtxId used to crash the ESP on band change)
RTX.h RTX.cpp      bundled IW7DMH RTX state (band edges, VFO/TRX/tune getters)
```

> The `RadioSource` / `OutputStage` split is phase **P0** of
> [docs/MULTI-RADIO-SO2R-PLAN.md](docs/MULTI-RADIO-SO2R-PLAN.md): a
> no-behaviour-change refactor so later phases can add more radio sources and an
> 8×2 output stage. **P1** added `FlexSource` (FlexRadio SmartSDR over TCP) and a
> `radio_type` config field selecting the transport (TCI / FlexRadio), with a
> v1→v2 EEPROM migration that preserves the saved band map. FlexRadio is
> build-verified only (no live rig tested yet). **P2b** added SO2R **Mode A**
> (master/slave over the LAN): a `mode` config (standalone/master/slave) with a
> v2→v3 EEPROM migration, `Interlock.h` (master `MasterArbiter` + slave
> `SlaveClient`), the `/interlock*` HTTP API, and app UI (role picker +
> dashboard interlock badge). First-come / current-holder-keeps-it interlock,
> with a **debounced bidirectional heartbeat** (slave claim/release every 2 s;
> loss declared after 3 consecutive missed beats / 6 s, so one dropped packet
> never drops an antenna; `beats_missed` in `/status`). **live-validated on two
> boards** (including heartbeat failover + recovery). **P2a** added **Mode B** (`mode=dual`): one
> board tracks both radios and drives an external **8×2** switch via `Relay8x2`
> (relay _i_ = antenna _i_; the relay for **each** receiver's antenna is energized,
> so up to **two** relays HIGH at once — interlock keeps them distinct) with an
> in-firmware `DualResolver` (first-come policy, per-radio TX-safety). A
> **per-radio TCI receiver index** (`radio_rx`/`radio2_rx`) drives a 2-receiver
> radio (SunSDR2) as SO2R from one board (both radios → same host/port, RX1/RX2).
> Config grew through v5; app gains Radio 2 + receiver pickers + dual dashboard.
> **Live-validated on a SunSDR2 + the 8×2 wiring.** **P2c** added **per-relay
> names** (config v6, `relay_name[8]`): operator-set antenna labels exposed via
> `/config`+`/save`, shown across the web UI and app instead of "R1–R8" (blank →
> default "R<n>"); v5→v6 migration preserves all settings. **P4** (multiband
> antennas, docs/MULTI-RADIO-SO2R-PLAN.md §11) began with a **per-band SO2R
> fallback antenna** (config v7, `band_relay2[]`): in Master/Slave/Dual, when a
> band's primary antenna is held by the other radio the controller switches that
> radio to the band's fallback instead of none (e.g. a HexBeam shared 20–6 m with
> a wire dipole fallback), never moving a TX radio; standalone ignores it. v6→v7
> migration preserves all settings.

## 6. Confirmed decisions

1. **Single radio only.** Track RX-1 VFO A of one radio; no dual-radio mode.
2. **Switching:** exclusive, one relay at a time, **break-before-make** (R2.8).
3. **No hot-switching** — relay changes forbidden while TX/tuning; defer to RX.
4. **Default state: de-energized** — safe/failsafe is all relays off (R2.10).
5. **60 m band included** (R2.6).
6. **Guard delay** for break-before-make defaults to ~50 ms (operator-tunable).
7. **Hostname:** `ANT-SW-Controller-xx` (`xx` = MAC-derived suffix), overridable.

---

# Part 2 — macOS app (`App/`)

SwiftUI app (and RadioPluginKit plugin) to manage multiple Antenna Switch
Controllers. Mirrors the conventions of `VU3ESV/BandPassFilterControllerApp`.

## Targets / products
- `AntennaSwitchController` (library target) → product **`AntennaSwitchControllerKit`**,
  consumed by the suite. Contains everything; only `AntennaSwitchPlugin` and the
  standalone `AntennaSwitchStandaloneApp` are `public`.
- `AntennaSwitchControllerMain` (executable) → product **`AntennaSwitchControllerApp`**
  (the standalone `.app` binary). Just calls `AntennaSwitchStandaloneApp.main()`.
  The product name deliberately differs from the library target name
  (`AntennaSwitchController`) — a product whose name equals a target's makes
  multi-arch/universal XCBuild emit "Multiple commands produce …" errors.
  `build-app.sh` also builds with `--product` so only the executable (not the
  library product too) compiles the shared target under universal builds.
- Depends on `RadioPluginKit` by **Git URL `from: "1.0.0"`** (same as the suite,
  so the whole graph resolves one identical RadioPluginKit — no path/URL clash).

## Layout (`App/Sources/AntennaSwitchController/`)
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
- `Snapshot.swift` + `ASC_DEMO` window mode — offscreen/demo rendering for the
  documentation screenshots only; not used at runtime.

## Key decisions
- **Multi-device:** `ControllersStore` persists `[Controller]` as JSON in
  `AppDefaults.store`; the plugin injects the host's namespaced defaults, the
  standalone app uses `.standard`.
- **Settings completeness:** every web-page option is in `SettingsView`. The app
  needs firmware `GET /config` to pre-fill the form; passwords are never read
  back and are only sent when the operator types a new value (blank = keep).
- `NSLocalNetworkUsageDescription` + `NSBonjourServices` in `Resources/Info.plist`
  are required for LAN/Bonjour access on macOS.

## RadioPluginKit 1.2 contract
`AntennaSwitchPlugin` implements the full 1.2 contract:
- **`manifest`** (`RadioPluginManifest`): id `antsw`, isolation `.inProcess`,
  capabilities `[.networkClient, .bonjour, .notifications]`; `metadata` derives
  from it.
- **`persistState`/`restoreState`**: saves/restores the selected controller id so
  a relaunch reopens it (`ControllersStore.restore(selection:)`).
- **Host context**: connectivity/save events reach the host's `report` (typed
  `PluginError`), `notify` (`PluginNotification`), and `setBadge` surfaces. View
  models stay RadioPluginKit-agnostic via the module-local `PluginHostBridge`
  protocol (`PluginBridge.swift`); the plugin implements it and forwards to the
  injected `PluginHost`. Standalone leaves the bridge nil (views show inline state).

## Build quirk
Global git `safe.bareRepository=explicit` blocks SwiftPM from reading fetched
checkouts. Prefix builds with
`GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.bareRepository GIT_CONFIG_VALUE_0=all`.

---

# Roadmap

- [docs/MULTI-RADIO-SO2R-PLAN.md](docs/MULTI-RADIO-SO2R-PLAN.md) — plan to support
  **multiple radio types/transports** (TCI + serial CAT via the ESP UART, with
  level converters) and **SO2R**: two radios driving an **8×2** matrix, with a
  runtime **8×1 / 8×2** mode. References `nigelfenton/shackswitch`.
