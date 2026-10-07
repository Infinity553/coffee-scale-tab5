#include "History.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <ctime>
#include <mutex>
#include "Recipes.h"
#include "Settings.h"
#include "Storage.h"
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <chrono>
#include <thread>
#endif

namespace history {

static const char* DIR = "/coffeescale";
static const char* SHOT_DIR = "/coffeescale/shots";
static const char* INDEX = "/coffeescale/index.json";

static std::recursive_mutex mtx;
static std::vector<ShotMeta> shots;
static bool loaded = false;
static uint32_t ver = 1;

static std::vector<BrewSample> lastCurve, prevCurve, refCurve;
static uint32_t refLoadedId = 0;
static std::string lastRecipe, prevRecipe, refRecipe;
static const std::vector<BrewSample> empty;

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
static void appendEscaped(std::string& o, const std::string& s) {
  o += '"';
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  o += '"';
}

static void appendNum(std::string& o, const char* fmt, float v) {
  char b[24];
  snprintf(b, sizeof(b), fmt, v);
  o += b;
}

std::string metaJson(const ShotMeta& m) {
  std::string o;
  o.reserve(400);
  char b[320];
  snprintf(b, sizeof(b),
           "{\"id\":%lu,\"time\":%lu,\"duration\":%.1f,\"yield\":%.1f,\"dose\":%.1f,"
           "\"peakFlow\":%.2f,\"firstDrop\":%.1f,\"grind\":%.1f,\"rating\":%u,\"recipe\":",
           (unsigned long)m.id, (unsigned long)m.epoch, m.time, m.yield, m.dose, m.peakFlow,
           m.firstDrop, m.grind, m.rating);
  o += b;
  appendEscaped(o, m.recipe);
  o += ",\"notes\":";
  appendEscaped(o, m.notes);
  o += ",\"spark\":[";
  for (size_t i = 0; i < m.spark.size(); i++) {
    if (i) o += ',';
    appendNum(o, "%.1f", m.spark[i]);
  }
  o += "]}";
  return o;
}

static void metaFromJson(JsonObjectConst j, ShotMeta& m) {
  m.id = j["id"] | 0u;
  m.epoch = j["time"] | 0u;
  m.time = j["duration"] | 0.0f;
  m.yield = j["yield"] | 0.0f;
  m.dose = j["dose"] | 0.0f;
  m.peakFlow = j["peakFlow"] | 0.0f;
  m.firstDrop = j["firstDrop"] | -1.0f;
  m.grind = j["grind"] | -1.0f;
  m.rating = j["rating"] | 0;
  m.recipe = (const char*)(j["recipe"] | "");
  m.notes = (const char*)(j["notes"] | "");
  m.spark.clear();
  for (float v : j["spark"].as<JsonArrayConst>()) m.spark.push_back(v);
}

static std::string shotPath(uint32_t id) {
  char b[48];
  snprintf(b, sizeof(b), "%s/%lu.json", SHOT_DIR, (unsigned long)id);
  return b;
}

static bool writeIndex() {
  std::string o = "{\"version\":1,\"shots\":[";
  for (size_t i = 0; i < shots.size(); i++) {
    if (i) o += ",\n";
    o += metaJson(shots[i]);
  }
  o += "]}";
  return storage::write(INDEX, o);
}

static void loadIndex() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  shots.clear();
  storage::mkdirs(SHOT_DIR);
  std::string s;
  if (storage::read(INDEX, s)) {
    JsonDocument doc;
    if (deserializeJson(doc, s) == DeserializationError::Ok) {
      for (JsonObjectConst j : doc["shots"].as<JsonArrayConst>()) {
        ShotMeta m;
        metaFromJson(j, m);
        if (m.id) shots.push_back(std::move(m));
      }
    } else {
      log_w("history index unreadable, starting a new one");
    }
  }
  loaded = true;
  refLoadedId = 0;
  ver++;
  log_i("history: %u shots", (unsigned)shots.size());
}

// ---------------------------------------------------------------------------
// SD watcher
// ---------------------------------------------------------------------------
static void pollOnce() {
  if (!storage::mounted()) {
    if (storage::mount()) loadIndex();
  } else if (!storage::check()) {
    std::lock_guard<std::recursive_mutex> l(mtx);
    loaded = false;
    shots.clear();
    ver++;
  }
}

#ifdef ARDUINO
static void watchTask(void*) {
  for (;;) {
    pollOnce();
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}
#endif

void begin() {
  pollOnce();
#ifdef ARDUINO
  xTaskCreatePinnedToCore(watchTask, "sdwatch", 6144, nullptr, 1, nullptr, 0);
#else
  std::thread([] {
    for (;;) { pollOnce(); std::this_thread::sleep_for(std::chrono::seconds(3)); }
  }).detach();
#endif
}

bool available() { return loaded && storage::mounted(); }
uint32_t version() { return ver; }

size_t count() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  return shots.size();
}

std::vector<ShotMeta> list(bool byRating) {
  std::vector<ShotMeta> v;
  {
    std::lock_guard<std::recursive_mutex> l(mtx);
    v = shots;
  }
  std::sort(v.begin(), v.end(), [byRating](const ShotMeta& a, const ShotMeta& b) {
    if (byRating && a.rating != b.rating) return a.rating > b.rating;
    return a.id > b.id;   // ids increase, so this is most recent first
  });
  return v;
}

bool get(uint32_t id, ShotMeta& out) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  for (auto& m : shots)
    if (m.id == id) { out = m; return true; }
  return false;
}

uint32_t save(ShotMeta& meta, const BrewSample* s, int n) {
  // sparkline: weight at 24 evenly spaced times
  meta.spark.clear();
  if (n > 1) {
    float tEnd = s[n - 1].t;
    int j = 0;
    for (int k = 0; k < 24; k++) {
      float t = tEnd * k / 23;
      while (j < n - 1 && s[j + 1].t <= t) j++;
      meta.spark.push_back(roundf(max(0.0f, s[j].w) * 10) / 10);
    }
  }
  if (!meta.epoch) {
    time_t now = time(nullptr);
    meta.epoch = now > 1700000000 ? (uint32_t)now : 0;
  }

  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!available()) return 0;
  uint32_t maxId = 0;
  for (auto& m : shots) maxId = max(maxId, m.id);
  meta.id = maxId + 1;

  std::string o = metaJson(meta);
  o.pop_back();   // reopen the object to append the curve
  std::string t = ",\"samples\":{\"t\":[", w = "],\"w\":[", f = "],\"f\":[";
  for (int i = 0; i < n; i++) {
    const char* sep = i ? "," : "";
    t += sep; w += sep; f += sep;
    appendNum(t, "%.2f", s[i].t);
    appendNum(w, "%.1f", s[i].w);
    appendNum(f, "%.2f", s[i].f);
  }
  o += t + w + f + "]}}";

  if (!storage::write(shotPath(meta.id).c_str(), o)) return 0;
  shots.push_back(meta);
  writeIndex();
  ver++;
  return meta.id;
}

bool update(const ShotMeta& meta) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!available()) return false;
  for (auto& m : shots) {
    if (m.id != meta.id) continue;
    m.rating = meta.rating;
    m.grind = meta.grind;
    m.notes = meta.notes;
    ver++;
    return writeIndex();
  }
  return false;
}

bool remove(uint32_t id) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!available()) return false;
  auto it = std::find_if(shots.begin(), shots.end(), [id](const ShotMeta& m) { return m.id == id; });
  if (it == shots.end()) return false;
  shots.erase(it);
  storage::remove(shotPath(id).c_str());
  if (settings.refShotId == id) setReference(0);
  ver++;
  return writeIndex();
}

bool readShotFile(uint32_t id, std::string& json) {
  return available() && storage::read(shotPath(id).c_str(), json);
}

bool readIndexFile(std::string& json) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!available()) return false;
  if (!storage::read(INDEX, json)) json = "{\"version\":1,\"shots\":[]}";
  return true;
}

bool loadCurve(uint32_t id, std::vector<BrewSample>& out) {
  out.clear();
  std::string s;
  if (!readShotFile(id, s)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, s) != DeserializationError::Ok) return false;
  JsonArrayConst t = doc["samples"]["t"], w = doc["samples"]["w"], f = doc["samples"]["f"];
  size_t n = min(t.size(), w.size());
  out.reserve(n);
  for (size_t i = 0; i < n; i++) {
    out.push_back({t[i].as<float>(), w[i].as<float>(), i < f.size() ? f[i].as<float>() : 0.0f});
  }
  return n > 1;
}

// ---------------------------------------------------------------------------
// ghost curves
// ---------------------------------------------------------------------------
void rememberLast(const BrewSample* s, int n, const char* recipe) {
  lastCurve.assign(s, s + n);
  lastRecipe = recipe;
}

void onRunStarted() {
  prevCurve = lastCurve;
  prevRecipe = lastRecipe;
  ver++;
}

void setReference(uint32_t id) {
  settings.refShotId = id;
  settings.save();
  refLoadedId = 0;
  ver++;
}

const std::vector<BrewSample>& ghost() {
  const char* active = recipes::active().name;
  if (settings.ghostMode == GHOST_LAST) return prevRecipe == active ? prevCurve : empty;
  if (settings.ghostMode == GHOST_REF && settings.refShotId) {
    if (refLoadedId != settings.refShotId && available()) {
      refLoadedId = settings.refShotId;
      ShotMeta m;
      refRecipe = get(refLoadedId, m) ? m.recipe : "";
      if (!loadCurve(refLoadedId, refCurve)) refCurve.clear();
    }
    return refRecipe == active ? refCurve : empty;
  }
  return empty;
}

const char* ghostLabel() {
  return settings.ghostMode == GHOST_REF ? "Reference" : "Last shot";
}

}  // namespace history
