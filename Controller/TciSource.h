// TciSource.h — RadioSource backed by the bundled IW7DMH TCI client.
//
// AntennaSwitchController (ESP8266). Single-radio operation: tracks RX-1 VFO A
// (rig 0, VFO 0) per CLAUDE.md decision #1. Poll-based (see RadioSource.h):
// process() pumps the TCI WebSocket via TCI::process() then reads the latest
// VFO / TRX / tune straight from rtx[0] — the same cached state the library's
// event callbacks would expose, without needing capture-less free functions.
#ifndef TCISOURCE_H
#define TCISOURCE_H

#include "RadioSource.h"
#include "BandPlan.h"
#include "TCI.h"

class TciSource : public RadioSource {
 public:
  // Point the client at a TCI server. Apply before connect(); on a live link,
  // disconnect() first (Controller.ino does this on /save). The TCI library's
  // set_host() takes a non-const char*, hence the const_cast. A no-op when this
  // source shares another's link (the owner configures it).
  void configure(const char* host, int port, int iaruRegion) override {
    if (!owns_) return;
    tci_->set_host(const_cast<char*>(host));
    tci_->set_port(port);
    tci_->set_iaru_region(iaruRegion);
  }

  void connect()    override { if (owns_) tci_->connect(); }
  void disconnect() override { if (owns_) tci_->disconnect(); }

  // Which TCI receiver to track: 0 = RX1 (default), 1 = RX2. A radio with two
  // receivers (e.g. SunSDR2) exposes them as two RTX slots on one TCI link.
  void setRig(int rig) { rig_ = (rig >= 0 && rig < N_MAX_RTX) ? rig : 0; }

  // Read RX1/RX2 off ANOTHER source's TCI client instead of opening a second
  // WebSocket. A single TCI link already demultiplexes both receivers into
  // rtx[0]/rtx[1] (the library routes vfo:rtxId,… by receiver), so for a
  // two-receiver radio (SunSDR2) the secondary source just reads the primary's
  // client at its own rig index. Halves inbound traffic vs. two sockets and
  // removes the per-socket drain backlog that lagged band switching.
  void shareWith(TciSource& primary) { tci_ = primary.client(); owns_ = false; }
  void useOwnClient()                { tci_ = &owned_; owns_ = true; }
  TCI* client() { return &owned_; }     // the link this source owns

  void process() override {
    if (owns_) tci_->process();           // only the owner pumps the WebSocket
    long hz = tci_->rtx[rig_].getVfo(0);  // selected receiver, VFO A
    if (hz > 0) {
      freq_ = (uint32_t)hz;
      band_ = freqToBand(freq_);
    }
    tx_   = tci_->rtx[rig_].getTrx();
    tune_ = tci_->rtx[rig_].getTune();
  }

  bool connected() const override { return tci_->connected(); }
  int  band() const override { return band_; }
  uint32_t freqHz() const override { return freq_; }
  bool isTx() const override { return tx_; }
  bool isTune() const override { return tune_; }

 private:
  TCI      owned_;           // this source's own client (used unless sharing)
  TCI*     tci_  = &owned_;  // active client: own, or a shared primary's
  bool     owns_ = true;     // do we pump/connect/disconnect tci_?
  int      rig_  = 0;        // TCI receiver index (0 = RX1, 1 = RX2)
  uint32_t freq_ = 0;
  int      band_ = -1;
  bool     tx_   = false;
  bool     tune_ = false;
};

#endif // TCISOURCE_H
