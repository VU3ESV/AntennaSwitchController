// WebPortal.h — HTTP configuration + control UI (R3.1/R3.2).
//
// AntennaSwitchController (ESP8266). Routes:
//   GET  /          config form: WiFi, TCI, band→relay map, manual override
//   POST /save      persist settings
//   GET  /status    JSON operational state
//   POST /relay     manual override (set=auto|none|0..7)
//   POST /reboot    soft reboot
//   GET  /discover  device identity + mDNS metadata
#ifndef WEBPORTAL_H
#define WEBPORTAL_H

#include <ESP8266WebServer.h>
#include "Config.h"
#include "BandPlan.h"
#include "OutputStage.h"

#ifndef FW_VERSION
#define FW_VERSION "1.0"
#endif

typedef String (*StatusFn)();
typedef void   (*SaveFn)();
typedef void   (*OverrideFn)(int);   // -2 auto, -1 none, 0..7 relay
typedef void   (*RebootFn)();
typedef int    (*ClaimFn)(int);      // SO2R: slave claim → 1 granted / 0 denied
typedef void   (*ReleaseFn)();       // SO2R: slave release
typedef String (*InterlockFn)();     // SO2R: interlock state JSON

class WebPortal {
 public:
  void begin(Config& cfg, StatusFn statusFn, SaveFn saveFn,
             OverrideFn ovrFn, RebootFn rebootFn,
             ClaimFn claimFn = nullptr, ReleaseFn releaseFn = nullptr,
             InterlockFn interlockFn = nullptr) {
    cfg_         = &cfg;
    statusFn_    = statusFn;
    saveFn_      = saveFn;
    ovrFn_       = ovrFn;
    rebootFn_    = rebootFn;
    claimFn_     = claimFn;
    releaseFn_   = releaseFn;
    interlockFn_ = interlockFn;

    server_.on("/",         HTTP_GET,  [this]{ handleRoot(); });
    server_.on("/save",     HTTP_POST, [this]{ handleSave(); });
    server_.on("/status",   HTTP_GET,  [this]{ server_.send(200, "application/json", statusFn_()); });
    server_.on("/config",   HTTP_GET,  [this]{ handleConfig(); });
    server_.on("/relay",    HTTP_POST, [this]{ handleRelay(); });
    server_.on("/reboot",   HTTP_POST, [this]{ server_.send(200, "text/plain", "rebooting"); rebootFn_(); });
    server_.on("/discover", HTTP_GET,  [this]{ handleDiscover(); });
    // SO2R Mode A interlock API (§6.1).
    server_.on("/interlock",         HTTP_GET,  [this]{ handleInterlockGet(); });
    server_.on("/interlock/claim",   HTTP_POST, [this]{ handleClaim(); });
    server_.on("/interlock/release", HTTP_POST, [this]{ handleRelease(); });
    server_.begin();
  }

  void tick() { server_.handleClient(); }

 private:
  ESP8266WebServer server_{80};
  Config*     cfg_         = nullptr;
  StatusFn    statusFn_    = nullptr;
  SaveFn      saveFn_      = nullptr;
  OverrideFn  ovrFn_       = nullptr;
  RebootFn    rebootFn_    = nullptr;
  ClaimFn     claimFn_     = nullptr;
  ReleaseFn   releaseFn_   = nullptr;
  InterlockFn interlockFn_ = nullptr;

  static String esc(const char* s) {
    String o;
    o.reserve(strlen(s) + 8);       // fits the common (unescaped) case in one alloc
    for (const char* p = s; *p; p++) {
      switch (*p) {
        case '&': o += "&amp;";  break;
        case '<': o += "&lt;";   break;
        case '>': o += "&gt;";   break;
        case '"': o += "&quot;"; break;
        default:  o += *p;       break;
      }
    }
    return o;
  }

  // Display label for a relay: the operator's name if set, else "Relay N".
  static String relayLabel(const Config& c, int r) {
    if (c.relay_name[r][0]) return esc(c.relay_name[r]);
    return "Relay " + String(r + 1);
  }

  void handleRoot() {
    Config& c = *cfg_;
    String h;
    h.reserve(4096);
    h += F("<!doctype html><html><head><meta charset=utf-8>"
           "<meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>Antenna Switch Controller</title><style>"
           "body{font-family:system-ui,sans-serif;max-width:640px;margin:1rem auto;padding:0 1rem;color:#111}"
           "h1{font-size:1.25rem}fieldset{margin:1rem 0;border:1px solid #ccc;border-radius:8px}"
           "label{display:block;margin:.4rem 0 .1rem;font-size:.9rem}"
           "input,select{width:100%;padding:.4rem;box-sizing:border-box}"
           "table{width:100%;border-collapse:collapse}td{padding:.2rem}"
           "button{padding:.5rem 1rem;margin:.2rem 0;border-radius:6px;border:1px solid #888;background:#f3f3f3}"
           ".row{display:flex;gap:.5rem}.row>div{flex:1}.muted{color:#666;font-size:.8rem}"
           "</style></head><body>");
    h += F("<h1>Antenna Switch Controller</h1>");
    h += "<p class=muted>Host: <b>" + esc(c.hostname) + ".local</b> &middot; "
         "<a href='/status'>status JSON</a></p>";

    // --- settings form ---
    h += F("<form method=POST action=/save>");

    h += F("<fieldset><legend>WiFi</legend>");
    h += "<label>SSID</label><input name=ssid value=\"" + esc(c.wifi_ssid) + "\">";
    h += F("<label>Password <span class=muted>(blank = keep)</span></label><input name=pass type=password>");
    h += F("</fieldset>");

    h += F("<fieldset><legend>Radio</legend>");
    h += F("<label>Type</label><select name=rtype>");
    h += "<option value=0" + String(c.radio_type == RADIO_TCI  ? " selected" : "") + ">TCI (ExpertSDR / SunSDR)</option>";
    h += "<option value=1" + String(c.radio_type == RADIO_FLEX ? " selected" : "") + ">FlexRadio (SmartSDR TCP)</option>";
    h += F("</select><div class=row>");
    h += "<div><label>Host / IP</label><input name=host value=\"" + esc(c.tci_host) + "\"></div>";
    h += "<div><label>Port</label><input name=port type=number value=" + String(c.tci_port) + "></div>";
    h += F("</div><label>Receiver <span class=muted>(TCI, 2-RX radios)</span></label><select name=rrx>");
    h += "<option value=0" + String(c.radio_rx == 0 ? " selected" : "") + ">RX1</option>";
    h += "<option value=1" + String(c.radio_rx == 1 ? " selected" : "") + ">RX2</option>";
    h += F("</select><p class=muted>TCI port default 50001; FlexRadio SmartSDR is 4992.</p>");
    h += F("<label>IARU region <span class=muted>(TCI only)</span></label><select name=region>");
    for (int r = 1; r <= 3; r++)
      h += "<option value=" + String(r) + (c.iaru_region == r ? " selected" : "") + ">" + String(r) + "</option>";
    h += F("</select></fieldset>");

    // --- relay (antenna) names ---
    h += F("<fieldset><legend>Relay names</legend>"
           "<p class=muted>Name each relay's antenna (e.g. \"80m Dipole\"). "
           "Blank uses the default \"Relay N\". Shown in the app and the band map below.</p><table>");
    for (int r = 0; r < NUM_RELAYS; r++) {
      h += "<tr><td><b>R" + String(r + 1) + "</b> <span class=muted>(GPIO" + String(kRelayPin[r]) + ")</span></td>"
           "<td><input name=rn" + String(r) + " maxlength=15 value=\"" + esc(c.relay_name[r]) +
           "\" placeholder=\"Relay " + String(r + 1) + "\"></td></tr>";
    }
    h += F("</table></fieldset>");

    h += F("<fieldset><legend>Band &rarr; Relay map</legend>"
           "<table><tr><td></td><td class=muted>Primary</td><td class=muted>Fallback (SO2R)</td></tr>");
    for (int b = 0; b < NUM_BANDS; b++) {
      h += "<tr><td><b>" + String(bandName(b)) + "</b></td>";
      // Primary antenna.
      h += "<td><select name=b" + String(b) + ">";
      h += "<option value=-1" + String(c.band_relay[b] == -1 ? " selected" : "") + ">None / bypass</option>";
      for (int r = 0; r < NUM_RELAYS; r++)
        h += "<option value=" + String(r) + (c.band_relay[b] == r ? " selected" : "") +
             ">" + relayLabel(c, r) + " (GPIO" + String(kRelayPin[r]) + ")</option>";
      h += F("</select></td>");
      // Secondary / fallback antenna (used when the primary is taken by the other radio).
      h += "<td><select name=s" + String(b) + ">";
      h += "<option value=-1" + String(c.band_relay2[b] == -1 ? " selected" : "") + ">None</option>";
      for (int r = 0; r < NUM_RELAYS; r++)
        h += "<option value=" + String(r) + (c.band_relay2[b] == r ? " selected" : "") +
             ">" + relayLabel(c, r) + "</option>";
      h += F("</select></td></tr>");
    }
    h += F("</table><p class=muted>Multiple bands may share one relay. "
           "<b>Fallback</b> is used in SO2R (Master/Slave or Dual) when the primary "
           "antenna is already in use by the other radio &mdash; e.g. a HexBeam on 20&ndash;6&nbsp;m "
           "with a wire dipole as fallback. Relays on GPIO0/15/16 may twitch at power-up.</p></fieldset>");

    // --- SO2R Mode A (master/slave) ---
    h += F("<fieldset><legend>SO2R role (Mode A)</legend>");
    h += F("<label>Role</label><select name=mode>");
    h += "<option value=0" + String(c.mode == MODE_STANDALONE ? " selected" : "") + ">Standalone (single radio)</option>";
    h += "<option value=1" + String(c.mode == MODE_MASTER     ? " selected" : "") + ">Master (Mode A — Radio 1, arbiter)</option>";
    h += "<option value=2" + String(c.mode == MODE_SLAVE      ? " selected" : "") + ">Slave (Mode A — Radio 2)</option>";
    h += "<option value=3" + String(c.mode == MODE_DUAL       ? " selected" : "") + ">Dual (Mode B — both radios, 8&times;2)</option>";
    h += F("</select>");
    h += "<label>Master address <span class=muted>(slave only — IP of the master)</span></label>"
         "<input name=peer value=\"" + esc(c.peer_host) + "\">";
    h += F("<label>Interlock policy</label><select name=ilk>");
    h += "<option value=0" + String(c.interlock_policy == ILK_FIRST_COME ? " selected" : "") + ">First-come (holder keeps it)</option>";
    h += "<option value=1" + String(c.interlock_policy == ILK_PRIORITY   ? " selected" : "") + ">Priority (Radio 1 wins)</option>";
    h += F("</select><label>On master loss (slave)</label><select name=ploss>");
    h += "<option value=0" + String(c.on_peer_loss == PEER_LOSS_SAFE ? " selected" : "") + ">Safe (all off)</option>";
    h += "<option value=1" + String(c.on_peer_loss == PEER_LOSS_HOLD ? " selected" : "") + ">Hold last</option>";
    h += F("</select><p class=muted>Mode A: two 8&times;1 boards (one radio each) over the LAN. "
           "Mode B: one board drives an external 8&times;2 switch from both radios below.</p></fieldset>");

    // --- Mode B: radio 2 + external switch type ---
    h += F("<fieldset><legend>Radio 2 (Mode B / Dual)</legend>");
    h += F("<label>Type</label><select name=r2type>");
    h += "<option value=0" + String(c.radio2_type == RADIO_TCI  ? " selected" : "") + ">TCI (ExpertSDR / SunSDR)</option>";
    h += "<option value=1" + String(c.radio2_type == RADIO_FLEX ? " selected" : "") + ">FlexRadio (SmartSDR TCP)</option>";
    h += F("</select><div class=row>");
    h += "<div><label>Host / IP</label><input name=r2host value=\"" + esc(c.radio2_host) + "\"></div>";
    h += "<div><label>Port</label><input name=r2port type=number value=" + String(c.radio2_port) + "></div>";
    h += F("</div><label>Receiver <span class=muted>(TCI, 2-RX radios)</span></label><select name=r2rx>");
    h += "<option value=0" + String(c.radio2_rx == 0 ? " selected" : "") + ">RX1</option>";
    h += "<option value=1" + String(c.radio2_rx == 1 ? " selected" : "") + ">RX2</option>";
    h += F("</select><p class=muted>For one 2-receiver radio (e.g. SunSDR2): set radio 1 + radio 2 to the "
           "<b>same Host/Port</b>, radio 1 = RX1, radio 2 = RX2.</p>");
    h += F("<label>External switch</label><select name=swtype>");
    h += "<option value=0" + String(c.switch_type == SWITCH_8X1 ? " selected" : "") + ">8&times;1 (single radio)</option>";
    h += "<option value=1" + String(c.switch_type == SWITCH_8X2 ? " selected" : "") + ">8&times;2 (per-antenna A/B, 8&times; SPDT)</option>";
    h += F("</select><p class=muted>Dual drives an external 8&times;2 switch: relay <i>i</i> "
           "de-energized routes antenna <i>i</i> to Radio 1, energized to Radio 2. "
           "One serial-CAT radio max per board, so radio 2 must be TCI/Flex here.</p></fieldset>");

    h += F("<fieldset><legend>Device</legend>");
    h += "<label>Hostname (mDNS / OTA)</label><input name=hostname value=\"" + esc(c.hostname) + "\">";
    h += F("<label>OTA password <span class=muted>(blank = keep)</span></label><input name=otapass type=password>");
    h += "<label>Break-before-make guard (ms)</label><input name=guard type=number value=" + String(c.guard_ms) + ">";
    h += F("</fieldset>");

    h += F("<button type=submit>Save &amp; apply</button></form>");

    // --- manual override + reboot ---
    h += F("<fieldset><legend>Manual override</legend>"
           "<p class=muted>Force a relay regardless of TCI. 'Auto' returns to band tracking.</p>"
           "<div class=row>"
           "<form method=POST action='/relay?set=auto'><button>Auto (TCI)</button></form>"
           "<form method=POST action='/relay?set=none'><button>All off</button></form>"
           "</div><div class=row>");
    for (int r = 0; r < NUM_RELAYS; r++)
      h += "<form method=POST action='/relay?set=" + String(r) + "'><button>" +
           (c.relay_name[r][0] ? esc(c.relay_name[r]) : "R" + String(r + 1)) + "</button></form>";
    h += F("</div></fieldset>");

    h += F("<form method=POST action=/reboot onsubmit=\"return confirm('Reboot?')\">"
           "<button>Reboot</button></form>");
    h += F("</body></html>");
    server_.send(200, "text/html", h);
  }

  void copyArg(const char* name, char* dst, size_t n, bool keepIfEmpty) {
    if (!server_.hasArg(name)) return;
    String v = server_.arg(name);
    if (keepIfEmpty && v.length() == 0) return;
    strncpy(dst, v.c_str(), n - 1);
    dst[n - 1] = '\0';
  }

  void handleSave() {
    Config& c = *cfg_;
    copyArg("ssid",     c.wifi_ssid, sizeof(c.wifi_ssid), false);
    copyArg("pass",     c.wifi_pass, sizeof(c.wifi_pass), true);   // blank = keep
    copyArg("host",     c.tci_host,  sizeof(c.tci_host),  false);
    copyArg("hostname", c.hostname,  sizeof(c.hostname),  false);
    copyArg("otapass",  c.ota_pass,  sizeof(c.ota_pass),  true);   // blank = keep
    copyArg("peer",     c.peer_host,   sizeof(c.peer_host),   false);
    copyArg("r2host",   c.radio2_host, sizeof(c.radio2_host), false);
    if (server_.hasArg("mode"))   c.mode             = (uint8_t)constrain(server_.arg("mode").toInt(), 0, 3);
    if (server_.hasArg("ilk"))    c.interlock_policy = (uint8_t)constrain(server_.arg("ilk").toInt(), 0, 1);
    if (server_.hasArg("ploss"))  c.on_peer_loss     = (uint8_t)constrain(server_.arg("ploss").toInt(), 0, 1);
    if (server_.hasArg("r2type")) c.radio2_type      = (uint8_t)constrain(server_.arg("r2type").toInt(), 0, 1);
    if (server_.hasArg("r2port")) c.radio2_port      = (uint16_t)server_.arg("r2port").toInt();
    if (server_.hasArg("swtype")) c.switch_type      = (uint8_t)constrain(server_.arg("swtype").toInt(), 0, 1);
    if (server_.hasArg("rrx"))    c.radio_rx         = (uint8_t)constrain(server_.arg("rrx").toInt(), 0, 1);
    if (server_.hasArg("r2rx"))   c.radio2_rx        = (uint8_t)constrain(server_.arg("r2rx").toInt(), 0, 1);
    if (server_.hasArg("port"))   c.tci_port    = (uint16_t)server_.arg("port").toInt();
    if (server_.hasArg("region")) c.iaru_region = (uint8_t)constrain(server_.arg("region").toInt(), 1, 3);
    if (server_.hasArg("rtype"))  c.radio_type  = (uint8_t)constrain(server_.arg("rtype").toInt(), 0, 1);
    if (server_.hasArg("guard"))  c.guard_ms    = (uint16_t)constrain(server_.arg("guard").toInt(), 0, 5000);
    for (int b = 0; b < NUM_BANDS; b++) {
      String key = "b" + String(b);
      if (server_.hasArg(key))
        c.band_relay[b] = (int8_t)constrain(server_.arg(key).toInt(), -1, NUM_RELAYS - 1);
      String skey = "s" + String(b);
      if (server_.hasArg(skey))
        c.band_relay2[b] = (int8_t)constrain(server_.arg(skey).toInt(), -1, NUM_RELAYS - 1);
    }
    for (int r = 0; r < NUM_RELAYS; r++) {
      String key = "rn" + String(r);
      copyArg(key.c_str(), c.relay_name[r], RELAY_NAME_LEN, false);  // "" clears → default Rn
    }
    configSave(c);
    if (saveFn_) saveFn_();
    server_.sendHeader("Location", "/");
    server_.send(303, "text/plain", "saved");
  }

  void handleRelay() {
    String s = server_.arg("set");
    int mode;
    if      (s == "auto") mode = -2;
    else if (s == "none") mode = -1;
    else                  mode = constrain(s.toInt(), 0, NUM_RELAYS - 1);
    if (ovrFn_) ovrFn_(mode);
    server_.sendHeader("Location", "/");
    server_.send(303, "text/plain", "ok");
  }

  // --- SO2R Mode A interlock API (§6.1) -------------------------------------
  void handleInterlockGet() {
    server_.send(200, "application/json", interlockFn_ ? interlockFn_() : "{}");
  }
  void handleClaim() {                       // slave → master: claim an antenna
    int ant = server_.hasArg("ant") ? server_.arg("ant").toInt() : -1;
    int granted = claimFn_ ? claimFn_(ant) : 0;
    server_.send(200, "text/plain", granted ? "1" : "0");
  }
  void handleRelease() {                     // slave → master: release its hold
    if (releaseFn_) releaseFn_();
    server_.send(200, "text/plain", "1");
  }

  // Current stored settings as JSON, for the macOS app to populate its forms.
  // Passwords (wifi_pass, ota_pass) are deliberately never returned.
  void handleConfig() {
    Config& c = *cfg_;
    String j;
    j.reserve(512);                 // whole config in one alloc (no per-append churn)
    j += "{";
    j += "\"hostname\":\""  + esc(c.hostname) + "\",";
    j += "\"ssid\":\""      + esc(c.wifi_ssid) + "\",";
    j += "\"tci_host\":\""  + esc(c.tci_host) + "\",";
    j += "\"tci_port\":"    + String(c.tci_port) + ",";
    j += "\"radio_type\":"  + String(c.radio_type) + ",";
    j += "\"mode\":"        + String(c.mode) + ",";
    j += "\"peer_host\":\"" + esc(c.peer_host) + "\",";
    j += "\"interlock_policy\":" + String(c.interlock_policy) + ",";
    j += "\"on_peer_loss\":" + String(c.on_peer_loss) + ",";
    j += "\"radio2_type\":" + String(c.radio2_type) + ",";
    j += "\"radio2_host\":\"" + esc(c.radio2_host) + "\",";
    j += "\"radio2_port\":" + String(c.radio2_port) + ",";
    j += "\"switch_type\":" + String(c.switch_type) + ",";
    j += "\"radio_rx\":"    + String(c.radio_rx) + ",";
    j += "\"radio2_rx\":"   + String(c.radio2_rx) + ",";
    j += "\"region\":"      + String(c.iaru_region) + ",";
    j += "\"guard_ms\":"    + String(c.guard_ms) + ",";
    j += "\"relay_names\":[";
    for (int r = 0; r < NUM_RELAYS; r++) {
      if (r) j += ",";
      j += "\"" + esc(c.relay_name[r]) + "\"";
    }
    j += "],";
    j += "\"bands\":[";
    for (int b = 0; b < NUM_BANDS; b++) {
      if (b) j += ",";
      j += String(c.band_relay[b]);
    }
    j += "],\"bands2\":[";                          // per-band SO2R fallback relay
    for (int b = 0; b < NUM_BANDS; b++) {
      if (b) j += ",";
      j += String(c.band_relay2[b]);
    }
    j += "]}";
    server_.send(200, "application/json", j);
  }

  void handleDiscover() {
    Config& c = *cfg_;
    String j = "{\"device\":\"AntennaSwitchController\",\"version\":\"" FW_VERSION "\",\"hostname\":\"";
    j += esc(c.hostname) + "\",\"mdns\":\"" + esc(c.hostname) + ".local\",";
    j += "\"ip\":\"" + WiFi.localIP().toString() + "\",\"relays\":" + String(NUM_RELAYS) + "}";
    server_.send(200, "application/json", j);
  }
};

#endif // WEBPORTAL_H
