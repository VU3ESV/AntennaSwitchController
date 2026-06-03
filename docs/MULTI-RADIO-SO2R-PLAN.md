# Plan — Multi-Radio Connectivity & SO2R

**Status:** proposal / design plan (not yet implemented). Upgrade the Antenna
Switch Controller from a single-radio, TCI-only, 8-relay switch into a controller
that:

1. supports **multiple radio types over multiple transports** — **TCI** (WiFi /
   WebSocket, as today), **network CAT / TCP** (e.g. FlexRadio SmartSDR 4992),
   and **serial CAT** (Kenwood / Yaesu / Icom CI-V) on the ESP's one UART (with
   the level converters in §4);
2. supports **SO2R (two radios)** in **two hardware topologies**, selectable in
   config:
   - **Mode A — Master/Slave:** two standard ESP-12F **8×1** controllers, each
     driving one radio, plus a **per-band 1×2 antenna switch** for high port-to-
     port **isolation**. Master = Radio 1, Slave = Radio 2, coordinated over the
     LAN.
   - **Mode B — Single dual-radio:** **one** controller tracks **both radios** and
     drives an external **8×2 antenna switch** by **direct relay control** — the
     controller's 8 onboard relays wire straight to the switch's control inputs
     (no slave, no BCD/serial encoding).
3. is **configurable at runtime** per unit as **Standalone (8×1)**, **Master**,
   **Slave**, or **Dual**.

Grounded in the companion TCI SO2R pattern (`VU3ESV/BandPassFilterController*`,
two RTX slots over TCI; BCD output to external switching) and **G0JKN ShackSwitch**
(`nigelfenton/shackswitch`) — we adopt its SO2R interlock semantics and band→
antenna map. The earlier MCP23017 / ESP32 idea is **dropped** in favour of the two
topologies above, both on the **standard ESP-12F board**.

---

## 1. What we keep, what changes

Today (see [CLAUDE.md](../CLAUDE.md)): one TCI radio → band → one of 8 relays,
exclusive break-before-make, on the ESP-12F board.

| Today | Becomes |
|---|---|
| single TCI client | 1–2 **RadioSource**s per unit (TCI / TCP / serial), §2 |
| `AntennaSwitch.h` (one 1-of-8 selector) | unchanged for 8×1; an **output stage** abstraction for Mode B's 8×2 |
| — | a **role/mode** (Standalone / Master / Slave / Dual) + interlock |
| `band_relay[NUM_BANDS]` | a **band→relay/antenna map** + role + association |
| single status JSON | adds role, peer/2nd-radio state, **interlock** |

---

## 2. Radio-source abstraction & the one-serial-port constraint

A radio is **a source of "current band + TX state"**:

```
        ┌──────────── RadioSource ────────────┐
        │ band() -> Band?  isTx() -> Bool  process() │
        └──────────────────────────────────────┘
          ▲                ▲                  ▲
     TciSource       NetCatSource        SerialCatSource
     WiFi/WebSocket  TCP (e.g. Flex      UART0, protocol =
                     SmartSDR 4992)      Kenwood|Yaesu|Icom CI-V
```

- CAT is **read-only** (poll frequency → `freqToBand()`; never command the radio),
  so drivers stay tiny. The bundled TCI lib already has **two RTX slots** — handy
  for two TCI radios (shared or dual server, as in BPF).

#### Multi-receiver radios (e.g. SunSDR2) — *implemented*
A single radio with **two receivers** (SunSDR2 PRO, ExpertSDR multi-RX) exposes
them over TCI as **two RTX slots** (`rtx[0]` = RX1, `rtx[1]` = RX2). `TciSource`
takes a **receiver index** (`setRig(0|1)`), so Mode B drives such a radio as
SO2R from **one board**: radio 1 and radio 2 point at the **same host/port**,
radio 1 = RX1, radio 2 = RX2. Each `TciSource` opens its own TCI connection and
reads its rig — verified working with two simultaneous clients on one SunSDR2
(RX1 40 m → antenna A, RX2 10 m → antenna B). Config: `radio_rx` / `radio2_rx`.

### ⚠️ One hardware serial port per board
The ESP-12F board has exactly **one usable UART** (UART0, GPIO1/3) — the relays
occupy the other GPIOs and UART1/GPIO2 is TX-only. Therefore **a single board can
host at most one serial-CAT radio.** This shapes the valid SO2R radio combos:

| Topology | Radios | Serial radios possible | Valid transport combos |
|---|---|---|---|
| Standalone (8×1) | 1 | 1 (this board's UART) | TCI **or** TCP **or** serial |
| **Mode A** Master + Slave | 2 (one per board) | **2** — one per board | any per board (incl. **serial + serial**) |
| **Mode B** Single dual-radio | 2 (one board) | **1** | TCI+TCI · TCP+TCI · **serial + (TCI/TCP)** — **NOT serial + serial** |

So if you need **two serial-CAT radios**, you must use **Mode A** (each board has
its own UART). Mode B requires at least one radio on a network transport
(TCI/TCP).

---

## 3. SO2R deployment modes

### Mode A — Master/Slave (two 8×1 boards + per-band 1×2 switches, best isolation)
Each radio gets its **own** 8×1 controller. For each band/antenna a small **1×2
antenna switch** selects which radio is connected, giving real RF **isolation**
between the two stations (the two controllers are galvanically/RF separated).

```
   Radio 1 ── MASTER 8×1 ──┐
                           ├──► per-antenna 1×2 switch ──► antenna i   (only one radio at a time)
   Radio 2 ── SLAVE  8×1 ──┘
        coordinated over LAN so both never select antenna i together
```

- Master tracks Radio 1, Slave tracks Radio 2; **each board = one radio**, so
  **two serial radios are fine** (one UART each).
- **Direct relay control:** each controller's **8 relays wire directly to its own
  external 8×1 switch** (relay _i_ = antenna _i_, exclusive 1-of-8) — exactly
  today's output, just feeding an external RF switch.
- The relay-index → antenna correspondence is identical on both boards (M3 and S3
  feed the same 1×2 switch / antenna).
- Interlock is enforced **over the LAN** (§6.1): an antenna held by one radio is
  denied to the other; the 1×2 switches give the physical isolation.

### Mode B — Single dual-radio board + external 8×2 antenna switch (no slave)
**One** controller tracks **both radios** and drives an external **8×2 antenna
switch** (the RF matrix is the external switch; the controller is the brain).

```
   Radio 1 ─┐
            ├─► ONE controller (2 RadioSources, interlock in firmware)
   Radio 2 ─┘          │ control output (see §4.3)
                       ▼
              external 8×2 antenna switch  ──►  8 antennas
```

- **Direct relay control:** the controller's **8 relays wire straight to the 8×2
  switch's control inputs**. The firmware computes the relay pattern from both
  radios' bands + the interlock (trivial — one MCU sees both radios) and writes
  the 8 relays.
- Radio connectivity is bounded by the **one-UART rule** (≤1 serial; see §2).
- **Confirmed wiring (§10.1):** **relay _i_ = antenna _i_**, and the relay for
  **each receiver's selected antenna is energized** — so **up to two relays are
  HIGH at once** (one per radio), the switch routing each energized antenna to
  its radio. The interlock guarantees the two are different antennas, so the two
  radios never share one. If both radios resolve to the same antenna, first-come
  keeps it with one radio and the other gets none (one relay HIGH).

---

## 4. Hardware

### 4.1 Boards
- **Standalone / Master / Slave:** the standard ESP-12F 8-relay board, one radio.
- **Mode A** adds a second identical board + **per-band 1×2 antenna switches**
  (relay or coax-relay type) for isolation.
- **Mode B** is one board driving an **external 8×2 antenna switch**.
- No MCP23017, no ESP32 — every brain is the standard ESP-12F board.

### 4.2 Serial CAT + level converters (per board; REQUIRED — ESP GPIO are 3.3 V, **not** 5 V tolerant)
A board's radio can be TCI/TCP (network) or one serial-CAT radio on **UART0**
(GPIO1/3) — which gives up the serial console (debug → UART1/GPIO2 TX-only) and
shares the flash/console pins (disconnect CAT or flash in boot mode).

| Radio family | CAT electrical | Converter |
|---|---|---|
| **Kenwood** (TS-590/890S/480), **Elecraft** K3/K4, **Yaesu** FT-DX10/991A (DB9) | **RS-232** (±5…±12 V) | **MAX3232** (RS-232 ↔ 3.3 V TTL); radio TX→ESP RX, ESP TX→radio RX, common GND |
| **Icom CI-V** (IC-7300/9700/705/7610) | **TTL** single-wire, **half-duplex**, open-collector at radio Vcc (often 5 V), 3.5 mm jack | **BSS138 3.3 V↔5 V bidirectional shifter** + a **CI-V bus interface** (one shared data line, pull-up; CT-17-style buffer). ESP TX+RX tie to the single CI-V line via the buffer. |
| **Yaesu** older (FT-817/818/857/897) | **TTL at radio Vcc** (often 5 V), mini-DIN | **BSS138 level shifter** (no RS-232 driver needed) |
| **USB-CAT-only** radios (some FT-891/991A via CP210x) | USB device | **Not directly attachable** — ESP can't host USB. Use the radio's RS-232/TTL jack, or its network/TCI/TCP path. |
| **FlexRadio / any TCI / network CAT** | Ethernet/WiFi | **None** — software transport |

ESP GPIO are **3.3 V, not 5 V tolerant** (the shifter/MAX3232 is mandatory); a
**common ground** is required.

### 4.3 Mode B — driving the external 8×2 switch (direct relay control)
The controller's **8 onboard relays wire directly** to the 8×2 switch's control
inputs — no BCD, no serial. The firmware computes the 8-relay pattern from both
radios' bands + the interlock and writes it.

With 8 direct (unencoded) lines, the switch is a **per-antenna A/B select** type:
each of the 8 lines routes one antenna to **Radio 1** or **Radio 2** (8× SPDT).
The firmware sets Radio 1's chosen antenna to its side and Radio 2's to the other,
with interlock ensuring the two radios never resolve to the same antenna. (A fully
independent "2 × exclusive 1-of-8" matrix would need 16 lines; that is out of
scope for direct 8-line control — see §10.1.)

The relay→line correspondence is fixed in config (`relay i = antenna i`), matching
the external switch's wiring.

---

## 5. Configuration model (per unit)

```jsonc
{
  "mode": "standalone",            // "standalone" | "master" | "slave" | "dual"
  "peer": {                        // Mode A only
    "master_address": "ANT-SW-Controller-1F.local",  // REQUIRED when mode == slave
    "slave_address":  ""           // master may learn this from the slave's claims
  },
  "interlock": { "priority": "radio1", "policy": "first_come", "on_peer_loss": "safe" },
  "radios": [                      // 1 entry (standalone/master/slave) or 2 (dual)
    { /* RadioSource — TCI / TCP / serial; see §2 */ },
    { /* 2nd radio — dual mode only; obeys the one-serial rule */ }
  ],
  "switch_type": "8x1",            // "8x1" (1-of-8 exclusive) | "8x2" (per-antenna A/B, dual mode)
  "antenna_map": {                 // band -> relay/antenna index (0..7), -1 = none/bypass
    "primary":   [0,1,2,3,-1,4,5,6,-1,7,-1],
    "secondary": [ ... ]           // per-band fallback used on interlock conflict
  },
  "hostname": "...", "guard_ms": 50, "ota_pass": ""
}
```

- **Standalone** = today's behaviour (v1 `band_relay[]` migrates into
  `antenna_map.primary`; `mode` defaults to standalone).
- The web page **must** require `peer.master_address` when `mode == slave`, and a
  valid 2nd radio + `switch_type == "8x2"` when `mode == dual`.
- **EEPROM**: bump `CFG_MAGIC`/version, keep CRC32, add a v1→v2 migration; corrupt/
  blank → safe defaults (standalone).

---

## 6. Interlock — *an antenna in use by one radio is never available to the other*

### 6.1 Mode A (Master/Slave, over the LAN; master = arbiter, master priority)
Both boards run an HTTP server; add a tiny interlock API (ESP8266 is single-
threaded, so the master serializes peer requests and its own loop — no locking):

| Endpoint | On | Purpose |
|---|---|---|
| `GET /interlock` | both | `{role, peer_up, beats_missed, master_ant, slave_ant}` (`*_ant` = relay idx or −1) |
| `POST /interlock/claim?ant=i` | master | slave asks for antenna i → body `1` (granted) / `0` (denied iff `i == master_ant`) |
| `POST /interlock/release` | master | slave gives up its hold → body `1` |

- **Slave** (Radio 2 → desired `d`): `claim(d)` → granted ⇒ break-before-make to
  `d`; denied ⇒ fall back to none (per-band `secondary[d]` is a P3 addition).
  `d < 0` ⇒ `release`. Master unreachable ⇒ **failsafe** per `on_peer_loss`
  (`safe` → none, default; `hold` → keep last).
- **Master** (Radio 1 → `d`): arbitrates its own radio against the slave's
  holding (`slave_ant`). First-come (default): if the slave holds `d`, the
  master falls back to none; otherwise it takes `d`. (Priority policy: the
  master preempts and the slave is denied on its next beat.) The master is the
  single arbiter, so the two boards never resolve to the same antenna index.

#### Heartbeat & failure detection (as built)
The slave's `claim`/`release` **doubles as the heartbeat** — it fires whenever
the desired antenna changes and at least every `HB_BEAT_MS` (2 s). Loss is
declared only after `HB_MAX_MISS` (3) **consecutive** missed beats (a 6 s
window), so a single dropped packet on a lossy LAN never drops an antenna:

- **Slave → master health.** Each beat is a short-timeout (600 ms) HTTP POST.
  A failed beat is *tolerated* — the slave holds its current antenna — until 3
  in a row, then it declares the master lost and applies `on_peer_loss`. A
  successful beat resets the miss counter and (if needed) re-establishes the
  grant. `beats_missed` is exposed in `/status`.
- **Master → slave health.** The master ages the slave's last contact; after
  the same 6 s window it treats the slave as gone, frees `slave_ant` (so the
  master may use that antenna), and reports `peer_up = 0`.
- Recovery is automatic: the first successful beat after the link returns
  re-claims the antenna and clears `beats_missed`.

### 6.2 Mode B (single board, in firmware)
One MCU sees both radios, so interlock is a local decision each control tick:
compute desired antenna for Radio 1 and Radio 2; if they collide, the
**current holder keeps it** (first-come) and the other takes `secondary[]` →
none; then emit both via the OutputStage. Per-radio TX-safety and
break-before-make still apply.

**CONFIRMED policy: first-come / current-holder-keeps-it** (`policy =
first_come`, the default) — the radio already on the contended antenna is not
kicked off mid-QSO; the newcomer falls back. A strict `priority` policy
(`interlock.priority = radio1`) remains available as an alternative.

---

## 7. Firmware changes (`Controller/`)

- **`RadioSource.h`** + **`cat/`** (`CatKenwood`, `CatYaesu`, `CatIcomCIV`) over a
  `Transport` (`Uart0Transport`, `TcpTransport`); `TciSource`, `NetCatSource`.
- **`OutputStage.h`** — drives the **8 onboard relays directly**: `Relay8x1`
  (exclusive 1-of-8, today / standalone / master / slave) and `Relay8x2`
  (per-antenna A/B pattern for Mode B's external 8×2 switch). No encoding layer.
- **`Interlock.h` / `PeerLink.h`** — Mode A LAN arbiter/heartbeat (§6.1) and Mode B
  in-firmware resolver (§6.2).
- **`Config.h`** — `mode`, `peer`, `interlock`, `radios[1..2]`, `output`,
  `antenna_map`; v1→v2 migration.
- **`WebPortal.h`** — **mode selector** (Standalone/Master/Slave/Dual): Slave
  requires a **master association** (mDNS-pick); Dual requires a **2nd radio**
  (validated against the one-serial rule) + **output** config; band→antenna map
  grid; `/interlock*` endpoints; `/status` + `/config` gain the new fields.
- **mDNS `_antsw._tcp` TXT** — advertise `mode` and (slave) `master`.

Same ESP8266 sketch; the mode selects the topology.

---

## 8. macOS app / plugin changes (`App/`)

- **Settings → Mode**: Standalone / Master / Slave / Dual; Slave → pick the master
  from discovery; Dual → configure a 2nd radio (UI enforces the one-serial rule)
  + the 8×2 output.
- **Settings → Radios**: per radio — TCI / TCP / serial (protocol, baud, CI-V
  addr / host:port).
- **Settings → Antenna Map**: band→antenna grid (primary + secondary).
- **Dashboard / SO2R view**: Input A (Radio 1) / Input B (Radio 2) with band +
  antenna and a live **interlock** badge (reuse RadioPluginUI components + host
  `report`/`notify`). A Master/Slave **pair** is shown as one SO2R system; a Dual
  unit shows both inputs from one device.
- **Models**: `DeviceConfig` (mode, peer, interlock, radios[], output, maps) and
  `DeviceStatus` (mode, peer/2nd radio, interlock).

---

## 9. Phased roadmap

1. ✅ **P0 — Refactor, no behaviour change.** `RadioSource` + `OutputStage`;
   single-TCI 8×1 preserved. *(done — commit 4f28fe6)*
2. ◑ **P1 — Multi-transport (per board).** `FlexSource` (FlexRadio SmartSDR TCP)
   + `radio_type` selector, app radio-source picker. *(done — 5b523e5;
   build-verified. Serial CAT — `SerialCatSource` Kenwood/Icom/Yaesu + level
   converters + HARDWARE.md — still TODO.)*
3. ✅ **P2a — Mode B (single dual-radio + 8×2).** 2nd `RadioSource`, in-firmware
   `DualResolver`, `Relay8x2` per-antenna A/B (8× SPDT); app Dual UI. *(done;
   build-verified — no 8×2 switch wired yet.)*
4. ✅ **P2b — Mode A (Master/Slave + 1×2).** `Interlock.h` LAN arbiter (§6.1),
   role selector + master association; app interlock view. *(done — a1bd725;
   **live-validated on two boards**.)*
5. **P3 — Polish.** Per-band secondary fallback, mDNS master-pick in the app,
   1-master-N-slaves, serial-CAT (carryover from P1), optional Antenna
   Genius/AetherSDR emulator.
6. **P4 — Multiband antennas (HexBeam / tribanders).** A first-class
   antenna-centric model + band-coverage validation, per-band secondary fallback
   wired into the resolver (folds in the P3 item), and triplexer "antenna group"
   modelling so a multiband antenna can be shared by both radios on different
   bands. **See §11.** *(proposed — the basic single-feed case already works via
   the many-to-one band→relay map; this phase is UX + SO2R contention.)*

---

## 10. Open questions (confirm before P2)

1. ~~**Mode B 8×2 control lines**~~ **CONFIRMED (hardware-verified):** relay _i_
   = antenna _i_, and the relay for **each receiver's antenna is energized** —
   **up to two relays HIGH at once** (one per radio), the switch routing each
   energized antenna to its radio. `Relay8x2` drives the set {Radio 1 antenna,
   Radio 2 antenna} with per-line break-before-make; the interlock keeps the two
   distinct (both want the same → first-come, one relay HIGH). *(The earlier
   "8× SPDT, one relay" A/B assumption was wrong — corrected after live test.)*
2. **Mode A 1×2 switches** — confirm a per-band/per-antenna 1×2 switch for
   isolation, and how it's wired/driven (passive, or driven by the same relay
   index on each board).
3. ~~**Interlock priority/policy**~~ **CONFIRMED: first-come / current-holder-
   keeps-it** is the default (§6.2) — never kick a radio off the antenna it is
   already using; the newcomer falls back to `secondary[]` → none.
4. **On peer loss (Mode A)** — slave → **safe/none** (proposed) vs. hold-last.
5. **Per-radio vs shared antenna map.**
6. **CAT families first** (Icom CI-V / Kenwood / Yaesu) and whether FlexRadio
   SmartSDR (TCP 4992) is in P1.

---

## 11. Multiband antennas (HexBeam, tribanders, fan dipoles)

A **multiband antenna** is one physical antenna usable on several bands — e.g. a
**HexBeam** (commonly 20·17·15·12·10·6 m, some variants 40·30·…·6 m), a
**tribander** (20·15·10 m), or a **fan/parallel dipole**. The operator wants to
assign *one* antenna to a *range* of bands and have SO2R behave sensibly when
both radios land on bands that the same antenna covers.

### 11.1 What already works today (single radio)
The band→relay map is **many-to-one** (R2.7) and relays are **named** (P2c). So a
single-feed HexBeam is configured *right now*, with no firmware change, by
pointing every band it covers at the same relay and naming it:

| Band | 160 | 80 | 60 | 40 | 30 | **20** | **17** | **15** | **12** | **10** | **6** |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Relay | 0 | 0 | -1 | 1 | -1 | **2** | **2** | **2** | **2** | **2** | **2** |
| Name | 80m Dipole | 80m Dipole | — | 40m Vert | — | **HexBeam** | **HexBeam** | **HexBeam** | **HexBeam** | **HexBeam** | **HexBeam** |

A "40 m–6 m" HexBeam is the same idea with 40/30 also pointing at relay 2. For a
**standalone (single-radio) unit this is complete** — the multiband requirement
is met by the existing model. Everything below is about making it *first-class*
(model + UX) and, crucially, about **SO2R**.

### 11.2 The hard part — shared-antenna contention in SO2R
One feedline carries **one radio at a time**. With *monoband* antennas two radios
on different bands rarely collide; with *multiband* antennas, two radios on
**different bands that the same antenna covers** now collide on **one physical
port** (e.g. Radio 1 on 20 m and Radio 2 on 15 m both resolve to the HexBeam).

- **Safety is already handled.** The interlock resolves by **relay/port index**,
  so both bands → the same relay → the second radio is **denied** (Mode A §6.1 /
  Mode B §6.2), and break-before-make + exclusive energizing means the two radios
  are never keyed onto one antenna. **No RF hazard is introduced.** ⚠️ Never
  feed two transceivers one antenna simultaneously — the existing port-level
  mutual exclusion is exactly what prevents it; multiband antennas just make the
  collision *frequent* rather than rare.
- **The cost is operator experience.** Radio 2 now hits "denied → none" far more
  often. Without a fallback the second op simply has no antenna on that band.
  This makes the per-band **secondary fallback** (already listed in §9 P3) go
  from nice-to-have to **essential** for multiband SO2R.

### 11.3 Two physical realities, one switching model
The same band→port model expresses both deployments — the difference is purely
how many ports the antenna presents:

| Feed | Hardware | Ports | Two radios, different bands of this antenna | How to model |
|---|---|---|---|---|
| **Single feedline** | antenna → one coax → one switch port | **1** | **Mutually exclusive** (one radio at a time) | all its bands → **one** relay |
| **Triplexed** | antenna → **triplexer** (splits by band) + **BPF/stub** filters → 2–3 ports | **2–3** | **Simultaneous OK** (each radio on a *different* triplexer leg) | each band-group → a **separate** relay |

Key insight: a **triplexer turns one multiband antenna into N band-specific
virtual ports**, and the existing index-based interlock then *correctly allows*
two radios to share it — because they resolve to **different relay indices**. So
the firmware needs **no special "multiband" switching logic**; it needs a
**model + UI** that lets the operator describe the antenna and a resolver that
falls back gracefully on contention.

> Triplexed sharing requires **external** hardware the controller does **not**
> provide: a band triplexer, bandpass filters, and often coaxial **stub** filters
> for inter-station isolation, and it only works for **non-adjacent** band
> combinations on separate legs (e.g. 20+10, not 20+20). Providing/validating
> that isolation is the operator's responsibility — see `docs/HARDWARE.md` (TBD).

### 11.4 Proposed antenna-centric config (quality-of-life)
Today's config is **band-centric** (`band_relay[band] = port`). Multiband use is
clearer if the operator also declares the **antennas** and which bands each
covers; the band→port map can stay authoritative with antennas as validated
metadata, or be *derived* from the antenna list:

```jsonc
"antennas": [
  { "port": 2, "name": "HexBeam",    "bands": ["20m","17m","15m","12m","10m","6m"], "feed": "single" },
  { "port": 0, "name": "80m Dipole", "bands": ["160m","80m"],                       "feed": "single" },
  { "port": 1, "name": "40m Vert",   "bands": ["40m"],                              "feed": "single" },
  // a triplexed tribander = one physical antenna, three independent legs/ports:
  { "port": 5, "name": "TB·20", "bands": ["20m"], "feed": "triplexed", "group": "Tribander" },
  { "port": 6, "name": "TB·15", "bands": ["15m"], "feed": "triplexed", "group": "Tribander" },
  { "port": 7, "name": "TB·10", "bands": ["10m"], "feed": "triplexed", "group": "Tribander" }
]
```

- `feed: single|triplexed` + `group` lets the UI render a triplexed antenna as
  **one physical unit with multiple ports**, and documents that those ports may
  be used concurrently (different legs) while a `single` antenna's bands are
  mutually exclusive.
- **Validation:** warn when a band is mapped to an antenna whose `bands` set does
  not include it (e.g. assigning 6 m to a 40–10 m tribander), and when two bands
  on the **same `single` port** are requested by two radios (expected contention,
  surfaced as info).
- **Multiple antennas cover one band** (HexBeam *and* a monoband 20 m Yagi both
  do 20 m) → needs a **per-band preference order** (the `primary`/`secondary`
  arrays already in §5/§6); multiband setups make this common rather than rare.

### 11.5 SO2R behaviour with multiband antennas
- **Mode A & Mode B:** the existing port-index interlock is already correct; no
  change to the safety logic.
- **Secondary fallback (P3) becomes the headline feature:** Radio 2 wants 15 m,
  HexBeam busy with Radio 1 → resolver picks `secondary["15m"]` (e.g. a 15 m
  wire on another port) → else none. Implemented in `DualResolver` (Mode B) and
  the slave's claim path (Mode A).
- **Status / UX:** surface the contention explicitly so the second op
  understands *why* — e.g. `"radio2": { "band":"15m", "blocked_by":"HexBeam (Radio 1)", "relay": -1 }`
  in `/status`, shown as a dashboard badge ("15 m — HexBeam in use by Radio 1").
- **Triplexed groups:** the resolver may energize **different legs** of the same
  `group` for the two radios simultaneously; it must never energize the **same
  leg** for both (ordinary port exclusion already guarantees this).

### 11.6 Challenges (summary)
| # | Challenge | Notes |
|---|---|---|
| C1 | **Config model + migration** | add `antennas[]` (name, port, bands, feed, group); EEPROM version bump + migration; today's `band_relay[]` maps in as `single` antennas. |
| C2 | **SO2R contention is now frequent** | safe (port interlock) but degrades Radio 2 UX → makes per-band **secondary fallback** mandatory, not optional. |
| C3 | **Simultaneous sharing needs external HW** | triplexer + BPF + stubs, operator-supplied; only non-adjacent band combos; controller just routes the legs. Out of scope to *provide*; in scope to *model*. |
| C4 | **Multiple antennas per band** | preference ordering (primary/secondary) per band; UI to set it. |
| C5 | **Validation & labelling** | band-vs-coverage warnings; show an antenna's band set in the map grid; one name across all its bands (already via P2c relay names). |
| C6 | **Variant band coverage** | "20–6 m" vs "40–6 m" HexBeam etc. — operator declares each antenna's `bands`; no hardcoding. |
| C7 | **Backward compatibility** | the basic single-feed case must keep working with zero reconfiguration; the antenna list is additive metadata over the existing map. |

### 11.7 Phased approach (P4, refines §9)
- **M0 — Document the today-pattern** (no code): map all of a HexBeam's bands to
  one named relay; covers every single-radio user immediately. *(README/CLAUDE.)*
- ✅ **M2 — Per-band secondary fallback in the resolver** *(done + **live-validated**
  — config **v7** `band_relay2[]`; fallback wired into the Mode A `MasterArbiter`
  + `SlaveClient` and the Mode B `DualResolver`, never moving a TX radio; app +
  web "Fallback" column. Verified on the two-board Mode A pair: slave falls back
  on a collision and reclaims its primary when the master leaves. Covered by the
  host resolver tests + the live integration suite (§11.9). Contention
  status/badges still TODO.)* **This is the SO2R payoff.** *Built before M1 — it
  delivers the functional win; M1 is the cosmetic model.*
- ✅ **M1 — Antenna metadata + coverage validation** *(done — config **v8**:
  per-relay `relay_bands` coverage bitmask + feed/group, over the existing
  `band_relay[]`. App "Antennas" section (name + band coverage multi-select +
  feed + group) and web equivalent; the band map warns when an assigned antenna's
  coverage excludes that band. v7→v8 migration. Per-relay metadata rather than a
  separate `antennas[]` array — simpler, same effect.)*
- ✅ **M3 — Triplexer "antenna group" modelling** *(done — `feed` (single/
  triplexed) + `group` fields; legs of one group are distinct relay indices, so
  the interlock already permits concurrent different-leg use and forbids the same
  leg twice (no resolver change needed). App/web warn if two legs of a group
  cover the same band. Isolation requirements documented in
  [HARDWARE.md](HARDWARE.md §2.2).)*

### 11.8 Open questions (multiband)
1. Is `antennas[]` **authoritative** (band→port derived from it) or **metadata**
   over the existing `band_relay[]`? (Leaning metadata-first for a clean
   migration, promote later.)
2. Secondary fallback: **explicit per-band secondary** only, or also an automatic
   "any antenna that covers this band and is free" search?
3. How far to model triplexers — just `group` for display + concurrency, or also
   encode **which leg owns which bands** for validation of adjacent-band conflicts?
4. Per-radio vs shared antenna preference order in SO2R (ties into §10 Q5).

### 11.9 Test infrastructure (scenario verification)
Two complementary layers verify the SO2R decision logic — the algorithm in
isolation, and the whole pipeline on real hardware:

1. **Host unit tests** (`Controller/test/`, `make run`). The *real* firmware
   headers (`Interlock.h`, `Config.h`, `BandPlan.h`) compiled against desktop
   shims (`shims/`), so the logic under test is exactly what ships — no
   duplication, no hardware, runs in CI. Covers every `DualResolver` /
   `MasterArbiter` branch (collision, first-come vs priority, TX-safety, fallback
   taken/blocked, recovery) and the v6→v7 config migration. *(48 assertions.)*

2. **Live integration suite** (`App/Tests/`, `swift test`). Drives **real boards**
   over HTTP with the app's own `AntennaSwitchClient` — the app configures the
   boards (`/save`), and the firmware's **`/test/inject` API** (compiled only with
   `-DANTSW_TEST`) puts a board into any band/TX scenario without tuning real
   radios. Asserts the end-to-end wiring (config → resolver → relay → `/status`).
   Self-skips unless `ANTSW_TEST_MASTER` / `ANTSW_TEST_SLAVE` name boards, so CI
   stays green without hardware. The production image omits the `/test/*` routes
   entirely.

This is the pattern to extend for every later P4 scenario (Mode B contention,
triplexer groups) and for regression-guarding the existing modes.
