# Hardware — external antenna switching & multiband sharing

This note covers the **external RF hardware** the controller drives (the
controller itself is the [ESP8266 ESP-12F 8-relay board](../CLAUDE.md#1-hardware)).
The firmware only switches relays; the antennas, RF switch, and any filtering are
yours to wire. See [MULTI-RADIO-SO2R-PLAN.md](MULTI-RADIO-SO2R-PLAN.md) for the
modes referenced here.

## 1. Switch topologies

| Mode | External hardware | Relays |
|---|---|---|
| **Standalone / Master / Slave (8×1)** | one 1-of-8 antenna switch per board (relay _i_ = antenna _i_, exclusive) | one HIGH at a time |
| **Mode A isolation** | a per-band/per-antenna **1×2** switch selecting which of the two boards reaches the antenna | one per board |
| **Mode B (Dual, 8×2)** | one external **8×2** switch; relay _i_ de-energized routes antenna _i_ to Radio 1, energized to Radio 2 | up to two HIGH (one per radio) |

**Break-before-make** is enforced in firmware (guard delay, default 50 ms) so two
antennas are never momentarily tied together. Relays on GPIO0/15/16 may twitch at
power-up — wire your most-used antennas to GPIO14/12/13/4/5.

## 2. Multiband antennas (HexBeam, tribander, fan dipole)

A multiband antenna is modelled in the **Antennas** settings as one relay/port
with a declared **band coverage** (used only to warn about mis-assignments) and a
**feed type**:

### 2.1 Single feedline (the common case)
One antenna → one coax → one switch port. **All of its bands are mutually
exclusive** — only one radio can use it at a time. Configure it by pointing every
band it covers at the same relay and naming it (e.g. "HexBeam" on 20–6 m). In
SO2R the interlock denies the second radio; set a per-band **fallback** antenna so
that radio isn't left with nothing. Set **Feed = Single**, **Group = 0**.

### 2.2 Triplexed (sharing one antenna between two radios)
To let **both** radios use one multiband antenna **simultaneously on different
bands**, you must add external hardware the controller does **not** provide:

```
   tribander ─ triplexer ─┬─ 20 m port ─ BPF ─ (stub) ─► switch port A  (Group 1, covers 20 m)
                          ├─ 15 m port ─ BPF ─ (stub) ─► switch port B  (Group 1, covers 15 m)
                          └─ 10 m port ─ BPF ─ (stub) ─► switch port C  (Group 1, covers 10 m)
```

- A **band triplexer** splits the single feedline into per-band ports.
- A **bandpass filter (BPF)** on each port, plus often coaxial **stub filters**,
  provides the inter-station **isolation** needed so one radio transmitting
  doesn't desensitize or damage the other.
- Model each leg as its **own relay/port** with **Feed = Triplexed** and a shared
  **Group** id, each covering its own band. Because the legs are distinct relay
  indices, the interlock already allows both radios to use them at once — and
  still prevents two radios on the *same* leg.

**Constraints / safety (operator's responsibility):**
- Only **non-adjacent** band combinations are practical (e.g. 20 + 10, not
  20 + 20, and 20 + 15 only with adequate filtering). The app warns if two legs
  of one group are set to cover the same band.
- Isolation between stations on nearby bands is real engineering — insufficient
  filtering risks receiver damage. Provide and verify it before sharing.
- The controller does not measure or guarantee isolation; it only routes relays.

## 3. Serial-CAT level conversion

A board's radio may be TCI/TCP (network, no extra hardware) or **one** serial-CAT
radio on UART0 (GPIO1/3). ESP GPIO are **3.3 V and not 5 V tolerant**, so a level
converter is **mandatory** and a common ground is required — see the converter
table in [MULTI-RADIO-SO2R-PLAN.md §4.2](MULTI-RADIO-SO2R-PLAN.md) (MAX3232 for
RS-232 rigs; BSS138 shifter / CI-V buffer for TTL rigs). One serial radio per
board (the other UART is TX-only), so two serial radios require **Mode A**.
