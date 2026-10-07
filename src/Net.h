#pragma once
#include <string>
#include <vector>

// Wi-Fi (home network or own hotspot), mDNS name, NTP time and the web UI.
// Runs in its own task; the UI only reads status and requests changes.

struct WifiNet {
  std::string ssid;
  int         rssi;
  bool        secure;
};

namespace net {

struct Status {
  bool        enabled = false;
  bool        hotspot = false;
  bool        connected = false;   // STA connected or AP running
  bool        connecting = false;
  std::string ip;
  std::string ssid;
  bool        passwordRejected = false;   // gave up: the router refused the password
  std::string problem;                    // why it isn't connecting, "" if unknown
};

void begin();
void apply();                     // re-read settings (enable / mode / credentials)
Status status();
void startScan();
bool scanning();
int lastScanResult();             // networks found, < 0 when the last scan failed
std::vector<WifiNet> networks();
bool takeTimeSynced();            // true once after NTP set the clock
const char* hotspotName();
const char* hotspotPassword();
const char* hostname();           // <hostname>.local

}  // namespace net
