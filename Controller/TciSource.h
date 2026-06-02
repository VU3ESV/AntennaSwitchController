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
  // set_host() takes a non-const char*, hence the const_cast.
  void configure(const char* host, int port, int iaruRegion) override {
    tci_.set_host(const_cast<char*>(host));
    tci_.set_port(port);
    tci_.set_iaru_region(iaruRegion);
  }

  void connect()    override { tci_.connect(); }
  void disconnect() override { tci_.disconnect(); }

  // Which TCI receiver to track: 0 = RX1 (default), 1 = RX2. A radio with two
  // receivers (e.g. SunSDR2) exposes them as two RTX slots on one TCI link, so
  // two TciSources on the same host/port reading rig 0 and rig 1 give SO2R.
  void setRig(int rig) { rig_ = (rig >= 0 && rig < N_MAX_RTX) ? rig : 0; }

  void process() override {
    tci_.process();                       // pump WebSocket + drain one frame
    long hz = tci_.rtx[rig_].getVfo(0);   // selected receiver, VFO A
    if (hz > 0) {
      freq_ = (uint32_t)hz;
      band_ = freqToBand(freq_);
    }
    tx_   = tci_.rtx[rig_].getTrx();
    tune_ = tci_.rtx[rig_].getTune();
  }

  bool connected() const override { return const_cast<TCI&>(tci_).connected(); }
  int  band() const override { return band_; }
  uint32_t freqHz() const override { return freq_; }
  bool isTx() const override { return tx_; }
  bool isTune() const override { return tune_; }

 private:
  TCI      tci_;
  int      rig_  = 0;        // TCI receiver index (0 = RX1, 1 = RX2)
  uint32_t freq_ = 0;
  int      band_ = -1;
  bool     tx_   = false;
  bool     tune_ = false;
};

#endif // TCISOURCE_H
