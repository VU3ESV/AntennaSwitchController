// Host-test shim for <ESP8266HTTPClient.h>. The unit tests never exercise the
// network (only MasterArbiter + DualResolver, which are pure), so this just lets
// SlaveClient compile; POST() returns -1 (no connection).
#pragma once
#include "Arduino.h"
#include "ESP8266WiFi.h"

class HTTPClient {
 public:
  bool begin(WiFiClient&, const String&) { return true; }
  void setTimeout(int) {}
  int POST(const uint8_t*, int) { return -1; }
  String getString() { return String("0"); }
  void end() {}
};
