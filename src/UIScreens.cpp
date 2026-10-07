// Settings, first-run setup, scale scan and recipe screens.
#include "AcaiaScale.h"
#include "History.h"
#include "Net.h"
#include "Recipes.h"
#include "Settings.h"
#include "UIKit.h"

namespace ui {

static String selectedAddr, selectedName;

void resetScanSelection() {
  selectedAddr = "";
  selectedName = "";
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------
static const uint8_t SLEEP_STEPS[] = {0, 1, 2, 5, 10, 30, 60};

static void brewingRows(int x, int y, int w) {
  const int rh = 82;
  char b[64];
  int cy = y + rh / 2;
  toggleRow(x, cy, w, "Auto tare", "Tare when a cup is placed or removed", settings.autoTare);
  divider(x, cy + rh / 2, w); cy += rh;
  toggleRow(x, cy, w, "Auto start timer", "Start when the weight starts rising", settings.autoStart);
  divider(x, cy + rh / 2, w); cy += rh;
  toggleRow(x, cy, w, "Auto stop timer", "Stop when the weight stops rising", settings.autoStop);
  divider(x, cy + rh / 2, w); cy += rh;

  snprintf(b, sizeof(b), "Beep early by the learned drip (now %.1f g)", settings.dripGrams);
  toggleRow(x, cy, w, "Drip compensation", b, settings.dripComp);
  divider(x, cy + rh / 2, w); cy += rh;

  settingRowLabel(x + 28, cy, "Ghost curve", "Earlier shot shown behind the live plot");
  static const char* const ghosts[] = {"Off", "Last", "Ref"};
  int g = segmented(x + w - 28, cy, ghosts, 3, settings.ghostMode, 92);
  if (g >= 0 && g != settings.ghostMode) {
    settings.ghostMode = g;
    settings.save();
  }
  divider(x, cy + rh / 2, w); cy += rh;

  settingRowLabel(x + 28, cy, "Screen sleep", "Turn the screen off when idle");
  if (settings.sleepMin) snprintf(b, sizeof(b), "%d min", settings.sleepMin);
  else snprintf(b, sizeof(b), "never");
  int d = stepper(x + w - 28, cy, b);
  if (d) {
    int i = 0;
    while (i < 6 && SLEEP_STEPS[i] < settings.sleepMin) i++;
    i = constrain(i + d, 0, 6);
    settings.sleepMin = SLEEP_STEPS[i];
    settings.save();
  }
}

void drawSettings() {
  topBar("Settings", Screen::Main);
  const int cy0 = 96;
  card(24, cy0, 740, 600);
  text("BREWING", 52, cy0 + 34, F_LABEL, MUTED);
  brewingRows(24, cy0 + 56, 740);

  const int rx = 784, rw = 472;
  card(rx, cy0, rw, 600);
  text("DEVICE", rx + 28, cy0 + 34, F_LABEL, MUTED);
  const int rh = 70;
  int y = cy0 + 56 + rh / 2;
  char b[64];
  settingRowLabel(rx + 28, y, "Colors", nullptr);
  static const char* const schemes[] = {"Roast", "Racer"};
  int sel = segmented(rx + rw - 28, y, schemes, 2, settings.colorScheme);
  if (sel >= 0 && sel != settings.colorScheme) {
    settings.colorScheme = sel;
    settings.save();
    theme::apply(sel);
    requestRedraw();
  }
  divider(rx, y + rh / 2, rw); y += rh;

  toggleRow(rx, y, rw, "Sounds", nullptr, settings.sound);
  divider(rx, y + rh / 2, rw); y += rh;

  settingRowLabel(rx + 28, y, "Brightness", nullptr);
  snprintf(b, sizeof(b), "%d%%", (settings.brightness * 100 + 127) / 255);
  int d = stepper(rx + rw - 28, y, b);
  if (d) {
    settings.brightness = constrain((int)settings.brightness + d * 25, 30, 255);
    settings.save();
    applyDisplaySettings();
  }
  divider(rx, y + rh / 2, rw); y += rh;

  if (toggleRow(rx, y, rw, "Flip screen", nullptr, settings.flipScreen)) applyDisplaySettings();
  divider(rx, y + rh / 2, rw); y += rh;

  // Wi-Fi row: whole row opens the Wi-Fi screen
  bool wt = hit(rx + 10, y - rh / 2 + 4, rw - 20, rh - 8);
  if (wt || flashing(rx + 10, y - rh / 2 + 4))
    canvas.fillSmoothRoundRect(rx + 10, y - rh / 2 + 4, rw - 20, rh - 8, 14, SURFACE2);
  net::Status ns = net::status();
  const char* sub = !ns.enabled ? "Off" : ns.connected ? (ns.hotspot ? "Hotspot on" : "Connected")
                                                       : "Connecting...";
  settingRowLabel(rx + 28, y, "Wi-Fi & web", sub);
  iconChevron(rx + rw - 40, y, MUTED, SURFACE);
  if (wt) setScreen(Screen::Wifi);
  divider(rx, y + rh / 2, rw); y += rh;

  // scale
  y += 4;
  text("SCALE", rx + 28, y, F_LABEL, MUTED);
  String nm = settings.scaleName.length() ? settings.scaleName : String("Any Acaia scale");
  text(nm.c_str(), rx + 120, y, F_LABEL, TEXT);
  y += 30;
  String ad = settings.scaleAddress.length() ? settings.scaleAddress : String("first one found");
  text(ad.c_str(), rx + 120, y, F_AXIS, MUTED);
  y += 22;
  const int bw = (rw - 56 - 2 * 12) / 3;
  if (button(rx + 28, y, bw, 56, "Change", Btn::Secondary, true, F_LABEL)) {
    resetScanSelection();
    scale.startDiscovery();
    setScreen(Screen::Scan);
  }
  if (button(rx + 28 + bw + 12, y, bw, 56, "Forget", Btn::Danger,
             settings.scaleAddress.length() > 0, F_LABEL)) {
    settings.forgetScale();
    scale.setTarget("");
    scale.disconnect();
  }
  if (button(rx + 28 + 2 * (bw + 12), y, bw, 56, "Setup", Btn::Ghost, true, F_LABEL)) {
    settings.configured = false;
    settings.save();
    setScreen(Screen::SetupWelcome);
  }
}

// ---------------------------------------------------------------------------
// first-run setup
// ---------------------------------------------------------------------------
void drawWelcome() {
  canvas.fillSmoothCircle(W / 2, 210, 96, SURFACE2);
  iconCup(W / 2 - 6, 230, 44, ACCENT, SURFACE2);
  text("Welcome", W / 2, 370, F_TITLE, TEXT, textdatum_t::middle_center);
  text("Your Tab5 becomes a display for your Acaia Lunar.", W / 2, 430, F_BODYL, MUTED,
       textdatum_t::middle_center);
  text("Switch on the scale and keep it close, then tap Continue.", W / 2, 474, F_BODYL, MUTED,
       textdatum_t::middle_center);
  if (button(W / 2 - 170, 550, 340, 88, "Continue", Btn::Primary)) {
    resetScanSelection();
    scale.startDiscovery();
    setScreen(Screen::SetupScan);
  }
  stepDots(0, 3);
}

void drawScan(bool setupMode) {
  if (setupMode) {
    text("Choose your scale", W / 2, 70, F_TITLE, TEXT, textdatum_t::middle_center);
  } else {
    topBar("Choose scale", Screen::Settings);
    if (currentScreen() != Screen::Scan) { scale.stopDiscovery(); return; }
  }
  text("Searching for Acaia scales...", W / 2, setupMode ? 124 : 120, F_BODY, MUTED,
       textdatum_t::middle_center);

  const int lx = 260, lw = 760, ly = 160, rh = 84;
  auto list = scale.discovered();
  card(lx, ly, lw, rh * 4 + 24);
  if (list.empty()) {
    text("No scale found yet. Make sure the scale is on", W / 2, ly + rh * 2 - 4, F_BODY,
         MUTED, textdatum_t::middle_center);
    text("and not connected to a phone app.", W / 2, ly + rh * 2 + 28, F_BODY, MUTED,
         textdatum_t::middle_center);
  }
  for (size_t i = 0; i < list.size() && i < 4; i++) {
    int ry = ly + 12 + i * rh;
    bool sel = list[i].address == selectedAddr;
    if (hit(lx + 12, ry, lw - 24, rh - 6)) {
      selectedAddr = list[i].address;
      selectedName = list[i].name;
      sel = true;
    }
    if (sel) {
      canvas.fillSmoothRoundRect(lx + 12, ry, lw - 24, rh - 6, 16, ACCENT);
      canvas.fillSmoothRoundRect(lx + 15, ry + 3, lw - 30, rh - 12, 14, SURFACE2);
    }
    text(list[i].name.c_str(), lx + 44, ry + 26, F_LABEL, TEXT);
    text(list[i].address.c_str(), lx + 44, ry + 56, F_AXIS, MUTED);
    rssiBars(lx + lw - 90, ry + 56, list[i].rssi);
  }

  const int by = 560, bh = 80;
  auto finishSelection = [&](const String& addr, const String& name) {
    settings.scaleAddress = addr;
    settings.scaleName = name;
    settings.save();
    scale.setTarget(addr);
    scale.stopDiscovery();
  };
  if (setupMode) {
    if (button(lx, by, 220, bh, "Back", Btn::Ghost)) {
      scale.stopDiscovery();
      setScreen(Screen::SetupWelcome);
    }
    if (button(lx + 236, by, 268, bh, "Use any Acaia", Btn::Secondary)) {
      finishSelection("", "");
      setScreen(Screen::SetupPrefs);
    }
    if (button(lx + lw - 240, by, 240, bh, "Next", Btn::Primary, selectedAddr.length() > 0)) {
      finishSelection(selectedAddr, selectedName);
      setScreen(Screen::SetupPrefs);
    }
    stepDots(1, 3);
  } else {
    if (button(lx, by, 300, bh, "Use any Acaia", Btn::Secondary)) {
      finishSelection("", "");
      setScreen(Screen::Settings);
    }
    if (button(lx + lw - 260, by, 260, bh, "Connect", Btn::Primary, selectedAddr.length() > 0)) {
      finishSelection(selectedAddr, selectedName);
      setScreen(Screen::Settings);
    }
  }
}

void drawSetupPrefs() {
  text("Brewing preferences", W / 2, 70, F_TITLE, TEXT, textdatum_t::middle_center);
  text("You can change these later in Settings.", W / 2, 124, F_BODY, MUTED,
       textdatum_t::middle_center);
  const int x = 260, w = 760, rh = 82;
  card(x, 160, w, rh * 4 + 20);
  int cy = 170 + rh / 2;
  toggleRow(x, cy, w, "Auto tare", "Tare when a cup is placed or removed", settings.autoTare);
  divider(x, cy + rh / 2, w); cy += rh;
  toggleRow(x, cy, w, "Auto start timer", "Start when the weight starts rising", settings.autoStart);
  divider(x, cy + rh / 2, w); cy += rh;
  toggleRow(x, cy, w, "Auto stop timer", "Stop when the weight stops rising", settings.autoStop);
  divider(x, cy + rh / 2, w); cy += rh;
  settingRowLabel(x + 28, cy, "Colors", "Roast: brown and caramel. Racer: black and red");
  static const char* const schemes[] = {"Roast", "Racer"};
  int sel = segmented(x + w - 28, cy, schemes, 2, settings.colorScheme);
  if (sel >= 0 && sel != settings.colorScheme) {
    settings.colorScheme = sel;
    settings.save();
    theme::apply(sel);
    requestRedraw();
  }
  if (button(x, 560, 220, 80, "Back", Btn::Ghost)) {
    scale.startDiscovery();
    setScreen(Screen::SetupScan);
  }
  if (button(x + w - 260, 560, 260, 80, "Finish", Btn::Primary)) {
    settings.configured = true;
    settings.save();
    setScreen(Screen::Main);
  }
  stepDots(2, 3);
}

// ---------------------------------------------------------------------------
// recipes
// ---------------------------------------------------------------------------
static void recipeSummary(const Recipe& r, char* b, size_t n) {
  if (r.target() > 0) snprintf(b, n, "%.1f g in, %.1f g out (1:%.1f)", r.dose, r.target(), r.ratio);
  else if (r.dose > 0) snprintf(b, n, "%.1f g in, no target", r.dose);
  else snprintf(b, n, "No dose or target");
}

void drawRecipes() {
  topBar("Recipes", Screen::Main);
  char b[96];
  // list
  const int lx = 24, lw = 420, rh = 116;
  for (int i = 0; i < recipes::COUNT; i++) {
    const Recipe& r = recipes::get(i);
    int y = 96 + i * (rh + 4);
    bool active = i == recipes::activeIndex();
    if (hit(lx, y, lw, rh)) {
      recipes::setActive(i);
      active = true;
    }
    canvas.fillSmoothRoundRect(lx, y, lw, rh, 20, active ? ACCENT : STROKE);
    canvas.fillSmoothRoundRect(lx + 2, y + 2, lw - 4, rh - 4, 18, active ? SURFACE2 : SURFACE);
    text(r.name, lx + 26, y + 40, F_BTN, TEXT);
    recipeSummary(r, b, sizeof(b));
    text(b, lx + 26, y + 80, F_BODY, MUTED);
    if (active) iconCheck(lx + lw - 40, y + rh / 2, ACCENT, SURFACE2);
  }

  // editor for the active recipe
  Recipe& r = recipes::active();
  int idx = recipes::activeIndex();
  const int ex = 464, ew = W - 24 - ex, ey = 96, eh = 600;
  card(ex, ey, ew, eh);
  text(r.name, ex + 28, ey + 44, F_TITLE, TEXT);
  text(r.pourOver ? "Pour-over with timed pours" : "Espresso style", ex + 28, ey + 88, F_BODY, MUTED);

  const int rowH = 84;
  int y = ey + 150;
  bool changed = false;

  settingRowLabel(ex + 28, y, "Dose", "Coffee in, or weigh it with DOSE");
  snprintf(b, sizeof(b), r.dose > 0 ? "%.1f g" : "-", r.dose);
  int d = stepper(ex + ew - 28, y, b);
  if (d) { r.dose = constrain(r.dose + d * 0.5f, 0.0f, 60.0f); changed = true; }
  divider(ex, y + rowH / 2, ew); y += rowH;

  settingRowLabel(ex + 28, y, "Ratio", "Beverage weight per gram of coffee");
  if (r.ratio > 0) snprintf(b, sizeof(b), "1:%.1f", r.ratio);
  else snprintf(b, sizeof(b), "none");
  d = stepper(ex + ew - 28, y, b);
  if (d) {
    float step = r.pourOver ? 0.5f : 0.1f;
    r.ratio = constrain(roundf((r.ratio + d * step) * 10) / 10, 0.0f, 25.0f);
    changed = true;
  }
  divider(ex, y + rowH / 2, ew); y += rowH;

  settingRowLabel(ex + 28, y, "Target", "Dose times ratio");
  if (r.target() > 0) snprintf(b, sizeof(b), "%.1f g", r.target());
  else snprintf(b, sizeof(b), "none");
  text(b, ex + ew - 28 - 54 - 60, y, F_LABEL, HIGHLIGHT, textdatum_t::middle_center);
  divider(ex, y + rowH / 2, ew); y += rowH;

  settingRowLabel(ex + 28, y, "Auto stop after", "Seconds without weight gain");
  snprintf(b, sizeof(b), "%d s", r.stopDelayS);
  d = stepper(ex + ew - 28, y, b);
  if (d) { r.stopDelayS = constrain(r.stopDelayS + d * (r.stopDelayS >= 10 ? 5 : 1), 2, 90); changed = true; }
  divider(ex, y + rowH / 2, ew); y += rowH;

  settingRowLabel(ex + 28, y, "Start threshold", "Weight change that starts the plot");
  snprintf(b, sizeof(b), "%.1f g", r.startThreshold);
  d = stepper(ex + ew - 28, y, b);
  if (d) { r.startThreshold = constrain(r.startThreshold + d * 0.1f, 0.2f, 3.0f); changed = true; }
  y += rowH / 2 + 14;

  if (r.pourOver && r.target() > 0) {
    int sx = ex + 28;
    text("POURS", sx, y + 10, F_LABEL, MUTED);
    sx += 104;
    for (int i = 0; i < r.stageCount; i++) {
      snprintf(b, sizeof(b), "%d:%02d %s to %g g", r.stages[i].atSec / 60, r.stages[i].atSec % 60,
               r.stages[i].label, r.stageTarget(i));
      text(b, sx, y + 10, F_BODY, TEXT);
      sx += textWidth(b, F_BODY) + 30;
    }
  }
  if (changed) recipes::save(idx);
}

}  // namespace ui
