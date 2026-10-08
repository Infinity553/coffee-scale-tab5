#include "Battery.h"
#include <M5Unified.h>

namespace battery {

static bool isPresent = false, isCharging = false;
static int lvl = -1, mv = 0;
static uint32_t lastPoll = 0;

void poll(uint32_t now) {
  if (lastPoll && now - lastPoll < 5000) return;
  lastPoll = now;
  mv = M5.Power.getBatteryVoltage();
  // a 2S pack is between ~6.0 V and 8.4 V; anything else means no battery
  isPresent = mv > 5600 && mv < 9000;
  lvl = isPresent ? constrain((int)M5.Power.getBatteryLevel(), 0, 100) : -1;
  isCharging = isPresent && M5.Power.isCharging() == m5::Power_Class::is_charging;
}

bool present() { return isPresent; }
int level() { return lvl; }
bool charging() { return isCharging; }
int voltageMv() { return mv; }

}  // namespace battery
