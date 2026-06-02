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

#define CFG_MAGIC_V1 0x414E5431UL  // "ANT1" — v1 layout (single TCI radio)
#define CFG_MAGIC    0x414E5432UL  // "ANT2" — v2 layout (adds radio_type)
#define EEPROM_SIZE  512

// Radio transport for this unit's (single, P1) radio source.
enum RadioType : uint8_t { RADIO_TCI = 0, RADIO_FLEX = 1 };

// v2 settings. tci_host/tci_port are the radio endpoint for whichever transport
// (TCI server, or FlexRadio SmartSDR IP:4992).
struct Config {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];         // radio host/IP (TCI server or Flex)
  uint16_t tci_port;             // radio port (TCI 50001, Flex 4992)
  uint8_t  iaru_region;          // 1/2/3 (informational; band uses freqToBand)
  uint8_t  radio_type;           // RadioType: 0=TCI, 1=FlexRadio TCP
  char     hostname[33];         // mDNS + OTA name
  char     ota_pass[33];         // ArduinoOTA password ("" = none)
  uint16_t guard_ms;             // break-before-make guard delay (R2.8)
  int8_t   band_relay[NUM_BANDS];// -1 = none/bypass, else relay 0..7
  uint32_t crc;                  // CRC32 over all preceding bytes
};

// Frozen v1 layout — used only to migrate older saved configs (no radio_type).
// DO NOT edit: it must match exactly what shipped before the v2 bump.
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
  c.magic       = CFG_MAGIC;
  c.tci_port    = 50001;          // ExpertSDR3 default
  c.iaru_region = 1;
  c.radio_type  = RADIO_TCI;
  c.guard_ms    = 50;             // CLAUDE.md §6.6
  defaultHostname(c.hostname, sizeof(c.hostname));
  for (int i = 0; i < NUM_BANDS; i++) c.band_relay[i] = -1;  // none
  c.crc = configCrc(c);
}

inline void clampConfig(Config& c) {
  for (int i = 0; i < NUM_BANDS; i++)
    if (c.band_relay[i] < -1 || c.band_relay[i] > 7) c.band_relay[i] = -1;
  if (c.radio_type > RADIO_FLEX) c.radio_type = RADIO_TCI;
}

// Migrate a v1 EEPROM image (no radio_type) into the v2 struct, defaulting the
// new field to TCI — so boards updated over OTA keep their saved band map.
// Returns true if a valid v1 image was found and migrated.
inline bool configMigrateV1(Config& c) {
  ConfigV1 v1;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, v1);
  EEPROM.end();
  if (v1.magic != CFG_MAGIC_V1) return false;
  uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(&v1), offsetof(ConfigV1, crc));
  if (crc != v1.crc) return false;

  configDefaults(c);                       // sets magic=v2, radio_type=TCI
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

// Returns true if a valid config was loaded (incl. a migrated v1); false if
// defaults were applied (blank/corrupt EEPROM → safe defaults, caller should
// enter setup/AP mode).
inline bool configLoad(Config& c) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, c);
  EEPROM.end();
  if (c.magic == CFG_MAGIC && c.crc == configCrc(c)) {
    clampConfig(c);
    return true;
  }
  if (configMigrateV1(c)) {                // older image → upgrade in place
    clampConfig(c);
    configSave(c);                         // persist as v2 so we migrate once
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
