// OutputStage.h — relay board map + the antenna output-stage abstraction.
//
// AntennaSwitchController (ESP8266). Part of the multi-radio refactor
// (docs/MULTI-RADIO-SO2R-PLAN.md, phase P0). An OutputStage turns a desired
// antenna index into relay drive, honoring TX-inhibit (R2.9) and the
// de-energized safe state (R2.10). Concrete stages:
//   - Relay8x1 — exclusive 1-of-8 with break-before-make (R2.8): today's
//                standalone behaviour, and what Master/Slave each drive.
//   - Relay8x2 — per-antenna A/B select for Mode B's external 8×2 switch (P2a).
//   - RelaySelector — radio selector: one exclusive 6×1 + independent 1×2s
//                (MODE_SELECTOR, manual).
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

// External 8×2 switch driver (confirmed wiring): relay i = antenna i, and the
// relay for EACH receiver's antenna is energized — so up to TWO relays are HIGH
// at once (one per receiver), the switch routing each energized antenna to its
// radio. The DualResolver guarantees a1 != a2, so the two are always distinct.
//
// Break-before-make is per-line: when a receiver moves to a new antenna, its
// OLD relay drops, then after the guard delay its NEW relay makes — while the
// other receiver's (unchanged) relay stays energized throughout, so it is never
// momentarily disconnected.
class Relay8x2 {
 public:
  void begin(uint16_t guardMs) { guard_ms_ = guardMs; beginSafe(); }

  void beginSafe() {
    for (int i = 0; i < NUM_RELAYS; i++) { pinMode(kRelayPin[i], OUTPUT); digitalWrite(kRelayPin[i], LOW); }
    a1_ = a2_ = -1;
    curMask_ = targetMask_ = 0;
    phase_ = IDLE;
  }

  void setGuardMs(uint16_t ms) { guard_ms_ = ms; }
  void setInhibit(bool tx)     { inhibit_ = tx; }

  // a1 → Radio 1's antenna, a2 → Radio 2's antenna (each -1 = none).
  void setDual(int a1, int a2) { a1_ = a1; a2_ = a2; }

  void tick() {
    int want = 0;
    if (a1_ >= 0) want |= (1 << a1_);
    if (a2_ >= 0) want |= (1 << a2_);

    if (phase_ == BREAKING) {
      if ((uint32_t)(millis() - breakStart_) >= guard_ms_) {
        applyMask(targetMask_);          // make: energize the arriving relays
        curMask_ = targetMask_;
        phase_ = IDLE;
      }
      return;
    }
    if (inhibit_) return;

    if (want != curMask_) {
      int keep = curMask_ & want;        // relays common to old + new stay on
      applyMask(keep);                   // break: drop the leaving relays now
      curMask_   = keep;
      targetMask_= want;
      breakStart_= millis();
      phase_     = BREAKING;             // make after the guard delay
    }
  }

  int  radio1Ant() const { return a1_; }
  int  radio2Ant() const { return a2_; }
  int  energizedMask() const { return curMask_; }   // bitmask of HIGH relays
  bool switching() const { return phase_ != IDLE; }
  void allOff()          { applyMask(0); curMask_ = 0; }

 private:
  void applyMask(int mask) {
    for (int i = 0; i < NUM_RELAYS; i++)
      digitalWrite(kRelayPin[i], (mask >> i) & 1 ? HIGH : LOW);
  }

  enum Phase { IDLE, BREAKING };
  int      a1_ = -1, a2_ = -1;
  int      curMask_ = 0, targetMask_ = 0;
  Phase    phase_ = IDLE;
  bool     inhibit_ = false;
  uint16_t guard_ms_ = 50;
  uint32_t breakStart_ = 0;
};

// Radio selector (MODE_SELECTOR): the 8 relays split by role into ONE exclusive
// "6×1" amplifier-input switch (every relay NOT in toggleMask) and independent
// "1×2" radio switches (relays in toggleMask; one SPDT each: de-energized =
// port 1, energized = port 2). Manual only — no radio tracking, no TX sensing.
//
//   - 6×1: at most one relay energized, break-before-make with the guard delay
//     (R2.8), so two radios are never bridged onto the amplifier input.
//   - 1×2: applied immediately (an SPDT breaks before it makes); never touches
//     a 6×1 relay, and 6×1 changes never touch a 1×2.
//   - Boot, a role change, or allOff(): everything de-energized — both 6×1s
//     open, every 1×2 on port 1 (R2.10).
class RelaySelector {
 public:
  void begin(uint16_t guardMs) { guard_ms_ = guardMs; beginSafe(); }

  void beginSafe() {
    for (int i = 0; i < NUM_RELAYS; i++) { pinMode(kRelayPin[i], OUTPUT); digitalWrite(kRelayPin[i], LOW); }
    port_ = target_ = desired_ = -1;
    toggles_ = desiredToggles_ = 0;
    phase_ = IDLE;
  }

  void setGuardMs(uint16_t ms) { guard_ms_ = ms; }

  // Which relays are 1×2 switches (bit i = relay i); the rest form the 6×1.
  // A change of roles drops everything to the safe state first.
  void setRoles(uint8_t toggleMask) {
    if (toggleMask == toggleMask_) return;
    toggleMask_ = toggleMask;
    beginSafe();
  }

  // 6×1: connect relay `relay` (must be a 6×1 member), or -1 = open. False if
  // `relay` is not a 6×1 relay.
  bool selectPort(int relay) {
    if (relay != -1 && !isSelect(relay)) return false;
    desired_ = relay;
    return true;
  }

  // 1×2: move relay `relay` (must be a 1×2) to port 2 (on) or port 1 (off).
  bool setToggle(int relay, bool on) {
    if (!isToggle(relay)) return false;
    if (on) desiredToggles_ |=  (uint8_t)(1u << relay);
    else    desiredToggles_ &= (uint8_t)~(1u << relay);
    return true;
  }

  void allOff() { desired_ = -1; desiredToggles_ = 0; }

  void tick() {
    // 1×2 switches: independent SPDTs, applied at once.
    if (desiredToggles_ != toggles_) {
      for (int i = 0; i < NUM_RELAYS; i++)
        if (isToggle(i)) digitalWrite(kRelayPin[i], (desiredToggles_ >> i) & 1 ? HIGH : LOW);
      toggles_ = desiredToggles_;
    }

    // 6×1: break-before-make. The whole 6×1 is open during the guard, so make the
    // LATEST commanded port (a re-command mid-guard doesn't click a stale relay).
    uint32_t now = millis();
    if (phase_ == BREAKING) {
      if ((uint32_t)(now - breakStart_) >= guard_ms_) {
        target_ = desired_;
        if (target_ >= 0) digitalWrite(kRelayPin[target_], HIGH);
        port_  = target_;
        phase_ = IDLE;
      }
      return;
    }
    if (desired_ != port_) {
      if (port_ >= 0) digitalWrite(kRelayPin[port_], LOW);   // break
      port_       = -1;
      target_     = desired_;
      breakStart_ = now;
      phase_      = BREAKING;                                  // make after the guard
    }
  }

  int     desiredPort() const   { return desired_; }         // commanded (may still be switching)
  uint8_t desiredToggles() const{ return desiredToggles_; }
  int     port() const          { return port_; }            // energized 6×1 relay, -1 = open
  uint8_t toggles() const       { return toggles_; }         // bit i = 1×2 relay i on port 2
  uint8_t toggleMask() const    { return toggleMask_; }      // bit i = relay i is a 1×2
  bool    switching() const     { return phase_ != IDLE; }
  uint8_t energizedMask() const { return (uint8_t)(toggles_ | (port_ >= 0 ? (1u << port_) : 0)); }

 private:
  bool isToggle(int r) const { return r >= 0 && r < NUM_RELAYS && ((toggleMask_ >> r) & 1); }
  bool isSelect(int r) const { return r >= 0 && r < NUM_RELAYS && !((toggleMask_ >> r) & 1); }

  enum Phase { IDLE, BREAKING };
  int      port_ = -1, target_ = -1, desired_ = -1;
  uint8_t  toggles_ = 0, desiredToggles_ = 0;
  uint8_t  toggleMask_ = 0;
  Phase    phase_ = IDLE;
  uint16_t guard_ms_ = 50;
  uint32_t breakStart_ = 0;
};

#endif // OUTPUTSTAGE_H
