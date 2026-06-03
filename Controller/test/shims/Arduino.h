// Host-test shim for <Arduino.h> — just enough of the Arduino runtime for the
// firmware headers (Config.h, BandPlan.h, Interlock.h) to compile and run
// natively under a desktop C++ compiler. NOT used in the real build.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#ifndef F
#define F(x) x
#endif

// Controllable fake clock so heartbeat / expiry timing is deterministic in tests.
extern unsigned long g_fakeMillis;
inline unsigned long millis() { return g_fakeMillis; }

// Minimal Arduino String — only the operations the firmware headers use.
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  int toInt() const { return std::atoi(s.c_str()); }
  String operator+(const String& o) const { String r; r.s = s + o.s; return r; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  bool operator==(const char* c) const { return s == (c ? c : ""); }
};
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
