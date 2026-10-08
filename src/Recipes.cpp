#include "Recipes.h"
#include <Preferences.h>
#include "Backup.h"
#include "Settings.h"

static const RecipeStage V60_STAGES[] = {
    {"Bloom", 0.15f, 0},
    {"Pour", 0.60f, 45},
    {"Pour", 1.00f, 90},
};

static const Recipe DEFAULTS[recipes::COUNT] = {
    // name        pourOver dose   ratio  stop  thr   stages         time window (s)  flow band (mode, set lo/hi)
    {"Espresso",   false,   18.0f, 2.0f,  4,    0.5f, nullptr, 0,    25, 32,           FLOW_AUTO, 1.0f, 2.8f},
    {"Ristretto",  false,   18.0f, 1.5f,  4,    0.5f, nullptr, 0,    18, 25,           FLOW_AUTO, 0.8f, 2.8f},
    {"Lungo",      false,   18.0f, 3.0f,  5,    0.5f, nullptr, 0,    32, 42,           FLOW_AUTO, 1.0f, 3.4f},
    // pour-over flow comes in pulses: the guide is off by default
    {"Pour-over",  true,    15.0f, 16.5f, 30,   1.0f, V60_STAGES, 3, 150, 210,         FLOW_OFF, 3.0f, 8.0f},
    {"Free",       false,   0.0f,  0.0f,  4,    0.5f, nullptr, 0,    0, 0,             FLOW_AUTO, 1.0f, 2.8f},
};

static Recipe list[recipes::COUNT];

namespace recipes {

static const char* NS = "recipes";

static void applyStyle(Recipe& r) {
  r.stages = r.pourOver ? V60_STAGES : nullptr;
  r.stageCount = r.pourOver ? 3 : 0;
}

void load() {
  Preferences p;
  p.begin(NS, false);  // read-write: avoids "NOT_FOUND" errors before the first save
  char k[8];
  for (int i = 0; i < COUNT; i++) {
    list[i] = DEFAULTS[i];
    snprintf(k, sizeof(k), "n%d", i);
    String n = p.getString(k, "");
    if (n.length()) snprintf(list[i].name, sizeof(list[i].name), "%s", n.c_str());
    snprintf(k, sizeof(k), "p%d", i); list[i].pourOver = p.getBool(k, list[i].pourOver);
    applyStyle(list[i]);
    snprintf(k, sizeof(k), "d%d", i); list[i].dose = p.getFloat(k, list[i].dose);
    snprintf(k, sizeof(k), "r%d", i); list[i].ratio = p.getFloat(k, list[i].ratio);
    snprintf(k, sizeof(k), "s%d", i); list[i].stopDelayS = p.getUChar(k, list[i].stopDelayS);
    snprintf(k, sizeof(k), "t%d", i); list[i].startThreshold = p.getFloat(k, list[i].startThreshold);
    snprintf(k, sizeof(k), "a%d", i); list[i].timeMin = p.getUShort(k, list[i].timeMin);
    snprintf(k, sizeof(k), "b%d", i); list[i].timeMax = p.getUShort(k, list[i].timeMax);
    snprintf(k, sizeof(k), "m%d", i); list[i].flowMode = p.getUChar(k, list[i].flowMode);
    snprintf(k, sizeof(k), "f%d", i); list[i].flowMin = p.getFloat(k, list[i].flowMin);
    snprintf(k, sizeof(k), "g%d", i); list[i].flowMax = p.getFloat(k, list[i].flowMax);
  }
  p.end();
}

void save(int i) {
  if (i < 0 || i >= COUNT) return;
  Preferences p;
  p.begin(NS, false);
  char k[8];
  snprintf(k, sizeof(k), "d%d", i); p.putFloat(k, list[i].dose);
  snprintf(k, sizeof(k), "r%d", i); p.putFloat(k, list[i].ratio);
  snprintf(k, sizeof(k), "s%d", i); p.putUChar(k, list[i].stopDelayS);
  snprintf(k, sizeof(k), "t%d", i); p.putFloat(k, list[i].startThreshold);
  snprintf(k, sizeof(k), "a%d", i); p.putUShort(k, list[i].timeMin);
  snprintf(k, sizeof(k), "b%d", i); p.putUShort(k, list[i].timeMax);
  snprintf(k, sizeof(k), "m%d", i); p.putUChar(k, list[i].flowMode);
  snprintf(k, sizeof(k), "f%d", i); p.putFloat(k, list[i].flowMin);
  snprintf(k, sizeof(k), "g%d", i); p.putFloat(k, list[i].flowMax);
  snprintf(k, sizeof(k), "n%d", i); p.putString(k, list[i].name);
  snprintf(k, sizeof(k), "p%d", i); p.putBool(k, list[i].pourOver);
  p.end();
  backup::markDirty();
}

const char* defaultName(int i) { return DEFAULTS[constrain(i, 0, COUNT - 1)].name; }

const char* rename(int i, const char* name) {
  if (i < 0 || i >= COUNT) return "";
  // trim, drop characters that would need escaping, limit the length
  char clean[MAX_NAME_LEN + 1];
  int n = 0;
  for (const char* s = name; *s && n < MAX_NAME_LEN; s++) {
    if (*s == '"' || *s == '\\' || (uint8_t)*s < 0x20) continue;
    if (n == 0 && *s == ' ') continue;
    clean[n++] = *s;
  }
  while (n > 0 && clean[n - 1] == ' ') n--;
  clean[n] = 0;
  if (!n) snprintf(clean, sizeof(clean), "%s", DEFAULTS[i].name);
  // keep names unique (history, ghost and dial-in match shots by name)
  char candidate[MAX_NAME_LEN + 4];
  snprintf(candidate, sizeof(candidate), "%s", clean);
  for (int suffix = 2; suffix < 10; suffix++) {
    bool taken = false;
    for (int j = 0; j < COUNT; j++)
      if (j != i && strcmp(list[j].name, candidate) == 0) taken = true;
    if (!taken) break;
    snprintf(candidate, sizeof(candidate), "%.*s %d", MAX_NAME_LEN - 2, clean, suffix);
  }
  snprintf(list[i].name, sizeof(list[i].name), "%s", candidate);
  save(i);
  return list[i].name;
}

void setPourOver(int i, bool pourOver) {
  if (i < 0 || i >= COUNT) return;
  list[i].pourOver = pourOver;
  applyStyle(list[i]);
  save(i);
}

void resetToDefault(int i) {
  if (i < 0 || i >= COUNT) return;
  list[i] = DEFAULTS[i];
  save(i);
}

Recipe& get(int i) { return list[constrain(i, 0, COUNT - 1)]; }
int activeIndex() { return constrain((int)settings.activeRecipe, 0, COUNT - 1); }
Recipe& active() { return list[activeIndex()]; }

void setActive(int i) {
  settings.activeRecipe = constrain(i, 0, COUNT - 1);
  settings.save();
}

}  // namespace recipes
