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

// ---- Heartbeat (Mode A) ---------------------------------------------------
// The slave contacts the master every HB_BEAT_MS (its claim/release doubles as
// the heartbeat). Peer loss is declared only after HB_MAX_MISS *consecutive*
// missed beats, so a single dropped packet on a lossy LAN never drops an
// antenna. Both sides use the same window: the slave counts its failed POSTs;
// the master ages the slave's last contact (HB_BEAT_MS × HB_MAX_MISS).
//   - Master sees slave loss  → frees the slave's antenna (master may reuse it).
//   - Slave  sees master loss → applies on_peer_loss (safe → none, or hold).
static const uint32_t HB_BEAT_MS  = 2000;
static const int      HB_MAX_MISS = 3;
static const uint32_t HB_WINDOW_MS = HB_BEAT_MS * HB_MAX_MISS;   // 6 s

// ---- Master side: the arbiter --------------------------------------------
class MasterArbiter {
 public:
  void setPolicy(uint8_t policy) { policy_ = policy; }

  // Resolve the MASTER radio's desired antenna against what the slave holds.
  // `secondary` is the band's fallback antenna (-1 = none): when first-come
  // leaves the master blocked on its primary, it takes the secondary if that is
  // free. Returns the antenna the master may drive (or -1 if blocked).
  int resolveMaster(int desired, int secondary = -1) {
    if (desired < 0) { masterAnt_ = -1; return -1; }
    if (slaveHeld() && slaveAnt_ == desired) {
      if (policy_ == ILK_PRIORITY) {        // master preempts the slave
        slaveAnt_ = -1;                      // slave denied on its next beat
        masterAnt_ = desired; return desired;
      }
      // first-come: slave keeps the primary; try the band's secondary instead.
      if (secondary >= 0 && !(slaveHeld() && slaveAnt_ == secondary)) {
        masterAnt_ = secondary; return secondary;
      }
      masterAnt_ = -1; return -1;
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

  // Expire the slave's holding once it has missed the whole heartbeat window
  // (crashed / off the LAN), so the master may then use that antenna. Call
  // every loop().
  void tick() {
    if (slaveAnt_ >= 0 && (uint32_t)(millis() - lastSlaveMs_) > HB_WINDOW_MS) slaveAnt_ = -1;
  }

  bool slaveHeld() const { return slaveAnt_ >= 0; }
  bool peerUp()    const { return lastSlaveMs_ != 0 && (uint32_t)(millis() - lastSlaveMs_) <= HB_WINDOW_MS; }
  int  masterAnt() const { return masterAnt_; }
  int  slaveAnt()  const { return slaveAnt_; }

  // Whole heartbeats since the slave last made contact (0 if fresh / never).
  int beatsMissed() const {
    if (lastSlaveMs_ == 0) return HB_MAX_MISS;
    return (int)((uint32_t)(millis() - lastSlaveMs_) / HB_BEAT_MS);
  }

 private:
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

  // Resolve desired antennas for Radio 1 / Radio 2 into granted outputs a1/a2
  // (guaranteed a1 != a2, or one is -1). Each radio has a primary (p1/p2) and a
  // per-band secondary fallback (s1/s2, -1 = none). On a collision the loser
  // falls back to its secondary if that is free, else none. tx1/tx2 hold the
  // respective radio's antenna fixed while it transmits (R2.9) — a transmitting
  // radio is never moved or preempted.
  void resolve(int p1, int s1, int p2, int s2, bool tx1, bool tx2, int& a1, int& a2) {
    a1 = tx1 ? cur1_ : p1;        // don't move a radio's antenna under TX
    a2 = tx2 ? cur2_ : p2;

    if (a1 >= 0 && a1 == a2) {    // both want the same antenna → arbitrate
      bool r1Holds = (cur1_ == a1);
      bool r2Holds = (cur2_ == a2);
      bool r1Wins;
      if      (tx1) r1Wins = true;            // never move a transmitting radio
      else if (tx2) r1Wins = false;
      else if (policy_ == ILK_PRIORITY) r1Wins = true;   // Radio 1 wins
      else if (r1Holds && !r2Holds) r1Wins = true;       // first-come: R1 on it
      else if (r2Holds && !r1Holds) r1Wins = false;      // first-come: R2 on it
      else r1Wins = true;                                // neither/both → R1

      if (r1Wins) a2 = (!tx2 && s2 >= 0 && s2 != a1) ? s2 : -1;  // loser → secondary/none
      else        a1 = (!tx1 && s1 >= 0 && s1 != a2) ? s1 : -1;
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
  bool peerUp()      const { return peerUp_; }
  int  missedBeats() const { return missed_; }

  // Resolve the SLAVE radio's desired antenna by claiming it from the master.
  // `primary` is the band's relay; `secondary` (-1 = none) is the fallback tried
  // when the master denies the primary (it holds it for Radio 1). Returns the
  // antenna the slave may drive (granted, or -1). The claim/release doubles as
  // the heartbeat: it fires when the desire changes or every HB_BEAT_MS, keeping
  // the master's contact timer alive. A failed beat is tolerated (the current
  // grant is held) until HB_MAX_MISS consecutive misses, at which point the
  // master is declared lost and on_peer_loss applies — so a single dropped
  // packet never drops the antenna.
  int resolve(int primary, int secondary = -1) {
    uint32_t now = millis();
    bool due = (primary != lastDesired_) || (uint32_t)(now - lastBeat_) >= HB_BEAT_MS;
    if (!due) return allowed_;
    lastBeat_ = now; lastDesired_ = primary;

    if (strlen(host_) == 0) { missed_ = HB_MAX_MISS; peerUp_ = false; return failsafe(); }

    bool granted = false;
    int code = (primary < 0)
             ? post("/interlock/release", granted)
             : post(String("/interlock/claim?ant=") + primary, granted);

    if (code != 200) return missedBeat();      // debounce transient loss
    missed_ = 0; peerUp_ = true;               // heartbeat ok

    if (primary < 0)     { allowed_ = -1;      return allowed_; }
    if (granted)         { allowed_ = primary; return allowed_; }

    // Primary denied (master holds it). Try the band's secondary fallback; a
    // failed claim here still counts as a good beat (the primary reached the
    // master), so we only fall through to none.
    if (secondary >= 0) {
      bool g2 = false;
      if (post(String("/interlock/claim?ant=") + secondary, g2) == 200 && g2) {
        allowed_ = secondary; return allowed_;
      }
    }
    allowed_ = -1; return allowed_;
  }

 private:
  // A missed heartbeat: hold the current grant until HB_MAX_MISS in a row, then
  // declare the master lost and fail safe.
  int missedBeat() {
    if (missed_ < HB_MAX_MISS) missed_++;
    if (missed_ >= HB_MAX_MISS) { peerUp_ = false; return failsafe(); }
    return allowed_;                            // tolerate: keep current antenna
  }

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
  int      missed_     = 0;       // consecutive missed heartbeats
  int      allowed_    = -1;
  int      lastDesired_ = -2;
  uint32_t lastBeat_   = 0;
};

#endif // INTERLOCK_H
