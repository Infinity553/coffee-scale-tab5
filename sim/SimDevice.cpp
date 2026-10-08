// Simulator stand-ins for the Tab5 battery and firmware updates.
//   SIM_BATT=72,1   battery level and charging flag (unset = no battery)
#include <cstdio>
#include <cstdlib>
#include "Battery.h"
#include "Ota.h"

namespace battery {
static int lvl = -1, chg = 0;
void poll(uint32_t) {
  if (const char* b = getenv("SIM_BATT")) sscanf(b, "%d,%d", &lvl, &chg);
}
bool present() { return lvl >= 0; }
int level() { return lvl; }
bool charging() { return chg != 0; }
int voltageMv() { return lvl >= 0 ? 6400 + lvl * 20 : 0; }
}  // namespace battery

namespace ota {
static uint32_t until = 0;
void allow(uint32_t minutes) { until = minutes * 60; }
void disallow() { until = 0; }
bool allowed() { return until != 0; }
uint32_t allowedSecondsLeft() { return until; }
bool begin(size_t, const char**) { return false; }
bool write(const uint8_t*, size_t) { return false; }
bool finish(const char**) { return false; }
void abort() {}
bool active() { return getenv("SIM_OTA") != nullptr; }
int progress() { return 42; }
bool succeeded() { return false; }
const char* lastError() { return ""; }
void confirmIfHealthy(uint32_t) {}
bool rolledBack() { return false; }
}  // namespace ota
