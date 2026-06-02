// FlexSource.h — RadioSource over the FlexRadio SmartSDR Ethernet API (TCP 4992).
//
// AntennaSwitchController (ESP8266). Phase P1 of docs/MULTI-RADIO-SO2R-PLAN.md.
// FlexRadio is NOT generic "CAT over TCP" — it speaks the SmartSDR line
// protocol, so this is a dedicated source rather than a NetCatSource.
//
// Protocol (flexradio/smartsdr-api-docs wiki, port 4992; read-only here):
//   On connect the radio emits  "V<ver>"  then  "H<hex-handle>".
//   Client commands:  "C<seq>|<command>\n"   replies: "R<seq>|<hex>|...".
//   Async status:     "S<handle>|<object> <k>=<v> ...".
//   We subscribe with "sub slice all" and "sub tx all", then parse:
//     slice:    "S..|slice 0 in_use=1 RF_frequency=14.329000 ... tx=1"
//               RF_frequency is in MHz; tx=1 marks the transmit slice.
//     interlock:"S0|interlock state=TRANSMITTING ..."  (TX-safety, R2.9)
//
// Non-blocking and self-reconnecting: process() services the socket and
// refreshes cached band/TX each loop().
//
// NOTE: build-verified only — not yet tested against a live FlexRadio.
#ifndef FLEXSOURCE_H
#define FLEXSOURCE_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "RadioSource.h"
#include "BandPlan.h"

class FlexSource : public RadioSource {
 public:
  void configure(const char* host, int port, int /*iaruRegion*/) override {
    strncpy(host_, host ? host : "", sizeof(host_) - 1);
    host_[sizeof(host_) - 1] = '\0';
    port_ = port ? port : 4992;          // SmartSDR command port
  }

  void connect()    override { want_ = true; }
  void disconnect() override {
    want_ = false;
    client_.stop();
    subscribed_ = false;
  }

  bool connected() const override {
    return const_cast<WiFiClient&>(client_).connected();
  }
  int      band()   const override { return band_; }
  uint32_t freqHz() const override { return freq_; }
  bool     isTx()   const override { return tx_; }
  bool     isTune() const override { return false; }  // Flex tune asserts TX too

  void process() override {
    if (!want_) return;

    if (!client_.connected()) {
      subscribed_ = false;
      uint32_t now = millis();
      if (now - lastTry_ < kRetryMs) return;     // backoff between attempts
      lastTry_ = now;
      if (strlen(host_) == 0) return;
      client_.connect(host_, port_);             // brief blocking connect
      return;
    }

    if (!subscribed_) {                          // (re)subscribe after connect
      client_.printf("C%lu|sub slice all\n", (unsigned long)seq_++);
      client_.printf("C%lu|sub tx all\n",    (unsigned long)seq_++);
      subscribed_ = true;
    }

    while (client_.available()) {                // drain whole lines
      char ch = (char)client_.read();
      if (ch == '\n') { handleLine(); line_ = ""; }
      else if (ch != '\r' && line_.length() < 512) line_ += ch;
    }
  }

 private:
  static const int      kMaxSlices = 4;
  static const uint32_t kRetryMs   = 5000;

  void handleLine() {
    if (line_.length() < 2 || line_[0] != 'S') return;   // only status lines
    int bar = line_.indexOf('|');
    if (bar < 0) return;
    String msg = line_.substring(bar + 1);               // "<object> k=v ..."
    if      (msg.startsWith("slice "))     parseSlice(msg);
    else if (msg.startsWith("interlock ")) parseInterlock(msg);
  }

  // "slice 0 in_use=1 RF_frequency=14.329000 ... tx=1"  (fields may be partial)
  void parseSlice(const String& msg) {
    int sp = msg.indexOf(' ', 6);                         // after "slice "
    int idx = msg.substring(6, sp < 0 ? msg.length() : sp).toInt();
    if (idx < 0 || idx >= kMaxSlices) return;

    String v;
    if (field(msg, "in_use=", v))       sliceInUse_[idx] = (v.toInt() != 0);
    if (field(msg, "RF_frequency=", v)) sliceFreq_[idx]  = (uint32_t)(v.toFloat() * 1000000.0 + 0.5);
    if (field(msg, "tx=", v))           txSlice_ = (v.toInt() != 0) ? idx : (txSlice_ == idx ? -1 : txSlice_);
    recompute();
  }

  // "interlock state=TRANSMITTING ..." — R2.9 TX-safety.
  void parseInterlock(const String& msg) {
    String st;
    if (!field(msg, "state=", st)) return;
    tx_ = (st == "TRANSMITTING" || st == "PTT_REQUESTED");
  }

  // Active band = the TX slice if known & in use, else the lowest in-use slice.
  void recompute() {
    int use = -1;
    if (txSlice_ >= 0 && txSlice_ < kMaxSlices && sliceInUse_[txSlice_]) use = txSlice_;
    else for (int i = 0; i < kMaxSlices; i++) if (sliceInUse_[i]) { use = i; break; }
    if (use >= 0 && sliceFreq_[use] > 0) {
      freq_ = sliceFreq_[use];
      band_ = freqToBand(freq_);
    }
  }

  // Extract the value of `key` (e.g. "RF_frequency=") up to the next space.
  static bool field(const String& msg, const char* key, String& out) {
    int k = msg.indexOf(key);
    if (k < 0) return false;
    k += strlen(key);
    int sp = msg.indexOf(' ', k);
    out = msg.substring(k, sp < 0 ? msg.length() : sp);
    return true;
  }

  WiFiClient client_;
  char       host_[64] = {0};
  int        port_     = 4992;
  bool       want_     = false;
  bool       subscribed_ = false;
  uint32_t   lastTry_  = 0;
  uint32_t   seq_      = 1;
  String     line_;

  uint32_t sliceFreq_[kMaxSlices]  = {0};
  bool     sliceInUse_[kMaxSlices] = {false};
  int      txSlice_ = -1;

  uint32_t freq_ = 0;
  int      band_ = -1;
  bool     tx_   = false;
};

#endif // FLEXSOURCE_H
