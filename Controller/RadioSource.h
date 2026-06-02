// RadioSource.h — a radio as a transport-agnostic source of "current band + TX".
//
// AntennaSwitchController (ESP8266). Part of the multi-radio refactor
// (docs/MULTI-RADIO-SO2R-PLAN.md, phase P0). The controller core only needs to
// know, per radio: is the link up, what band is it on, and is it transmitting/
// tuning (R2.9 no hot-switching). Concrete sources implement this over their
// transport:
//   - TciSource     — WiFi / WebSocket TCI (today; see TciSource.h)
//   - NetCatSource  — network CAT over TCP (P1; e.g. FlexRadio SmartSDR 4992)
//   - SerialCatSource — serial CAT on UART0, Kenwood/Yaesu/Icom CI-V (P1)
//
// Sources are POLL-BASED: process() pumps the transport and refreshes cached
// state; the accessors are cheap reads. This avoids the bundled TCI library's
// C-style (capture-less) event callbacks and unifies every transport on one
// "tick + read" shape.
#ifndef RADIOSOURCE_H
#define RADIOSOURCE_H

#include <Arduino.h>

class RadioSource {
 public:
  virtual ~RadioSource() {}

  // One-time setup (open sockets / UART). Safe to call after WiFi is up.
  virtual void begin() {}

  // Point the source at its radio. `host`/`port` are the radio endpoint
  // (TCI server, Flex IP, …); `iaruRegion` is informational (TCI only). Apply
  // before connect(); on a live link, disconnect() first. Sources ignore
  // arguments they don't use. Default no-op for argument-less sources.
  virtual void configure(const char* host, int port, int iaruRegion) {
    (void)host; (void)port; (void)iaruRegion;
  }
  virtual void connect()    {}   // begin/maintain the link to the radio
  virtual void disconnect() {}   // drop the link

  // Pump the transport and refresh cached band/TX/tune. Call every loop().
  virtual void process() {}

  // Is the link to the radio currently up?
  virtual bool connected() const = 0;

  // Last resolved band index (BandPlan), or -1 if none/unknown. May hold the
  // last-known value while disconnected; callers apply the link failsafe.
  virtual int band() const = 0;

  // Last RX VFO frequency in Hz (0 if never seen). Informational (status JSON).
  virtual uint32_t freqHz() const { return 0; }

  // R2.9 — the radio is transmitting / tuning; relays must not change.
  virtual bool isTx() const = 0;
  virtual bool isTune() const = 0;
};

#endif // RADIOSOURCE_H
