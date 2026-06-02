/*
 * AntennaSwitchController — ESP8266 ESP-12F 8-channel relay board.
 *
 * Listens to a transceiver's TCI server (IW7DMH TCI library, bundled and ported
 * to ESP8266 cooperative polling), resolves the active band from RX-1 VFO A,
 * and switches the operator-mapped antenna relay using exclusive
 * break-before-make. Web portal for band→relay mapping and manual override;
 * ArduinoOTA for firmware updates. See CLAUDE.md for the full requirements.
 *
 * Board: werner.rothschopf.net ESP-12F 8-ch relay (relays active-HIGH).
 * Library: TCI by IW7DMH (bundled in this sketch folder; see CLAUDE.md).
 */
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include "Config.h"
#include "BandPlan.h"
#include "OutputStage.h"
#include "RadioSource.h"
#include "TciSource.h"
#include "WebPortal.h"

#define STATUS_LED   2          // onboard blue LED (active-LOW), GPIO2
#define AP_SSID      "ANT-SW-Setup"

Config     g_cfg;
Relay8x1   g_out;               // 8×1 exclusive break-before-make output stage
WebPortal  g_web;
TciSource  g_radio;             // single TCI radio (RX-1 VFO A)

bool     g_apMode   = false;
int      g_override = -2;       // -2 auto (TCI), -1 force none, 0..7 force relay

// ---- web portal callbacks --------------------------------------------------
String statusJson() {
  String j = "{";
  j += "\"ap\":"           + String(g_apMode ? 1 : 0) + ",";
  j += "\"wifi\":"         + String(WiFi.status() == WL_CONNECTED ? 1 : 0) + ",";
  j += "\"ip\":\""         + (g_apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\",";
  j += "\"tci\":"          + String(g_radio.connected() ? 1 : 0) + ",";
  j += "\"freq\":"         + String(g_radio.freqHz()) + ",";
  j += "\"band\":\""       + String(bandName(g_radio.band())) + "\",";
  j += "\"tx\":"           + String(g_radio.isTx() ? 1 : 0) + ",";
  j += "\"tune\":"         + String(g_radio.isTune() ? 1 : 0) + ",";
  j += "\"override\":"     + String(g_override) + ",";
  j += "\"active_relay\":" + String(g_out.activeRelay()) + ",";   // -1 = none
  j += "\"switching\":"    + String(g_out.switching() ? 1 : 0);
  j += "}";
  return j;
}

void onSave() {
  g_out.setGuardMs(g_cfg.guard_ms);
  if (!g_apMode) {
    g_radio.disconnect();
    g_radio.configure(g_cfg.tci_host, g_cfg.tci_port, g_cfg.iaru_region);
    if (strlen(g_cfg.tci_host)) g_radio.connect();
  }
}
void onOverride(int mode) { g_override = mode; }
void onReboot()           { delay(200); ESP.restart(); }

// ---- WiFi / OTA ------------------------------------------------------------
bool connectSTA() {
  if (strlen(g_cfg.wifi_ssid) == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(g_cfg.hostname);
  WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(200); yield(); }
  return WiFi.status() == WL_CONNECTED;
}

void startAP() {
  g_apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
}

void setupOTA() {
  ArduinoOTA.setHostname(g_cfg.hostname);
  if (strlen(g_cfg.ota_pass)) ArduinoOTA.setPassword(g_cfg.ota_pass);
  ArduinoOTA.onStart([]() {
    // R4.3: safe state + drop TCI so the WS client can't fight the updater.
    g_out.setInhibit(true);
    g_out.beginSafe();
    g_radio.disconnect();
  });
  ArduinoOTA.begin();
}

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line == "reset") {
    Serial.println(F("[serial] factory reset"));
    configWipe();
    delay(200);
    ESP.restart();
  } else if (line == "status") {
    Serial.println(statusJson());
  } else if (line == "auto") {
    g_override = -2;
  } else if (line == "off") {
    g_override = -1;
  } else if (line.startsWith("relay ")) {
    int r = line.substring(6).toInt() - 1;
    if (r >= 0 && r < NUM_RELAYS) g_override = r;
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println(F("AntennaSwitchController (ESP8266) booting"));

  // 1. Relays to the safe (de-energized) state FIRST, before WiFi/TCI, to
  //    cover the GPIO0/15/16 boot glitch (CLAUDE.md R1 / R2.10).
  bool valid = configLoad(g_cfg);
  g_out.begin(g_cfg.guard_ms);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);   // LED off (active-LOW)
  Serial.printf("config %s, hostname=%s\n", valid ? "loaded" : "defaulted", g_cfg.hostname);

  // 2. Network.
  if (valid && connectSTA()) {
    g_apMode = false;
    Serial.printf("WiFi up: %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(g_cfg.hostname)) {
      MDNS.addService("http", "tcp", 80);
      // Dedicated service so the macOS app discovers only antenna switches.
      MDNS.addService("antsw", "tcp", 80);
      MDNS.addServiceTxt("antsw", "tcp", "version", FW_VERSION);
      MDNS.addServiceTxt("antsw", "tcp", "product", "AntennaSwitchController");
      MDNS.addServiceTxt("antsw", "tcp", "host", g_cfg.hostname);
    }
    setupOTA();

    g_radio.configure(g_cfg.tci_host, g_cfg.tci_port, g_cfg.iaru_region);
    if (strlen(g_cfg.tci_host)) g_radio.connect();
  } else {
    startAP();
    Serial.printf("setup AP '%s' at http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  // 3. Web portal.
  g_web.begin(g_cfg, statusJson, onSave, onOverride, onReboot);
  Serial.println(F("ready"));
}

void loop() {
  g_web.tick();

  bool link = false;
  if (!g_apMode) {
    MDNS.update();
    ArduinoOTA.handle();
    if (WiFi.status() == WL_CONNECTED) {
      g_radio.process();                 // pump TCI WebSocket + refresh state
      link = g_radio.connected();
    }
  }

  // R2.9: no hot-switching while the radio is transmitting/tuning.
  g_out.setInhibit(g_radio.isTx() || g_radio.isTune());

  // Decide what should be connected (R2.7 mapping, R2.10 failsafe).
  int band = g_radio.band();
  int desired;
  if (g_override != -2) {
    desired = (g_override == -1) ? -1 : g_override;   // manual override (R2.11)
  } else if (!link || band < 0) {
    desired = -1;                                     // failsafe: all off
  } else {
    desired = g_cfg.band_relay[band];                 // may be -1 (none)
  }
  g_out.setDesired(desired);
  g_out.tick();                                       // break-before-make

  digitalWrite(STATUS_LED, link ? LOW : HIGH);        // LED on = TCI linked
  handleSerial();
  delay(2);                                           // yield to WiFi/TCI
}
