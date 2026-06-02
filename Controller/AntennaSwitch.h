// AntennaSwitch.h — relay control with exclusive break-before-make switching.
//
// AntennaSwitchController (ESP8266). Implements CLAUDE.md R2.8 (one relay at a
// time, break-before-make), R2.9 (no hot-switching: freeze while TX/tune), and
// R2.10 (de-energized safe state). Relays are active-HIGH on this board.
#ifndef ANTENNASWITCH_H
#define ANTENNASWITCH_H

#include <Arduino.h>

#define NUM_RELAYS 8

// Board-fixed GPIO → relay map (relay 1..8 = index 0..7).
// werner.rothschopf.net ESP-12F 8-ch relay board. GPIO0/15/16 are boot straps
// that glitch at reset — beginSafe() drives everything LOW as early as possible.
static const uint8_t kRelayPin[NUM_RELAYS] = { 16, 14, 12, 13, 15, 0, 4, 5 };

class AntennaSwitch {
 public:
  void begin(uint16_t guardMs) {
    guard_ms_ = guardMs;
    beginSafe();
  }

  // Drive all relays de-energized. Safe to call before WiFi/TCI (boot safety).
  void beginSafe() {
    for (int i = 0; i < NUM_RELAYS; i++) {
      pinMode(kRelayPin[i], OUTPUT);
      digitalWrite(kRelayPin[i], LOW);
    }
    active_  = -1;
    target_  = -1;
    desired_ = -1;
    phase_   = IDLE;
  }

  void setGuardMs(uint16_t ms) { guard_ms_ = ms; }

  // What the controller wants connected: relay 0..7, or -1 for none/bypass.
  void setDesired(int relay) { desired_ = relay; }

  // R2.9: while TX or tune is asserted, no relay change may occur.
  void setInhibit(bool tx)   { inhibit_ = tx; }

  int  activeRelay() const { return active_; }   // -1 if none energized
  bool inhibited()   const { return inhibit_; }
  bool switching()   const { return phase_ != IDLE; }

  // Call every loop(). Reconciles active vs desired with break-before-make.
  void tick() {
    uint32_t now = millis();

    if (phase_ == BREAKING) {
      // Current relay already de-energized; wait out the guard, then make.
      if ((uint32_t)(now - breakStart_) >= guard_ms_) {
        if (target_ >= 0) digitalWrite(kRelayPin[target_], HIGH);
        active_ = target_;
        phase_  = IDLE;
      }
      return;
    }

    // IDLE: never start a transition while transmitting/tuning.
    if (inhibit_) return;

    if (desired_ != active_) {
      allOff();                 // break: de-energize whatever is on
      active_     = -1;
      target_     = desired_;
      breakStart_ = now;
      phase_      = BREAKING;   // make happens after the guard delay
    }
  }

  void allOff() {
    for (int i = 0; i < NUM_RELAYS; i++) digitalWrite(kRelayPin[i], LOW);
  }

 private:
  enum Phase { IDLE, BREAKING };
  int      active_     = -1;
  int      desired_    = -1;
  int      target_     = -1;
  Phase    phase_      = IDLE;
  bool     inhibit_    = false;
  uint16_t guard_ms_   = 50;
  uint32_t breakStart_ = 0;
};

#endif // ANTENNASWITCH_H
