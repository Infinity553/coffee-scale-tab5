#pragma once
#include <Arduino.h>

// A pour-over stage: pour up to `fraction` of the final yield, starting at `atSec`.
struct RecipeStage {
  const char* label;
  float       fraction;
  uint16_t    atSec;
};

struct Recipe {
  const char*        name;
  bool               pourOver;
  float              dose;            // g of coffee (0 = not set)
  float              ratio;           // yield / dose (0 = no target)
  uint8_t            stopDelayS;      // auto stop after this many seconds without gain
  float              startThreshold;  // g increase that starts the plot
  const RecipeStage* stages;
  uint8_t            stageCount;
  uint16_t           timeMin;         // dial-in: good shot time window (s), 0 = none
  uint16_t           timeMax;

  // Target beverage weight, or 0 when the recipe has no target.
  float target() const {
    return (dose > 0 && ratio > 0) ? roundf(dose * ratio * 10) / 10 : 0;
  }
  float stageTarget(int i) const { return roundf(target() * stages[i].fraction); }
};

namespace recipes {
constexpr int COUNT = 5;
void    load();
void    save(int index);
Recipe& get(int index);
Recipe& active();
int     activeIndex();
void    setActive(int index);
}  // namespace recipes
