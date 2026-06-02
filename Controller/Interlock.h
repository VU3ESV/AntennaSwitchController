// Interlock.h — SO2R Mode A LAN interlock (docs/MULTI-RADIO-SO2R-PLAN.md §6.1).
//
// AntennaSwitchController (ESP8266), phase P2b. Two standard 8×1 controllers
// coordinate over the LAN so the two radios never select the same antenna
// index ("an antenna in use by one radio is never available to the other").
//
//   MASTER = Radio 1, the arbiter. Owns the authoritative ownership of the two
//            sides' antennas; the slave asks it over HTTP.
//   SLAVE  = Radio 2, a client. Before driving antenna i it claims i from the
//            master; if denied (or the master is unreachable) it falls back.
//
// Policy (confirmed): FIRST-COME / current-holder-keeps-it — the side already
// on the contended antenna keeps it; the newcomer is denied and falls back to
// none. PRIORITY (master preempts) is offered as an alternative.
#ifndef INTERLOCK_H
#define INTERLOCK_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "Config.h"

// ---- Master side: the arbiter --------------------------------------------
class MasterArbiter {
 public:
  void setPolicy(uint8_t policy) { policy_ = policy; }

  // Resolve the MASTER radio's desired antenna against what the slave holds.
  // Returns the antenna the master may drive (its desired, or -1 if blocked).
  int resolveMaster(int desired) {
    if (desired < 0) { masterAnt_ = -1; return -1; }
    if (slaveHeld() && slaveAnt_ == desired) {
      if (policy_ == ILK_PRIORITY) {        // master preempts the slave
        slaveAnt_ = -1;                      // slave denied on its next beat
        masterAnt_ = desired; return desired;
      }
      masterAnt_ = -1; return -1;            // first-come: slave keeps it
    }
    masterAnt_ = desired; return desired;
  }

  // Slave claim handler (called from the HTTP route). 1 = granted, 0 = denied.
  // ant < 0 is a release. Refreshes the slave-contact timer either way.
  int claim(int ant) {
    lastSlaveMs_ = millis();
    if (ant < 0) { slaveAnt_ = -1; return 1; }
    if (masterAnt_ == ant) { slaveAnt_ = -1; return 0; }  // master holds it
    slaveAnt_ = ant; return 1;
  }
  void release() { lastSlaveMs_ = millis(); slaveAnt_ = -1; }

  // Expire the slave's holding if it has gone silent (crashed / off the LAN),
  // so the master may then use that antenna. Call every loop().
  void tick() {
    if (slaveAnt_ >= 0 && (uint32_t)(millis() - lastSlaveMs_) > kTimeoutMs) slaveAnt_ = -1;
  }

  bool slaveHeld() const { return slaveAnt_ >= 0; }
  bool peerUp()    const { return lastSlaveMs_ != 0 && (uint32_t)(millis() - lastSlaveMs_) <= kTimeoutMs; }
  int  masterAnt() const { return masterAnt_; }
  int  slaveAnt()  const { return slaveAnt_; }

 private:
  static const uint32_t kTimeoutMs = 6000;   // 3× the slave heartbeat
  uint8_t  policy_      = ILK_FIRST_COME;
  int      masterAnt_   = -1;
  int      slaveAnt_    = -1;
  uint32_t lastSlaveMs_ = 0;
};

// ---- Mode B: single-board dual-radio resolver (§6.2) ----------------------
// One MCU sees both radios, so the interlock is a local decision: if the two
// radios want the same antenna index, FIRST-COME keeps it with the current
// holder and denies the newcomer (→ none). Per-radio TX-safety freezes a
// radio's antenna while it is transmitting/tuning (R2.9).
class DualResolver {
 public:
  void setPolicy(uint8_t policy) { policy_ = policy; }

  // Resolve desired antennas d1 (Radio 1) and d2 (Radio 2), each -1..7, into
  // granted outputs a1/a2 (guaranteed a1 != a2, or one is -1). tx1/tx2 hold the
  // respective radio's antenna fixed while it transmits.
  void resolve(int d1, int d2, bool tx1, bool tx2, int& a1, int& a2) {
    a1 = tx1 ? cur1_ : d1;        // don't move a radio's antenna under TX
    a2 = tx2 ? cur2_ : d2;

    if (a1 >= 0 && a1 == a2) {    // both want the same antenna → arbitrate
      bool r1Holds = (cur1_ == a1);
      bool r2Holds = (cur2_ == a2);
      if (policy_ == ILK_PRIORITY) {          // Radio 1 wins
        a2 = -1;
      } else if (r1Holds && !r2Holds) {       // first-come: R1 already on it
        a2 = -1;
      } else if (r2Holds && !r1Holds) {       // first-come: R2 already on it
        a1 = -1;
      } else {                                // neither (or both) held → R1
        a2 = -1;
      }
    }
    cur1_ = a1;
    cur2_ = a2;
  }

  int radio1Ant() const { return cur1_; }
  int radio2Ant() const { return cur2_; }

 private:
  uint8_t policy_ = ILK_FIRST_COME;
  int     cur1_   = -1;
  int     cur2_   = -1;
};

// ---- Slave side: the client -----------------------------------------------
class SlaveClient {
 public:
  void configure(const char* masterHost, uint8_t onPeerLoss) {
    strncpy(host_, masterHost ? masterHost : "", sizeof(host_) - 1);
    host_[sizeof(host_) - 1] = '\0';
    onPeerLoss_ = onPeerLoss;
  }
  bool peerUp() const { return peerUp_; }

  // Resolve the SLAVE radio's desired antenna by claiming it from the master.
  // Returns the antenna the slave may drive (granted, or -1). Throttled: only
  // talks to the master when the desire changes or on a ~2 s heartbeat (which
  // also keeps the master's contact timer alive).
  int resolve(int desired) {
    uint32_t now = millis();
    bool due = (desired != lastDesired_) || (uint32_t)(now - lastBeat_) >= kBeatMs;
    if (!due) return allowed_;
    lastBeat_ = now; lastDesired_ = desired;

    if (strlen(host_) == 0) { peerUp_ = false; return failsafe(); }

    bool granted = false;
    int code = (desired < 0)
             ? post("/interlock/release", granted)
             : post(String("/interlock/claim?ant=") + desired, granted);

    if (code != 200) { peerUp_ = false; return failsafe(); }
    peerUp_  = true;
    allowed_ = (desired < 0) ? -1 : (granted ? desired : -1);
    return allowed_;
  }

 private:
  static const uint32_t kBeatMs = 2000;

  int failsafe() {
    // PEER_LOSS_SAFE → none; PEER_LOSS_HOLD → keep the last granted antenna.
    if (onPeerLoss_ != PEER_LOSS_HOLD) allowed_ = -1;
    return allowed_;
  }

  // POST to the master; granted = (body == "1"). Returns the HTTP status (<=0
  // on connect/timeout failure). Short timeout so a missing master can't stall
  // the control loop.
  int post(const String& path, bool& granted) {
    WiFiClient c;
    HTTPClient http;
    String url = String("http://") + host_ + path;
    if (!http.begin(c, url)) return -1;
    http.setTimeout(600);
    int code = http.POST(reinterpret_cast<uint8_t*>(0), 0);
    if (code == 200) granted = (http.getString().toInt() != 0);
    http.end();
    return code;
  }

  char     host_[64]   = {0};
  uint8_t  onPeerLoss_ = PEER_LOSS_SAFE;
  bool     peerUp_     = false;
  int      allowed_    = -1;
  int      lastDesired_ = -2;
  uint32_t lastBeat_   = 0;
};

#endif // INTERLOCK_H
