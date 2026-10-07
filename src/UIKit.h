#pragma once
// Internal UI toolkit shared by the screen files (UI*.cpp). Immediate-mode:
// widgets draw themselves and return true when the pending tap hit them.
#include <M5Unified.h>
#include <functional>
#include <string>
#include <vector>
#include "Brew.h"
#include "Theme.h"
#include "UI.h"

struct Recipe;

namespace ui {
using namespace theme;

extern M5Canvas canvas;
extern int W, H;
extern const lgfx::IFont* F_AXIS;   // 9pt
extern const lgfx::IFont* F_LABEL;  // 12pt bold
extern const lgfx::IFont* F_BODY;   // 12pt
extern const lgfx::IFont* F_BODYL;  // 18pt
extern const lgfx::IFont* F_BTN;    // 18pt bold
extern const lgfx::IFont* F_TITLE;  // 24pt bold

struct Rect { int x, y, w, h; };

void requestRedraw();          // full repaint on the next frame
// tap state: set by the frame loop, consumed by widgets via hit()
void setTap(int x, int y);
bool hasTap();
bool tapPosition(int& x, int& y);
void clearTap();
bool flashActive(uint32_t now);
bool flashShown();                 // the last tap's pressed state was drawn by a widget
bool flashPoint(int& x, int& y);   // top-left of the last tapped widget
bool takeHit();                    // a widget handled a tap since the last call
Screen currentScreen();

// --- text ---
void text(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c,
          textdatum_t d = textdatum_t::middle_left);
int  textWidth(const char* s, const lgfx::IFont* f);
// Left-aligned text cut to maxW with an ellipsis.
void textFit(const char* s, int x, int y, int maxW, const lgfx::IFont* f, uint16_t c);

// --- widgets ---
enum class Btn { Primary, Secondary, Ghost, Danger };
void card(int x, int y, int w, int h, uint16_t bg = SURFACE);
void fillRectFast(int x, int y, int w, int h, uint16_t c);   // DMA fill for large areas
void setFastFill(bool on);
bool hit(int x, int y, int w, int h);
bool flashing(int x, int y);
bool button(int x, int y, int w, int h, const char* label, Btn style = Btn::Secondary,
            bool enabled = true, const lgfx::IFont* font = nullptr);
bool circleButton(int cx, int cy, int r, void (*icon)(int, int, uint16_t, uint16_t));
bool toggle(int x, int y, bool& value);
int  segmented(int xr, int cy, const char* const* labels, int n, int selected, int segW = 112);
void chip(int x, int y, const char* label, uint16_t fg, uint16_t bg, textdatum_t align);
void toggleChip(int xr, int y, const char* label, bool on);
// -1 / 0 / +1, or STEP_EDIT when `editable` and the value itself was tapped
constexpr int STEP_EDIT = 2;
int  stepper(int xr, int cy, const char* value, bool editable = false);
void settingRowLabel(int x, int y, const char* label, const char* sub);
bool toggleRow(int x, int y, int w, const char* label, const char* sub, bool& v);
void divider(int x, int y, int w);
void rssiBars(int x, int y, int rssi);
void drawStars(int x, int cy, int size, int rating);
int  starsInput(int x, int cy, int size, int rating);        // new rating or -1
void stepDots(int step, int total);
void statusPill(int cx, int cy);
void topBar(const char* title, Screen backTo);               // screen with a back button

// --- icons (cx, cy, colour, background) ---
void iconCup(int cx, int cy, int s, uint16_t c, uint16_t bg);
void iconGear(int cx, int cy, uint16_t c, uint16_t bg);
void iconHistory(int cx, int cy, uint16_t c, uint16_t bg);
void iconWifi(int cx, int cy, uint16_t c, uint16_t bg);
void iconBack(int cx, int cy, uint16_t c, uint16_t bg);
void iconPlay(int cx, int cy, uint16_t c, uint16_t bg);
void iconStop(int cx, int cy, uint16_t c, uint16_t bg);
void iconReset(int cx, int cy, uint16_t c, uint16_t bg);
void iconTare(int cx, int cy, uint16_t c, uint16_t bg);
void iconBean(int cx, int cy, uint16_t c, uint16_t bg);
void iconCheck(int cx, int cy, uint16_t c, uint16_t bg);
void iconLock(int cx, int cy, uint16_t c, uint16_t bg);
void iconChevron(int cx, int cy, uint16_t c, uint16_t bg);
void iconBattery(int x, int y, int pct, uint16_t c);
void iconStar(int cx, int cy, int r, bool filled, uint16_t c);

// --- segment digits ---
float segTextWidth(const char* s, float h);
void  segText(const char* s, float x, float y, float h, uint16_t col, bool alignRight);

// --- plot ---
struct PlotOpts {
  bool        dim = false;          // previous shot (greyed)
  bool        liveMarker = false;   // dot at the newest sample
  float       target = 0;           // dashed target line
  const std::vector<BrewSample>* ghost = nullptr;
  const Recipe* recipe = nullptr;   // pour-over stage lines
};
void drawPlotArea(int px, int py, int pw, int ph, const BrewSample* s, int n,
                  float maxW, float maxF, const PlotOpts& o);

// --- misc ---
void beep(int freq, int ms);
void formatTimer(char* b, size_t n, float secs);
void formatDate(char* b, size_t n, uint32_t epoch, uint32_t id);
uint16_t lerp565(uint16_t a, uint16_t b, float t);

// --- screens (implemented in UI*.cpp) ---
void drawMain();
bool mainPartialUpdate(uint32_t& drawUs, uint32_t& pushUs);
void mainEvents();             // brew events: save shot, open summary, stage cues
void drawSettings();
void drawWelcome();
void drawScan(bool setupMode);
void drawSetupPrefs();
void drawRecipes();
void drawHistory();
// Redraws only the History panels a tap affects. False = needs a full repaint.
bool historyPartialUpdate(bool flashEnd, uint32_t& drawUs, uint32_t& pushUs);
void drawWifi();
void drawNetworks();
void drawKeyboard();
void drawSystem();
void drawKeypad();

// Numeric keypad (full screen). One or two fields; `apply` gets the values
// (already range-checked) and returns an error message, or "" to close.
struct NumField {
  const char* label;     // shown when there are two fields, e.g. "From"
  float       value;
  float       min, max;
  int         decimals;
  const char* unit;      // "g", "s" or ""
  const char* prefix;    // e.g. "1:" for ratios, or ""
};
void openKeypad(const char* title, const char* hint, std::vector<NumField> fields,
                std::function<std::string(const std::vector<float>&)> apply, Screen returnTo);
// selectAll: the initial text starts selected, so typing replaces it (renaming).
void openKeyboard(const char* title, const std::string& initial, size_t maxLen, bool secret,
                  std::function<void(const std::string&)> done, Screen returnTo,
                  bool selectAll = false);
void resetScanSelection();
void pushRect(const Rect& r);

}  // namespace ui
