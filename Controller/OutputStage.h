// OutputStage.h — relay board map + the antenna output-stage abstraction.
//
// AntennaSwitchController (ESP8266). Part of the multi-radio refactor
// (docs/MULTI-RADIO-SO2R-PLAN.md, phase P0). An OutputStage turns a desired
// antenna index into relay drive, honoring TX-inhibit (R2.9) and the
// de-energized safe state (R2.10). Concrete stages:
//   - Relay8x1 — exclusive 1-of-8 with break-before-make (R2.8): today's
//                standalone behaviour, and what Master/Slave each drive.
//   - Relay8x2 — per-antenna A/B select for Mode B's external 8×2 switch (P2a).
//
// Relays are active-HIGH on this board. GPIO0/15/16 are boot straps that glitch
// at reset, so beginSafe() drives everything LOW as early as possible (R1).
#ifndef OUTPUTSTAGE_H
#define OUTPUTSTAGE_H

#include <Arduino.h>

#define NUM_RELAYS 8

// Board-fixed GPIO → relay map (relay 1..8 = index 0..7).
// werner.rothschopf.net ESP-12F 8-ch relay board.
static const uint8_t kRelayPin[NUM_RELAYS] = { 16, 14, 12, 13, 15, 0, 4, 5 };

// Abstract output stage. The controller core calls setDesired()/setInhibit()
// then tick() every loop(); the stage reconciles the relays.
class OutputStage {
 public:
  virtual ~OutputStage() {}

  virtual void begin(uint16_t guardMs) = 0;   // init pins + safe state
  virtual void beginSafe() = 0;                // drive all relays de-energized
  virtual void setGuardMs(uint16_t ms) = 0;

  // What the controller wants connected: relay/antenna 0..7, or -1 = none.
  virtual void setDesired(int relay) = 0;

  // R2.9 — while TX/tune is asserted, no relay change may occur.
  virtual void setInhibit(bool tx) = 0;

  virtual void tick() = 0;                      // reconcile active vs desired

  virtual int  activeRelay() const = 0;         // -1 if none energized
  virtual bool switching() const = 0;
  virtual bool inhibited() const = 0;
  virtual void allOff() = 0;
};

// Exclusive 1-of-8 with break-before-make (R2.8): at most one relay energized;
// on a change, de-energize first, wait the guard delay, then energize the new
// relay. This prevents momentarily bridging two antennas.
class Relay8x1 : public OutputStage {
 public:
  void begin(uint16_t guardMs) override {
    guard_ms_ = guardMs;
    beginSafe();
  }

  void beginSafe() override {
    for (int i = 0; i < NUM_RELAYS; i++) {
      pinMode(kRelayPin[i], OUTPUT);
      digitalWrite(kRelayPin[i], LOW);
    }
    active_  = -1;
    target_  = -1;
    desired_ = -1;
    phase_   = IDLE;
  }

  void setGuardMs(uint16_t ms) override { guard_ms_ = ms; }
  void setDesired(int relay)   override { desired_ = relay; }
  void setInhibit(bool tx)     override { inhibit_ = tx; }

  int  activeRelay() const override { return active_; }
  bool inhibited()   const override { return inhibit_; }
  bool switching()   const override { return phase_ != IDLE; }

  void tick() override {
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

  void allOff() override {
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

#endif // OUTPUTSTAGE_H
