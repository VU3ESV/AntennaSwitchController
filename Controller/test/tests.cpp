// Host unit tests for the firmware's pure decision logic — the SO2R interlock
// resolvers (DualResolver / MasterArbiter) and the EEPROM config migration.
// These compile the REAL firmware headers (Config.h, Interlock.h, BandPlan.h)
// against the desktop shims in shims/, so the logic under test is exactly what
// ships — no duplication. No hardware required.
//
//   make run        # build + run
//
// Covers every branch of the P4 per-band fallback (docs/MULTI-RADIO-SO2R-PLAN.md
// §11): collisions, first-come vs priority, TX-safety, fallback taken / blocked,
// and recovery. The on-board /test/inject API drives the same scenarios live;
// the Swift integration suite asserts them end-to-end.
#include "Config.h"           // real — via shims/ (enums, struct, migration)
#include "Interlock.h"        // real — the resolvers under test
#include "SerialCatSource.h"  // real — the CAT frequency parsers under test
#include <cstdio>

// Globals the shims declare extern.
unsigned long  g_fakeMillis = 0;
WiFiClass      WiFi;
EEPROMClass    EEPROM;
HardwareSerial Serial;

static int g_pass = 0, g_fail = 0;
static const char* g_group = "";
#define GROUP(n) do { g_group = n; printf("[%s]\n", n); } while (0)
#define CHECK(cond) do { \
  if (cond) { g_pass++; } \
  else { g_fail++; printf("  FAIL (%s line %d): %s\n", g_group, __LINE__, #cond); } \
} while (0)

// --- DualResolver (Mode B, in-firmware) ------------------------------------
static void test_dual() {
  GROUP("DualResolver");
  int a1, a2;

  // No collision: each radio gets its primary.
  { DualResolver r; r.resolve(2, -1, 3, -1, false, false, a1, a2);
    CHECK(a1 == 2); CHECK(a2 == 3); }

  // Both off-band → both none.
  { DualResolver r; r.resolve(-1, -1, -1, -1, false, false, a1, a2);
    CHECK(a1 == -1); CHECK(a2 == -1); }

  // Collision, neither holds yet → first-come defaults to Radio 1; R2 to none.
  { DualResolver r; r.resolve(2, -1, 2, -1, false, false, a1, a2);
    CHECK(a1 == 2); CHECK(a2 == -1); }

  // Collision, loser (R2) has a fallback → R2 takes it.
  { DualResolver r; r.resolve(2, -1, 2, 5, false, false, a1, a2);
    CHECK(a1 == 2); CHECK(a2 == 5); }

  // Fallback blocked because it equals the winner's antenna → none.
  { DualResolver r; r.resolve(2, -1, 2, 2, false, false, a1, a2);
    CHECK(a1 == 2); CHECK(a2 == -1); }

  // First-come: R2 already holds the antenna, so R1 (newcomer) falls back.
  { DualResolver r;
    r.resolve(-1, -1, 2, -1, false, false, a1, a2);   // R2 acquires 2
    CHECK(a2 == 2);
    r.resolve(2, 5, 2, -1, false, false, a1, a2);      // R1 now wants 2 too
    CHECK(a1 == 5); CHECK(a2 == 2); }

  // TX-safety: a transmitting radio is never moved. R1 holds 2 and is keying;
  // R2 wants 2 → R2 is the one that falls back.
  { DualResolver r;
    r.resolve(2, -1, -1, -1, false, false, a1, a2);    // R1 acquires 2
    r.resolve(2, -1, 2, 5, true, false, a1, a2);        // R1 TX, R2 contends
    CHECK(a1 == 2); CHECK(a2 == 5); }

  // TX-safety beats first-come the other way: R2 holds 2 and is keying; R1
  // contends → R1 falls back, the transmitting R2 keeps its antenna.
  { DualResolver r;
    r.resolve(-1, -1, 2, -1, false, false, a1, a2);    // R2 acquires 2
    r.resolve(2, 5, 2, -1, false, true, a1, a2);        // R2 TX, R1 contends
    CHECK(a1 == 5); CHECK(a2 == 2); }

  // Priority policy: Radio 1 wins the collision regardless of who held it.
  { DualResolver r; r.setPolicy(ILK_PRIORITY);
    r.resolve(-1, -1, 2, -1, false, false, a1, a2);    // R2 acquires 2
    r.resolve(2, -1, 2, 7, false, false, a1, a2);       // R1 priority-wins 2
    CHECK(a1 == 2); CHECK(a2 == 7); }                    // R2 → its fallback

  // Priority must still not move a transmitting radio (safety > priority).
  { DualResolver r; r.setPolicy(ILK_PRIORITY);
    r.resolve(-1, -1, 2, -1, false, false, a1, a2);    // R2 acquires 2
    r.resolve(2, 5, 2, -1, false, true, a1, a2);        // R2 TX → keeps 2
    CHECK(a1 == 5); CHECK(a2 == 2); }
}

// --- MasterArbiter (Mode A, LAN arbiter) -----------------------------------
static void test_master() {
  GROUP("MasterArbiter");

  // Free: master drives its desired antenna.
  { MasterArbiter m; CHECK(m.resolveMaster(2, -1) == 2); CHECK(m.masterAnt() == 2); }

  // Slave claims a different antenna → master keeps its own.
  { MasterArbiter m; m.resolveMaster(2, -1);
    CHECK(m.claim(3) == 1); CHECK(m.resolveMaster(2, -1) == 2); }

  // Slave is denied the antenna the master currently holds.
  { MasterArbiter m; m.resolveMaster(2, -1); CHECK(m.claim(2) == 0); }

  // First-come: slave holds it, master has no fallback → master goes none.
  { MasterArbiter m; m.resolveMaster(3, -1);
    CHECK(m.claim(2) == 1);                 // slave grabs 2 (master on 3)
    CHECK(m.resolveMaster(2, -1) == -1); }   // master now wants 2 → blocked

  // First-come with a fallback: blocked master takes its secondary.
  { MasterArbiter m; m.resolveMaster(3, -1);
    CHECK(m.claim(2) == 1);
    CHECK(m.resolveMaster(2, 5) == 5); CHECK(m.masterAnt() == 5); }

  // Priority: master preempts the slave's hold.
  { MasterArbiter m; m.setPolicy(ILK_PRIORITY); m.resolveMaster(3, -1);
    m.claim(2);
    CHECK(m.resolveMaster(2, -1) == 2);
    CHECK(m.slaveAnt() == -1); }             // slave denied on its next beat

  // Heartbeat expiry: a slave that stops beating loses its hold after the window.
  { MasterArbiter m; g_fakeMillis = 1000; m.claim(2);
    CHECK(m.slaveAnt() == 2); CHECK(m.peerUp());
    g_fakeMillis = 1000 + HB_WINDOW_MS + 1; m.tick();
    CHECK(m.slaveAnt() == -1); CHECK(!m.peerUp());
    g_fakeMillis = 0; }
}

// --- Config migration + round-trip -----------------------------------------
static void test_config() {
  GROUP("Config defaults + v8");

  // Defaults: fallback map all-none, antenna metadata zeroed, magic is v8.
  { Config c; configDefaults(c);
    CHECK(c.magic == CFG_MAGIC);
    bool clean = true;
    for (int i = 0; i < NUM_BANDS; i++) if (c.band_relay2[i] != -1) clean = false;
    for (int i = 0; i < NUM_RELAYS; i++)
      if (c.relay_bands[i] || c.relay_feed[i] || c.relay_group[i]) clean = false;
    CHECK(clean); }

  // Round-trip fallback + antenna metadata through save/load.
  { Config c; configDefaults(c);
    c.band_relay[5] = 2; c.band_relay2[5] = 5;
    c.relay_bands[2] = (1 << 5) | (1 << 6) | (1 << 7);   // HexBeam covers 20/17/15
    c.relay_feed[2] = FEED_TRIPLEXED; c.relay_group[2] = 3;
    configSave(c);
    Config d; bool ok = configLoad(d);
    CHECK(ok); CHECK(d.band_relay2[5] == 5);
    CHECK(d.relay_bands[2] == ((1 << 5) | (1 << 6) | (1 << 7)));
    CHECK(d.relay_feed[2] == FEED_TRIPLEXED); CHECK(d.relay_group[2] == 3); }

  // Migrate a v6 image (skips v7): band map + names preserved, new fields default.
  { ConfigV6 v6; memset(&v6, 0, sizeof(v6));
    v6.magic = CFG_MAGIC_V6; v6.mode = MODE_MASTER;
    for (int i = 0; i < NUM_BANDS; i++) v6.band_relay[i] = (int8_t)(i % 8);
    strncpy(v6.relay_name[0], "HexBeam", RELAY_NAME_LEN - 1);
    v6.crc = crc32(reinterpret_cast<const uint8_t*>(&v6), offsetof(ConfigV6, crc));
    EEPROM.put(0, v6);
    Config c; bool ok = configLoad(c);
    CHECK(ok); CHECK(c.magic == CFG_MAGIC); CHECK(c.mode == MODE_MASTER);
    CHECK(c.band_relay[3] == 3);
    CHECK(std::string(c.relay_name[0]) == "HexBeam");
    CHECK(c.relay_bands[0] == 0 && c.relay_group[0] == 0); }

  // Migrate a v7 image into v8: fallback map preserved, antenna metadata default.
  { ConfigV7 v7; memset(&v7, 0, sizeof(v7));
    v7.magic = CFG_MAGIC_V7; v7.mode = MODE_DUAL;
    v7.band_relay[5] = 2; v7.band_relay2[5] = 5;
    strncpy(v7.relay_name[2], "HexBeam", RELAY_NAME_LEN - 1);
    v7.crc = crc32(reinterpret_cast<const uint8_t*>(&v7), offsetof(ConfigV7, crc));
    EEPROM.put(0, v7);
    Config c; bool ok = configLoad(c);
    CHECK(ok); CHECK(c.magic == CFG_MAGIC); CHECK(c.mode == MODE_DUAL);
    CHECK(c.band_relay[5] == 2); CHECK(c.band_relay2[5] == 5);
    CHECK(std::string(c.relay_name[2]) == "HexBeam");
    bool zeroed = true;
    for (int i = 0; i < NUM_RELAYS; i++)
      if (c.relay_bands[i] || c.relay_feed[i] || c.relay_group[i]) zeroed = false;
    CHECK(zeroed); }
}

// --- Serial CAT frequency parsers (SerialCatSource.h) ----------------------
static void test_cat() {
  GROUP("Serial CAT parsers");

  // Kenwood/Yaesu "IF;" — frequency is the 11 digits after "IF".
  { const char* r = "IF00014074000     +0000000000;";   // 20 m
    CHECK(catParseIF(r, strlen(r)) == 14074000); }
  { const char* r = "IF00007150000;";                     // 40 m
    CHECK(catParseIF(r, strlen(r)) == 7150000); }
  { const char* r = "ZZFA00014074000;";                   // no "IF" → 0
    CHECK(catParseIF(r, strlen(r)) == 0); }
  { const char* r = "garbage;";  CHECK(catParseIF(r, strlen(r)) == 0); }

  // CI-V little-endian BCD: 5 bytes, 2 digits each, LSB pair first.
  { uint8_t b[] = {0x00, 0x40, 0x07, 0x14, 0x00};   // 14,074,000 Hz
    CHECK(catDecodeBcd(b, 5) == 14074000); }
  { uint8_t b[] = {0x00, 0x50, 0x15, 0x00, 0x00};   // 1,550,000? check 3.5 MHz
    // 3,500,000 → groups 00 00 50 03 00 (LSB first): bytes 0x00,0x00,0x50,0x03,0x00
    uint8_t c[] = {0x00, 0x00, 0x50, 0x03, 0x00};
    CHECK(catDecodeBcd(c, 5) == 3500000); (void)b; }

  // Full CI-V read-freq reply frame addressed to the controller (E0).
  { uint8_t f[] = {0xFE, 0xFE, 0xE0, 0x94, 0x03, 0x00, 0x40, 0x07, 0x14, 0x00, 0xFD};
    CHECK(catParseCiv(f, sizeof(f)) == 14074000); }
  // Transceive broadcast (cmd 0x00) is also accepted; leading noise is skipped.
  { uint8_t f[] = {0x11, 0xFE, 0xFE, 0xE0, 0x94, 0x00, 0x00, 0x40, 0x07, 0x14, 0x00, 0xFD};
    CHECK(catParseCiv(f, sizeof(f)) == 14074000); }
  // Frame addressed elsewhere (not E0) is ignored.
  { uint8_t f[] = {0xFE, 0xFE, 0x94, 0xE0, 0x03, 0x00, 0x40, 0x07, 0x14, 0x00, 0xFD};
    CHECK(catParseCiv(f, sizeof(f)) == 0); }
}

int main() {
  test_dual();
  test_master();
  test_config();
  test_cat();
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
