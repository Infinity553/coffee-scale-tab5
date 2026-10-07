// Desktop simulator entry point (pio run -e native).
// Env vars:
//   SIM_SETUP=1          start in the first-run setup
//   SIM_SCREEN=name      welcome | scan | prefs | settings | recipes | history | wifi |
//                        networks | keyboard
//   SIM_THEME=1          Racer colour scheme
//   SIM_SEED=N           write N made-up shots to the simulated SD card first
//   SIM_NO_SD=1          behave as if no SD card is inserted
//   SIM_TAP=x,y@sec      tap at logical coordinates after sec seconds (repeat with ;)
//   SIM_SNAPS=2,10,25    save PNG screenshots to sim/out at these seconds, then exit
#include <M5Unified.h>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <vector>
#include "AcaiaScale.h"
#include "Brew.h"
#include "History.h"
#include "Net.h"
#include "Recipes.h"
#include "Settings.h"
#include "UI.h"

static std::vector<float> snaps;
static size_t nextSnap = 0;
struct SimTap { float at; int x, y; bool done; };
static std::vector<SimTap> taps;

static void seedHistory(int count) {
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> u(0, 1);
  time_t now = time(nullptr);
  for (int i = 0; i < count; i++) {
    bool pour = i % 9 == 4;
    bool rist = i % 7 == 3;
    float dose = pour ? 15.0f : 18.0f;
    float yield = pour ? 245 + u(rng) * 12 : rist ? 26 + u(rng) * 3 : 34 + u(rng) * 6;
    float dur = pour ? 170 + u(rng) * 30 : 24 + u(rng) * 10;
    std::vector<BrewSample> s;
    float prevW = 0;
    for (float t = 0; t <= dur + 4; t += pour ? 0.5f : 0.1f) {
      float x = t / dur;
      float w = yield / (1 + expf(-(x - 0.45f) * 9)) - yield / (1 + expf(0.45f * 9));
      w = std::max(0.0f, std::min(w, yield));
      float f = (w - prevW) / (pour ? 0.5f : 0.1f);
      s.push_back({t, roundf(w * 10) / 10, std::max(0.0f, f)});
      prevW = w;
    }
    float peak = 0;
    for (auto& x : s) peak = std::max(peak, x.f);
    ShotMeta m;
    m.epoch = (uint32_t)(now - (count - i) * 5 * 3600 - (int)(u(rng) * 3000));
    m.time = dur;
    m.yield = roundf(yield * 10) / 10;
    m.dose = dose;
    m.peakFlow = peak;
    m.firstDrop = 4 + u(rng) * 4;
    m.grind = pour ? 22.0f : 9.5f + (int)(u(rng) * 4) * 0.5f;
    m.rating = (uint8_t)(u(rng) < 0.15f ? 0 : 1 + (int)(u(rng) * 5));
    m.recipe = pour ? "Pour-over" : rist ? "Ristretto" : "Espresso";
    if (i % 5 == 0) m.notes = "Sweet, a little bright. Try one step finer.";
    history::save(m, s.data(), (int)s.size());
  }
  printf("seeded %d shots\n", count);
}

static void openScreen(const std::string& v) {
  if (v == "welcome") ui::setScreen(Screen::SetupWelcome);
  if (v == "scan") { scale.startDiscovery(); ui::setScreen(Screen::SetupScan); }
  if (v == "prefs") ui::setScreen(Screen::SetupPrefs);
  if (v == "settings") ui::setScreen(Screen::Settings);
  if (v == "recipes") ui::setScreen(Screen::Recipes);
  if (v == "history") ui::setScreen(Screen::History);
  if (v == "wifi") ui::setScreen(Screen::Wifi);
  if (v == "networks") ui::setScreen(Screen::WifiNetworks);
}

void setup() {
  M5.begin();
  setenv("TZ", DEVICE_TZ, 1);
  tzset();
  settings.load();
  if (getenv("SIM_WIFI")) { settings.wifiEnabled = true; settings.wifiMode = WIFI_HOME; }
  recipes::load();
  brew.begin();
  ui::begin();
  scale.begin();
  history::begin();
  net::begin();
  if (const char* n = getenv("SIM_SEED")) seedHistory(atoi(n));

  if (const char* s = getenv("SIM_SCREEN")) openScreen(s);
  if (const char* s = getenv("SIM_SNAPS")) {
    std::string v = s;
    size_t p = 0;
    while (p < v.size()) {
      size_t e = v.find(',', p);
      snaps.push_back(std::stof(v.substr(p, e - p)));
      if (e == std::string::npos) break;
      p = e + 1;
    }
  }
  if (const char* s = getenv("SIM_TAP")) {
    std::string v = s;
    size_t p = 0;
    while (p < v.size()) {
      size_t e = v.find(';', p);
      std::string one = v.substr(p, e == std::string::npos ? std::string::npos : e - p);
      int x, y;
      float at;
      if (sscanf(one.c_str(), "%d,%d@%f", &x, &y, &at) == 3) taps.push_back({at, x, y, false});
      if (e == std::string::npos) break;
      p = e + 1;
    }
  }
}

void loop() {
  M5.update();
  float g;
  uint32_t at;
  if (scale.poll(g, at)) brew.onWeight(g, at);
  brew.tick(millis());
  float sec = millis() / 1000.0f;
  for (auto& t : taps) {
    if (!t.done && sec >= t.at) { t.done = true; ui::simulateTap(t.x, t.y); }
  }
  ui::update();

  if (nextSnap < snaps.size() && sec >= snaps[nextSnap]) {
    ui::invalidate();
    size_t len = 0;
    void* png = ui::snapshotPng(&len);
    char name[64];
    snprintf(name, sizeof(name), "sim/out/snap_%s_%02d.png",
             getenv("SIM_SCREEN") ? getenv("SIM_SCREEN") : "main", (int)snaps[nextSnap]);
    if (FILE* f = fopen(name, "wb")) { fwrite(png, 1, len, f); fclose(f); }
    free(png);
    printf("saved %s\n", name);
    if (getenv("SIM_NATIVE_SNAP")) {  // what the panel actually shows (native orientation)
      png = M5.Display.createPng(&len, 0, 0, M5.Display.width(), M5.Display.height());
      snprintf(name, sizeof(name), "sim/out/native_%02d.png", (int)snaps[nextSnap]);
      if (FILE* f = fopen(name, "wb")) { fwrite(png, 1, len, f); fclose(f); }
      free(png);
    }
    if (++nextSnap == snaps.size()) exit(0);
  }
  M5.delay(4);
}
