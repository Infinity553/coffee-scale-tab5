#include "UIKit.h"
#include <ctime>
#include "AcaiaScale.h"
#include "Battery.h"
#include "Blit.h"
#include "Recipes.h"
#include "Settings.h"

namespace ui {

M5Canvas canvas(&M5.Display);
int W = 1280, H = 720;
const lgfx::IFont* F_AXIS  = &fonts::FreeSans9pt7b;
const lgfx::IFont* F_LABEL = &fonts::FreeSansBold12pt7b;
const lgfx::IFont* F_BODY  = &fonts::FreeSans12pt7b;
const lgfx::IFont* F_BODYL = &fonts::FreeSans18pt7b;
const lgfx::IFont* F_BTN   = &fonts::FreeSansBold18pt7b;
const lgfx::IFont* F_TITLE = &fonts::FreeSansBold24pt7b;

// tap state (set by UI.cpp, consumed by widgets)
static bool tapPending = false;
static int tapX = 0, tapY = 0;
static int flashX = -1, flashY = -1;
static uint32_t flashMs = 0;
static bool flashDrawn = false;   // a widget actually showed a pressed state
static bool hitFlag = false;      // some widget handled a tap since the last takeHit()

void setTap(int x, int y) { tapPending = true; tapX = x; tapY = y; }
bool hasTap() { return tapPending; }
bool tapPosition(int& x, int& y) { x = tapX; y = tapY; return tapPending; }
void clearTap() { tapPending = false; }
bool flashActive(uint32_t now) { return flashX >= 0 && now - flashMs < 200; }
bool flashShown() { return flashDrawn; }
bool takeHit() { bool h = hitFlag; hitFlag = false; return h; }
bool flashPoint(int& x, int& y) { x = flashX; y = flashY; return flashX >= 0; }

// ---------------------------------------------------------------------------
// text
// ---------------------------------------------------------------------------
void text(const char* s, int x, int y, const lgfx::IFont* f, uint16_t c, textdatum_t d) {
  canvas.setFont(f);
  canvas.setTextColor(c);
  canvas.setTextDatum(d);
  canvas.drawString(s, x, y);
}

int textWidth(const char* s, const lgfx::IFont* f) {
  canvas.setFont(f);
  return canvas.textWidth(s);
}

void textFit(const char* s, int x, int y, int maxW, const lgfx::IFont* f, uint16_t c) {
  if (textWidth(s, f) <= maxW) return text(s, x, y, f, c);
  std::string t = s;
  while (!t.empty() && textWidth((t + "...").c_str(), f) > maxW) t.pop_back();
  text((t + "...").c_str(), x, y, f, c);
}

// ---------------------------------------------------------------------------
// widgets
// ---------------------------------------------------------------------------
// --- fast fills ---------------------------------------------------------------
// Large solid areas are filled by the PPA (DMA, ~5x faster than the CPU writing
// to PSRAM). Only possible when the canvas is landscape, unrotated and aligned.
static bool fastFillOn = false;
void setFastFill(bool on) { fastFillOn = on; }

void fillRectFast(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  if (fastFillOn && w * h >= 4096 &&
      blit::fill(canvas.getBuffer(), (size_t)W * H * 2, W, H, x, y, w, h, c))
    return;
  canvas.fillRect(x, y, w, h, c);
}

void card(int x, int y, int w, int h, uint16_t bg) {
  const int r = 22;
  if (!fastFillOn || w < 3 * r || h < 3 * r) {
    canvas.fillSmoothRoundRect(x, y, w, h, r, STROKE);
    canvas.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, bg);
    return;
  }
  // interior by DMA, 1 px border by the CPU
  fillRectFast(x + 1, y + r, w - 2, h - 2 * r, bg);
  fillRectFast(x + r, y + 1, w - 2 * r, r - 1, bg);
  fillRectFast(x + r, y + h - r, w - 2 * r, r - 1, bg);
  canvas.drawFastHLine(x + r, y, w - 2 * r, STROKE);
  canvas.drawFastHLine(x + r, y + h - 1, w - 2 * r, STROKE);
  canvas.drawFastVLine(x, y + r, h - 2 * r, STROKE);
  canvas.drawFastVLine(x + w - 1, y + r, h - 2 * r, STROKE);
  // anti-aliased corners: a small rounded square, clipped to each corner
  const int s = 2 * r + 2;
  const int cx[4] = {x, x + w - r, x, x + w - r};
  const int cy[4] = {y, y, y + h - r, y + h - r};
  const int ox[4] = {x, x + w - s, x, x + w - s};
  const int oy[4] = {y, y, y + h - s, y + h - s};
  for (int i = 0; i < 4; i++) {
    canvas.setClipRect(cx[i], cy[i], r, r);
    canvas.fillSmoothRoundRect(ox[i], oy[i], s, s, r, STROKE);
    canvas.fillSmoothRoundRect(ox[i] + 1, oy[i] + 1, s - 2, s - 2, r - 1, bg);
  }
  canvas.clearClipRect();
}

bool hit(int x, int y, int w, int h) {
  if (!tapPending) return false;
  if (tapX < x || tapX >= x + w || tapY < y || tapY >= y + h) return false;
  tapPending = false;
  flashX = x; flashY = y; flashMs = millis();
  flashDrawn = false;
  hitFlag = true;   // the frame loop schedules one follow-up frame
  return true;
}

bool flashing(int x, int y) {
  bool f = flashX == x && flashY == y && millis() - flashMs < 160;
  if (f) flashDrawn = true;
  return f;
}

bool button(int x, int y, int w, int h, const char* label, Btn style, bool enabled,
            const lgfx::IFont* font) {
  bool clicked = enabled && hit(x, y, w, h);
  bool pressed = flashing(x, y) || clicked;
  uint16_t bg, fg, border;
  switch (style) {
    case Btn::Primary: bg = pressed ? ACCENT_HI : ACCENT; fg = ON_ACCENT; border = bg; break;
    case Btn::Danger:  bg = pressed ? STROKE : SURFACE2;  fg = BAD;       border = STROKE; break;
    case Btn::Ghost:   bg = pressed ? SURFACE2 : SURFACE; fg = TEXT;      border = STROKE; break;
    default:           bg = pressed ? STROKE : SURFACE2;  fg = TEXT;      border = STROKE; break;
  }
  if (!enabled) { bg = SURFACE; fg = STROKE; border = SURFACE2; }
  int r = min(h / 2, 22);
  canvas.fillSmoothRoundRect(x, y, w, h, r, border);
  canvas.fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, r - 2, bg);
  if (label && *label)
    text(label, x + w / 2, y + h / 2 + 1, font ? font : F_BTN, fg, textdatum_t::middle_center);
  return clicked;
}

bool circleButton(int cx, int cy, int r, void (*icon)(int, int, uint16_t, uint16_t)) {
  bool c = hit(cx - r - 6, cy - r - 6, 2 * r + 12, 2 * r + 12);
  uint16_t bg = c || flashing(cx - r - 6, cy - r - 6) ? STROKE : SURFACE2;
  canvas.fillSmoothCircle(cx, cy, r, bg);
  icon(cx, cy, TEXT, bg);
  return c;
}

bool toggle(int x, int y, bool& value) {
  const int w = 92, h = 48;
  bool clicked = hit(x - 20, y - 14, w + 40, h + 28);
  if (clicked) value = !value;
  uint16_t bg = value ? ACCENT : SURFACE2;
  canvas.fillSmoothRoundRect(x, y, w, h, h / 2, value ? ACCENT : STROKE);
  canvas.fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, h / 2 - 2, bg);
  int kx = value ? x + w - h / 2 : x + h / 2;
  canvas.fillSmoothCircle(kx, y + h / 2, h / 2 - 6, value ? TEXT : MUTED);
  return clicked;
}

int segmented(int xr, int cy, const char* const* labels, int n, int selected, int segW) {
  const int h = 52;
  int x = xr - segW * n;
  int clicked = -1;
  canvas.fillSmoothRoundRect(x, cy - h / 2, segW * n, h, h / 2, SURFACE2);
  for (int i = 0; i < n; i++) {
    int sx = x + i * segW;
    if (hit(sx, cy - h / 2 - 10, segW, h + 20)) clicked = i;
    bool on = (clicked >= 0 ? clicked : selected) == i;
    if (on) canvas.fillSmoothRoundRect(sx + 4, cy - h / 2 + 4, segW - 8, h - 8, h / 2 - 4, ACCENT);
    text(labels[i], sx + segW / 2, cy + 1, F_LABEL, on ? ON_ACCENT : MUTED,
         textdatum_t::middle_center);
  }
  return clicked;
}

void chip(int x, int y, const char* label, uint16_t fg, uint16_t bg, textdatum_t align) {
  int w = textWidth(label, F_LABEL) + 32, h = 38;
  if (align == textdatum_t::middle_right) x -= w;
  canvas.fillSmoothRoundRect(x, y - h / 2, w, h, h / 2, bg);
  text(label, x + w / 2, y + 1, F_LABEL, fg, textdatum_t::middle_center);
}

void toggleChip(int xr, int y, const char* label, bool on) {
  int w = textWidth(label, F_LABEL) + 32, h = 38;
  int x = xr - w;
  if (on) {
    canvas.fillSmoothRoundRect(x, y - h / 2, w, h, h / 2, CHIP_ON_LINE);
    canvas.fillSmoothRoundRect(x + 2, y - h / 2 + 2, w - 4, h - 4, h / 2 - 2, CHIP_ON);
  } else {
    canvas.fillSmoothRoundRect(x, y - h / 2, w, h, h / 2, SURFACE2);
  }
  text(label, x + w / 2, y + 1, F_LABEL, on ? CHIP_ON_FG : MUTED, textdatum_t::middle_center);
}

int stepper(int xr, int cy, const char* value, bool editable) {
  const int bs = 54, vw = 120;
  int r = 0;
  if (button(xr - bs, cy - bs / 2, bs, bs, "+", Btn::Secondary)) r = 1;
  if (editable) {
    // tappable value: a field look, opens the keypad
    const int vx = xr - bs - vw + 6, vy = cy - bs / 2, fw = vw - 12;
    if (hit(vx, vy, fw, bs)) r = STEP_EDIT;
    canvas.fillSmoothRoundRect(vx, vy, fw, bs, 12, flashing(vx, vy) || r == STEP_EDIT ? STROKE : SURFACE2);
    canvas.drawFastHLine(vx + 14, vy + bs - 9, fw - 28, STROKE);
  }
  text(value, xr - bs - vw / 2, cy + 1, F_LABEL, HIGHLIGHT, textdatum_t::middle_center);
  if (button(xr - bs * 2 - vw, cy - bs / 2, bs, bs, "-", Btn::Secondary)) r = -1;
  return r;
}

void settingRowLabel(int x, int y, const char* label, const char* sub) {
  if (sub) {
    text(label, x, y - 13, F_LABEL, TEXT);
    text(sub, x, y + 17, F_AXIS, MUTED);
  } else {
    text(label, x, y, F_LABEL, TEXT);
  }
}

bool toggleRow(int x, int y, int w, const char* label, const char* sub, bool& v) {
  settingRowLabel(x + 28, y, label, sub);
  bool c = toggle(x + w - 28 - 92, y - 24, v);
  if (c) settings.save();
  return c;
}

void divider(int x, int y, int w) { canvas.drawFastHLine(x + 28, y, w - 56, GRID); }

void rssiBars(int x, int y, int rssi) {
  int lvl = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int bh = 8 + i * 7;
    canvas.fillSmoothRoundRect(x + i * 11, y - bh, 7, bh, 3, i < lvl ? ACCENT : STROKE);
  }
}

void drawStars(int x, int cy, int size, int rating) {
  for (int i = 0; i < 5; i++)
    iconStar(x + size / 2 + i * (size + size / 5), cy, size / 2, i < rating, i < rating ? ACCENT : STROKE);
}

int starsInput(int x, int cy, int size, int rating) {
  int step = size + size / 5;
  int result = -1;
  for (int i = 0; i < 5; i++) {
    if (hit(x + i * step - 4, cy - size / 2 - 10, step, size + 20)) result = (rating == i + 1) ? 0 : i + 1;
  }
  drawStars(x, cy, size, result >= 0 ? result : rating);
  return result;
}

void stepDots(int step, int total) {
  int cx = W / 2 - (total - 1) * 16;
  for (int i = 0; i < total; i++) {
    if (i == step) canvas.fillSmoothRoundRect(cx + i * 32 - 14, H - 46, 28, 12, 6, ACCENT);
    else canvas.fillSmoothCircle(cx + i * 32, H - 40, 6, STROKE);
  }
}

void statusPill(int cx, int cy) {
  const char* label;
  uint16_t dot;
  String name = scale.connectedName();
  char buf[64];
  switch (scale.state()) {
    case ScaleState::Connected:
      snprintf(buf, sizeof(buf), "%s", name.length() ? name.c_str() : "Scale connected");
      label = buf; dot = scale.weightFresh() ? GOOD : WARN; break;
    case ScaleState::Connecting:  label = "Connecting..."; dot = WARN; break;
    case ScaleState::Discovering: label = "Scanning..."; dot = WARN; break;
    case ScaleState::Searching:   label = "Waiting for scale"; dot = BAD; break;
    default:                      label = "Bluetooth off"; dot = BAD; break;
  }
  // the scale's battery lives inside the pill, next to its name
  int bat = scale.connected() ? scale.battery() : -1;
  char pct[8] = "";
  if (bat >= 0) snprintf(pct, sizeof(pct), "%d%%", bat);
  int extra = bat >= 0 ? 24 + 36 + 8 + textWidth(pct, F_LABEL) : 0;
  int w = textWidth(label, F_LABEL) + 72 + extra, h = 46;
  canvas.fillSmoothRoundRect(cx - w / 2, cy - h / 2, w, h, h / 2, SURFACE2);
  bool pulse = scale.state() != ScaleState::Connected && (millis() / 600) % 2;
  canvas.fillSmoothCircle(cx - w / 2 + 28, cy, 8, pulse ? STROKE : dot);
  int tx = cx - w / 2 + 48;
  text(label, tx, cy + 1, F_LABEL, TEXT);
  if (bat >= 0) {
    int bx = tx + textWidth(label, F_LABEL) + 24;
    // small battery
    uint16_t bc = bat < 15 ? BAD : MUTED;
    canvas.fillSmoothRoundRect(bx, cy - 8, 30, 16, 4, bc);
    canvas.fillSmoothRoundRect(bx + 2, cy - 6, 26, 12, 3, SURFACE2);
    canvas.fillRect(bx + 30, cy - 4, 3, 8, bc);
    int fw = 22 * constrain(bat, 0, 100) / 100;
    if (fw > 0) canvas.fillRect(bx + 4, cy - 4, fw, 8, bc);
    text(pct, bx + 42, cy + 1, F_LABEL, MUTED);
  }
}

int deviceBattery(int xr, int cy) {
  if (!battery::present()) return 0;
  int lvl = battery::level();
  bool chg = battery::charging();
  uint16_t c = chg ? GOOD : lvl <= 10 ? BAD : MUTED;
  char b[8];
  snprintf(b, sizeof(b), "%d%%", lvl);
  int tw = textWidth(b, F_LABEL);
  text(b, xr, cy, F_LABEL, c, textdatum_t::middle_right);
  int ix = xr - tw - 60;
  iconBattery(ix, cy - 12, lvl, c);
  if (chg) {   // lightning bolt over the icon
    int bx = ix + 23, by = cy;
    canvas.fillTriangle(bx + 3, by - 10, bx - 5, by + 2, bx + 1, by + 2, BG);
    canvas.fillTriangle(bx - 1, by - 2, bx + 5, by - 2, bx - 3, by + 10, BG);
    canvas.fillTriangle(bx + 2, by - 8, bx - 3, by + 1, bx + 1, by + 1, TEXT);
    canvas.fillTriangle(bx, by - 1, bx + 3, by - 1, bx - 2, by + 8, TEXT);
  }
  return tw + 64;
}

void topBar(const char* title, Screen backTo) {
  bool b = hit(0, 0, 260, 80);
  canvas.fillSmoothCircle(48, 40, 26, b || flashing(0, 0) ? STROKE : SURFACE2);
  iconBack(46, 40, TEXT, SURFACE2);
  textFit(title, 90, 40, 470, F_BTN, TEXT);
  statusPill(W / 2 + 120, 40);
  deviceBattery(W - 36, 40);
  if (b) {
    settings.save();
    setScreen(backTo);
  }
}

// ---------------------------------------------------------------------------
// icons
// ---------------------------------------------------------------------------
void iconCup(int cx, int cy, int s, uint16_t c, uint16_t bg) {
  canvas.fillSmoothRoundRect(cx - s, cy - s / 2, s * 2 - s / 3, s + s / 2, s / 3, c);
  canvas.fillSmoothCircle(cx + s - s / 4, cy + s / 6, s / 2, c);
  canvas.fillSmoothCircle(cx + s - s / 4, cy + s / 6, s / 4, bg);
  int sw = max(3, s / 6);
  for (int i = -1; i <= 1; i++) {
    int x = cx - s / 6 + i * s / 2 - sw / 2;
    int len = i == 0 ? s * 3 / 4 : s / 2;
    canvas.fillSmoothRoundRect(x, cy - s * 0.7f - len, sw, len, sw / 2, c);
  }
}

void iconGear(int cx, int cy, uint16_t c, uint16_t bg) {
  const int r = 12;
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    canvas.drawWideLine(cx, cy, cx + cosf(a) * r * 1.3f, cy + sinf(a) * r * 1.3f, r * 0.28f, c);
  }
  canvas.fillSmoothCircle(cx, cy, r, c);
  canvas.fillSmoothCircle(cx, cy, r * 0.45f, bg);
}

void iconHistory(int cx, int cy, uint16_t c, uint16_t bg) {
  canvas.fillSmoothCircle(cx, cy, 15, c);
  canvas.fillSmoothCircle(cx, cy, 12, bg);
  canvas.drawWideLine(cx, cy, cx, cy - 8, 1.6f, c);
  canvas.drawWideLine(cx, cy, cx + 6, cy + 3, 1.6f, c);
}

void iconWifi(int cx, int cy, uint16_t c, uint16_t bg) {
  for (int i = 0; i < 3; i++) {
    int r1 = 8 + i * 7;
    canvas.fillArc(cx, cy + 9, r1 - 3, r1, 225, 315, c);
  }
  canvas.fillSmoothCircle(cx, cy + 9, 3, c);
}

void iconBack(int cx, int cy, uint16_t c, uint16_t) {
  canvas.drawWideLine(cx + 8, cy - 14, cx - 6, cy, 3.5f, c);
  canvas.drawWideLine(cx - 6, cy, cx + 8, cy + 14, 3.5f, c);
}

void iconChevron(int cx, int cy, uint16_t c, uint16_t) {
  canvas.drawWideLine(cx - 5, cy - 10, cx + 5, cy, 2.5f, c);
  canvas.drawWideLine(cx + 5, cy, cx - 5, cy + 10, 2.5f, c);
}

void iconPlay(int cx, int cy, uint16_t c, uint16_t) {
  canvas.fillTriangle(cx - 9, cy - 13, cx - 9, cy + 13, cx + 13, cy, c);
}

void iconStop(int cx, int cy, uint16_t c, uint16_t) {
  canvas.fillSmoothRoundRect(cx - 11, cy - 11, 22, 22, 4, c);
}

void iconReset(int cx, int cy, uint16_t c, uint16_t) {
  canvas.fillArc(cx, cy, 10, 14, 300, 540, c);   // open ring
  canvas.fillTriangle(cx + 6, cy - 19, cx + 6, cy - 3, cx + 17, cy - 13, c);
}

void iconTare(int cx, int cy, uint16_t c, uint16_t bg) {
  canvas.fillTriangle(cx - 22, cy - 8, cx - 22, cy + 8, cx - 12, cy, c);
  canvas.fillTriangle(cx + 22, cy - 8, cx + 22, cy + 8, cx + 12, cy, c);
  canvas.fillSmoothCircle(cx, cy, 9, c);
  canvas.fillSmoothCircle(cx, cy, 5, bg);
}

void iconBean(int cx, int cy, uint16_t c, uint16_t bg) {
  canvas.fillEllipse(cx, cy, 10, 14, c);
  canvas.drawWideLine(cx - 1, cy - 11, cx + 2, cy - 3, 1.3f, bg);
  canvas.drawWideLine(cx + 2, cy - 3, cx - 2, cy + 4, 1.3f, bg);
  canvas.drawWideLine(cx - 2, cy + 4, cx + 1, cy + 11, 1.3f, bg);
}

void iconCheck(int cx, int cy, uint16_t c, uint16_t) {
  canvas.drawWideLine(cx - 12, cy, cx - 3, cy + 9, 3.0f, c);
  canvas.drawWideLine(cx - 3, cy + 9, cx + 13, cy - 9, 3.0f, c);
}

void iconLock(int cx, int cy, uint16_t c, uint16_t bg) {
  canvas.fillArc(cx, cy - 4, 5, 8, 180, 360, c);
  canvas.fillRect(cx - 8, cy - 4, 3, 4, c);
  canvas.fillRect(cx + 5, cy - 4, 3, 4, c);
  canvas.fillSmoothRoundRect(cx - 10, cy - 1, 20, 14, 3, c);
}

void iconBattery(int x, int y, int pct, uint16_t c) {
  const int w = 46, h = 24;
  canvas.fillSmoothRoundRect(x, y, w, h, 6, c);
  canvas.fillSmoothRoundRect(x + 3, y + 3, w - 6, h - 6, 4, BG);
  canvas.fillSmoothRoundRect(x + w, y + 7, 5, h - 14, 2, c);
  int fw = (w - 10) * constrain(pct, 0, 100) / 100;
  if (fw > 0) canvas.fillSmoothRoundRect(x + 5, y + 5, fw, h - 10, 2, pct < 20 ? BAD : c);
}

void iconStar(int cx, int cy, int r, bool filled, uint16_t c) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = -PI / 2 + i * PI / 5;
    float rr = (i % 2) ? r * 0.45f : r;
    px[i] = cx + cosf(a) * rr;
    py[i] = cy + sinf(a) * rr;
  }
  // solid in both states (an empty star is drawn in a dim colour by the caller):
  // outlines took 10 anti-aliased lines per star, which made lists slow
  (void)filled;
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    canvas.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], c);
  }
}

// ---------------------------------------------------------------------------
// segment digits (crisp at any size)
// ---------------------------------------------------------------------------
static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static float segWidth(char c, float h) {
  if (c >= '0' && c <= '9') return h * 0.5f;
  if (c == '-') return h * 0.45f;
  if (c == '.' || c == ':') return h * 0.16f;
  return h * 0.3f;
}

float segTextWidth(const char* s, float h) {
  float w = 0;
  int n = 0;
  for (const char* p = s; *p; p++, n++) w += segWidth(*p, h);
  return w + (n > 1 ? (n - 1) * h * 0.11f : 0);
}

static void segChar(char c, float x, float y, float h, uint16_t col) {
  float t = max(4.0f, h * 0.115f);
  float cw = h * 0.5f;
  float g = max(1.5f, t * 0.16f);
  int r = (int)(t / 2);
  auto hseg = [&](float yy) {
    canvas.fillSmoothRoundRect(x + t * 0.5f + g, yy, cw - t - 2 * g, t, r, col);
  };
  auto vseg = [&](float xx, float yy) {
    canvas.fillSmoothRoundRect(xx, yy, t, h / 2 - t / 2 - 2 * g, r, col);
  };
  if (c >= '0' && c <= '9') {
    uint8_t m = SEG[c - '0'];
    if (m & 0x01) hseg(y);
    if (m & 0x02) vseg(x + cw - t, y + t / 2 + g);
    if (m & 0x04) vseg(x + cw - t, y + h / 2 + g);
    if (m & 0x08) hseg(y + h - t);
    if (m & 0x10) vseg(x, y + h / 2 + g);
    if (m & 0x20) vseg(x, y + t / 2 + g);
    if (m & 0x40) hseg(y + h / 2 - t / 2);
  } else if (c == '-') {
    canvas.fillSmoothRoundRect(x, y + h / 2 - t / 2, h * 0.45f, t, r, col);
  } else if (c == '.') {
    canvas.fillSmoothCircle(x + h * 0.08f, y + h - t * 0.6f, t * 0.62f, col);
  } else if (c == ':') {
    canvas.fillSmoothCircle(x + h * 0.08f, y + h * 0.3f, t * 0.55f, col);
    canvas.fillSmoothCircle(x + h * 0.08f, y + h * 0.72f, t * 0.55f, col);
  }
}

void segText(const char* s, float x, float y, float h, uint16_t col, bool alignRight) {
  if (alignRight) x -= segTextWidth(s, h);
  for (const char* p = s; *p; p++) {
    segChar(*p, x, y, h, col);
    x += segWidth(*p, h) + h * 0.11f;
  }
}

// ---------------------------------------------------------------------------
// plot
// ---------------------------------------------------------------------------
uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) |
                    (int)(ab + (bb - ab) * t));
}

static float niceStep(float range, int ticks) {
  float raw = range / ticks;
  float mag = powf(10, floorf(log10f(raw)));
  float n = raw / mag;
  float s = n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5f ? 2.5f : n <= 5 ? 5 : 10;
  return s * mag;
}

// value of a curve at time t (linear interpolation, walking index i forward)
static bool curveAt(const BrewSample* s, int n, float t, int& i, float& w, float& f) {
  if (n < 2 || t > s[n - 1].t) return false;
  while (i < n - 2 && s[i + 1].t < t) i++;
  const BrewSample& a = s[i];
  const BrewSample& b = s[min(i + 1, n - 1)];
  float k = b.t > a.t ? constrain((t - a.t) / (b.t - a.t), 0.0f, 1.0f) : 0;
  w = max(0.0f, a.w + (b.w - a.w) * k);
  f = max(0.0f, a.f + (b.f - a.f) * k);
  return true;
}

void drawPlotArea(int px, int py, int pw, int ph, const BrewSample* s, int n, float maxW,
                  float maxF, const PlotOpts& o) {
  const BrewSample* g = o.ghost && o.ghost->size() > 1 ? o.ghost->data() : nullptr;
  int gn = g ? (int)o.ghost->size() : 0;

  float tEnd = n ? s[n - 1].t : 0;
  float gEnd = gn ? g[gn - 1].t : 0;
  float tNeed = max(30.0f, max(tEnd * 1.08f + 2, gEnd * 1.04f));
  float tStep = tNeed <= 40 ? 5 : tNeed <= 80 ? 10 : tNeed <= 160 ? 20 : tNeed <= 360 ? 60 : 120;
  float tMax = ceilf(tNeed / tStep) * tStep;

  float gMaxW = 0;
  for (int i = 0; i < gn; i++) gMaxW = max(gMaxW, g[i].w);
  float wNeed = max(20.0f, max(maxW, gMaxW) * 1.12f);
  if (o.target > 0) wNeed = max(wNeed, o.target * 1.15f);
  float wStep = niceStep(wNeed, 5);
  float wMax = ceilf(wNeed / wStep) * wStep;

  float fNeed = max(4.0f, maxF * 1.2f);
  float fStep = niceStep(fNeed, 5);
  float fMax = wMax / wStep * fStep;   // align flow ticks with weight grid
  if (fMax < fNeed) { fStep *= 2; fMax = wMax / wStep * fStep; }

  // flow guide band: a faint strip on the flow axis, under the grid
  if (o.flowHi > 0 && o.flowHi > o.flowLo) {
    int yHi = py + ph - (int)(min(o.flowHi, fMax) / fMax * ph);
    int yLo = py + ph - (int)(min(o.flowLo, fMax) / fMax * ph);
    canvas.fillRect(px, yHi, pw, yLo - yHi, lerp565(SURFACE, FLOW, 0.09f));
    text("flow band", px + pw - 8, yHi + 14, F_AXIS, lerp565(SURFACE, FLOW, 0.55f), textdatum_t::middle_right);
  }

  char b[24];
  for (float v = 0; v <= wMax + 0.01f; v += wStep) {
    int yy = py + ph - (int)(v / wMax * ph);
    canvas.drawFastHLine(px, yy, pw, v == 0 ? STROKE : GRID);
    snprintf(b, sizeof(b), "%g", v);
    text(b, px - 14, yy, F_AXIS, MUTED, textdatum_t::middle_right);
    snprintf(b, sizeof(b), "%g", v / wMax * fMax);
    text(b, px + pw + 12, yy, F_AXIS, o.dim ? MUTED : FLOW, textdatum_t::middle_left);
  }
  for (float t = 0; t <= tMax + 0.01f; t += tStep) {
    int xx = px + (int)(t / tMax * pw);
    if (t > 0) canvas.drawFastVLine(xx, py, ph, GRID);
    if (tMax >= 120) snprintf(b, sizeof(b), "%d:%02d", (int)t / 60, (int)t % 60);
    else snprintf(b, sizeof(b), "%ds", (int)t);
    text(b, xx, py + ph + 22, F_AXIS, MUTED, textdatum_t::middle_center);
  }
  text("g", px - 14, py - 24, F_AXIS, MUTED, textdatum_t::middle_right);
  text("g/s", px + pw + 12, py - 24, F_AXIS, o.dim ? MUTED : FLOW, textdatum_t::middle_left);

  auto hDashed = [&](float v, uint16_t c) {
    int yy = py + ph - (int)(v / wMax * ph);
    for (int xx = px; xx < px + pw; xx += 16) canvas.drawFastHLine(xx, yy, 9, c);
    return yy;
  };
  if (o.recipe && o.recipe->pourOver && o.recipe->target() > 0) {
    for (int k = 0; k < o.recipe->stageCount - 1; k++) {
      float v = o.recipe->stageTarget(k);
      int yy = hDashed(v, STROKE);
      int xx = px + (int)(o.recipe->stages[k + 1].atSec / tMax * pw);
      if (xx < px + pw) canvas.drawFastVLine(xx, yy, py + ph - yy, STROKE);
    }
  }
  if (o.target > 0 && o.target <= wMax) {
    int yy = hDashed(o.target, ACCENT_DIM);
    snprintf(b, sizeof(b), "target %g g", o.target);
    text(b, px + 10, yy + 16, F_AXIS, ACCENT_DIM, textdatum_t::middle_left);
  }

  auto Yw = [&](float w) { return py + ph - (int)(min(w, wMax) / wMax * ph); };
  auto Yf = [&](float f) { return py + ph - (int)(min(f, fMax) / fMax * ph); };

  // ghost (reference / last shot): thin muted line behind everything
  if (gn) {
    int prevX = -1, prevY = 0, gi = 0;
    for (int c = 0; c < pw; c += 3) {
      float w, f;
      if (!curveAt(g, gn, (float)c / pw * tMax, gi, w, f)) break;
      int xx = px + c, yy = Yw(w);
      if (prevX >= 0) canvas.drawLine(prevX, prevY, xx, yy, MUTED);
      prevX = xx; prevY = yy;
    }
  }
  if (n < 2) return;

  uint16_t lineC = o.dim ? ACCENT_DIM : ACCENT;
  uint16_t flowC = o.dim ? STROKE : FLOW;
  int lastCol = min(pw - 1, (int)(tEnd / tMax * pw));
  // flow line (no fill under the curves: just the lines)
  int i = 0, prevX = -1, prevYf = 0;
  for (int c = 0; c <= lastCol; c += 2) {
    float w, f;
    if (!curveAt(s, n, (float)c / pw * tMax, i, w, f)) break;
    int xx = px + c, yf = Yf(f);
    if (prevX >= 0) {   // 2 px plain line: AA lines are costly
      canvas.drawLine(prevX, prevYf, xx, yf, flowC);
      canvas.drawLine(prevX, prevYf + 1, xx, yf + 1, flowC);
    }
    prevX = xx; prevYf = yf;
  }
  // weight line on top
  int prevYw = 0;
  prevX = -1;
  i = 0;
  for (int c = 0; c <= lastCol; c += 3) {
    float w, f;
    if (!curveAt(s, n, (float)c / pw * tMax, i, w, f)) break;
    int xx = px + c, yw = Yw(w);
    if (prevX >= 0) canvas.drawWideLine(prevX, prevYw, xx, yw, 2.6f, lineC);
    prevX = xx; prevYw = yw;
  }
  // make sure the line reaches the newest sample
  if (prevX >= 0 && prevX < px + lastCol) {
    float w, f;
    int j = 0;
    if (curveAt(s, n, (float)lastCol / pw * tMax, j, w, f)) {
      int yw = Yw(w);
      canvas.drawWideLine(prevX, prevYw, px + lastCol, yw, 2.6f, lineC);
      prevX = px + lastCol; prevYw = yw;
    }
  }
  if (prevX >= 0 && o.liveMarker) {
    canvas.fillSmoothCircle(prevX, prevYw, 11, ACCENT_LO);
    canvas.fillSmoothCircle(prevX, prevYw, 6, ACCENT_HI);
  }
}

// ---------------------------------------------------------------------------
// misc
// ---------------------------------------------------------------------------
void beep(int f, int ms) {
  if (settings.sound) M5.Speaker.tone(f, ms);
}

void formatTimer(char* b, size_t n, float secs) {
  if (secs < 0) secs = 0;
  int m = (int)secs / 60;
  snprintf(b, n, "%d:%04.1f", m, secs - m * 60);
}

void formatDate(char* b, size_t n, uint32_t epoch, uint32_t id) {
  if (!epoch) { snprintf(b, n, "Shot #%lu", (unsigned long)id); return; }
  time_t e = epoch;
  struct tm t;
  localtime_r(&e, &t);
  strftime(b, n, "%a %d %b, %H:%M", &t);
}

}  // namespace ui
