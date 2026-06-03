// SerialCatSource.h — RadioSource over read-only serial CAT on UART0.
//
// AntennaSwitchController (ESP8266). Phase P1 of docs/MULTI-RADIO-SO2R-PLAN.md.
// Polls the radio's VFO frequency over the one hardware UART and derives the
// band (freqToBand). Two protocol families (grounded in nigelfenton/shackswitch):
//
//   Kenwood / modern Yaesu (ASCII) — send "IF;", reply "IF" + 11-digit Hz + …;
//     the frequency is the 11 digits at offset 2. (Kenwood TS-590/890, Elecraft
//     K3/K4, Yaesu FT-991A / FT-DX10.)
//   Icom CI-V (binary) — send  FE FE <addr> E0 03 FD;  reply carries 5 BCD bytes
//     (little-endian, 2 digits/byte). (IC-7300/9700/705/7610.)
//
// CAT here is **read-only** and provides band tracking only — it does NOT report
// TX/tune, so unlike TCI/Flex it can't inhibit hot-switching (R2.9). Don't change
// bands while transmitting on a CAT-only radio; sequence any amplifier externally.
// One serial radio per board (UART0 = the console; see HARDWARE.md §3) — so this
// takes over Serial, and the interactive serial console is disabled when active.
//
// NOTE: build-verified only — the parsers have host unit tests (Controller/test/),
// but the serial path has not been exercised against a live rig.
#ifndef SERIALCATSOURCE_H
#define SERIALCATSOURCE_H

#include <Arduino.h>
#include "RadioSource.h"
#include "BandPlan.h"
#include "Config.h"

// --- pure parsers (also compiled by the host tests) ------------------------

// Parse a Kenwood/Yaesu "IF" response: "IF" + 11-digit Hz frequency + more.
// Returns Hz, or 0 if the buffer isn't a usable IF reply.
inline uint32_t catParseIF(const char* s, size_t n) {
  for (size_t i = 0; i + 13 <= n; i++) {
    if (s[i] != 'I' || s[i + 1] != 'F') continue;
    uint32_t f = 0; bool ok = true;
    for (size_t k = i + 2; k < i + 13; k++) {
      if (s[k] < '0' || s[k] > '9') { ok = false; break; }
      f = f * 10 + (uint32_t)(s[k] - '0');
    }
    if (ok) return f;
  }
  return 0;
}

// Decode 5 little-endian BCD bytes (2 digits/byte) into Hz: Σ bcd2(b[i])·100^i.
inline uint32_t catDecodeBcd(const uint8_t* b, size_t n) {
  uint32_t freq = 0, mult = 1;
  for (size_t i = 0; i < n; i++) {
    freq += (uint32_t)(b[i] & 0x0F) * mult;
    freq += (uint32_t)((b[i] >> 4) & 0x0F) * mult * 10;
    mult *= 100;
  }
  return freq;
}

// Find a CI-V read-frequency reply in `buf` and return its Hz, or 0. Accepts a
// frame  FE FE E0 <from> <cmd> <5 BCD> FD  with cmd 0x03 (poll reply) or 0x00
// (transceive broadcast). `to` (E0) is the controller; we don't require a
// specific `from` so auto-addressed setups still work.
inline uint32_t catParseCiv(const uint8_t* buf, size_t n) {
  for (size_t i = 0; i + 11 <= n; i++) {
    if (buf[i] != 0xFE || buf[i + 1] != 0xFE) continue;
    uint8_t to = buf[i + 2], cmd = buf[i + 4];
    if (to != 0xE0) continue;                       // addressed to the controller
    if (cmd != 0x03 && cmd != 0x00) continue;       // read-freq / transceive
    if (buf[i + 10] != 0xFD) continue;              // 5 data bytes then end
    return catDecodeBcd(&buf[i + 5], 5);
  }
  return 0;
}

class SerialCatSource : public RadioSource {
 public:
  void setSerial(uint8_t proto, uint32_t baud, uint8_t civAddr) {
    proto_ = proto; baud_ = baud ? baud : 9600; civ_ = civAddr;
  }

  void connect() override {
    Serial.begin(baud_);
    want_ = true; len_ = 0; lastGood_ = 0; connected_ = false; lastPoll_ = 0;
  }
  void disconnect() override { want_ = false; connected_ = false; }

  bool     connected() const override { return connected_; }
  int      band()   const override { return band_; }
  uint32_t freqHz() const override { return freq_; }
  bool     isTx()   const override { return false; }   // CAT is read-only (see header)
  bool     isTune() const override { return false; }

  void process() override {
    if (!want_) return;
    uint32_t now = millis();

    if (now - lastPoll_ >= kPollMs) { lastPoll_ = now; sendPoll(); }

    while (Serial.available()) {
      uint8_t ch = (uint8_t)Serial.read();
      if (proto_ == RADIO_CAT_ICOM) {
        if (len_ < sizeof(buf_)) buf_[len_++] = ch;
        if (ch == 0xFD) { ingestCiv(); len_ = 0; }     // end of a CI-V frame
        else if (len_ >= sizeof(buf_)) len_ = 0;        // overflow guard
      } else {
        if (ch == ';') { ingestAscii(); len_ = 0; }     // end of an ASCII reply
        else { if (len_ < sizeof(buf_)) buf_[len_++] = ch; else len_ = 0; }
      }
    }

    if (lastGood_ != 0 && now - lastGood_ > kLinkTimeoutMs) connected_ = false;
  }

 private:
  static const uint32_t kPollMs        = 300;     // poll the VFO ~3×/s
  static const uint32_t kLinkTimeoutMs = 4000;    // no valid reply → link down

  void sendPoll() {
    if (proto_ == RADIO_CAT_ICOM) {
      const uint8_t f[] = { 0xFE, 0xFE, civ_, 0xE0, 0x03, 0xFD };  // read freq
      Serial.write(f, sizeof(f));
    } else {
      Serial.print("IF;");                                         // Kenwood/Yaesu
    }
  }

  void ingestAscii() { accept(catParseIF((const char*)buf_, len_)); }
  void ingestCiv()   { accept(catParseCiv(buf_, len_)); }

  void accept(uint32_t f) {
    if (f == 0) return;
    freq_ = f; band_ = freqToBand(f);
    connected_ = true; lastGood_ = millis();
  }

  uint8_t  proto_ = RADIO_CAT_KENWOOD;
  uint32_t baud_  = 9600;
  uint8_t  civ_   = 0x94;
  bool     want_  = false;
  bool     connected_ = false;
  uint32_t lastPoll_  = 0;
  uint32_t lastGood_  = 0;

  uint8_t  buf_[64];
  size_t   len_ = 0;

  uint32_t freq_ = 0;
  int      band_ = -1;
};

#endif // SERIALCATSOURCE_H
