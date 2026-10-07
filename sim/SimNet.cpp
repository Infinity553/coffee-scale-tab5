// Simulator stand-in for Wi-Fi: fake status and scan results so the screens
// can be previewed. The web UI itself is previewed with tools/webui_preview.py.
#include <cstdlib>
#include "Net.h"
#include "Settings.h"

namespace net {
void begin() {}
void apply() {}
Status status() {
  Status s;
  s.enabled = settings.wifiEnabled || getenv("SIM_WIFI");
  s.hotspot = settings.wifiMode == WIFI_HOTSPOT && !getenv("SIM_WIFI");
  s.connected = s.enabled;
  s.ip = s.hotspot ? "192.168.4.1" : "192.168.1.42";
  s.ssid = s.hotspot ? "CoffeeScale-2B41" : "Home Network";
  if (getenv("SIM_WIFI_REJECT")) {
    s.connected = false;
    s.passwordRejected = true;
    s.problem = "The password was rejected";
  }
  return s;
}
void startScan() {}
bool scanning() { return false; }
int lastScanResult() { return getenv("SIM_SCAN_FAIL") ? -2 : 4; }
std::vector<WifiNet> networks() {
  if (getenv("SIM_SCAN_FAIL")) return {};
  return {{"Home Network", -48, true}, {"Cafe Guest", -63, false},
          {"FRITZ!Box 7590 XY", -71, true}, {"Neighbour", -84, true}};
}
bool takeTimeSynced() { return false; }
const char* hotspotName() { return "CoffeeScale-2B41"; }
const char* hotspotPassword() { return "espresso"; }
const char* hostname() { return "coffeescale"; }
}  // namespace net
