# Antenna Switch Controller — Requirements

An ESP8266-based antenna switch controller for amateur radio. It listens to a
transceiver's **TCI** server, reads the active band, and switches the matching
antenna relay automatically. A built-in web server lets the operator map each
band to a relay, and firmware can be updated **over-the-air (OTA)**.

This document is the authoritative requirements specification. Implementation
mirrors the conventions of the companion project
[VU3ESV/BandPassFilterController](https://github.com/VU3ESV/BandPassFilterController)
wherever practical (bundled TCI library, ArduinoOTA, web-config + EEPROM
persistence, arduino-cli build flow).

---

## 1. Hardware

**Target board:** ESP8266 ESP-12F 8-Channel Relay Board
([reference](https://werner.rothschopf.net/microcontroller/202108_esp8266_esp12f_relay_x8_en.htm)).

![ESP8266 ESP-12F 8-channel relay board](AntennaSwitchControllerApp/docs/images/relay-board.jpg)

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

---

## 2. Functional requirements

### 2.1 TCI client

- **R2.1** The controller SHALL act as a **TCI client** and connect to the
  radio's TCI server (WebSocket). It SHALL use **IW7DMH's TCI library
  (v1.0.1)**, bundled inside the sketch folder exactly as in
  BandPassFilterController (`TCI.h`, `TCI.cpp`, `RTX.h`, `RTX.cpp` compiled into
  the sketch, with the `RTX.h` include quoted for in-sketch compilation and the
  "Unhandled message!" log gated behind a `TCI_LOG_UNHANDLED` flag, default off).
- **R2.2** TCI server `host` and `port` SHALL be configurable via the web UI and
  persisted. Default port `50001` (ExpertSDR3); SHALL also work with SunSDR2 PRO
  and other TCI-compliant servers.
- **R2.3** The controller SHALL track the active **band** from the radio's
  RX-1 VFO A frequency (single-radio operation) and derive the band from the
  frequency.
- **R2.4** On a band change, the controller SHALL select the relay mapped to the
  new band per the operator's configuration (§2.2).
- **R2.5** The controller SHOULD auto-reconnect to the TCI server on connection
  loss, with backoff, without operator intervention.

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
  events), the firmware SHOULD NOT change relays mid-transmission. Switching
  SHALL be deferred until the radio returns to RX, or performed only on a clean
  band change while in RX. (Hot-switching an antenna relay under RF must be
  avoided.)
- **R2.10 Failsafe.** On boot, WiFi loss, TCI disconnection, an unmapped band, or
  an out-of-range frequency, the firmware SHALL enter a defined safe state
  (default: all relays de-energized). The safe state SHOULD be operator-aware
  (shown in the web UI / status JSON).
- **R2.11** Manual override: the operator SHALL be able to force a specific relay
  (or all-off) from the web UI for testing/maintenance, overriding TCI until
  cleared.

### 2.3 Web server / configuration

- **R3.1** The controller SHALL run an HTTP server exposing a configuration
  portal. Suggested routes (mirroring BandPassFilterController):

  | Route      | Method | Purpose                                          |
  |------------|--------|--------------------------------------------------|
  | `/`        | GET    | Config form: WiFi, TCI host/port, band→relay map, manual override |
  | `/save`    | POST   | Persist settings                                 |
  | `/status`  | GET    | JSON: current band, active relay, TCI state, safe-state flag |
  | `/config`  | GET    | JSON: stored settings (band→relay map, TCI host/port, region, hostname, guard, SSID — **never** passwords) for the macOS app to read |
  | `/relay`   | POST   | Manual force relay N / all-off                   |
  | `/discover`| GET    | Device identity + mDNS metadata + firmware version |
  | `/reboot`  | POST   | Soft reboot                                      |

- **R3.2** The band→relay assignment UI SHALL present each supported band with a
  dropdown of relays 1–8 plus "none/bypass", and SHALL show the live current
  band and active relay.
- **R3.3** Settings (WiFi, TCI host/port, band→relay map, hostname, OTA
  password, guard delay) SHALL be persisted in EEPROM with a CRC32 integrity
  check; corrupt/blank config falls back to safe defaults.
- **R3.4** The device SHALL be reachable via **mDNS** at `<hostname>.local`.
  Default hostname is `ANT-SW-Controller-xx`, where `xx` is a per-device suffix
  derived from the last byte(s) of the ESP8266 MAC (so multiple units coexist on
  one LAN); the operator MAY override the full hostname in the web UI.
- **R3.5** First-boot / no-WiFi behavior SHOULD provide a SoftAP captive setup so
  the operator can enter WiFi credentials.

### 2.4 OTA updates

- **R4.1** After the initial USB flash, the firmware SHALL support **OTA updates
  via ArduinoOTA over WiFi / mDNS**, as in BandPassFilterController.
- **R4.2** The OTA password SHALL be configurable (default empty), settable in
  the sketch and/or web UI; changes take effect after reboot without USB.
- **R4.3** During an OTA update the firmware SHALL force the **safe state (all
  relays de-energized)** and SHALL disconnect the TCI client to avoid WebSocket
  reader contention with the update stack.
- **R4.4** Flash/partition layout SHALL leave sufficient OTA headroom for the
  ESP8266 (e.g. 4 MB flash with an OTA-capable layout; sketch must fit in under
  half of usable flash for the OTA image).

### 2.5 Companion macOS app

- **R6.1** A macOS app ([AntennaSwitchControllerApp/](AntennaSwitchControllerApp))
  SHALL manage **multiple** controllers: add by IP/hostname, auto-discover via
  Bonjour `_antsw._tcp`, monitor status, and expose **every** web-page option.
- **R6.2** The app SHALL be a [RadioPluginKit](https://github.com/VU3ESV/RadioPluginKit)
  plugin (product `AntennaSwitchControllerKit`, adapter `AntennaSwitchPlugin`),
  runnable standalone and hostable in
  [AmateurRadioApps](https://github.com/VU3ESV/AmateurRadioApps).
- **R6.3** To support the app, the firmware advertises a dedicated `_antsw._tcp`
  mDNS service (TXT: version/product/host) and serves `GET /config` (§2.3 table).

---

## 3. Non-functional requirements

- **R5.1** Single firmware image, no external config files required at runtime.
- **R5.2** Responsive switching: band-change → relay-settled within a few hundred
  ms (excluding the configured guard delay).
- **R5.3** Robust: survives WiFi/TCI flaps and radio restarts without a manual
  reboot.
- **R5.4** Onboard LED (GPIO2) SHOULD indicate status (e.g. WiFi/TCI connected,
  activity) where it does not conflict with relay use.

---

## 4. Build & toolchain

Use **arduino-cli** with the ESP8266 core (mirroring the reference project's
flow, adapted from esp32 → esp8266).

```bash
# Core + libraries
arduino-cli core install esp8266:esp8266
arduino-cli lib install WebSockets
# (TCI library is bundled in the sketch folder, not installed via lib manager)

# Initial USB flash (generic ESP-12F / NodeMCU-class board, 4MB flash)
arduino-cli compile \
  --fqbn esp8266:esp8266:generic:eesz=4M2M,baud=115200 \
  --upload --port /dev/cu.usbserial-XXXX ESP8266_ANT_SW

# OTA update over mDNS (after first flash)
arduino-cli compile \
  --fqbn esp8266:esp8266:generic:eesz=4M2M,baud=115200 \
  --upload --port ANT-SW.local ESP8266_ANT_SW
```

> The exact `--fqbn` board id and `eesz` (flash/OTA split) will be finalized when
> the board is in hand; `4M2M` (4 MB flash, 2 MB FS) or a no-FS / larger-OTA
> split satisfies R4.4.

---

## 5. Suggested project structure

```
ESP8266_ANT_SW/              (main sketch folder)
  ESP8266_ANT_SW.ino
  TCI.h  TCI.cpp  RTX.h  RTX.cpp   (bundled IW7DMH TCI v1.0.1)
  config.*                          (EEPROM schema, CRC32)
  web.*                             (HTTP routes, HTML)
docs/
  HARDWARE.md        (GPIO map, boot caveats, wiring, BOM)
  CONFIGURATION.md   (EEPROM schema, web routes)
  RADIO_PROTOCOLS.md (TCI details, tested servers)
README.md
CLAUDE.md            (this file)
LICENSE              (MIT)
```

---

## 6. Confirmed decisions

1. **Single radio only.** Track RX-1 VFO A of one radio; no dual-radio mode.
2. **Switching:** exclusive, one relay at a time, **break-before-make** (R2.8).
3. **No hot-switching** — relay changes are forbidden while the radio is
   transmitting/tuning; defer until RX (R2.9).
4. **Default state: de-energized** — the safe/failsafe state is all relays off
   (antenna disconnected), not a designated default antenna (R2.10).
5. **60 m band included** (R2.6).
6. **Guard delay** for break-before-make defaults to ~50 ms (operator-tunable).
7. **Hostname:** `ANT-SW-Controller-xx` (`xx` = MAC-derived suffix), overridable
   in the web UI (R3.4).
