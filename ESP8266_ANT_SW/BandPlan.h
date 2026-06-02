// BandPlan.h — supported amateur bands and frequency→band resolution.
//
// AntennaSwitchController (ESP8266). Single source of truth for the band list
// used by the band→relay map (Config), the web UI, and the TCI VFO handler.
#ifndef BANDPLAN_H
#define BANDPLAN_H

#include <Arduino.h>

// Supported HF/6 m bands, 60 m included (per CLAUDE.md R2.6).
// Index order is persisted in EEPROM (Config.band_relay[]) — append only,
// never reorder, or saved configs will remap to the wrong band.
enum Band : int8_t {
  BAND_160 = 0,
  BAND_80,
  BAND_60,
  BAND_40,
  BAND_30,
  BAND_20,
  BAND_17,
  BAND_15,
  BAND_12,
  BAND_10,
  BAND_6,
  NUM_BANDS
};

struct BandRange {
  const char* name;
  uint32_t lowHz;
  uint32_t highHz;
};

// Edges are deliberately generous so any IARU region's allocation resolves to
// the right band. Order MUST match the Band enum.
static const BandRange kBands[NUM_BANDS] = {
  { "160m",  1800000UL,  2000000UL },
  { "80m",   3500000UL,  4000000UL },
  { "60m",   5250000UL,  5450000UL },
  { "40m",   7000000UL,  7300000UL },
  { "30m",  10100000UL, 10150000UL },
  { "20m",  14000000UL, 14350000UL },
  { "17m",  18068000UL, 18168000UL },
  { "15m",  21000000UL, 21450000UL },
  { "12m",  24890000UL, 24990000UL },
  { "10m",  28000000UL, 29700000UL },
  { "6m",   50000000UL, 54000000UL },
};

// Resolve a frequency (Hz) to a Band index, or -1 if out of any ham band.
inline int freqToBand(uint32_t hz) {
  for (int i = 0; i < NUM_BANDS; i++) {
    if (hz >= kBands[i].lowHz && hz <= kBands[i].highHz) return i;
  }
  return -1;
}

inline const char* bandName(int band) {
  if (band < 0 || band >= NUM_BANDS) return "---";
  return kBands[band].name;
}

#endif // BANDPLAN_H
