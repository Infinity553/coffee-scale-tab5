#include "Net.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <algorithm>
#include <mutex>
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "AcaiaScale.h"
#include "Backup.h"
#include "Brew.h"
#include "Ota.h"
#include "Diag.h"
#include "History.h"
#include "Recipes.h"
#include "Settings.h"
#include "generated/web_index.h"

namespace net {

static const char* HOSTNAME = "coffeescale";
static const char* AP_PASS = "espresso";

static WebServer server(80);
static std::mutex mtx;
static Status st;
static std::vector<WifiNet> scanList;
static volatile bool applyRequested = true;
static volatile bool scanRequested = false;
static volatile bool scanBusy = false;
static volatile bool timeSynced = false;
static char apName[24] = "CoffeeScale";

const char* hotspotName() { return apName; }
const char* hotspotPassword() { return AP_PASS; }
const char* hostname() { return HOSTNAME; }

Status status() {
  std::lock_guard<std::mutex> l(mtx);
  return st;
}

void apply() { applyRequested = true; }
void startScan() { scanRequested = true; scanBusy = true; }
bool scanning() { return scanBusy; }

std::vector<WifiNet> networks() {
  std::lock_guard<std::mutex> l(mtx);
  return scanList;
}

bool takeTimeSynced() {
  bool t = timeSynced;
  timeSynced = false;
  return t;
}

// ---------------------------------------------------------------------------
// web API
// ---------------------------------------------------------------------------
static uint32_t argId() { return (uint32_t)server.arg("id").toInt(); }

static void sendJson(int code, const std::string& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body.c_str());
}

static void sendNoSd() { sendJson(503, "{\"error\":\"no_sd\"}"); }

static void attachment(const char* name) {
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
}

static void handleInfo() {
  char b[400];
  time_t now = time(nullptr);
  snprintf(b, sizeof(b),
           "{\"name\":\"Coffee Scale\",\"scheme\":\"%s\",\"sd\":%s,\"count\":%u,\"ref\":%lu,"
           "\"recipe\":\"%s\",\"deviceTime\":%lu,\"restart\":\"%s\",\"firmware\":\"%s\",\"updateAllowed\":%s}",
           settings.colorScheme == 1 ? "racer" : "roast", history::available() ? "true" : "false",
           (unsigned)history::count(), (unsigned long)settings.refShotId, recipes::active().name,
           (unsigned long)(now > 1700000000 ? now : 0), diag::shortReason(), diag::firmwareId(),
           ota::allowed() ? "true" : "false");
  sendJson(200, b);
}

static void handleShots() {
  std::string s;
  if (!history::readIndexFile(s)) return sendNoSd();
  sendJson(200, s);
}

static void handleShot() {
  std::string s;
  if (!history::available()) return sendNoSd();
  if (!history::readShotFile(argId(), s)) return sendJson(404, "{\"error\":\"not_found\"}");
  if (server.hasArg("download")) {
    char n[40];
    snprintf(n, sizeof(n), "shot-%lu.json", (unsigned long)argId());
    attachment(n);
  }
  sendJson(200, s);
}

static void handleShotCsv() {
  std::string s;
  if (!history::available()) return sendNoSd();
  uint32_t id = argId();
  if (!history::readShotFile(id, s)) return sendJson(404, "{\"error\":\"not_found\"}");
  JsonDocument doc;
  if (deserializeJson(doc, s)) return sendJson(500, "{\"error\":\"corrupt\"}");
  JsonArrayConst t = doc["samples"]["t"], w = doc["samples"]["w"], f = doc["samples"]["f"];
  std::string csv = "time_s,weight_g,flow_gps\n";
  char line[48];
  for (size_t i = 0; i < t.size() && i < w.size(); i++) {
    snprintf(line, sizeof(line), "%.2f,%.1f,%.2f\n", t[i].as<float>(), w[i].as<float>(),
             i < f.size() ? f[i].as<float>() : 0.0f);
    csv += line;
  }
  char n[40];
  snprintf(n, sizeof(n), "shot-%lu.csv", (unsigned long)id);
  attachment(n);
  server.send(200, "text/csv", csv.c_str());
}

static void handleExportJson() {
  if (!history::available()) return sendNoSd();
  attachment("coffee-shots.json");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"shots\":[\n");
  bool first = true;
  for (auto& m : history::list(false)) {
    std::string s;
    if (!history::readShotFile(m.id, s)) continue;
    if (!first) server.sendContent(",\n");
    server.sendContent(s.c_str(), s.size());
    first = false;
  }
  server.sendContent("\n]}");
  server.sendContent("");
}

static void csvField(std::string& o, const std::string& s) {
  o += '"';
  for (char c : s) { if (c == '"') o += '"'; o += c; }
  o += '"';
}

static void handleExportCsv() {
  if (!history::available()) return sendNoSd();
  std::string o = "id,time_iso,recipe,dose_g,yield_g,ratio,duration_s,avg_flow_gps,peak_flow_gps,"
                  "first_drop_s,grind,rating,notes\n";
  char b[200];
  for (auto& m : history::list(false)) {
    char iso[24] = "";
    if (m.epoch) {
      time_t e = m.epoch;
      strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", gmtime(&e));
    }
    snprintf(b, sizeof(b), "%lu,%s,", (unsigned long)m.id, iso);
    o += b;
    csvField(o, m.recipe);
    snprintf(b, sizeof(b), ",%.1f,%.1f,%.2f,%.1f,%.2f,%.2f,%.1f,%.1f,%u,", m.dose, m.yield,
             m.ratio(), m.time, m.avgFlow(), m.peakFlow, m.firstDrop, m.grind, m.rating);
    o += b;
    csvField(o, m.notes);
    o += '\n';
  }
  attachment("coffee-shots.csv");
  server.send(200, "text/csv", o.c_str());
}

static void handleRate() {
  ShotMeta m;
  if (!history::get(argId(), m)) return sendJson(404, "{\"error\":\"not_found\"}");
  m.rating = constrain((int)server.arg("stars").toInt(), 0, 5);
  sendJson(history::update(m) ? 200 : 500, "{\"ok\":true}");
}

static void handleDelete() {
  sendJson(history::remove(argId()) ? 200 : 404, "{\"ok\":true}");
}

static void handleReference() {
  uint32_t id = argId();
  ShotMeta m;
  if (id && !history::get(id, m)) return sendJson(404, "{\"error\":\"not_found\"}");
  history::setReference(id);
  sendJson(200, "{\"ok\":true}");
}

static void handleBackupGet() {
  attachment("coffee-scale-backup.json");
  sendJson(200, backup::toJson());
}

static void handleBackupPost() {
  std::string err;
  if (!server.hasArg("plain") || !backup::fromJson(server.arg("plain").c_str(), &err)) {
    std::string body = "{\"error\":\"" + (err.empty() ? std::string("No backup received.") : err) + "\"}";
    return sendJson(400, body);
  }
  backup::saveToSd();
  sendJson(200, "{\"ok\":true}");
  backup::restartSoon();   // the display restarts to apply everything
}

// Firmware upload (multipart form, field "firmware")
static const char* otaError = nullptr;
static bool otaForbidden = false;

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaError = nullptr;
    otaForbidden = !ota::allowed();
    if (otaForbidden) { otaError = "On the Tab5, open Settings > System and tap Allow web update."; return; }
    if (brew.state() == BrewState::Running) { otaError = "A shot is running. Try again after the shot."; return; }
    ota::begin(server.clientContentLength(), &otaError);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaError && !ota::write(up.buf, up.currentSize)) otaError = ota::lastError();
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaError) ota::finish(&otaError);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    ota::abort();
    otaError = "The upload was interrupted.";
  }
}

static void handleUpdateDone() {
  if (otaError || !ota::succeeded()) {
    std::string e = otaError ? otaError : "The update failed.";
    for (auto& ch : e) if (ch == '"') ch = '\'';
    return sendJson(otaForbidden ? 403 : 400, "{\"error\":\"" + e + "\"}");
  }
  sendJson(200, "{\"ok\":true}");
  backup::restartSoon();   // boots the new firmware; it rolls back by itself if it fails
}

static void handleIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (const char*)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

static void setupServer() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/shots", HTTP_GET, handleShots);
  server.on("/api/shot", HTTP_GET, handleShot);
  server.on("/api/shot.csv", HTTP_GET, handleShotCsv);
  server.on("/api/export.json", HTTP_GET, handleExportJson);
  server.on("/api/export.csv", HTTP_GET, handleExportCsv);
  server.on("/api/rate", HTTP_POST, handleRate);
  server.on("/api/delete", HTTP_POST, handleDelete);
  server.on("/api/reference", HTTP_POST, handleReference);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/api/backup", HTTP_GET, handleBackupGet);
  server.on("/api/backup", HTTP_POST, handleBackupPost);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
}

// ---------------------------------------------------------------------------
// connection management
// ---------------------------------------------------------------------------
static bool serverUp = false;
static bool wifiStarted = false;
static uint32_t lastRetry = 0;   // last connect attempt (home Wi-Fi), 0 = retry now

// Wi-Fi is only brought up on demand, so the C6 is left to Bluetooth otherwise.
static void ensureApName() {
  static bool done = false;
  if (done) return;
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  if (mac[4] | mac[5]) {
    snprintf(apName, sizeof(apName), "CoffeeScale-%02X%02X", mac[4], mac[5]);
    done = true;
  }
}

static void setStatus(bool connected, bool connecting, const std::string& ip,
                      const std::string& ssid, const std::string& problem = "",
                      bool rejected = false) {
  std::lock_guard<std::mutex> l(mtx);
  st.enabled = settings.wifiEnabled;
  st.hotspot = settings.wifiMode == WIFI_HOTSPOT;
  st.connected = connected;
  st.connecting = connecting;
  st.ip = ip;
  st.ssid = ssid;
  st.problem = problem;
  st.passwordRejected = rejected;
}

// --- why the station disconnected (from the Wi-Fi event) ---
static volatile uint8_t lastReason = 0;
static volatile int authFails = 0;
static bool gaveUp = false;
static constexpr int MAX_AUTH_FAILS = 3;

static bool isAuthReason(uint8_t r) {
  return r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_FAIL ||
         r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_EXPIRE;
}

static const char* reasonText(uint8_t r) {
  if (isAuthReason(r)) return "The password was rejected";
  if (r == WIFI_REASON_NO_AP_FOUND) return "Network not found";
  if (r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
      r == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD)
    return "The network's security mode isn't supported";
  return "";
}

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastReason = info.wifi_sta_disconnected.reason;
    if (isAuthReason(lastReason)) authFails = authFails + 1;
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    lastReason = 0;
    authFails = 0;
  }
}

// --- link watchdog --------------------------------------------------------------
// Wi-Fi can report "connected" while no packets get through anymore (the radio
// lives on the C6; its link to the P4 or the C6's Wi-Fi can stall). Then the
// web page doesn't load and the Tab5 doesn't even answer ping. While connected
// we ping the router; once that has worked, repeated silence means the link is
// dead and Wi-Fi is reconnected. Routers that never answer ping are left alone.
static constexpr uint32_t PROBE_MS = 15000;
static constexpr int PROBE_FAILS = 3;
static esp_ping_handle_t pingSession = nullptr;
static volatile bool pingBusy = false, pingOk = false;
static uint32_t lastProbe = 0;
static bool routerAnswers = false;   // the router answered at least once on this connection
static int probeFails = 0;

static void onPingSuccess(esp_ping_handle_t, void*) { pingOk = true; }
static void onPingEnd(esp_ping_handle_t, void*) { pingBusy = false; }

static void probeReset() {
  routerAnswers = false;
  probeFails = 0;
  lastProbe = millis();
}

static void probeStart() {
  if (pingSession) { esp_ping_delete_session(pingSession); pingSession = nullptr; }
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress((uint32_t)0)) return;
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
  cfg.count = 2;
  cfg.interval_ms = 300;
  cfg.timeout_ms = 1500;
  cfg.task_stack_size = 3072;
  esp_ping_callbacks_t cb = {};
  cb.on_ping_success = onPingSuccess;
  cb.on_ping_end = onPingEnd;
  if (esp_ping_new_session(&cfg, &cb, &pingSession) != ESP_OK) return;
  pingOk = false;
  pingBusy = true;
  esp_ping_start(pingSession);
}

// Returns true when the link looks dead and Wi-Fi should be reconnected.
static bool probeLoop() {
  uint32_t now = millis();
  if (pingBusy) return false;
  if (pingSession) {   // a probe finished
    esp_ping_delete_session(pingSession);
    pingSession = nullptr;
    if (pingOk) {
      if (!routerAnswers) Serial.printf("[WiFi] router %s answers, link watchdog active\n",
                                        WiFi.gatewayIP().toString().c_str());
      routerAnswers = true;
      probeFails = 0;
    } else if (routerAnswers) {
      probeFails++;
      Serial.printf("[WiFi] router not answering (%d/%d), RSSI %d dBm\n", probeFails, PROBE_FAILS,
                    (int)WiFi.RSSI());
      if (probeFails >= PROBE_FAILS) return true;
      lastProbe = now - PROBE_MS + 3000;   // check again soon
    }
  }
  if (now - lastProbe >= PROBE_MS) {
    lastProbe = now;
    probeStart();
  }
  return false;
}

static const char* psName(wifi_ps_type_t ps) {
  return ps == WIFI_PS_NONE ? "off" : ps == WIFI_PS_MIN_MODEM ? "min modem" : "max modem";
}

// Log the connection and make sure power save really is off (the C6 may refuse
// it while Bluetooth shares the radio; then we know from the log).
static void logConnected() {
  wifi_ps_type_t ps = WIFI_PS_MIN_MODEM;
  esp_wifi_get_ps(&ps);
  if (ps != WIFI_PS_NONE) {
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) Serial.printf("[WiFi] can't turn power save off: %s\n", esp_err_to_name(err));
    esp_wifi_get_ps(&ps);
  }
  Serial.printf("[WiFi] connected to %s: IP %s, router %s, RSSI %d dBm, channel %d, power save %s\n",
                settings.wifiSsid.c_str(), WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(), (int)WiFi.RSSI(), (int)WiFi.channel(), psName(ps));
}

static void startServices() {
  if (!serverUp) {
    server.begin();
    serverUp = true;
  }
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

static void stopAll() {
  if (serverUp) { server.stop(); serverUp = false; }
  if (!wifiStarted) return;
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Starting a Wi-Fi connection while Bluetooth sets up the scale connection
// overloads the shared C6 radio chip; wait for the BLE side to settle first.
static void waitForBleQuiet() {
  uint32_t t0 = millis();
  while (scale.state() == ScaleState::Connecting && millis() - t0 < 10000) vTaskDelay(pdMS_TO_TICKS(100));
}

static void doApply() {
  stopAll();
  waitForBleQuiet();
  lastReason = 0;
  authFails = 0;
  gaveUp = false;
  if (!settings.wifiEnabled) {
    setStatus(false, false, "", "");
    return;
  }
  WiFi.setHostname(HOSTNAME);
  // No Wi-Fi power save: in modem sleep the C6 only picks up traffic addressed to
  // the Tab5 now and then (multicast works, ping and HTTP mostly don't), so the
  // web page doesn't load. Applied when the station starts.
  WiFi.setSleep(false);
  wifiStarted = true;
  if (settings.wifiMode == WIFI_HOTSPOT) {
    WiFi.mode(WIFI_AP);
    ensureApName();
    WiFi.softAP(apName, AP_PASS);
    delay(200);
    startServices();
    setStatus(true, false, WiFi.softAPIP().toString().c_str(), apName);
  } else if (settings.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   // reconnecting is handled in task()
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
    lastRetry = millis();
    setStatus(false, true, "", settings.wifiSsid.c_str());
  } else {
    setStatus(false, false, "", "");
  }
}

static volatile int scanResult = 0;   // networks found, or < 0 when the scan failed

static void doScan() {
  wifi_mode_t m = wifiStarted ? WiFi.getMode() : WIFI_OFF;
  wifiStarted = true;
  WiFi.setSleep(false);
  if (m == WIFI_OFF) WiFi.mode(WIFI_STA);
  else if (m == WIFI_AP) WiFi.mode(WIFI_AP_STA);

  // The radio lives on the C6 (ESP-Hosted): wait until the station is really up,
  // otherwise the scan request fails immediately.
  uint32_t t0 = millis();
  while (!WiFi.STA.started() && millis() - t0 < 5000) delay(50);
  // A pending connect attempt makes the driver refuse to scan.
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
  delay(100);

  int n = WIFI_SCAN_FAILED;
  for (int attempt = 1; attempt <= 3 && n < 0; attempt++) {
    if (attempt > 1) delay(1000);
    n = WiFi.scanNetworks(false, false, false, 400);
    Serial.printf("[WiFi] scan attempt %d: %d (STA started: %d, after %lu ms)\n", attempt, n,
                  WiFi.STA.started(), (unsigned long)(millis() - t0));
  }

  std::vector<WifiNet> found;
  for (int i = 0; i < n; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    bool dup = false;
    for (auto& f : found)
      if (f.ssid == ssid) { dup = true; f.rssi = max(f.rssi, (int)WiFi.RSSI(i)); }
    if (!dup) found.push_back({ssid, (int)WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(found.begin(), found.end(), [](const WifiNet& a, const WifiNet& b) { return a.rssi > b.rssi; });

  // back to AP-only if the hotspot was running; otherwise keep the station up,
  // choosing a network is the likely next step
  if (m == WIFI_AP) WiFi.mode(WIFI_AP);
  lastRetry = 0;   // home Wi-Fi: reconnect right away
  scanResult = n;
  std::lock_guard<std::mutex> l(mtx);
  scanList = found;
}

int lastScanResult() { return scanResult; }

static void task(void*) {
  // Let the Bluetooth task bring up the C6 link first: initialising it from two
  // tasks at the same time is not safe.
  uint32_t t0 = millis();
  while (!scale.bleReady() && millis() - t0 < 15000) vTaskDelay(pdMS_TO_TICKS(100));
  vTaskDelay(pdMS_TO_TICKS(1500));
  WiFi.persistent(false);   // credentials live in our own settings; no extra flash writes
  setupServer();
  WiFi.onEvent(onWifiEvent);

  bool wasConnected = false;
  bool ntpStarted = false;
  for (;;) {
    if (applyRequested) {
      applyRequested = false;
      wasConnected = false;
      doApply();
    }
    if (scanRequested) {
      scanRequested = false;
      doScan();
      scanBusy = false;
    }

    if (settings.wifiEnabled && settings.wifiMode == WIFI_HOME && settings.wifiSsid.length()) {
      bool c = WiFi.status() == WL_CONNECTED;
      if (c && !wasConnected) {
        logConnected();
        probeReset();
        startServices();
        setStatus(true, false, WiFi.localIP().toString().c_str(), settings.wifiSsid.c_str());
        if (!ntpStarted) {
          configTzTime(DEVICE_TZ, "pool.ntp.org", "time.google.com");
          ntpStarted = true;
        }
      } else if (c && probeLoop()) {
        Serial.printf("[WiFi] connected to %s but no traffic gets through, reconnecting\n",
                      settings.wifiSsid.c_str());
        setStatus(false, true, "", settings.wifiSsid.c_str(), "No traffic, reconnecting");
        WiFi.disconnect(false, false);
        delay(500);
        WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
        lastRetry = millis();
        c = false;
      } else if (!c && wasConnected) {
        setStatus(false, true, "", settings.wifiSsid.c_str());
        lastRetry = millis();
      } else if (!c && !gaveUp) {
        if (authFails >= MAX_AUTH_FAILS) {
          // a wrong password won't get better by retrying: stop and ask
          gaveUp = true;
          WiFi.disconnect();
          Serial.printf("[WiFi] password rejected by %s, giving up\n", settings.wifiSsid.c_str());
          setStatus(false, false, "", settings.wifiSsid.c_str(), reasonText(lastReason), true);
        } else {
          static uint8_t shownReason = 0;
          if (lastReason != shownReason) {
            shownReason = lastReason;
            setStatus(false, true, "", settings.wifiSsid.c_str(), reasonText(lastReason));
          }
          if ((lastRetry == 0 || millis() - lastRetry > 15000) &&
              scale.state() != ScaleState::Connecting) {
            lastRetry = millis();
            WiFi.disconnect();
            WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
          }
        }
      }
      wasConnected = c;
      if (ntpStarted && !timeSynced) {
        static bool reported = false;
        if (!reported && time(nullptr) > 1700000000) { reported = true; timeSynced = true; }
      }
    }

    if (serverUp) server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(serverUp ? 2 : 50));
  }
}

void begin() {
  setStatus(false, false, "", "");
  xTaskCreatePinnedToCore(task, "net", 12288, nullptr, 2, nullptr, 0);
}

}  // namespace net
