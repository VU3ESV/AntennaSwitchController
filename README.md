# Antenna Switch Controller

[![CI](https://github.com/VU3ESV/AntennaSwitchController/actions/workflows/ci.yml/badge.svg)](https://github.com/VU3ESV/AntennaSwitchController/actions/workflows/ci.yml)
[![Release](https://github.com/VU3ESV/AntennaSwitchController/actions/workflows/release.yml/badge.svg)](https://github.com/VU3ESV/AntennaSwitchController/actions/workflows/release.yml)

A TCI-driven **antenna switch** for amateur radio: an ESP8266 controller that
listens to the radio's TCI server, reads the active band, and switches the
matching antenna relay automatically — plus a **macOS app** to manage multiple
controllers from one window.

<p align="center">
  <img src="App/docs/images/app-icon.png" width="120" alt="App icon">
</p>

This repo has two parts:

| Folder | What |
|--------|------|
| [`Controller/`](Controller/) | ESP8266 firmware (Arduino sketch) for the [ESP-12F 8-channel relay board](https://werner.rothschopf.net/microcontroller/202108_esp8266_esp12f_relay_x8_en.htm) |
| [`App/`](App/) | macOS SwiftUI app + [RadioPluginKit](https://github.com/VU3ESV/RadioPluginKit) plugin (hostable in [AmateurRadioApps](https://github.com/VU3ESV/AmateurRadioApps)) |

See [CLAUDE.md](CLAUDE.md) for the full requirements/architecture spec.

---

## The Controller (firmware)

Runs on the ESP8266 ESP-12F 8-channel relay board. It connects to the radio's
**TCI** server (using IW7DMH's TCI library, bundled and ported to ESP8266),
derives the band from RX-1 VFO A, and energizes the mapped antenna relay with
exclusive **break-before-make** switching — never two antennas at once, and
never switching while transmitting. A web portal maps each band to a relay;
firmware updates go **over-the-air**.

<p align="center">
  <img src="App/docs/images/relay-board.jpg" width="460" alt="ESP8266 ESP-12F 8-channel relay board">
</p>

**Highlights**
- TCI client (ExpertSDR3 / SunSDR2 PRO / TCI-compliant), single radio, RX-1 VFO A.
- 11 bands (160 m–6 m incl. 60 m); each band → one of 8 relays or none/bypass.
- Exclusive, break-before-make switching (~50 ms guard); deferred during TX/tune.
- Failsafe: all relays de-energized on boot / WiFi loss / TCI loss / unmapped band.
- Web config portal, EEPROM + CRC32 persistence, mDNS, SoftAP first-run setup.
- ArduinoOTA updates; `_antsw._tcp` Bonjour service + `/config` JSON for the app.

### Build & flash

```bash
arduino-cli core install esp8266:esp8266
arduino-cli lib install WebSockets          # TCI lib is bundled in Controller/

# Initial USB flash (board in flash mode: GPIO0->GND + reset)
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=4M1M \
  --upload --port /dev/cu.usbserial-XXXX Controller

# OTA update (board already on the LAN)
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=4M1M \
  --output-dir /tmp/antsw_build Controller
python3 ~/Library/Arduino15/packages/esp8266/hardware/esp8266/*/tools/espota.py \
  -i <board-ip> -p 8266 -f /tmp/antsw_build/Controller.ino.bin -r
```

On first boot (no WiFi saved) the controller starts a setup AP **`ANT-SW-Setup`**
→ open `http://192.168.4.1/` to enter WiFi + TCI settings. After that it joins
your LAN as `ANT-SW-Controller-xx.local`.

> Many bare USB-TTL adapters lack DTR/RTS auto-reset, so flash mode is manual
> (hold GPIO0→GND, tap reset) and you must release GPIO0 + reset to run the sketch.

---

## The App (macOS)

Manage **multiple** controllers on your network — add by IP/hostname or pick them
up automatically via Bonjour, then monitor and fully configure each one. Every
option from the controller's web page is available natively.

**Dashboard** — live band, frequency, active relay, mode, and WiFi/TCI/TX/Tune
status (here: controller `2F` connected, on 40 m / 7.140 MHz, relay R2, Auto/TCI):

![App dashboard](App/docs/images/app-dashboard.png)

**Controls** — manual override: force any relay, All Off, or return to Auto (TCI):

![App controls](App/docs/images/app-controls.png)

**Settings** — every web-page option: WiFi, TCI host/port + IARU region, the full
band→relay map, hostname, OTA password, break-before-make guard:

![App settings](App/docs/images/app-settings.png)

**Add by IP & auto-discovery** — the sidebar lists saved controllers and any found
on the network; add one by IP or pick a discovered unit:

![Add and discover](App/docs/images/app-empty-discovery.png)

### The controller's built-in web page

The app mirrors the controller's own web portal (same `/config`, `/save`,
`/status`, `/relay` routes), shown here for reference:

<p align="center">
  <img src="App/docs/images/web-portal.png" width="420" alt="Controller web portal">
</p>

### Build & run (standalone)

```bash
cd App
swift build                              # debug
./build-app.sh                           # release → dist/Antenna Switch Controller.app
open "dist/Antenna Switch Controller.app"
```

> If your global git sets `safe.bareRepository=explicit`, prefix builds with
> `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.bareRepository GIT_CONFIG_VALUE_0=all`
> so SwiftPM can read the fetched RadioPluginKit checkout.

### Install a release build (unsigned app)

Prebuilt **universal** (Apple Silicon + Intel) `.app` bundles are attached to each
[GitHub Release](https://github.com/VU3ESV/AntennaSwitchController/releases).

The app is **not code-signed or notarized** (no Apple Developer ID), so when you
copy it to another Mac, Gatekeeper quarantines it and refuses to open it
("…is damaged" / "cannot be opened"). Clear the quarantine flag once:

```bash
# 1. Unzip and move the app to /Applications, then:
xattr -dr com.apple.quarantine "/Applications/Antenna Switch Controller.app"
# 2. Open it (first launch can also be done via right-click → Open):
open "/Applications/Antenna Switch Controller.app"
```

`xattr -dr com.apple.quarantine …` recursively removes the
`com.apple.quarantine` extended attribute macOS adds to downloaded files; without
it Gatekeeper blocks an unsigned, un-notarized bundle. On first launch, allow
**Local Network** access so discovery and controller communication work.

### Releases & CI

- **CI** ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) builds the macOS
  app (debug + release `.app`) on every push/PR, and compiles the ESP8266 firmware
  with `arduino-cli`.
- **Release** ([`.github/workflows/release.yml`](.github/workflows/release.yml))
  triggers on a `v*` tag: it builds the universal `.app`, zips it with `ditto`,
  and publishes a GitHub Release with the zip + install instructions. Cut one with:
  ```bash
  git tag v1.0.0 && git push origin v1.0.0
  ```

### Host inside the Amateur Radio Suite

Verified building and running as a tab ("Antenna Switch") in the
`AmateurRadioApps` container, alongside LP-700, LP-100A, and Band Pass Filter:

![Suite integration](App/docs/images/suite-integration.png)

The package exposes the plugin product `AntennaSwitchControllerKit` and the
adapter `AntennaSwitchPlugin: RadioPlugin`. With this repo checked out next to
`AmateurRadioApps`, make the same two edits used for the other plugins:

**1. `AmateurRadioApps/Package.swift`** — add the dependency and product:

```swift
dependencies: [
    .package(url: "https://github.com/VU3ESV/RadioPluginKit.git", from: "1.0.0"),
    // …existing plugin apps…
    .package(path: "../AntennaSwitchController/App"),
],
targets: [
    .executableTarget(
        name: "RadioSuite",
        dependencies: [
            .product(name: "RadioPluginKit", package: "RadioPluginKit"),
            // …existing…
            .product(name: "AntennaSwitchControllerKit", package: "App"),
        ],
        path: "Sources/RadioSuite"
    ),
]
```

**2. `AmateurRadioApps/Sources/RadioSuite/PluginRegistry.swift`** — import and register:

```swift
import AntennaSwitchController            // the module (target), not the product
// …
static func all(host: PluginHost) -> [any RadioPlugin] {
    [
        LP700Plugin(host: host),
        LP100APlugin(host: host),
        BPFPlugin(host: host),
        AntennaSwitchPlugin(host: host),   // ← added
    ]
}
```

The plugin uses the host-provided isolated `UserDefaults`
(`host.defaults(for: "antsw")`) for its saved controller list, so it never
collides with other plugins.

### Controller HTTP API used by the app

| Route | Method | Used for |
|-------|--------|----------|
| `/discover` | GET | identity (device, firmware version, relays) |
| `/status`   | GET | live state (band, active relay, TCI/WiFi/TX…) |
| `/config`   | GET | stored settings → Settings form (no passwords) |
| `/save`     | POST (form) | persist settings |
| `/relay?set=auto\|none\|0..7` | POST | manual override |
| `/reboot`   | POST | soft reboot |

Discovery is via the firmware's `_antsw._tcp` Bonjour service (TXT: version,
product, host).
