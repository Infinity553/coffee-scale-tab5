#pragma once
#include <string>
#include "History.h"
#include "Recipes.h"

// Dial-in assistant: looks at a finished shot (time, ratio, taste) and suggests
// the next change. Grind suggestions use a model learned from the shot history:
// how many seconds one grind unit changes the shot time for this recipe and dose.
namespace dialin {

enum Taste : uint8_t { TASTE_NONE = 0, TASTE_SOUR = 1, TASTE_BALANCED = 2, TASTE_BITTER = 3 };

struct Advice {
  bool        valid = false;
  std::string headline;      // short, e.g. "Ran 5 s fast: grind finer"
  std::string detail;        // how much and why
  float       grind = -1;    // suggested grind setting (-1 = none)
  float       ratio = 0;     // suggested recipe ratio (0 = none)
  bool        good = false;  // nothing to change
};

// Time window the shot is judged against (reference shot of the same recipe if
// set, otherwise the recipe's window). Returns false if there is none.
bool targetWindow(const ShotMeta& shot, const Recipe& r, float& lo, float& hi, bool& fromReference);

Advice advise(const ShotMeta& shot, const Recipe& r);

}  // namespace dialin
