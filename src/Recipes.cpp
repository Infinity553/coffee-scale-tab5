#include "Recipes.h"
#include <Preferences.h>
#include "Settings.h"

static const RecipeStage V60_STAGES[] = {
    {"Bloom", 0.15f, 0},
    {"Pour", 0.60f, 45},
    {"Pour", 1.00f, 90},
};

static Recipe list[recipes::COUNT] = {
    // name        pourOver dose   ratio  stop  thr   stages         time window (s)
    {"Espresso",   false,   18.0f, 2.0f,  4,    0.5f, nullptr, 0,    25, 32},
    {"Ristretto",  false,   18.0f, 1.5f,  4,    0.5f, nullptr, 0,    18, 25},
    {"Lungo",      false,   18.0f, 3.0f,  5,    0.5f, nullptr, 0,    32, 42},
    {"Pour-over",  true,    15.0f, 16.5f, 30,   1.0f, V60_STAGES, 3, 150, 210},
    {"Free",       false,   0.0f,  0.0f,  4,    0.5f, nullptr, 0,    0, 0},
};

namespace recipes {

static const char* NS = "recipes";

void load() {
  Preferences p;
  p.begin(NS, false);  // read-write: avoids "NOT_FOUND" errors before the first save
  char k[8];
  for (int i = 0; i < COUNT; i++) {
    snprintf(k, sizeof(k), "d%d", i); list[i].dose = p.getFloat(k, list[i].dose);
    snprintf(k, sizeof(k), "r%d", i); list[i].ratio = p.getFloat(k, list[i].ratio);
    snprintf(k, sizeof(k), "s%d", i); list[i].stopDelayS = p.getUChar(k, list[i].stopDelayS);
    snprintf(k, sizeof(k), "t%d", i); list[i].startThreshold = p.getFloat(k, list[i].startThreshold);
    snprintf(k, sizeof(k), "a%d", i); list[i].timeMin = p.getUShort(k, list[i].timeMin);
    snprintf(k, sizeof(k), "b%d", i); list[i].timeMax = p.getUShort(k, list[i].timeMax);
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
  p.end();
}

Recipe& get(int i) { return list[constrain(i, 0, COUNT - 1)]; }
int activeIndex() { return constrain((int)settings.activeRecipe, 0, COUNT - 1); }
Recipe& active() { return list[activeIndex()]; }

void setActive(int i) {
  settings.activeRecipe = constrain(i, 0, COUNT - 1);
  settings.save();
}

}  // namespace recipes
