// Host-test shim for <EEPROM.h> — an in-memory buffer so Config.h's load/save/
// migrate compile and run on the host (lets the resolver tests link, and makes
// config round-trip testable too).
#pragma once
#include "Arduino.h"

class EEPROMClass {
 public:
  uint8_t buf[4096] = {0};
  void begin(size_t) {}
  template <typename T> T& get(int idx, T& t) { memcpy(&t, buf + idx, sizeof(T)); return t; }
  template <typename T> const T& put(int idx, const T& t) { memcpy(buf + idx, &t, sizeof(T)); return t; }
  bool commit() { return true; }
  void end() {}
  void write(int idx, uint8_t v) { buf[idx] = v; }
};
extern EEPROMClass EEPROM;
