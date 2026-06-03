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
#include "FlexSource.h"
#include "Interlock.h"
#include "WebPortal.h"

#define STATUS_LED   2          // onboard blue LED (active-LOW), GPIO2
#define AP_SSID      "ANT-SW-Setup"

Config     g_cfg;
Relay8x1   g_out;               // 8×1 break-before-make (standalone/master/slave)
Relay8x2   g_dual;              // 8×2 per-antenna A/B select (Mode B)
WebPortal  g_web;

// Radio 1 sources; g_radio points at the one selected by cfg.radio_type.
TciSource    g_tci;             // TCI (RX-1 VFO A)
FlexSource   g_flex;            // FlexRadio SmartSDR (TCP 4992)
RadioSource* g_radio = &g_tci;

// Radio 2 sources (Mode B only); g_radio2 selected by cfg.radio2_type.
TciSource    g_tci2;
FlexSource   g_flex2;
RadioSource* g_radio2 = &g_tci2;

// Interlock: Mode A over the LAN (master/slave); Mode B in firmware (dual).
MasterArbiter g_master;         // master: the LAN arbiter
SlaveClient   g_slave;          // slave: claims antennas from the master
DualResolver  g_resolver;       // dual: in-firmware first-come resolver

bool     g_apMode   = false;
int      g_override = -2;       // -2 auto, -1 force none, 0..7 force relay

// Point g_radio at the configured transport, (re)connect it. Disconnects the
// previously-selected source first so /save can switch transports cleanly.
void applyRadio() {
  g_radio->disconnect();
  g_tci.setRig(g_cfg.radio_rx);        // which TCI receiver (0=RX1, 1=RX2)
  g_radio = (g_cfg.radio_type == RADIO_FLEX) ? (RadioSource*)&g_flex
                                             : (RadioSource*)&g_tci;
  g_radio->configure(g_cfg.tci_host, g_cfg.tci_port, g_cfg.iaru_region);
  if (strlen(g_cfg.tci_host)) g_radio->connect();
}

// Mode B radio 2. Disconnects both candidates first, then selects + connects.
// Must run AFTER applyRadio() so radio 1's TCI link is configured first (the
// shared-client path below reads it).
void applyRadio2() {
  g_tci2.disconnect();                       // no-op if it was sharing radio 1
  g_flex2.disconnect();
  g_tci2.useOwnClient();                     // reset; re-decide sharing below
  if (g_cfg.mode != MODE_DUAL) return;       // radio 2 only exists in dual mode
  g_tci2.setRig(g_cfg.radio2_rx);            // RX2 for a 2-receiver radio

  // SunSDR2 case: both "radios" are the same TCI server (one rig, two
  // receivers). One link already carries RX1+RX2 (rtx[0]/rtx[1]), so share
  // radio 1's client instead of opening a second WebSocket — this is the fix
  // for the laggy switching seen with two sockets to one radio.
  bool shareTci = g_cfg.radio_type  == RADIO_TCI &&
                  g_cfg.radio2_type == RADIO_TCI &&
                  g_cfg.radio2_port == g_cfg.tci_port &&
                  strcmp(g_cfg.radio2_host, g_cfg.tci_host) == 0;
  if (shareTci) {
    g_tci2.shareWith(g_tci);                 // reads rtx[radio2_rx] off radio 1
    g_radio2 = &g_tci2;                       // no second connection
    return;
  }

  g_radio2 = (g_cfg.radio2_type == RADIO_FLEX) ? (RadioSource*)&g_flex2
                                               : (RadioSource*)&g_tci2;
  g_radio2->configure(g_cfg.radio2_host, g_cfg.radio2_port, g_cfg.iaru_region);
  if (strlen(g_cfg.radio2_host)) g_radio2->connect();
}

// Apply interlock config (Mode A master policy / slave peer; Mode B policy).
void applyInterlock() {
  g_master.setPolicy(g_cfg.interlock_policy);
  g_slave.configure(g_cfg.peer_host, g_cfg.on_peer_loss);
  g_resolver.setPolicy(g_cfg.interlock_policy);
}

// Map a radio's band to its desired antenna, honoring the link failsafe.
int desiredFor(RadioSource* r, bool link) {
  int b = r->band();
  if (!link || b < 0) return -1;
  return g_cfg.band_relay[b];
}

// Resolve the local desired antenna through the active mode's interlock
// (single-relay modes only; dual is driven separately in loop()).
int interlockResolve(int localDesired) {
  switch (g_cfg.mode) {
    case MODE_MASTER: g_master.tick(); return g_master.resolveMaster(localDesired);
    case MODE_SLAVE:  return g_slave.resolve(localDesired);
    default:          return localDesired;   // standalone
  }
}

// ---- interlock HTTP callbacks (master side) --------------------------------
int  onClaim(int ant)  { return (g_cfg.mode == MODE_MASTER) ? g_master.claim(ant) : 0; }
void onRelease()       { if (g_cfg.mode == MODE_MASTER) g_master.release(); }
String interlockJson() {
  const char* role = g_cfg.mode == MODE_MASTER ? "master"
                   : g_cfg.mode == MODE_SLAVE  ? "slave"
                   : g_cfg.mode == MODE_DUAL   ? "dual" : "standalone";
  String j = "{\"role\":\"";
  j += role;
  if (g_cfg.mode == MODE_DUAL) {
    // One board sees both radios; report each radio's granted antenna.
    j += "\",\"peer_up\":1";
    j += ",\"master_ant\":" + String(g_dual.radio1Ant());   // Radio 1
    j += ",\"slave_ant\":"  + String(g_dual.radio2Ant());   // Radio 2
  } else {
    bool up   = g_cfg.mode == MODE_MASTER ? g_master.peerUp()
              : g_cfg.mode == MODE_SLAVE  ? g_slave.peerUp() : false;
    int missed = g_cfg.mode == MODE_MASTER ? g_master.beatsMissed()
               : g_cfg.mode == MODE_SLAVE  ? g_slave.missedBeats() : 0;
    j += "\",\"peer_up\":" + String(up ? 1 : 0);
    j += ",\"beats_missed\":" + String(missed);   // heartbeat health
    j += ",\"master_ant\":" + String(g_master.masterAnt());
    j += ",\"slave_ant\":"  + String(g_master.slaveAnt());
  }
  j += "}";
  return j;
}

// ---- web portal callbacks --------------------------------------------------
String statusJson() {
  String j = "{";
  j += "\"ap\":"           + String(g_apMode ? 1 : 0) + ",";
  j += "\"wifi\":"         + String(WiFi.status() == WL_CONNECTED ? 1 : 0) + ",";
  j += "\"ip\":\""         + (g_apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\",";
  j += "\"tci\":"          + String(g_radio->connected() ? 1 : 0) + ",";
  j += "\"freq\":"         + String(g_radio->freqHz()) + ",";
  j += "\"band\":\""       + String(bandName(g_radio->band())) + "\",";
  j += "\"tx\":"           + String(g_radio->isTx() ? 1 : 0) + ",";
  j += "\"tune\":"         + String(g_radio->isTune() ? 1 : 0) + ",";
  j += "\"override\":"     + String(g_override) + ",";
  // In dual mode the energized relay is Radio 2's antenna; expose both.
  int activeRelay = (g_cfg.mode == MODE_DUAL) ? g_dual.radio2Ant() : g_out.activeRelay();
  bool sw         = (g_cfg.mode == MODE_DUAL) ? g_dual.switching()  : g_out.switching();
  j += "\"active_relay\":" + String(activeRelay) + ",";           // -1 = none
  j += "\"switching\":"    + String(sw ? 1 : 0) + ",";
  if (g_cfg.mode == MODE_DUAL) {
    j += "\"relay_mask\":" + String(g_dual.energizedMask()) + ",";  // bits of HIGH relays
    j += "\"radio2\":{\"tci\":"  + String(g_radio2->connected() ? 1 : 0);
    j += ",\"freq\":"            + String(g_radio2->freqHz());
    j += ",\"band\":\""          + String(bandName(g_radio2->band())) + "\"";
    j += ",\"tx\":"              + String((g_radio2->isTx() || g_radio2->isTune()) ? 1 : 0) + "},";
  }
  j += "\"interlock\":"    + interlockJson();
  j += "}";
  return j;
}

void onSave() {
  g_out.setGuardMs(g_cfg.guard_ms);
  g_dual.setGuardMs(g_cfg.guard_ms);
  applyInterlock();                 // master policy / slave peer / dual policy
  if (!g_apMode) { applyRadio(); applyRadio2(); }   // radio 1 (+ radio 2 if dual)
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
    // R4.3: safe state + drop the radio link(s) so they can't fight the updater.
    g_out.setInhibit(true);
    g_out.beginSafe();
    g_dual.beginSafe();
    g_radio->disconnect();
    g_radio2->disconnect();
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
  g_dual.begin(g_cfg.guard_ms);    // shares the relay pins; both boot to safe
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

    applyRadio();                      // radio 1: select TCI/Flex by radio_type
    applyRadio2();                     // radio 2: connect only in dual mode
  } else {
    startAP();
    Serial.printf("setup AP '%s' at http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  // 3. SO2R interlock + web portal.
  applyInterlock();
  g_web.begin(g_cfg, statusJson, onSave, onOverride, onReboot,
              onClaim, onRelease, interlockJson);
  Serial.println(F("ready"));
}

void loop() {
  g_web.tick();

  bool link = false, link2 = false;
  if (!g_apMode) {
    MDNS.update();
    ArduinoOTA.handle();
    if (WiFi.status() == WL_CONNECTED) {
      g_radio->process();                // pump transport + refresh state
      link = g_radio->connected();
      if (g_cfg.mode == MODE_DUAL) {     // Mode B also tracks radio 2
        g_radio2->process();
        link2 = g_radio2->connected();
      }
    }
  }

  if (g_cfg.mode == MODE_DUAL) {
    // Mode B: one board, both radios → external 8×2 switch. Resolve the two
    // desired antennas (first-come, per-radio TX-safety) then drive the A/B
    // lines. Manual override forces Radio 1; Radio 2 stays automatic.
    bool tx1 = g_radio->isTx()  || g_radio->isTune();
    bool tx2 = g_radio2->isTx() || g_radio2->isTune();
    int d1 = (g_override != -2) ? (g_override == -1 ? -1 : g_override)
                                : desiredFor(g_radio, link);
    int d2 = desiredFor(g_radio2, link2);
    int a1, a2;
    g_resolver.resolve(d1, d2, tx1, tx2, a1, a2);  // TX-safety inside resolver
    g_dual.setInhibit(false);
    g_dual.setDual(a1, a2);
    g_dual.tick();                                  // break-before-make (R2 line)
    digitalWrite(STATUS_LED, (link && link2) ? LOW : HIGH);
  } else {
    // R2.9: no hot-switching while the radio is transmitting/tuning.
    g_out.setInhibit(g_radio->isTx() || g_radio->isTune());

    int localDesired;
    if (g_override != -2) localDesired = (g_override == -1) ? -1 : g_override;  // R2.11
    else                  localDesired = desiredFor(g_radio, link);            // R2.7/R2.10

    // SO2R Mode A: arbitrate against the peer so the two radios never share an
    // antenna index (standalone passes through unchanged).
    int desired = interlockResolve(localDesired);
    g_out.setDesired(desired);
    g_out.tick();                                   // break-before-make
    digitalWrite(STATUS_LED, link ? LOW : HIGH);    // LED on = radio linked
  }

  handleSerial();
  delay(2);                                         // yield to WiFi/TCI
}
