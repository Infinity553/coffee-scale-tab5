// In-memory Preferences stub for the simulator.
#pragma once
#include <cstdlib>
#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  void end() {}
  bool getBool(const char* k, bool d) {
    if (!strcmp(k, "configured")) return getenv("SIM_SETUP") == nullptr;
    return d;
  }
  String getString(const char* k, const char* d) {
    if (!strcmp(k, "name")) return "LUNAR-2B41";
    if (!strcmp(k, "addr")) return "c4:de:e2:19:2b:41";
    if (!strcmp(k, "wifiSsid") && getenv("SIM_WIFI")) return "Home Network";
    return d;
  }
  uint8_t getUChar(const char* k, uint8_t d) {
    if (!strcmp(k, "scheme") && getenv("SIM_THEME")) return (uint8_t)atoi(getenv("SIM_THEME"));
    return d;
  }
  float getFloat(const char*, float d) { return d; }
  uint32_t getUInt(const char*, uint32_t d) { return d; }
  void putUInt(const char*, uint32_t) {}
  void putBool(const char*, bool) {}
  void putString(const char*, const String&) {}
  void putUChar(const char*, uint8_t) {}
  void putFloat(const char*, float) {}
};
