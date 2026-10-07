// Main screen: weight, timer, buttons, extraction plot and the shot summary.
#include "AcaiaScale.h"
#include "Diag.h"
#include "History.h"
#include "Net.h"
#include "Recipes.h"
#include "Settings.h"
#include "UIKit.h"

namespace ui {

// Each region is redrawn and pushed only when what it shows changes.
static const Rect R_TOP     = {0, 0, 1280, 80};
static const Rect R_WEIGHT  = {24, 88, 500, 312};
static const Rect R_TIMER   = {24, 416, 500, 150};
static const Rect R_BUTTONS = {24, 582, 500, 122};
static const Rect R_PLOT    = {544, 88, 712, 616};

// shot summary (shown in the plot area after a shot)
static bool summaryOpen = false;
static bool summarySaved = false;
static ShotMeta summary;
static int lastStage = -1;

static void clearRect(const Rect& r) { fillRectFast(r.x, r.y, r.w, r.h, BG); }

static void arrowRight(int x, int cy, uint16_t c) {
  canvas.drawWideLine(x, cy, x + 16, cy, 1.4f, c);
  canvas.fillTriangle(x + 13, cy - 5, x + 13, cy + 5, x + 20, cy, c);
}

// Current pour-over stage (index into recipe stages), -1 when not applicable.
static int currentStage(const Recipe& r, float t) {
  if (!r.pourOver || !r.stageCount || r.target() <= 0) return -1;
  int k = 0;
  for (int i = 0; i < r.stageCount; i++)
    if (t >= r.stages[i].atSec) k = i;
  return k;
}

static bool stageActive() {
  return brew.state() == BrewState::Running && brew.timerRunning() &&
         currentStage(recipes::active(), brew.timerSeconds(millis())) >= 0;
}

// ---------------------------------------------------------------------------
// top bar
// ---------------------------------------------------------------------------
static void drawTop() {
  iconCup(56, 46, 18, ACCENT, BG);

  // recipe selector
  const Recipe& r = recipes::active();
  const int px = 96, ph = 50, pyy = 15;
  int pw = textWidth(r.name, F_LABEL) + 70;
  bool t = hit(px - 6, 4, pw + 12, 72);
  uint16_t bg = t || flashing(px - 6, 4) ? STROKE : SURFACE2;
  canvas.fillSmoothRoundRect(px, pyy, pw, ph, ph / 2, bg);
  text(r.name, px + 24, 40, F_LABEL, TEXT);
  int cx = px + pw - 28;
  canvas.drawWideLine(cx - 7, 36, cx, 43, 2.2f, MUTED);
  canvas.drawWideLine(cx, 43, cx + 7, 36, 2.2f, MUTED);
  if (t) setScreen(Screen::Recipes);

  if (diag::bannerActive(millis())) {
    const char* msg = diag::shortReason();
    int w = textWidth(msg, F_LABEL) + 64, h = 46, cx = W / 2 + 40;
    canvas.fillSmoothRoundRect(cx - w / 2, 40 - h / 2, w, h, h / 2, SURFACE2);
    canvas.fillSmoothCircle(cx - w / 2 + 26, 40, 8, BAD);
    text(msg, cx - w / 2 + 44, 41, F_LABEL, TEXT);
  } else {
    statusPill(W / 2 + 40, 40);
  }

  if (circleButton(W - 52, 40, 30, iconGear)) setScreen(Screen::Settings);
  if (circleButton(W - 128, 40, 30, iconHistory)) setScreen(Screen::History);
  int bx = W - 176;
  net::Status ns = net::status();
  if (ns.enabled) {
    bool w = hit(bx - 48, 8, 52, 64);
    iconWifi(bx - 22, 36, ns.connected ? GOOD : (ns.connecting ? WARN : MUTED), BG);
    if (w) setScreen(Screen::Wifi);
    bx -= 64;
  }
  int bat = scale.battery();
  if (scale.connected() && bat >= 0) {
    char b[8];
    snprintf(b, sizeof(b), "%d%%", bat);
    text(b, bx, 40, F_LABEL, MUTED, textdatum_t::middle_right);
    iconBattery(bx - textWidth(b, F_LABEL) - 66, 28, bat, MUTED);
  }
}

// ---------------------------------------------------------------------------
// weight card
// ---------------------------------------------------------------------------
static void drawWeightCard() {
  const int wx = R_WEIGHT.x, wy = R_WEIGHT.y, ww = R_WEIGHT.w, wh = R_WEIGHT.h;
  card(wx, wy, ww, wh);
  bool conn = scale.connected();
  bool dosing = brew.dosing();
  const Recipe& r = recipes::active();
  text(dosing ? "DOSING" : "WEIGHT", wx + 28, wy + 36, F_LABEL, dosing ? ACCENT : MUTED);
  char b[48];
  if (conn && !dosing) {
    float f = brew.flow();
    snprintf(b, sizeof(b), "%.1f g/s", f < 0 ? 0.0f : f);
    text(b, wx + ww - 28, wy + 36, F_LABEL, HIGHLIGHT, textdatum_t::middle_right);
  }
  if (conn && brew.stable() && brew.state() != BrewState::Running) {
    canvas.fillSmoothCircle(wx + 28 + textWidth(dosing ? "DOSING" : "WEIGHT", F_LABEL) + 18,
                            wy + 36, 6, GOOD);
  }
  float dh = 148;
  if (conn) {
    float w = brew.weight();
    if (fabsf(w) < 0.05f) w = 0;
    snprintf(b, sizeof(b), "%.1f", w);
    segText(b, wx + ww - 84, wy + 82, dh, TEXT, true);
  } else {
    segText("--.-", wx + ww - 84, wy + 82, dh, STROKE, true);
  }
  text("g", wx + ww - 70, wy + 82 + dh - 18, F_TITLE, conn ? ACCENT : STROKE);

  const int by = wy + wh - 54;   // info line; progress bar at by + 14
  const int bw = ww - 56;
  auto bar = [&](float p, float mark) {
    p = constrain(p, 0.0f, 1.0f);
    canvas.fillSmoothRoundRect(wx + 28, by + 14, bw, 14, 7, SURFACE2);
    if (p > 0.01f)
      canvas.fillSmoothRoundRect(wx + 28, by + 14, max(14, (int)(bw * p)), 14, 7,
                                 p >= 1 ? ACCENT_HI : ACCENT);
    if (mark > 0 && mark < 1) {   // where the "stop now" signal fires
      int mx = wx + 28 + (int)(bw * mark);
      canvas.fillSmoothRoundRect(mx - 2, by + 8, 4, 26, 2, TEXT);
    }
  };

  if (!conn) {
    text("Switch on your Acaia scale", wx + ww / 2, by + 6, F_BODY, MUTED, textdatum_t::middle_center);
    return;
  }
  if (dosing) {
    text("Put the beans on the scale, then tap SAVE", wx + ww / 2, by + 6, F_BODY, MUTED,
         textdatum_t::middle_center);
    return;
  }
  float t = brew.timerSeconds(millis());
  int k = stageActive() ? currentStage(r, t) : -1;
  if (k >= 0) {
    float st = r.stageTarget(k);
    snprintf(b, sizeof(b), "%s to %g g", r.stages[k].label, st);
    text(b, wx + 28, by - 8, F_LABEL, TEXT);
    if (k + 1 < r.stageCount) {
      int left = (int)ceilf(r.stages[k + 1].atSec - t);
      snprintf(b, sizeof(b), "next pour in %d:%02d", left / 60, left % 60);
    } else {
      snprintf(b, sizeof(b), "final pour");
    }
    text(b, wx + ww - 28, by - 8, F_BODY, MUTED, textdatum_t::middle_right);
    bar(brew.weight() / max(1.0f, st), 0);
    return;
  }
  float target = r.target();
  if (r.dose > 0) {
    snprintf(b, sizeof(b), "%.1f g", r.dose);
    text(b, wx + 28, by - 8, F_BODY, MUTED);
    int ax = wx + 28 + textWidth(b, F_BODY) + 10;
    arrowRight(ax, by - 8, MUTED);
    if (target > 0) snprintf(b, sizeof(b), "%.1f g", target);
    else snprintf(b, sizeof(b), "no target");
    text(b, ax + 30, by - 8, F_BODY, MUTED);
    float w = max(0.0f, brew.weight());
    snprintf(b, sizeof(b), "1:%.1f", w / r.dose);
    text(b, wx + ww - 28, by - 8, F_LABEL, w > 0.5f ? HIGHLIGHT : MUTED, textdatum_t::middle_right);
  } else if (target <= 0) {
    text("Free brew, no target", wx + 28, by - 8, F_BODY, MUTED);
  }
  if (target > 0) {
    float sig = brew.signalWeight();
    bar(brew.weight() / target, settings.dripComp ? sig / target : 0);
  }
}

// ---------------------------------------------------------------------------
// timer card
// ---------------------------------------------------------------------------
static const char* brewStateLabel() {
  if (brew.dosing()) return "DOSING";
  switch (brew.state()) {
    case BrewState::Running:     return brew.timerRunning() ? "BREWING" : "RECORDING";
    case BrewState::Finished:    return "DONE";
    case BrewState::TarePending: return "TARING";
    default:                     return "READY";
  }
}

static void drawTimerCard() {
  const int tx = R_TIMER.x, ty = R_TIMER.y, tw = R_TIMER.w, th = R_TIMER.h;
  card(tx, ty, tw, th);
  text("SHOT TIMER", tx + 28, ty + 36, F_LABEL, MUTED);
  uint16_t stFg = ON_ACCENT, stBg = ACCENT;
  switch (brew.state()) {
    case BrewState::Finished:    stBg = ACCENT_HI; break;
    case BrewState::TarePending: stFg = TEXT; stBg = SURFACE2; break;
    case BrewState::Armed:       stFg = MUTED; stBg = SURFACE2; break;
    default: break;
  }
  if (brew.dosing()) { stFg = TEXT; stBg = SURFACE2; }
  chip(tx + tw - 24, ty + 36, brewStateLabel(), stFg, stBg, textdatum_t::middle_right);
  char b[24];
  float secs = brew.timerSeconds(millis());
  formatTimer(b, sizeof(b), secs);
  segText(b, tx + 28, ty + 64, 66, brew.timerRunning() ? ACCENT_HI : TEXT, false);
  if (brew.state() == BrewState::Finished && brew.weight() > 0.5f && secs > 0.5f) {
    snprintf(b, sizeof(b), "%.1f g/s avg", brew.weight() / secs);
    text(b, tx + tw - 28, ty + 108, F_BODY, MUTED, textdatum_t::middle_right);
  } else if (brew.state() == BrewState::Running && brew.firstDropSeconds() > 0.5f) {
    snprintf(b, sizeof(b), "first drop %.1f s", brew.firstDropSeconds());
    text(b, tx + tw - 28, ty + 108, F_BODY, MUTED, textdatum_t::middle_right);
  }
}

// ---------------------------------------------------------------------------
// buttons
// ---------------------------------------------------------------------------
static constexpr int BTN_GAP = 12;
static constexpr int BTN_W = (500 - 3 * BTN_GAP) / 4;

static bool iconButton(int x, int y, int w, int h, const char* label,
                       void (*icon)(int, int, uint16_t, uint16_t), Btn style, bool enabled) {
  bool clicked = enabled && hit(x, y, w, h);
  bool pressed = flashing(x, y) || clicked;
  uint16_t bg, fg, border;
  if (style == Btn::Primary) { bg = pressed ? ACCENT_HI : ACCENT; fg = ON_ACCENT; border = bg; }
  else { bg = pressed ? STROKE : SURFACE2; fg = TEXT; border = STROKE; }
  if (!enabled) { bg = SURFACE; fg = STROKE; border = SURFACE2; }
  canvas.fillSmoothRoundRect(x, y, w, h, 22, border);
  canvas.fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, 20, bg);
  icon(x + w / 2, y + h / 2 - 16, fg, bg);
  text(label, x + w / 2, y + h - 30, F_LABEL, fg, textdatum_t::middle_center);
  return clicked;
}

static void drawButtons() {
  const int bx = R_BUTTONS.x, by = R_BUTTONS.y, bh = R_BUTTONS.h;
  bool conn = scale.connected();
  bool dosing = brew.dosing();
  if (iconButton(bx, by, BTN_W, bh, "TARE", iconTare, Btn::Secondary, conn)) {
    brew.manualTare();
    beep(1800, 40);
  }
  int x = bx + BTN_W + BTN_GAP;
  if (iconButton(x, by, BTN_W, bh, dosing ? "SAVE" : "DOSE", dosing ? iconCheck : iconBean,
                 dosing ? Btn::Primary : Btn::Secondary, conn)) {
    if (!dosing) {
      brew.setDosing(true);
      summaryOpen = false;
    } else {
      float w = brew.weight();
      if (w >= 1.0f) {
        Recipe& r = recipes::active();
        r.dose = roundf(w * 10) / 10;
        recipes::save(recipes::activeIndex());
        beep(2400, 60);
      }
      brew.setDosing(false);
    }
  }
  x += BTN_W + BTN_GAP;
  bool running = brew.timerRunning();
  if (iconButton(x, by, BTN_W, bh, running ? "STOP" : "START", running ? iconStop : iconPlay,
                 Btn::Primary, !dosing)) {
    brew.toggleTimer();
    beep(running ? 1400 : 2200, 50);
  }
  x += BTN_W + BTN_GAP;
  if (iconButton(x, by, BTN_W, bh, "RESET", iconReset, Btn::Secondary, true)) {
    brew.setDosing(false);
    brew.reset();
    summaryOpen = false;
    beep(1200, 40);
  }
}

// ---------------------------------------------------------------------------
// plot card
// ---------------------------------------------------------------------------
static void drawPlotCard() {
  const int x = R_PLOT.x, y = R_PLOT.y, w = R_PLOT.w, h = R_PLOT.h;
  card(x, y, w, h);
  BrewState st = brew.state();
  bool dim = st == BrewState::Armed || st == BrewState::TarePending;
  int n = brew.sampleCount();

  text("EXTRACTION", x + 28, y + 36, F_LABEL, MUTED);
  int cx = x + w - 24;
  const char* at = "Auto timer";
  int aw = textWidth(at, F_LABEL) + 32;
  if (hit(cx - aw, y + 12, aw, 48)) {
    settings.autoStart = settings.autoStop = !(settings.autoStart || settings.autoStop);
    settings.save();
  }
  toggleChip(cx, y + 36, at, settings.autoStart || settings.autoStop);
  cx -= aw + 12;
  const char* tt = "Auto tare";
  int tw = textWidth(tt, F_LABEL) + 32;
  if (hit(cx - tw, y + 12, tw, 48)) {
    settings.autoTare = !settings.autoTare;
    settings.save();
  }
  toggleChip(cx, y + 36, tt, settings.autoTare);

  const int px = x + 72, py = y + 104, pw = w - 72 - 64, ph = h - 104 - 56;
  const std::vector<BrewSample>& ghost = history::ghost();

  // legend
  int lx = px + 8, ly = py - 30;
  canvas.fillSmoothRoundRect(lx, ly - 3, 22, 6, 3, ACCENT);
  text("Weight", lx + 30, ly, F_BODY, TEXT);
  lx += 30 + textWidth("Weight", F_BODY) + 26;
  canvas.fillSmoothRoundRect(lx, ly - 2, 22, 4, 2, FLOW);
  text("Flow", lx + 30, ly, F_BODY, TEXT);
  if (ghost.size() > 1) {
    lx += 30 + textWidth("Flow", F_BODY) + 26;
    canvas.fillSmoothRoundRect(lx, ly - 1, 22, 3, 1, MUTED);
    text(history::ghostLabel(), lx + 30, ly, F_BODY, MUTED);
  }

  const Recipe& r = recipes::active();
  PlotOpts o;
  o.dim = dim;
  o.liveMarker = st == BrewState::Running;
  o.target = r.target();
  o.ghost = &ghost;
  o.recipe = &r;
  drawPlotArea(px, py, pw, ph, brew.samples(), n, brew.maxWeight(), brew.maxFlow(), o);

  if (n < 2 && st != BrewState::Running && ghost.size() < 2) {
    text("Plot starts automatically when the weight changes", px + pw / 2, py + ph / 2, F_BODY,
         MUTED, textdatum_t::middle_center);
  }
  if (dim && n >= 2) text("LAST SHOT", px + 16, py + 20, F_LABEL, MUTED);
}

// ---------------------------------------------------------------------------
// shot summary
// ---------------------------------------------------------------------------
static void statBlock(int x, int y, const char* label, const char* value, const char* unit) {
  text(label, x, y, F_LABEL, MUTED);
  segText(value, x, y + 26, 58, TEXT, false);
  if (unit) text(unit, x + (int)segTextWidth(value, 58) + 10, y + 70, F_LABEL, ACCENT);
}

static void smallStat(int x, int y, const char* label, const char* value) {
  text(label, x, y, F_AXIS, MUTED);
  text(value, x, y + 30, F_BTN, TEXT);
}

static void drawSummary() {
  const int x = R_PLOT.x, y = R_PLOT.y, w = R_PLOT.w, h = R_PLOT.h;
  card(x, y, w, h);
  char b[48], d[40];
  text("SHOT COMPLETE", x + 28, y + 36, F_LABEL, ACCENT);
  formatDate(d, sizeof(d), summary.epoch, summary.id);
  snprintf(b, sizeof(b), "%s  |  %s", summary.recipe.c_str(), summarySaved ? d : "not saved");
  text(b, x + w - 28, y + 36, F_BODY, MUTED, textdatum_t::middle_right);

  // headline figures
  formatTimer(b, sizeof(b), summary.time);
  statBlock(x + 28, y + 86, "TIME", b, nullptr);
  snprintf(b, sizeof(b), "%.1f", summary.yield);
  statBlock(x + 268, y + 86, "YIELD", b, "g");
  if (summary.dose > 0) {
    text("RATIO", x + 500, y + 86, F_LABEL, MUTED);
    text("1:", x + 500, y + 142, F_TITLE, TEXT);
    snprintf(b, sizeof(b), "%.1f", summary.ratio());
    segText(b, x + 500 + textWidth("1:", F_TITLE) + 8, y + 112, 58, TEXT, false);
  }

  // secondary figures
  int sy = y + 214;
  snprintf(b, sizeof(b), summary.dose > 0 ? "%.1f g" : "-", summary.dose);
  smallStat(x + 28, sy, "DOSE", b);
  snprintf(b, sizeof(b), "%.1f g/s", summary.avgFlow());
  smallStat(x + 196, sy, "AVG FLOW", b);
  snprintf(b, sizeof(b), "%.1f g/s", summary.peakFlow);
  smallStat(x + 364, sy, "PEAK FLOW", b);
  if (summary.firstDrop >= 0) snprintf(b, sizeof(b), "%.1f s", summary.firstDrop);
  else snprintf(b, sizeof(b), "-");
  smallStat(x + 532, sy, "FIRST DROP", b);
  canvas.drawFastHLine(x + 28, y + 290, w - 56, GRID);

  const int by = y + h - 92;
  if (!summarySaved) {
    text("Insert an SD card to keep shots,", x + w / 2, y + 380, F_BODYL, MUTED, textdatum_t::middle_center);
    text("ratings and notes.", x + w / 2, y + 420, F_BODYL, MUTED, textdatum_t::middle_center);
    if (button(x + w - 228, by, 200, 68, "Done", Btn::Primary)) summaryOpen = false;
    return;
  }

  // rating
  int ry = y + 340;
  text("Rating", x + 28, ry, F_LABEL, TEXT);
  int nr = starsInput(x + 200, ry, 48, summary.rating);
  if (nr >= 0) { summary.rating = nr; history::update(summary); }

  // grind
  int gy = y + 420;
  settingRowLabel(x + 28, gy, "Grind", "Setting on your grinder");
  if (summary.grind >= 0) snprintf(b, sizeof(b), "%.1f", summary.grind);
  else snprintf(b, sizeof(b), "-");
  int dd = stepper(x + 470, gy, b);
  if (dd) {
    if (summary.grind < 0) summary.grind = settings.lastGrind >= 0 ? settings.lastGrind : 10.0f;
    else summary.grind = constrain(summary.grind + dd * 0.5f, 0.0f, 99.5f);
    settings.lastGrind = summary.grind;
    settings.save();
    history::update(summary);
  }
  // notes
  if (button(x + 494, gy - 30, w - 522, 60, summary.notes.empty() ? "Add note" : "Edit note",
             Btn::Secondary, true, F_LABEL)) {
    openKeyboard("Notes for this shot", summary.notes, 80, false, [](const std::string& s) {
      summary.notes = s;
      history::update(summary);
    }, Screen::Main);
  }
  if (!summary.notes.empty()) {
    snprintf(b, sizeof(b), "\"%s\"", summary.notes.c_str());
    textFit(b, x + 28, gy + 54, w - 56, F_BODY, MUTED);
  }

  bool isRef = settings.refShotId == summary.id;
  if (button(x + 28, by, 300, 68, isRef ? "Reference shot" : "Use as reference",
             isRef ? Btn::Ghost : Btn::Secondary, true, F_LABEL)) {
    history::setReference(isRef ? 0 : summary.id);
  }
  if (isRef) iconCheck(x + 52, by + 34, ACCENT, SURFACE);
  if (button(x + w - 228, by, 200, 68, "Done", Btn::Primary)) summaryOpen = false;
}

static void drawPlotRegion() {
  if (summaryOpen) drawSummary();
  else drawPlotCard();
}

// ---------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------
static void onShotFinished() {
  beep(2000, 80);
  int n = brew.sampleCount();
  history::rememberLast(brew.samples(), n, recipes::active().name);
  if (brew.finalWeight() < 2.0f || n < 2) return;   // nothing worth keeping

  const Recipe& r = recipes::active();
  summary = ShotMeta();
  summary.time = brew.lastShotSeconds() > 0 ? brew.lastShotSeconds() : brew.sample(n - 1).t;
  summary.yield = brew.finalWeight();
  summary.dose = r.dose;
  summary.peakFlow = brew.maxFlow();
  summary.firstDrop = brew.firstDropSeconds();
  summary.grind = settings.lastGrind;
  summary.recipe = r.name;
  summarySaved = history::save(summary, brew.samples(), n) != 0;
  summaryOpen = true;
}

void mainEvents() {
  if (brew.takeRunStarted()) {
    history::onRunStarted();
    summaryOpen = false;
    lastStage = -1;
  }
  if (brew.takeShotFinished()) onShotFinished();
  if (brew.takeTargetReached()) beep(2600, 140);

  // pour-over: beep when the next pour is due
  if (stageActive()) {
    int k = currentStage(recipes::active(), brew.timerSeconds(millis()));
    if (k > lastStage) {
      if (lastStage >= 0) beep(2300, 180);
      lastStage = k;
    }
  }
}

// ---------------------------------------------------------------------------
// region bookkeeping
// ---------------------------------------------------------------------------
static uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

static uint32_t sigTop() {
  char b[128];
  net::Status ns = net::status();
  bool pulse = scale.state() != ScaleState::Connected && (millis() / 600) % 2;
  snprintf(b, sizeof(b), "%d|%d|%d|%d|%d|%d|%d|%d|%s", (int)scale.state(), scale.battery(), pulse,
           recipes::activeIndex(), ns.enabled, ns.connected, flashActive(millis()),
           diag::bannerActive(millis()), scale.connectedName().c_str());
  return fnv(b);
}

static uint32_t sigWeight() {
  char b[160];
  const Recipe& r = recipes::active();
  float w = brew.weight();
  if (fabsf(w) < 0.05f) w = 0;
  float t = brew.timerSeconds(millis());
  int k = stageActive() ? currentStage(r, t) : -1;
  int left = (k >= 0 && k + 1 < r.stageCount) ? (int)ceilf(r.stages[k + 1].atSec - t) : -1;
  snprintf(b, sizeof(b), "%d|%.1f|%.1f|%d|%d|%d|%.1f|%.1f|%.1f|%d|%d|%d", scale.connected(), w,
           max(0.0f, brew.flow()), brew.stable() && brew.state() != BrewState::Running,
           brew.dosing(), recipes::activeIndex(), r.dose, r.ratio, brew.signalWeight(), k, left,
           settings.dripComp);
  return fnv(b);
}

static uint32_t sigTimer() {
  char b[80], t[24];
  formatTimer(t, sizeof(t), brew.timerSeconds(millis()));
  snprintf(b, sizeof(b), "%s|%s|%d|%.1f|%.1f", t, brewStateLabel(), brew.timerRunning(),
           brew.weight(), brew.firstDropSeconds());
  return fnv(b);
}

static uint32_t sigButtons() {
  char b[48];
  snprintf(b, sizeof(b), "%d|%d|%d|%d", scale.connected(), brew.timerRunning(), brew.dosing(),
           flashActive(millis()));
  return fnv(b);
}

static uint32_t sigPlot() {
  char b[200];
  int n = brew.sampleCount();
  snprintf(b, sizeof(b), "%d|%.2f|%d|%d|%d|%.1f|%lu|%d|%lu|%d|%.1f|%d|%u|%lu|%d|%d", n,
           n ? brew.sample(n - 1).t : 0.0f, (int)brew.state(), settings.autoTare,
           settings.autoStart || settings.autoStop, recipes::active().target(),
           (unsigned long)history::version(), summaryOpen, (unsigned long)summary.id,
           summary.rating, summary.grind, (int)summary.notes.size(), (unsigned)fnv(summary.notes.c_str()),
           (unsigned long)settings.refShotId, flashActive(millis()), settings.ghostMode);
  return fnv(b);
}

struct Region {
  const Rect* r;
  uint32_t (*sig)();
  void (*draw)();
  uint32_t last;
};

static Region regions[] = {
    {&R_TOP, sigTop, drawTop, 0},
    {&R_WEIGHT, sigWeight, drawWeightCard, 0},
    {&R_TIMER, sigTimer, drawTimerCard, 0},
    {&R_BUTTONS, sigButtons, drawButtons, 0},
    {&R_PLOT, sigPlot, drawPlotRegion, 0},
};

void drawMain() {
  for (auto& rg : regions) {
    rg.last = rg.sig();
    rg.draw();
  }
}

bool mainPartialUpdate(uint32_t& drawUs, uint32_t& pushUs) {
  bool any = false;
  uint32_t t0 = micros();
  for (auto& rg : regions) {
    uint32_t s = rg.sig();
    if (s == rg.last) continue;
    rg.last = s;
    clearRect(*rg.r);
    rg.draw();
    uint32_t t1 = micros();
    drawUs += t1 - t0;
    pushRect(*rg.r);
    t0 = micros();
    pushUs += t0 - t1;
    any = true;
  }
  return any;
}

}  // namespace ui
