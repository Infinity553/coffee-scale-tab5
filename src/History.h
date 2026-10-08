#pragma once
#include <Arduino.h>
#include <string>
#include <vector>
#include "Brew.h"

// Shot history on the SD card (only when a card is inserted):
//   /coffeescale/index.json        list of all shots (metadata + sparkline)
//   /coffeescale/shots/<id>.json   one shot incl. the full weight/flow curve
// Also keeps the curves used as "ghost" on the live plot.

struct ShotMeta {
  uint32_t    id = 0;
  uint32_t    epoch = 0;      // unix time, 0 = clock was not set
  float       time = 0;       // shot time (s)
  float       yield = 0;      // g in the cup
  float       dose = 0;       // g of coffee, 0 = unknown
  float       peakFlow = 0;   // g/s
  float       firstDrop = -1; // s after timer start, -1 = unknown
  float       grind = -1;     // grinder setting, -1 = not set
  uint8_t     rating = 0;     // 0 = not rated, 1..5 stars
  uint8_t     taste = 0;      // dialin::Taste (0 = not set, 1 sour, 2 balanced, 3 bitter)
  int8_t      inBand = -1;    // % of the main extraction inside the flow band, -1 = not measured
  std::string recipe;
  std::string notes;
  std::vector<float> spark;   // ~24 weight points for previews

  float ratio() const { return dose > 0 ? yield / dose : 0; }
  float avgFlow() const { return time > 0 ? yield / time : 0; }
};

namespace history {

void begin();                 // mounts the SD card and watches for insert/removal
bool available();             // SD card present and index loaded
uint32_t version();           // changes whenever the list or ghost changes

std::vector<ShotMeta> list(bool byRating);
bool get(uint32_t id, ShotMeta& out);
size_t count();

// Saves a finished shot; fills in id/epoch/spark. Returns the id, 0 if not saved.
uint32_t save(ShotMeta& meta, const BrewSample* samples, int n);
bool update(const ShotMeta& meta);   // rating / taste / grind / notes
bool remove(uint32_t id);
bool loadCurve(uint32_t id, std::vector<BrewSample>& out);
bool readShotFile(uint32_t id, std::string& json);
bool readIndexFile(std::string& json);

// Ghost curve for the live plot (per settings.ghostMode).
void rememberLast(const BrewSample* samples, int n, const char* recipe);  // in memory, works without SD
void onRunStarted();
void setReference(uint32_t id);                        // 0 clears
const std::vector<BrewSample>& ghost();             // empty if from another recipe
const char* ghostLabel();

// JSON helpers shared with the web API
std::string metaJson(const ShotMeta& m);

}  // namespace history
