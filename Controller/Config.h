// Config.h — persisted settings in EEPROM with CRC32 integrity (R3.3).
//
// AntennaSwitchController (ESP8266). Header-only: one definition compiled into
// the single sketch translation unit, mirroring the reference project's style.
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <stddef.h>      // offsetof
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include "BandPlan.h"

#define CFG_MAGIC_V1 0x414E5431UL  // "ANT1" — v1: single TCI radio
#define CFG_MAGIC_V2 0x414E5432UL  // "ANT2" — v2: adds radio_type
#define CFG_MAGIC_V3 0x414E5433UL  // "ANT3" — v3: adds SO2R mode/peer/interlock
#define CFG_MAGIC_V4 0x414E5434UL  // "ANT4" — v4: adds radio 2 + switch_type (Mode B)
#define CFG_MAGIC_V5 0x414E5435UL  // "ANT5" — v5: adds per-radio TCI receiver index
#define CFG_MAGIC_V6 0x414E5436UL  // "ANT6" — v6: adds per-relay names
#define CFG_MAGIC_V7 0x414E5437UL  // "ANT7" — v7: adds per-band secondary (fallback) relay
#define CFG_MAGIC_V8 0x414E5438UL  // "ANT8" — v8: adds per-relay antenna metadata
#define CFG_MAGIC    0x414E5439UL  // "ANT9" — v9: adds serial-CAT (baud + CI-V addr)
#define EEPROM_SIZE  1024          // grew past 512 with relay names; ESP8266
                                   // reserves a 4 KB flash sector regardless

#ifndef NUM_RELAYS
#define NUM_RELAYS 8               // matches OutputStage.h (relays 0..7)
#endif
#define RELAY_NAME_LEN 16          // per-relay display name (15 chars + null)

// Antenna feed type (per relay/port), for multiband-antenna modelling
// (docs/MULTI-RADIO-SO2R-PLAN.md §11). SINGLE = one feedline, all its bands are
// mutually exclusive across radios. TRIPLEXED = one leg of a band-split antenna;
// legs of the same group may be used by both radios at once (different ports).
enum FeedType : uint8_t { FEED_SINGLE = 0, FEED_TRIPLEXED = 1 };

// Radio transport for a radio source.
//   TCI / FLEX  — network (WiFi); no extra hardware.
//   CAT_*       — read-only serial CAT on UART0 (P1; one serial radio per board,
//                 see docs/HARDWARE.md §3). Kenwood and modern Yaesu share the
//                 ASCII `IF;` poll; Icom is CI-V binary. Radio 2 is never serial.
enum RadioType : uint8_t {
  RADIO_TCI = 0, RADIO_FLEX = 1,
  RADIO_CAT_KENWOOD = 2,   // Kenwood / Elecraft (TS-590/890, K3/K4): "IF;"
  RADIO_CAT_ICOM    = 3,   // Icom CI-V (IC-7300/9700/705/7610)
  RADIO_CAT_YAESU   = 4,   // modern Yaesu (FT-991A / FT-DX10): "IF;"
};
inline bool radioTypeIsSerial(uint8_t t) { return t >= RADIO_CAT_KENWOOD && t <= RADIO_CAT_YAESU; }

// SO2R role of this unit (docs/MULTI-RADIO-SO2R-PLAN.md §3).
//   STANDALONE — single-radio 8×1 (default).
//   MASTER     — Mode A Radio-1 board; the LAN interlock arbiter.
//   SLAVE      — Mode A Radio-2 board; claims antennas from the master.
//   DUAL       — Mode B single board: tracks BOTH radios, drives an external
//                8×2 switch (per-antenna A/B) with in-firmware interlock.
enum CtrlMode : uint8_t { MODE_STANDALONE = 0, MODE_MASTER = 1, MODE_SLAVE = 2, MODE_DUAL = 3 };

// Interlock arbitration when both radios want the same antenna index.
//   FIRST_COME — current holder keeps it; the newcomer falls back (default).
//   PRIORITY   — interlock_priority radio wins, preempting the other.
enum InterlockPolicy : uint8_t { ILK_FIRST_COME = 0, ILK_PRIORITY = 1 };

// What a slave does when the master is unreachable.
enum PeerLoss : uint8_t { PEER_LOSS_SAFE = 0, PEER_LOSS_HOLD = 1 };

// External antenna switch wiring driven by the 8 relays.
//   8X1 — exclusive 1-of-8 (standalone / master / slave).
//   8X2 — per-antenna A/B select, 8× SPDT (Mode B): relay i de-energized routes
//         antenna i to Radio 1, energized routes it to Radio 2.
enum SwitchType : uint8_t { SWITCH_8X1 = 0, SWITCH_8X2 = 1 };

// v5 settings. tci_host/tci_port are radio 1; radio2_* is the Mode B second
// radio. tci_host/tci_port serve whichever transport (TCI server, or Flex IP).
// radio*_rx pick the TCI receiver (0=RX1, 1=RX2) — a 2-receiver radio (SunSDR2)
// is driven as Mode B with both radios on the same host, rx 0 and rx 1.
struct Config {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];         // radio 1 host/IP (TCI server or Flex)
  uint16_t tci_port;             // radio 1 port (TCI 50001, Flex 4992)
  uint8_t  iaru_region;          // 1/2/3 (informational; band uses freqToBand)
  uint8_t  radio_type;           // radio 1 RadioType: 0=TCI, 1=FlexRadio TCP
  char     hostname[33];         // mDNS + OTA name
  char     ota_pass[33];         // ArduinoOTA password ("" = none)
  uint16_t guard_ms;             // break-before-make guard delay (R2.8)
  int8_t   band_relay[NUM_BANDS];// -1 = none/bypass, else relay 0..7
  uint8_t  mode;                 // CtrlMode: 0=standalone,1=master,2=slave,3=dual
  char     peer_host[64];        // SLAVE: master's address (IP)
  uint8_t  interlock_policy;     // InterlockPolicy: 0=first-come, 1=priority
  uint8_t  on_peer_loss;         // PeerLoss: 0=safe/none, 1=hold (slave only)
  uint8_t  radio2_type;          // DUAL: radio 2 RadioType
  char     radio2_host[64];      // DUAL: radio 2 host/IP
  uint16_t radio2_port;          // DUAL: radio 2 port
  uint8_t  switch_type;          // SwitchType: 0=8x1, 1=8x2 (dual)
  uint8_t  radio_rx;             // radio 1 TCI receiver index (0=RX1, 1=RX2)
  uint8_t  radio2_rx;            // radio 2 TCI receiver index (0=RX1, 1=RX2)
  char     relay_name[NUM_RELAYS][RELAY_NAME_LEN];  // per-relay name ("" = default Rn)
  int8_t   band_relay2[NUM_BANDS];// SO2R fallback: relay used when band_relay[b]
                                 // is taken by the other radio (-1 = none)
  uint16_t relay_bands[NUM_RELAYS]; // antenna band-coverage bitmask (bit b = Band b);
                                 // 0 = undeclared (no validation). Multiband model.
  uint8_t  relay_feed[NUM_RELAYS];  // FeedType per relay (single / triplexed)
  uint8_t  relay_group[NUM_RELAYS]; // 0 = none; else a group id shared by the legs
                                 // of one triplexed physical antenna
  uint32_t cat_baud;             // radio 1 serial-CAT baud (RADIO_CAT_*); else unused
  uint8_t  civ_addr;             // radio 1 Icom CI-V address (RADIO_CAT_ICOM)
  uint32_t crc;                  // CRC32 over all preceding bytes
};

// Frozen v8 layout — migrate v8 saved configs (antenna metadata, no serial CAT).
// DO NOT edit: must match exactly what shipped at the v8 bump.
struct ConfigV8 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  uint8_t  radio_type;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint8_t  mode;
  char     peer_host[64];
  uint8_t  interlock_policy;
  uint8_t  on_peer_loss;
  uint8_t  radio2_type;
  char     radio2_host[64];
  uint16_t radio2_port;
  uint8_t  switch_type;
  uint8_t  radio_rx;
  uint8_t  radio2_rx;
  char     relay_name[NUM_RELAYS][RELAY_NAME_LEN];
  int8_t   band_relay2[NUM_BANDS];
  uint16_t relay_bands[NUM_RELAYS];
  uint8_t  relay_feed[NUM_RELAYS];
  uint8_t  relay_group[NUM_RELAYS];
  uint32_t crc;
};

// Frozen v7 layout — migrate v7 saved configs (fallback map, no antenna metadata).
// DO NOT edit: must match exactly what shipped at the v7 bump.
struct ConfigV7 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  uint8_t  radio_type;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint8_t  mode;
  char     peer_host[64];
  uint8_t  interlock_policy;
  uint8_t  on_peer_loss;
  uint8_t  radio2_type;
  char     radio2_host[64];
  uint16_t radio2_port;
  uint8_t  switch_type;
  uint8_t  radio_rx;
  uint8_t  radio2_rx;
  char     relay_name[NUM_RELAYS][RELAY_NAME_LEN];
  int8_t   band_relay2[NUM_BANDS];
  uint32_t crc;
};

// Frozen v6 layout — migrate v6 saved configs (relay names, no secondary map).
// DO NOT edit: must match exactly what shipped at the v6 bump.
struct ConfigV6 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  uint8_t  radio_type;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint8_t  mode;
  char     peer_host[64];
  uint8_t  interlock_policy;
  uint8_t  on_peer_loss;
  uint8_t  radio2_type;
  char     radio2_host[64];
  uint16_t radio2_port;
  uint8_t  switch_type;
  uint8_t  radio_rx;
  uint8_t  radio2_rx;
  char     relay_name[NUM_RELAYS][RELAY_NAME_LEN];
  uint32_t crc;
};

// Frozen v5 layout — migrate v5 saved configs (per-radio rx, no relay names).
// DO NOT edit: must match exactly what shipped at the v5 bump.
struct ConfigV5 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  uint8_t  radio_type;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint8_t  mode;
  char     peer_host[64];
  uint8_t  interlock_policy;
  uint8_t  on_peer_loss;
  uint8_t  radio2_type;
  char     radio2_host[64];
  uint16_t radio2_port;
  uint8_t  switch_type;
  uint8_t  radio_rx;
  uint8_t  radio2_rx;
  uint32_t crc;
};

// Frozen v4 layout — migrate v4 saved configs (radio 2 + switch_type, no rx).
// DO NOT edit: must match exactly what shipped at the v4 bump.
struct ConfigV4 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  uint8_t  radio_type;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint8_t  mode;
  char     peer_host[64];
  uint8_t  interlock_policy;
  uint8_t  on_peer_loss;
  uint8_t  radio2_type;
  char     radio2_host[64];
  uint16_t radio2_port;
  uint8_t  switch_type;
  uint32_t crc;
};

// Frozen v3 layout — migrate v3 saved configs (SO2R mode, no radio 2).
// DO NOT edit: must match exactly what shipped at the v3 bump.
struct ConfigV3 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  uint8_t  radio_type;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint8_t  mode;
  char     peer_host[64];
  uint8_t  interlock_policy;
  uint8_t  on_peer_loss;
  uint32_t crc;
};

// Frozen v1 layout — migrate older saved configs (no radio_type, no mode).
// DO NOT edit: must match exactly what shipped before the v2 bump.
struct ConfigV1 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint32_t crc;
};

// Frozen v2 layout — migrate v2 saved configs (has radio_type, no mode).
// DO NOT edit: must match exactly what shipped at the v2 bump.
struct ConfigV2 {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;
  uint8_t  radio_type;
  char     hostname[33];
  char     ota_pass[33];
  uint16_t guard_ms;
  int8_t   band_relay[NUM_BANDS];
  uint32_t crc;
};

// --- CRC32 (standard reflected poly 0xEDB88320) ---------------------------
inline uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}

inline uint32_t configCrc(const Config& c) {
  // CRC everything except the trailing crc field itself.
  return crc32(reinterpret_cast<const uint8_t*>(&c), offsetof(Config, crc));
}

inline void configSave(Config& c);   // fwd decl (used by the v1 migration)

// Default hostname: ANT-SW-Controller-xx where xx = last MAC byte (R3.4).
inline void defaultHostname(char* out, size_t n) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(out, n, "ANT-SW-Controller-%02X", mac[5]);
}

inline void configDefaults(Config& c) {
  memset(&c, 0, sizeof(c));
  c.magic            = CFG_MAGIC;
  c.tci_port         = 50001;     // ExpertSDR3 default
  c.iaru_region      = 1;
  c.radio_type       = RADIO_TCI;
  c.guard_ms         = 50;        // CLAUDE.md §6.6
  c.mode             = MODE_STANDALONE;
  c.interlock_policy = ILK_FIRST_COME;
  c.on_peer_loss     = PEER_LOSS_SAFE;
  c.radio2_type      = RADIO_TCI;
  c.radio2_port      = 50001;
  c.switch_type      = SWITCH_8X1;
  c.radio_rx         = 0;
  c.radio2_rx        = 0;
  c.cat_baud         = 9600;      // common CAT default (Kenwood/Icom)
  c.civ_addr         = 0x94;      // Icom default (IC-7300)
  defaultHostname(c.hostname, sizeof(c.hostname));
  for (int i = 0; i < NUM_BANDS; i++) { c.band_relay[i] = -1; c.band_relay2[i] = -1; }  // none
  c.crc = configCrc(c);
}

inline void clampConfig(Config& c) {
  for (int i = 0; i < NUM_BANDS; i++) {
    if (c.band_relay[i]  < -1 || c.band_relay[i]  > 7) c.band_relay[i]  = -1;
    if (c.band_relay2[i] < -1 || c.band_relay2[i] > 7) c.band_relay2[i] = -1;
  }
  if (c.radio_type > RADIO_CAT_YAESU)  c.radio_type       = RADIO_TCI;
  if (c.mode > MODE_DUAL)              c.mode             = MODE_STANDALONE;
  if (c.interlock_policy > ILK_PRIORITY) c.interlock_policy = ILK_FIRST_COME;
  if (c.on_peer_loss > PEER_LOSS_HOLD) c.on_peer_loss     = PEER_LOSS_SAFE;
  if (c.radio2_type > RADIO_FLEX)      c.radio2_type      = RADIO_TCI;  // radio 2 never serial
  if (c.cat_baud == 0 || c.cat_baud > 250000) c.cat_baud  = 9600;
  if (c.switch_type > SWITCH_8X2)      c.switch_type      = SWITCH_8X1;
  if (c.radio_rx  > 1)                 c.radio_rx         = 0;
  if (c.radio2_rx > 1)                 c.radio2_rx        = 0;
  for (int i = 0; i < NUM_RELAYS; i++) c.relay_name[i][RELAY_NAME_LEN - 1] = '\0';
  const uint16_t bandMask = (uint16_t)((1u << NUM_BANDS) - 1);
  for (int i = 0; i < NUM_RELAYS; i++) {
    c.relay_bands[i] &= bandMask;                       // only valid band bits
    if (c.relay_feed[i] > FEED_TRIPLEXED) c.relay_feed[i] = FEED_SINGLE;
    if (c.relay_group[i] > NUM_RELAYS)    c.relay_group[i] = 0;   // 0..8
  }
}

// Migrate a v1 image (no radio_type, no mode) into v3. configDefaults() supplies
// the v2/v3 fields (radio_type=TCI, mode=standalone); we copy the v1 fields over.
inline bool configMigrateV1(Config& c) {
  ConfigV1 v1;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v1);
  EEPROM.end();
  if (v1.magic != CFG_MAGIC_V1) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v1), offsetof(ConfigV1, crc)) != v1.crc) return false;

  configDefaults(c);
  memcpy(c.wifi_ssid, v1.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v1.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v1.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v1.tci_port;
  c.iaru_region = v1.iaru_region;
  memcpy(c.hostname, v1.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v1.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v1.guard_ms;
  memcpy(c.band_relay, v1.band_relay, sizeof(c.band_relay));
  return true;
}

// Migrate a v2 image (has radio_type, no mode) into v3 — keeps band map +
// radio_type; new SO2R fields default to standalone/first-come/safe.
inline bool configMigrateV2(Config& c) {
  ConfigV2 v2;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v2);
  EEPROM.end();
  if (v2.magic != CFG_MAGIC_V2) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v2), offsetof(ConfigV2, crc)) != v2.crc) return false;

  configDefaults(c);
  memcpy(c.wifi_ssid, v2.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v2.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v2.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v2.tci_port;
  c.iaru_region = v2.iaru_region;
  c.radio_type  = v2.radio_type;
  memcpy(c.hostname, v2.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v2.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v2.guard_ms;
  memcpy(c.band_relay, v2.band_relay, sizeof(c.band_relay));
  return true;
}

// Migrate a v3 image (SO2R mode, no radio 2) into v4 — keeps everything; the
// new Mode B fields default to TCI/8x1.
inline bool configMigrateV3(Config& c) {
  ConfigV3 v3;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v3);
  EEPROM.end();
  if (v3.magic != CFG_MAGIC_V3) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v3), offsetof(ConfigV3, crc)) != v3.crc) return false;

  configDefaults(c);
  memcpy(c.wifi_ssid, v3.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v3.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v3.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v3.tci_port;
  c.iaru_region = v3.iaru_region;
  c.radio_type  = v3.radio_type;
  memcpy(c.hostname, v3.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v3.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v3.guard_ms;
  memcpy(c.band_relay, v3.band_relay, sizeof(c.band_relay));
  c.mode             = v3.mode;
  memcpy(c.peer_host, v3.peer_host, sizeof(c.peer_host));
  c.interlock_policy = v3.interlock_policy;
  c.on_peer_loss     = v3.on_peer_loss;
  return true;
}

// Migrate a v4 image (radio 2 + switch_type, no rx index) into v5 — keeps
// everything; the new per-radio receiver indices default to 0 (RX1).
inline bool configMigrateV4(Config& c) {
  ConfigV4 v4;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v4);
  EEPROM.end();
  if (v4.magic != CFG_MAGIC_V4) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v4), offsetof(ConfigV4, crc)) != v4.crc) return false;

  configDefaults(c);
  memcpy(c.wifi_ssid, v4.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v4.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v4.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v4.tci_port;
  c.iaru_region = v4.iaru_region;
  c.radio_type  = v4.radio_type;
  memcpy(c.hostname, v4.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v4.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v4.guard_ms;
  memcpy(c.band_relay, v4.band_relay, sizeof(c.band_relay));
  c.mode             = v4.mode;
  memcpy(c.peer_host, v4.peer_host, sizeof(c.peer_host));
  c.interlock_policy = v4.interlock_policy;
  c.on_peer_loss     = v4.on_peer_loss;
  c.radio2_type      = v4.radio2_type;
  memcpy(c.radio2_host, v4.radio2_host, sizeof(c.radio2_host));
  c.radio2_port      = v4.radio2_port;
  c.switch_type      = v4.switch_type;
  return true;
}

// Migrate a v5 image (per-radio rx, no relay names) into v6 — keeps everything;
// the new per-relay names default to "" (the app/web shows Rn for a blank name).
inline bool configMigrateV5(Config& c) {
  ConfigV5 v5;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v5);
  EEPROM.end();
  if (v5.magic != CFG_MAGIC_V5) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v5), offsetof(ConfigV5, crc)) != v5.crc) return false;

  configDefaults(c);                                // zeroes relay_name (blank)
  memcpy(c.wifi_ssid, v5.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v5.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v5.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v5.tci_port;
  c.iaru_region = v5.iaru_region;
  c.radio_type  = v5.radio_type;
  memcpy(c.hostname, v5.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v5.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v5.guard_ms;
  memcpy(c.band_relay, v5.band_relay, sizeof(c.band_relay));
  c.mode             = v5.mode;
  memcpy(c.peer_host, v5.peer_host, sizeof(c.peer_host));
  c.interlock_policy = v5.interlock_policy;
  c.on_peer_loss     = v5.on_peer_loss;
  c.radio2_type      = v5.radio2_type;
  memcpy(c.radio2_host, v5.radio2_host, sizeof(c.radio2_host));
  c.radio2_port      = v5.radio2_port;
  c.switch_type      = v5.switch_type;
  c.radio_rx         = v5.radio_rx;
  c.radio2_rx        = v5.radio2_rx;
  return true;
}

// Migrate a v6 image (relay names, no secondary map) into v7 — keeps everything;
// the new per-band secondary (fallback) relays default to -1 (none).
inline bool configMigrateV6(Config& c) {
  ConfigV6 v6;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v6);
  EEPROM.end();
  if (v6.magic != CFG_MAGIC_V6) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v6), offsetof(ConfigV6, crc)) != v6.crc) return false;

  configDefaults(c);                                // zeroes band_relay2 (none)
  memcpy(c.wifi_ssid, v6.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v6.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v6.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v6.tci_port;
  c.iaru_region = v6.iaru_region;
  c.radio_type  = v6.radio_type;
  memcpy(c.hostname, v6.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v6.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v6.guard_ms;
  memcpy(c.band_relay, v6.band_relay, sizeof(c.band_relay));
  c.mode             = v6.mode;
  memcpy(c.peer_host, v6.peer_host, sizeof(c.peer_host));
  c.interlock_policy = v6.interlock_policy;
  c.on_peer_loss     = v6.on_peer_loss;
  c.radio2_type      = v6.radio2_type;
  memcpy(c.radio2_host, v6.radio2_host, sizeof(c.radio2_host));
  c.radio2_port      = v6.radio2_port;
  c.switch_type      = v6.switch_type;
  c.radio_rx         = v6.radio_rx;
  c.radio2_rx        = v6.radio2_rx;
  memcpy(c.relay_name, v6.relay_name, sizeof(c.relay_name));
  return true;
}

// Migrate a v7 image (fallback map, no antenna metadata) into v8 — keeps
// everything; the new per-relay band coverage / feed / group default to 0
// (undeclared single-feed, no group → no validation, behaviour unchanged).
inline bool configMigrateV7(Config& c) {
  ConfigV7 v7;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v7);
  EEPROM.end();
  if (v7.magic != CFG_MAGIC_V7) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v7), offsetof(ConfigV7, crc)) != v7.crc) return false;

  configDefaults(c);                                // zeroes relay_bands/feed/group
  memcpy(c.wifi_ssid, v7.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v7.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v7.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v7.tci_port;
  c.iaru_region = v7.iaru_region;
  c.radio_type  = v7.radio_type;
  memcpy(c.hostname, v7.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v7.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v7.guard_ms;
  memcpy(c.band_relay, v7.band_relay, sizeof(c.band_relay));
  c.mode             = v7.mode;
  memcpy(c.peer_host, v7.peer_host, sizeof(c.peer_host));
  c.interlock_policy = v7.interlock_policy;
  c.on_peer_loss     = v7.on_peer_loss;
  c.radio2_type      = v7.radio2_type;
  memcpy(c.radio2_host, v7.radio2_host, sizeof(c.radio2_host));
  c.radio2_port      = v7.radio2_port;
  c.switch_type      = v7.switch_type;
  c.radio_rx         = v7.radio_rx;
  c.radio2_rx        = v7.radio2_rx;
  memcpy(c.relay_name, v7.relay_name, sizeof(c.relay_name));
  memcpy(c.band_relay2, v7.band_relay2, sizeof(c.band_relay2));
  return true;
}

// Migrate a v8 image (antenna metadata, no serial CAT) into v9 — keeps
// everything; the new serial-CAT fields default (9600 baud, CI-V 0x94).
inline bool configMigrateV8(Config& c) {
  ConfigV8 v8;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v8);
  EEPROM.end();
  if (v8.magic != CFG_MAGIC_V8) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&v8), offsetof(ConfigV8, crc)) != v8.crc) return false;

  configDefaults(c);                                // sets cat_baud / civ_addr defaults
  memcpy(c.wifi_ssid, v8.wifi_ssid, sizeof(c.wifi_ssid));
  memcpy(c.wifi_pass, v8.wifi_pass, sizeof(c.wifi_pass));
  memcpy(c.tci_host,  v8.tci_host,  sizeof(c.tci_host));
  c.tci_port    = v8.tci_port;
  c.iaru_region = v8.iaru_region;
  c.radio_type  = v8.radio_type;
  memcpy(c.hostname, v8.hostname, sizeof(c.hostname));
  memcpy(c.ota_pass, v8.ota_pass, sizeof(c.ota_pass));
  c.guard_ms = v8.guard_ms;
  memcpy(c.band_relay, v8.band_relay, sizeof(c.band_relay));
  c.mode             = v8.mode;
  memcpy(c.peer_host, v8.peer_host, sizeof(c.peer_host));
  c.interlock_policy = v8.interlock_policy;
  c.on_peer_loss     = v8.on_peer_loss;
  c.radio2_type      = v8.radio2_type;
  memcpy(c.radio2_host, v8.radio2_host, sizeof(c.radio2_host));
  c.radio2_port      = v8.radio2_port;
  c.switch_type      = v8.switch_type;
  c.radio_rx         = v8.radio_rx;
  c.radio2_rx        = v8.radio2_rx;
  memcpy(c.relay_name, v8.relay_name, sizeof(c.relay_name));
  memcpy(c.band_relay2, v8.band_relay2, sizeof(c.band_relay2));
  memcpy(c.relay_bands, v8.relay_bands, sizeof(c.relay_bands));
  memcpy(c.relay_feed,  v8.relay_feed,  sizeof(c.relay_feed));
  memcpy(c.relay_group, v8.relay_group, sizeof(c.relay_group));
  return true;
}

// Returns true if a valid config was loaded (incl. a migrated v1..v8); false
// if defaults were applied (blank/corrupt EEPROM → safe defaults, caller should
// enter setup/AP mode).
inline bool configLoad(Config& c) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, c);
  EEPROM.end();
  if (c.magic == CFG_MAGIC && c.crc == configCrc(c)) {
    clampConfig(c);
    return true;
  }
  if (configMigrateV8(c) || configMigrateV7(c) || configMigrateV6(c) ||
      configMigrateV5(c) || configMigrateV4(c) || configMigrateV3(c) ||
      configMigrateV2(c) || configMigrateV1(c)) {
    clampConfig(c);
    configSave(c);                                 // persist as v9 (migrate once)
    return true;
  }
  configDefaults(c);
  return false;
}

inline void configSave(Config& c) {
  c.magic = CFG_MAGIC;
  c.crc   = configCrc(c);
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, c);
  EEPROM.commit();
  EEPROM.end();
}

inline void configWipe() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < (int)sizeof(Config); i++) EEPROM.write(i, 0);
  EEPROM.commit();
  EEPROM.end();
}

#endif // CONFIG_H
