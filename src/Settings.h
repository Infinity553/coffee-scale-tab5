#pragma once
#include <Arduino.h>

// Timezone for times shown on the device (POSIX TZ string). Web UI uses the browser's.
#define DEVICE_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

enum GhostMode : uint8_t { GHOST_OFF = 0, GHOST_LAST = 1, GHOST_REF = 2 };
enum WifiMode : uint8_t { WIFI_HOME = 0, WIFI_HOTSPOT = 1 };

struct Settings {
  bool     configured     = false;  // first-run setup completed
  String   scaleAddress;            // BLE MAC of the paired scale ("" = any Acaia)
  String   scaleName;
  bool     autoTare       = true;
  bool     autoStart      = true;   // start shot timer when weight starts rising
  bool     autoStop       = true;   // stop timer when weight stops rising
  bool     sound          = true;
  uint8_t  brightness     = 180;
  bool     flipScreen     = false;
  uint8_t  colorScheme    = 0;      // theme::ROAST / theme::RACER

  // brewing helpers
  uint8_t  activeRecipe   = 0;
  bool     dripComp       = true;   // signal the target early by the learned drip
  float    dripGrams      = 1.0f;   // learned weight that still drips in after stopping
  uint8_t  ghostMode      = GHOST_LAST;
  uint32_t refShotId      = 0;      // reference shot (0 = none)
  float    lastGrind      = -1;     // last grind setting used (-1 = unset)
  bool     finerIsLower   = true;   // grinder: a lower number grinds finer
  uint8_t  sleepMin       = 10;     // screen sleep after minutes idle (0 = never)

  // network
  bool     wifiEnabled    = false;
  uint8_t  wifiMode       = WIFI_HOTSPOT;
  String   wifiSsid;
  String   wifiPass;

  void load();
  void save() const;
  void forgetScale();
};

extern Settings settings;
