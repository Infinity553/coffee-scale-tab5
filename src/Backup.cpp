#include "Backup.h"
#include <ArduinoJson.h>
#include <ctime>
#include "History.h"
#include "Recipes.h"
#include "Settings.h"
#include "Storage.h"

namespace backup {

static const char* PATH = "/coffeescale/settings-backup.json";
static const char* FORMAT = "coffeescale-backup";
static bool dirty = false;
static bool restoring = false;   // don't schedule a backup while applying one
static uint32_t dirtyMs = 0;
static uint32_t savedMs = 0;
static uint32_t restartAt = 0;

std::string toJson() {
  JsonDocument doc;
  doc["format"] = FORMAT;
  doc["version"] = 1;
  time_t now = time(nullptr);
  doc["created"] = (uint32_t)(now > 1700000000 ? now : 0);

  JsonObject s = doc["settings"].to<JsonObject>();
  s["scaleAddress"] = settings.scaleAddress.c_str();
  s["scaleName"] = settings.scaleName.c_str();
  s["autoTare"] = settings.autoTare;
  s["autoStart"] = settings.autoStart;
  s["autoStop"] = settings.autoStop;
  s["sound"] = settings.sound;
  s["brightness"] = settings.brightness;
  s["flipScreen"] = settings.flipScreen;
  s["colorScheme"] = settings.colorScheme;
  s["activeRecipe"] = settings.activeRecipe;
  s["dripComp"] = settings.dripComp;
  s["dripGrams"] = settings.dripGrams;
  s["ghostMode"] = settings.ghostMode;
  s["refShotId"] = settings.refShotId;
  s["lastGrind"] = settings.lastGrind;
  s["finerIsLower"] = settings.finerIsLower;
  s["sleepMin"] = settings.sleepMin;
  s["autoBackup"] = settings.autoBackup;
  s["wifiEnabled"] = settings.wifiEnabled;
  s["wifiMode"] = settings.wifiMode;
  s["wifiSsid"] = settings.wifiSsid.c_str();   // the password is deliberately not saved

  JsonArray rs = doc["recipes"].to<JsonArray>();
  for (int i = 0; i < recipes::COUNT; i++) {
    const Recipe& r = recipes::get(i);
    JsonObject o = rs.add<JsonObject>();
    o["name"] = r.name;
    o["pourOver"] = r.pourOver;
    o["dose"] = r.dose;
    o["ratio"] = r.ratio;
    o["stopDelay"] = r.stopDelayS;
    o["startThreshold"] = r.startThreshold;
    o["timeMin"] = r.timeMin;
    o["timeMax"] = r.timeMax;
  }
  std::string out;
  serializeJsonPretty(doc, out);
  return out;
}

template <class T>
static void take(JsonObjectConst o, const char* key, T& field) {
  if (!o[key].isNull()) field = o[key].as<T>();
}

static void takeStr(JsonObjectConst o, const char* key, String& field) {
  if (o[key].is<const char*>()) field = o[key].as<const char*>();
}

bool fromJson(const std::string& json, std::string* error) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    if (error) *error = "The file is not a valid backup (JSON error).";
    return false;
  }
  if (!(doc["format"] == FORMAT)) {
    if (error) *error = "The file is not a Coffee Scale backup.";
    return false;
  }
  restoring = true;
  JsonObjectConst s = doc["settings"];
  if (!s.isNull()) {
    takeStr(s, "scaleAddress", settings.scaleAddress);
    takeStr(s, "scaleName", settings.scaleName);
    take(s, "autoTare", settings.autoTare);
    take(s, "autoStart", settings.autoStart);
    take(s, "autoStop", settings.autoStop);
    take(s, "sound", settings.sound);
    take(s, "brightness", settings.brightness);
    take(s, "flipScreen", settings.flipScreen);
    take(s, "colorScheme", settings.colorScheme);
    take(s, "activeRecipe", settings.activeRecipe);
    take(s, "dripComp", settings.dripComp);
    take(s, "dripGrams", settings.dripGrams);
    take(s, "ghostMode", settings.ghostMode);
    take(s, "refShotId", settings.refShotId);
    take(s, "lastGrind", settings.lastGrind);
    take(s, "finerIsLower", settings.finerIsLower);
    take(s, "sleepMin", settings.sleepMin);
    take(s, "autoBackup", settings.autoBackup);
    take(s, "wifiEnabled", settings.wifiEnabled);
    take(s, "wifiMode", settings.wifiMode);
    String ssid = settings.wifiSsid;
    takeStr(s, "wifiSsid", settings.wifiSsid);
    if (!(settings.wifiSsid == ssid)) settings.wifiPass = "";   // other network: password unknown
    // keep values in range even if the file was edited by hand
    settings.brightness = constrain((int)settings.brightness, 30, 255);
    settings.colorScheme = settings.colorScheme > 1 ? 0 : settings.colorScheme;
    settings.activeRecipe = constrain((int)settings.activeRecipe, 0, recipes::COUNT - 1);
    settings.ghostMode = settings.ghostMode > GHOST_REF ? GHOST_LAST : settings.ghostMode;
    settings.wifiMode = settings.wifiMode > WIFI_HOTSPOT ? WIFI_HOTSPOT : settings.wifiMode;
  }
  settings.configured = true;
  settings.save();

  JsonArrayConst rs = doc["recipes"];
  int i = 0;
  for (JsonObjectConst o : rs) {
    if (i >= recipes::COUNT) break;
    Recipe& r = recipes::get(i);
    take(o, "dose", r.dose);
    take(o, "ratio", r.ratio);
    take(o, "stopDelay", r.stopDelayS);
    take(o, "startThreshold", r.startThreshold);
    take(o, "timeMin", r.timeMin);
    take(o, "timeMax", r.timeMax);
    r.dose = constrain(r.dose, 0.0f, 60.0f);
    r.ratio = constrain(r.ratio, 0.0f, 25.0f);
    if (!o["pourOver"].isNull()) recipes::setPourOver(i, o["pourOver"].as<bool>());
    if (o["name"].is<const char*>()) recipes::rename(i, o["name"].as<const char*>());
    else recipes::save(i);
    i++;
  }
  restoring = false;
  dirty = false;
  return true;
}

bool saveToSd() {
  if (!history::available()) return false;
  storage::mkdirs("/coffeescale");
  if (!storage::write(PATH, toJson())) return false;
  savedMs = millis();
  if (!savedMs) savedMs = 1;
  dirty = false;
  return true;
}

bool sdBackupInfo(uint32_t& createdEpoch) {
  createdEpoch = 0;
  std::string s;
  if (!storage::mounted() || !storage::read(PATH, s)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, s) != DeserializationError::Ok || !(doc["format"] == FORMAT)) return false;
  createdEpoch = doc["created"] | 0u;
  return true;
}

bool restoreFromSd(std::string* error) {
  std::string s;
  if (!storage::mounted() || !storage::read(PATH, s)) {
    if (error) *error = "No backup on the SD card.";
    return false;
  }
  return fromJson(s, error);
}

uint32_t lastSavedMs() { return savedMs; }

void markDirty() {
  if (restoring) return;
  dirty = true;
  dirtyMs = millis();
}

void loop(uint32_t now, bool busy) {
  if (restartAt && (int32_t)(now - restartAt) >= 0) {
#ifdef ARDUINO
    ESP.restart();
#else
    restartAt = 0;
#endif
  }
  if (!dirty || busy || !settings.autoBackup || now - dirtyMs < 5000) return;
  if (!history::available()) return;   // keep it pending until a card is inserted
  saveToSd();
}

void restartSoon() { restartAt = millis() + 1500; if (!restartAt) restartAt = 1; }
bool restartPending() { return restartAt != 0; }

}  // namespace backup
