// Settings, first-run setup, scale scan and recipe screens.
#include "AcaiaScale.h"
#include "Backup.h"
#include "Battery.h"
#include "Ota.h"
#include "Blit.h"
#include "Diag.h"
#include "History.h"
#include "Net.h"
#include "Recipes.h"
#include "Settings.h"
#include "Storage.h"
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
  const int rh = 76;
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

  settingRowLabel(x + 28, cy, "Grinder", "Which number grinds finer");
  static const char* const dirs[] = {"Lower", "Higher"};
  int gd = segmented(x + w - 28, cy, dirs, 2, settings.finerIsLower ? 0 : 1, 112);
  if (gd >= 0 && (gd == 0) != settings.finerIsLower) {
    settings.finerIsLower = gd == 0;
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
  if (button(rx + 28 + 2 * (bw + 12), y, bw, 56, "System", Btn::Ghost, true, F_LABEL))
    setScreen(Screen::System);
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
  uint32_t created = 0;
  bool haveBackup = backup::sdBackupInfo(created);
  if (haveBackup) {
    char b[96], d[40];
    formatDate(d, sizeof(d), created, 0);
    snprintf(b, sizeof(b), "A settings backup was found on the SD card%s%s.", created ? " from " : "",
             created ? d : "");
    text(b, W / 2, 520, F_BODY, ACCENT_HI, textdatum_t::middle_center);
    if (button(W / 2 - 360, 560, 340, 88, "Restore backup", Btn::Secondary)) {
      if (backup::restoreFromSd()) backup::restartSoon();
    }
  }
  if (backup::restartPending()) {
    text("Restored. Restarting...", W / 2, 680, F_LABEL, GOOD, textdatum_t::middle_center);
  }
  if (button(haveBackup ? W / 2 + 20 : W / 2 - 170, haveBackup ? 560 : 550, 340, 88, "Continue",
             Btn::Primary, !backup::restartPending())) {
    resetScanSelection();
    scale.startDiscovery();
    setScreen(Screen::SetupScan);
  }
  stepDots(0, 3);
}

// ---------------------------------------------------------------------------
// firmware update in progress
// ---------------------------------------------------------------------------
void drawUpdating() {
  bool done = ota::succeeded();
  canvas.fillSmoothCircle(W / 2, 220, 80, SURFACE2);
  iconCup(W / 2 - 6, 238, 36, ACCENT, SURFACE2);
  text(done ? "Update installed" : "Updating firmware", W / 2, 360, F_TITLE, TEXT, textdatum_t::middle_center);
  text(done ? "Restarting with the new firmware..." : "Keep the Tab5 powered and the page open.", W / 2, 410,
       F_BODYL, MUTED, textdatum_t::middle_center);
  int p = ota::progress();
  const int bw = 640, bx = (W - bw) / 2, by = 470;
  canvas.fillSmoothRoundRect(bx, by, bw, 20, 10, SURFACE2);
  if (p > 0) canvas.fillSmoothRoundRect(bx, by, max(20, bw * p / 100), 20, 10, done ? GOOD : ACCENT);
  char b[16];
  snprintf(b, sizeof(b), "%d %%", p);
  text(b, W / 2, by + 56, F_BTN, TEXT, textdatum_t::middle_center);
  if (!done)   // the panel can't read the frame buffer while flash is written
    text("The screen may flicker while the update is written. That's normal.", W / 2, 640, F_BODY,
         MUTED, textdatum_t::middle_center);
}

// ---------------------------------------------------------------------------
// system: backup / restore, about
// ---------------------------------------------------------------------------
void drawSystem() {
  topBar("System", Screen::Settings);
  char b[128], d[40];
  static uint32_t savedNoteMs = 0;
  static bool restoreArmed = false;
  static std::string message;
  static bool messageBad = false;

  // --- backup ---
  const int lx = 24, lw = 600, top = 96;
  card(lx, top, lw, 600);
  text("BACKUP", lx + 28, top + 34, F_LABEL, MUTED);
  bool sd = history::available();
  uint32_t created = 0;
  bool have = sd && backup::sdBackupInfo(created);
  int y = top + 84;
  if (!sd) snprintf(b, sizeof(b), "No SD card inserted");
  else if (!have) snprintf(b, sizeof(b), "No backup on the SD card yet");
  else if (!created) snprintf(b, sizeof(b), "Backup on the SD card");
  else { formatDate(d, sizeof(d), created, 0); snprintf(b, sizeof(b), "Last backup: %s", d); }
  canvas.fillSmoothCircle(lx + 36, y, 7, have ? GOOD : sd ? WARN : STROKE);
  text(b, lx + 54, y, F_LABEL, TEXT);
  text("Settings and recipes. The Wi-Fi password is not included.", lx + 28, y + 34, F_AXIS, MUTED);
  y += 82;
  toggleRow(lx, y, lw, "Automatic backup", "To the SD card whenever something changes",
            settings.autoBackup);
  divider(lx, y + 41, lw);
  y += 76;

  const int bw = (lw - 56 - 16) / 2;
  if (button(lx + 28, y, bw, 70, "Back up now", Btn::Primary, sd, F_LABEL)) {
    restoreArmed = false;
    if (backup::saveToSd()) { message = "Saved to the SD card."; messageBad = false; }
    else { message = "Couldn't write to the SD card."; messageBad = true; }
    savedNoteMs = millis();
  }
  if (button(lx + 28 + bw + 16, y, bw, 70, restoreArmed ? "Confirm restore" : "Restore", Btn::Danger,
             have && !backup::restartPending(), F_LABEL)) {
    if (!restoreArmed) {
      restoreArmed = true;
      message = "Replaces settings and recipes, then restarts.";
      messageBad = false;
      savedNoteMs = millis();
    } else {
      restoreArmed = false;
      std::string err;
      if (backup::restoreFromSd(&err)) { message = "Restored. Restarting..."; messageBad = false; backup::restartSoon(); }
      else { message = err; messageBad = true; }
      savedNoteMs = millis();
    }
  }
  if (!message.empty() && (backup::restartPending() || restoreArmed || millis() - savedNoteMs < 4000))
    text(message.c_str(), lx + 28, y + 104, F_BODY, messageBad ? BAD : (restoreArmed ? WARN : GOOD));
  text("The web page can download a backup file and restore one", lx + 28, top + 540, F_AXIS, MUTED);
  text("(Settings > Wi-Fi & web).", lx + 28, top + 564, F_AXIS, MUTED);

  // --- about ---
  const int rx = 644, rw = W - 24 - rx;
  card(rx, top, rw, 600);
  text("ABOUT", rx + 28, top + 34, F_LABEL, MUTED);
  struct { const char* k; std::string v; } rows[5];
  rows[0] = {"Firmware", std::string(diag::firmwareId()) + (ota::rolledBack() ? " (update undone)" : "")};
  rows[1] = {"Display", blit::active() ? "Hardware rotation (PPA)" : "Software rotation"};
  if (battery::present())
    snprintf(b, sizeof(b), "%d %%, %.2f V%s", battery::level(), battery::voltageMv() / 1000.0f,
             battery::charging() ? ", charging" : "");
  else
    snprintf(b, sizeof(b), "no battery (USB power)");
  rows[2] = {"Tab5 battery", b};
  char shots[24];
  snprintf(shots, sizeof(shots), "%u", (unsigned)history::count());
  rows[3] = {"Shots saved", sd ? shots : "no SD card"};
  char freeMb[24];
  snprintf(freeMb, sizeof(freeMb), "%llu MB", (unsigned long long)(storage::freeBytes() >> 20));
  rows[4] = {"SD card free", sd ? freeMb : "-"};
  for (int i = 0; i < 5; i++) {
    int ry = top + 84 + i * 54;
    text(rows[i].k, rx + 28, ry, F_LABEL, MUTED);
    textFit(rows[i].v.c_str(), rx + 210, ry, rw - 238, F_LABEL, TEXT);
    divider(rx, ry + 27, rw);
  }

  // firmware update over Wi-Fi: uploads only while allowed here
  int uy = top + 84 + 5 * 54 + 8;
  text("Firmware update over Wi-Fi", rx + 28, uy, F_LABEL, TEXT);
  if (ota::allowed()) {
    uint32_t s = ota::allowedSecondsLeft();
    snprintf(b, sizeof(b), "Allowed for %lu:%02lu. Upload firmware.bin on the web page.",
             (unsigned long)(s / 60), (unsigned long)(s % 60));
    text(b, rx + 28, uy + 28, F_AXIS, GOOD);
  } else {
    text("Uploads from the web page need your OK here first.", rx + 28, uy + 28, F_AXIS, MUTED);
  }
  if (button(rx + 28, uy + 50, rw - 56, 60, ota::allowed() ? "Stop allowing updates" : "Allow web update (10 min)",
             ota::allowed() ? Btn::Ghost : Btn::Secondary, true, F_LABEL)) {
    if (ota::allowed()) ota::disallow(); else ota::allow(10);
  }
  if (button(rx + 28, top + 600 - 84, rw - 56, 60, "Run first-time setup", Btn::Ghost, true, F_LABEL)) {
    settings.configured = false;
    settings.save();
    setScreen(Screen::SetupWelcome);
  }
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

  // name + rename / reset
  static int resetArmed = -1;
  const int bw = 140, bh = 52, by0 = ey + 18;
  textFit(r.name, ex + 28, ey + 44, ew - 56 - 2 * bw - 24, F_TITLE, TEXT);
  if (button(ex + ew - 28 - bw, by0, bw, bh, resetArmed == idx ? "Confirm" : "Reset",
             Btn::Danger, true, F_LABEL)) {
    if (resetArmed == idx) {
      recipes::resetToDefault(idx);
      resetArmed = -1;
    } else {
      resetArmed = idx;
    }
  }
  if (button(ex + ew - 28 - 2 * bw - 12, by0, bw, bh, "Rename", Btn::Secondary, true, F_LABEL)) {
    resetArmed = -1;
    std::string title = std::string("Name for ") + r.name;
    openKeyboard(title.c_str(), r.name, recipes::MAX_NAME_LEN, false, [idx](const std::string& n) {
      recipes::rename(idx, n.c_str());
    }, Screen::Recipes, true);
  }
  if (resetArmed == idx) {
    char rb[64];
    snprintf(rb, sizeof(rb), "Restores \"%s\"", recipes::defaultName(idx));
    text(rb, ex + ew - 28, by0 + bh + 16, F_AXIS, BAD, textdatum_t::middle_right);
  }

  // style: espresso (no stages) or pour-over with timed pours
  settingRowLabel(ex + 28, ey + 100, "Style", nullptr);
  static const char* const styles[] = {"Espresso", "Pour-over"};
  int st = segmented(ex + 28 + 360, ey + 100, styles, 2, r.pourOver ? 1 : 0, 138);
  if (st >= 0 && (st == 1) != r.pourOver) {
    recipes::setPourOver(idx, st == 1);
    resetArmed = -1;
  }

  const int rowH = 70;
  int y = ey + 160;
  bool changed = false;

  settingRowLabel(ex + 28, y, "Dose", "Coffee in, or weigh it with DOSE");
  snprintf(b, sizeof(b), r.dose > 0 ? "%.1f g" : "-", r.dose);
  int d = stepper(ex + ew - 28, y, b, true);
  if (d == STEP_EDIT) {
    openKeypad("Dose", "Coffee in, 0 = not set", {{"Dose", r.dose, 0, 60, 1, "g", ""}},
               [idx](const std::vector<float>& v) {
                 recipes::get(idx).dose = v[0];
                 recipes::save(idx);
                 return std::string();
               }, Screen::Recipes);
  } else if (d) { r.dose = constrain(r.dose + d * 0.5f, 0.0f, 60.0f); changed = true; }
  divider(ex, y + rowH / 2, ew); y += rowH;

  char sub[64];
  if (r.target() > 0) snprintf(sub, sizeof(sub), "Beverage weight per gram: %.1f g out", r.target());
  else snprintf(sub, sizeof(sub), "Beverage weight per gram of coffee");
  settingRowLabel(ex + 28, y, "Ratio", sub);
  if (r.ratio > 0) snprintf(b, sizeof(b), "1:%.1f", r.ratio);
  else snprintf(b, sizeof(b), "none");
  d = stepper(ex + ew - 28, y, b, true);
  if (d == STEP_EDIT) {
    openKeypad("Ratio", "Grams out per gram in, 0 = no target", {{"Ratio", r.ratio, 0, 25, 1, "", "1:"}},
               [idx](const std::vector<float>& v) {
                 recipes::get(idx).ratio = v[0];
                 recipes::save(idx);
                 return std::string();
               }, Screen::Recipes);
  } else if (d) {
    float step = r.pourOver ? 0.5f : 0.1f;
    r.ratio = constrain(roundf((r.ratio + d * step) * 10) / 10, 0.0f, 25.0f);
    changed = true;
  }
  divider(ex, y + rowH / 2, ew); y += rowH;

  settingRowLabel(ex + 28, y, "Shot time", "Window the dial-in assistant aims for");
  if (r.timeMax > 0) {
    if (r.timeMax >= 120) snprintf(b, sizeof(b), "%d:%02d-%d:%02d", r.timeMin / 60, r.timeMin % 60, r.timeMax / 60, r.timeMax % 60);
    else snprintf(b, sizeof(b), "%d-%d s", r.timeMin, r.timeMax);
  } else {
    snprintf(b, sizeof(b), "off");
  }
  d = stepper(ex + ew - 28, y, b, true);
  if (d == STEP_EDIT) {
    float lo = r.timeMax ? r.timeMin : 25, hi = r.timeMax ? r.timeMax : 32;
    openKeypad("Shot time window", "Seconds, 0 and 0 = off",
               {{"From", lo, 0, 600, 0, "s", ""}, {"To", hi, 0, 600, 0, "s", ""}},
               [idx](const std::vector<float>& v) {
                 int a = (int)v[0], b2 = (int)v[1];
                 if (!(a == 0 && b2 == 0)) {
                   if (a < 5) return std::string("From must be at least 5 s (or 0 and 0 for off)");
                   if (b2 <= a) return std::string("To must be later than From");
                 }
                 Recipe& rr = recipes::get(idx);
                 rr.timeMin = a;
                 rr.timeMax = b2;
                 recipes::save(idx);
                 return std::string();
               }, Screen::Recipes);
  } else if (d) {
    // shift the window; from "off" start with a typical espresso window
    int step = r.timeMax >= 120 ? 10 : 1;
    if (r.timeMax == 0 && d > 0) { r.timeMin = 25; r.timeMax = 32; }
    else if (r.timeMin + d * step < 5) { r.timeMin = r.timeMax = 0; }
    else { r.timeMin += d * step; r.timeMax += d * step; }
    changed = true;
  }
  divider(ex, y + rowH / 2, ew); y += rowH;

  {
    float lo, hi;
    bool have = r.flowBand(lo, hi);
    const char* sub = r.flowMode == FLOW_AUTO
                          ? (have ? "Auto: from target yield and shot time" : "Auto: needs a target and a shot time")
                          : r.flowMode == FLOW_SET ? "Set by you; tap the value to change it"
                                                   : "No flow warnings for this recipe";
    settingRowLabel(ex + 28, y, "Flow band", sub);
    auto openBand = [&](float l, float h) {
      openKeypad("Flow band", "Grams per second",
                 {{"From", l, 0.1f, 15, 1, "g/s", ""}, {"To", h, 0.1f, 15, 1, "g/s", ""}},
                 [idx](const std::vector<float>& v) {
                   if (v[1] <= v[0]) return std::string("To must be higher than From");
                   Recipe& rr = recipes::get(idx);
                   rr.flowMode = FLOW_SET;
                   rr.flowMin = v[0];
                   rr.flowMax = v[1];
                   recipes::save(idx);
                   return std::string();
                 }, Screen::Recipes);
    };
    static const char* const modes[] = {"Auto", "Set", "Off"};
    const int segW = 76, segRight = ex + ew - 28;
    int m = segmented(segRight, y, modes, 3, r.flowMode, segW);
    // the band in effect, tappable to type your own
    const int vw = 128, vx = segRight - 3 * segW - 12 - vw;
    if (r.flowMode != FLOW_OFF) {
      bool tap = hit(vx, y - 27, vw, 54);
      canvas.fillSmoothRoundRect(vx, y - 27, vw, 54, 12, tap || flashing(vx, y - 27) ? STROKE : SURFACE2);
      canvas.drawFastHLine(vx + 14, y + 18, vw - 28, STROKE);
      if (have) snprintf(b, sizeof(b), "%.1f-%.1f", lo, hi);
      else snprintf(b, sizeof(b), "-");
      text(b, vx + vw / 2, y + 1, F_LABEL, HIGHLIGHT, textdatum_t::middle_center);
      if (tap) openBand(have ? lo : 1.0f, have ? hi : 2.8f);
    }
    if (m >= 0 && m != r.flowMode) {
      if (m == FLOW_SET) {
        openBand(have ? lo : 1.0f, have ? hi : 2.8f);   // start from the band you saw
      } else {
        r.flowMode = m;
        changed = true;
      }
    }
  }
  divider(ex, y + rowH / 2, ew); y += rowH;

  settingRowLabel(ex + 28, y, "Auto stop after", "Seconds without weight gain");
  snprintf(b, sizeof(b), "%d s", r.stopDelayS);
  d = stepper(ex + ew - 28, y, b, true);
  if (d == STEP_EDIT) {
    openKeypad("Auto stop after", "Seconds without weight gain", {{"Delay", (float)r.stopDelayS, 2, 90, 0, "s", ""}},
               [idx](const std::vector<float>& v) {
                 recipes::get(idx).stopDelayS = (uint8_t)v[0];
                 recipes::save(idx);
                 return std::string();
               }, Screen::Recipes);
  } else if (d) { r.stopDelayS = constrain(r.stopDelayS + d * (r.stopDelayS >= 10 ? 5 : 1), 2, 90); changed = true; }
  divider(ex, y + rowH / 2, ew); y += rowH;

  settingRowLabel(ex + 28, y, "Start threshold", "Weight change that starts the plot");
  snprintf(b, sizeof(b), "%.1f g", r.startThreshold);
  d = stepper(ex + ew - 28, y, b, true);
  if (d == STEP_EDIT) {
    openKeypad("Start threshold", "Weight change that starts the plot",
               {{"Threshold", r.startThreshold, 0.2f, 3.0f, 1, "g", ""}},
               [idx](const std::vector<float>& v) {
                 recipes::get(idx).startThreshold = v[0];
                 recipes::save(idx);
                 return std::string();
               }, Screen::Recipes);
  } else if (d) { r.startThreshold = constrain(r.startThreshold + d * 0.1f, 0.2f, 3.0f); changed = true; }
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
