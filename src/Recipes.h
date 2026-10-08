#pragma once
#include <Arduino.h>

// A pour-over stage: pour up to `fraction` of the final yield, starting at `atSec`.
struct RecipeStage {
  const char* label;
  float       fraction;
  uint16_t    atSec;
};

struct Recipe {
  char               name[24];        // user can rename (max MAX_NAME_LEN characters)
  bool               pourOver;
  float              dose;            // g of coffee (0 = not set)
  float              ratio;           // yield / dose (0 = no target)
  uint8_t            stopDelayS;      // auto stop after this many seconds without gain
  float              startThreshold;  // g increase that starts the plot
  const RecipeStage* stages;
  uint8_t            stageCount;
  uint16_t           timeMin;         // dial-in: good shot time window (s), 0 = none
  uint16_t           timeMax;
  float              flowMin;         // flow guide: target band (g/s), 0 = off
  float              flowMax;

  // Target beverage weight, or 0 when the recipe has no target.
  float target() const {
    return (dose > 0 && ratio > 0) ? roundf(dose * ratio * 10) / 10 : 0;
  }
  float stageTarget(int i) const { return roundf(target() * stages[i].fraction); }
};

namespace recipes {
constexpr int COUNT = 5;
constexpr int MAX_NAME_LEN = 18;
void    load();
void    save(int index);
Recipe& get(int index);
Recipe& active();
int     activeIndex();
void    setActive(int index);
// Renames a recipe. Empty or duplicate names are adjusted; returns the final name.
const char* rename(int index, const char* name);
// Espresso style (no stages) or pour-over with timed pours.
void    setPourOver(int index, bool pourOver);
// Back to the built-in settings for this slot (name, dose, ratio, ...).
void    resetToDefault(int index);
const char* defaultName(int index);
}  // namespace recipes
