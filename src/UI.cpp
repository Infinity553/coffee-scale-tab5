// Frame loop: touch, partial / full redraws, rotation and screen sleep.
#include "UI.h"
#include "AcaiaScale.h"
#include "Backup.h"
#include "Battery.h"
#include "Ota.h"
#include "Blit.h"
#include "History.h"
#include "Net.h"
#include "Settings.h"
#include "UIKit.h"
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui {

static Screen current = Screen::SetupWelcome;
static bool dirty = true;        // full repaint requested
static bool softDirty = false;   // data changed (screens other than main)
static uint32_t lastDraw = 0;
static int baseRotation = 1;
static int nativeW = 720, nativeH = 1280;   // panel size in its native orientation
static bool hwRotation = false;             // PPA rotates the landscape canvas into the panel
static uint32_t lastHistoryVer = 0;
static uint32_t lastScreenSig = 0;
static bool flashRedrawDue = false;   // one more frame when a button's pressed look ends

// screen sleep
static bool asleep = false;
static uint32_t lastActivity = 0;
static float activityWeight = 0;

void requestRedraw() { dirty = true; }
Screen currentScreen() { return current; }
Screen screen() { return current; }
void invalidate() { softDirty = true; }

void setScreen(Screen s) {
  current = s;
  clearTap();   // a tap never carries over to the next screen
  dirty = true;
}

// Rotation between the logical (landscape) UI and the portrait panel.
static int canvasRotation() {
  return settings.flipScreen ? (baseRotation + 2) % 4 : baseRotation;
}

// Two ways to get the landscape UI onto the portrait panel:
//  - hardware (PPA): the canvas is landscape and unrotated (fast drawing); the
//    PPA rotates changed regions into the frame buffer.
//  - software fallback: the canvas is portrait and rotated internally, so a
//    push is a straight memcpy into the frame buffer, but drawing is slower.
// These map between logical (landscape) and native panel coordinates.
static void logicalToNative(int x, int y, int& nx, int& ny) {
  const int NW = nativeW, NH = nativeH;
  switch (canvasRotation() & 3) {
    case 1:  nx = NW - 1 - y; ny = x; break;
    case 2:  nx = NW - 1 - x; ny = NH - 1 - y; break;
    case 3:  nx = y; ny = NH - 1 - x; break;
    default: nx = x; ny = y; break;
  }
}

static void nativeToLogical(int nx, int ny, int& x, int& y) {
  const int NW = nativeW, NH = nativeH;
  switch (canvasRotation() & 3) {
    case 1:  x = ny; y = NW - 1 - nx; break;
    case 2:  x = NW - 1 - nx; y = NH - 1 - ny; break;
    case 3:  x = NH - 1 - ny; y = nx; break;
    default: x = nx; y = ny; break;
  }
}

void pushRect(const Rect& r) {
  if (hwRotation) {
    if (!blit::push(canvas.getBuffer(), W, H, r.x, r.y, r.w, r.h, canvasRotation())) {
      static uint32_t lastLog = 0;   // the landscape canvas can't use the software path
      if (millis() - lastLog > 5000) { lastLog = millis(); log_e("PPA transfer failed"); }
    }
    return;
  }
  int ax, ay, bx, by;
  logicalToNative(r.x, r.y, ax, ay);
  logicalToNative(r.x + r.w - 1, r.y + r.h - 1, bx, by);
  M5.Display.setClipRect(min(ax, bx), min(ay, by), abs(bx - ax) + 1, abs(by - ay) + 1);
  canvas.pushSprite(0, 0);
  M5.Display.clearClipRect();
}

void applyDisplaySettings() {
  canvas.setRotation(hwRotation ? 0 : canvasRotation());
  if (!asleep) M5.Display.setBrightness(settings.brightness);
  dirty = true;
}

void begin() {
  theme::apply(settings.colorScheme);
  M5.Display.setRotation(0);
  nativeW = M5.Display.width();
  nativeH = M5.Display.height();
  baseRotation = nativeW < nativeH ? 1 : 0;   // landscape layout

  // same pixel format as the panel frame buffer -> no conversion on push
  canvas.setColorDepth(M5.Display.getColorDepth());
  canvas.setPsram(true);
  hwRotation = false;
#if defined(ESP_PLATFORM)
  if (blit::begin(nativeW, nativeH)) {
    // landscape canvas, 64-byte aligned so the PPA can also fill areas in it
    size_t bytes = (size_t)nativeW * nativeH * 2;
    void* buf = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM);
    if (buf) {
      memset(buf, 0, bytes);
      canvas.setBuffer(buf, nativeH, nativeW);
      hwRotation = true;
    }
  }
#endif
  setFastFill(hwRotation);
  if (!hwRotation && !canvas.createSprite(nativeW, nativeH)) log_e("canvas allocation failed");
  canvas.setTextWrap(false);
  applyDisplaySettings();
  W = canvas.width();
  H = canvas.height();
  M5.Speaker.setVolume(140);
  current = settings.configured ? Screen::Main : Screen::SetupWelcome;
  lastActivity = millis();
}

void* snapshotPng(size_t* len) { return canvas.createPng(len, 0, 0, W, H); }

void simulateTap(int x, int y) { setTap(x, y); }

// --- frame timing stats (printed every 5 s) ---
static uint32_t statFrames = 0, statFull = 0, statDrawUs = 0, statPushUs = 0, statMs = 0;

static void logStats(uint32_t now) {
  if (now - statMs < 5000) return;
  if (statFrames) {
    Serial.printf("[UI] %lu frames (%lu full) in 5 s, avg draw %.1f ms, push %.1f ms\n",
                  (unsigned long)statFrames, (unsigned long)statFull,
                  statDrawUs / 1000.0f / statFrames, statPushUs / 1000.0f / statFrames);
  }
  statFrames = statFull = statDrawUs = statPushUs = 0;
  statMs = now;
}

static void wake(uint32_t now) {
  lastActivity = now;
  if (!asleep) return;
  asleep = false;
  M5.Display.setBrightness(settings.brightness);
  dirty = true;
}

static void drawScreen() {
  switch (current) {
    case Screen::SetupWelcome: drawWelcome(); break;
    case Screen::SetupScan:    drawScan(true); break;
    case Screen::SetupPrefs:   drawSetupPrefs(); break;
    case Screen::Main:         drawMain(); break;
    case Screen::Settings:     drawSettings(); break;
    case Screen::Scan:         drawScan(false); break;
    case Screen::Recipes:      drawRecipes(); break;
    case Screen::History:      drawHistory(); break;
    case Screen::Wifi:         drawWifi(); break;
    case Screen::WifiNetworks: drawNetworks(); break;
    case Screen::Keyboard:     drawKeyboard(); break;
    case Screen::System:       drawSystem(); break;
    case Screen::Keypad:       drawKeypad(); break;
  }
}

// What a (non-main) screen shows that can change without a touch. The screen is
// only repainted when this changes: a full repaint costs ~150 ms on the Tab5.
static uint32_t screenSignature() {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  auto mixs = [&](const std::string& s) { for (char ch : s) mix((uint8_t)ch); mix(0); };
  mix((uint32_t)scale.state() + 16 * scale.weightFresh());
  mix((uint32_t)scale.battery());
  mix((uint32_t)battery::level());
  mix(battery::charging());
  switch (current) {
    case Screen::SetupScan:
    case Screen::Scan:
      for (auto& f : scale.discovered()) { mixs(f.address.c_str()); mix(f.rssi / 10); }
      break;
    case Screen::Wifi:
    case Screen::Settings: {
      net::Status s = net::status();
      mix(s.enabled); mix(s.hotspot); mix(s.connected); mix(s.connecting);
      mix(s.passwordRejected); mixs(s.ip); mixs(s.ssid); mixs(s.problem);
      mix((uint32_t)history::count()); mix(history::available());
      break;
    }
    case Screen::WifiNetworks:
      mix(net::scanning()); mix((uint32_t)net::lastScanResult());
      for (auto& n : net::networks()) { mixs(n.ssid); mix(n.rssi / 10); }
      break;
    case Screen::SetupWelcome:
    case Screen::System:
      mix(history::available()); mix(backup::lastSavedMs()); mix(settings.autoBackup);
      mix(ota::allowedSecondsLeft());
      mix((uint32_t)history::count()); mix(millis() / 1000 % 4 == 0);   // refresh "saved" notes
      break;
    default:
      break;
  }
  return h;
}

void update() {
  uint32_t now = millis();
  auto t = M5.Touch.getDetail();
  bool touched = t.wasPressed();

  // ---- firmware update: progress screen only, no touch ----
  if (ota::active() || ota::succeeded()) {
    if (asleep) wake(now);
    static int lastShown = -1;
    int p = ota::progress() + (ota::succeeded() ? 1000 : 0);
    if (p != lastShown && now - lastDraw >= 150) {
      lastShown = p;
      fillRectFast(0, 0, W, H, BG);
      drawUpdating();
      pushRect({0, 0, W, H});
      lastDraw = now;
    }
    dirty = true;   // full repaint once it's over (e.g. after a failed upload)
    return;
  }

  // ---- screen sleep ----
  if (fabsf(brew.weight() - activityWeight) > 0.5f || brew.state() == BrewState::Running) {
    activityWeight = brew.weight();
    if (asleep) wake(now);
    lastActivity = now;
  }
  if (asleep) {
    if (touched) wake(now);   // the waking tap is not passed on
    mainEvents();
    return;
  }
  if (touched) {
    int x, y;
    nativeToLogical(t.x, t.y, x, y);
    setTap(x, y);
    lastActivity = now;
  }
  if (settings.sleepMin && now - lastActivity > settings.sleepMin * 60000UL &&
      current != Screen::Keyboard) {
    asleep = true;
    M5.Display.setBrightness(0);
    return;
  }

  mainEvents();
  logStats(now);
  if (history::version() != lastHistoryVer) {
    lastHistoryVer = history::version();
    // the History screen refreshes its own panels (below); elsewhere repaint
    if (current != Screen::History || !history::available()) softDirty = true;
  }

  // ---- main screen: partial updates ----
  if (current == Screen::Main && !hasTap() && !dirty) {
    if (now - lastDraw < 20) return;   // cap at ~50 fps
    uint32_t drawUs = 0, pushUs = 0;
    if (mainPartialUpdate(drawUs, pushUs)) {
      statFrames++;
      statDrawUs += drawUs;
      statPushUs += pushUs;
      lastDraw = now;
    }
    softDirty = false;
    if (!dirty) return;
  }

  // ---- History: redraw only the panels a tap changed ----
  if (current == Screen::History && !dirty && !softDirty) {
    bool flashEnd = flashRedrawDue && !flashActive(now);
    if (hasTap() || flashEnd || now - lastDraw >= 100) {
      uint32_t drawUs = 0, pushUs = 0;
      if (historyPartialUpdate(flashEnd, drawUs, pushUs)) {
        bool hit = takeHit();
        flashRedrawDue = hit && flashShown() && current == Screen::History;
        if (current == Screen::History) dirty = false;
        statFrames++;
        statDrawUs += drawUs;
        statPushUs += pushUs;
        lastDraw = now;
        return;
      }
    }
  }

  // ---- full redraw (touch, screen change, or the screen's content changed) ----
  bool refresh = false;
  if (current != Screen::Main && now - lastDraw >= 200) {
    uint32_t sig = screenSignature();
    refresh = softDirty || sig != lastScreenSig;
  }
  bool flashEnd = flashRedrawDue && !flashActive(now);
  if (!hasTap() && !dirty && !refresh && !flashEnd) return;
  softDirty = false;
  flashRedrawDue = false;

  uint32_t t0 = micros();
  dirty = false;
  fillRectFast(0, 0, W, H, BG);
  drawScreen();
  clearTap();
  if (current != Screen::Main) lastScreenSig = screenSignature();
  // One follow-up frame after a tap: shows state changed by the tap and ends the
  // pressed look. (The main screen's regions handle this themselves.)
  if (takeHit() && current != Screen::Main) flashRedrawDue = true;
  uint32_t t1 = micros();
  pushRect({0, 0, W, H});
  statFrames++;
  statFull++;
  statDrawUs += t1 - t0;
  statPushUs += micros() - t1;
  lastDraw = now;
}

}  // namespace ui
