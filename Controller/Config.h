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

#define CFG_MAGIC   0x414E5431UL  // "ANT1"
#define EEPROM_SIZE 512

struct Config {
  uint32_t magic;
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     tci_host[64];
  uint16_t tci_port;
  uint8_t  iaru_region;          // 1/2/3 (informational; band uses freqToBand)
  char     hostname[33];         // mDNS + OTA name
  char     ota_pass[33];         // ArduinoOTA password ("" = none)
  uint16_t guard_ms;             // break-before-make guard delay (R2.8)
  int8_t   band_relay[NUM_BANDS];// -1 = none/bypass, else relay 0..7
  uint32_t crc;                  // CRC32 over all preceding bytes
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
  c.guard_ms    = 50;             // CLAUDE.md §6.6
  defaultHostname(c.hostname, sizeof(c.hostname));
  for (int i = 0; i < NUM_BANDS; i++) c.band_relay[i] = -1;  // none
  c.crc = configCrc(c);
}

// Returns true if a valid config was loaded; false if defaults were applied
// (blank/corrupt EEPROM → safe defaults, caller should enter setup/AP mode).
inline bool configLoad(Config& c) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, c);
  EEPROM.end();
  if (c.magic == CFG_MAGIC && c.crc == configCrc(c)) {
    // Clamp band_relay to valid range defensively.
    for (int i = 0; i < NUM_BANDS; i++)
      if (c.band_relay[i] < -1 || c.band_relay[i] > 7) c.band_relay[i] = -1;
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
