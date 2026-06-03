// Host-test shim for <ESP8266WiFi.h>. Provides the WiFi global (Config.h's
// defaultHostname reads the MAC) and an empty WiFiClient (Interlock.h's HTTP).
#pragma once
#include "Arduino.h"

class WiFiClass {
 public:
  void macAddress(uint8_t* m) { for (int i = 0; i < 6; i++) m[i] = 0; }
};
extern WiFiClass WiFi;

class WiFiClient {};
