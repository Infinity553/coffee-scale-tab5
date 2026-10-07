// Numeric keypad: type a value instead of stepping with - / +.
// One field (e.g. dose) or two (e.g. shot-time window From / To).
#include <cstdlib>
#include <cstring>
#include "UIKit.h"

namespace ui {

static std::string kpTitle, kpHint, kpError;
static std::vector<NumField> kpFields;
static std::vector<std::string> kpText;
static std::vector<bool> kpSelected;   // whole value selected: the next digit replaces it
static int kpActive = 0;
static std::function<std::string(const std::vector<float>&)> kpApply;
static Screen kpReturn = Screen::Main;

static std::string fmtNum(float v, int decimals) {
  char b[24];
  snprintf(b, sizeof(b), "%.*f", decimals, v);
  return b;
}

void openKeypad(const char* title, const char* hint, std::vector<NumField> fields,
                std::function<std::string(const std::vector<float>&)> apply, Screen returnTo) {
  kpTitle = title;
  kpHint = hint ? hint : "";
  kpError.clear();
  kpFields = std::move(fields);
  kpText.clear();
  kpSelected.clear();
  for (auto& f : kpFields) {
    kpText.push_back(fmtNum(f.value, f.decimals));
    kpSelected.push_back(true);
  }
  kpActive = 0;
  kpApply = std::move(apply);
  kpReturn = returnTo;
  setScreen(Screen::Keypad);
}

static void typeKey(char c) {
  kpError.clear();
  const NumField& f = kpFields[kpActive];
  std::string& t = kpText[kpActive];
  if (kpSelected[kpActive]) { t.clear(); kpSelected[kpActive] = false; }
  size_t dot = t.find('.');
  if (c == '.') {
    if (f.decimals == 0 || dot != std::string::npos) return;
    if (t.empty()) t = "0";
  } else if (dot != std::string::npos && (int)(t.size() - dot - 1) >= f.decimals) {
    return;   // no more decimals than the value has
  }
  if (t.size() >= 6) return;
  if (t == "0" && c != '.') t.clear();   // no leading zeros
  t += c;
}

static bool parse(int i, float& v) {
  const std::string& t = kpText[i];
  if (t.empty() || t == ".") return false;
  v = strtof(t.c_str(), nullptr);
  return true;
}

static void done() {
  std::vector<float> values;
  for (size_t i = 0; i < kpFields.size(); i++) {
    const NumField& f = kpFields[i];
    float v;
    char b[96];
    const char* who = kpFields.size() > 1 ? f.label : "a value";
    if (!parse(i, v)) {
      snprintf(b, sizeof(b), "Enter %s%s", kpFields.size() > 1 ? "a value for " : "", who);
      kpError = b;
      kpActive = i;
      return;
    }
    if (v < f.min - 1e-4f || v > f.max + 1e-4f) {
      snprintf(b, sizeof(b), "%s%s must be between %s and %s%s%s", kpFields.size() > 1 ? f.label : "The value",
               "", fmtNum(f.min, f.decimals).c_str(), fmtNum(f.max, f.decimals).c_str(),
               *f.unit ? " " : "", f.unit);
      kpError = b;
      kpActive = i;
      kpSelected[i] = true;
      return;
    }
    values.push_back(v);
  }
  std::string err = kpApply ? kpApply(values) : "";
  if (!err.empty()) { kpError = err; return; }
  setScreen(kpReturn);
}

static void drawField(int i, int x, int y, int w, int h) {
  const NumField& f = kpFields[i];
  bool active = i == kpActive;
  if (hit(x, y, w, h)) { kpActive = i; kpError.clear(); active = true; }
  canvas.fillSmoothRoundRect(x, y, w, h, 20, active ? ACCENT : STROKE);
  canvas.fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, 18, SURFACE);
  if (kpFields.size() > 1) text(f.label, x + 24, y + 26, F_LABEL, active ? ACCENT : MUTED);

  // value in segment digits: prefix + typed text, unit after it
  const float dh = 58;
  std::string shown = std::string(f.prefix) + kpText[i];
  float tw = segTextWidth(shown.c_str(), dh);
  int uw = *f.unit ? textWidth(f.unit, F_BTN) + 12 : 0;
  float sx = x + (w - tw - uw) / 2;
  float sy = y + h - dh - 22;
  if (kpSelected[i] && !kpText[i].empty()) {
    float pw = segTextWidth(f.prefix, dh) + (*f.prefix ? dh * 0.11f : 0);
    canvas.fillSmoothRoundRect(sx + pw - 8, sy - 8, tw - pw + 16, dh + 16, 10, ACCENT_LO);
  }
  segText(shown.c_str(), sx, sy, dh, TEXT, false);
  if (*f.unit) text(f.unit, sx + tw + 12, sy + dh - 16, F_BTN, MUTED);
  if (active && !kpSelected[i]) canvas.fillRect(sx + tw + 4 + uw, sy + 4, 3, dh - 8, ACCENT);
}

void drawKeypad() {
  topBar(kpTitle.c_str(), kpReturn);
  if (currentScreen() != Screen::Keypad) return;   // cancelled

  // fields
  const int n = (int)kpFields.size();
  const int fw = n > 1 ? 400 : 520, fh = 120, gap = 40, fy = 96;
  int fx = (W - (n * fw + (n - 1) * gap)) / 2;
  for (int i = 0; i < n; i++) drawField(i, fx + i * (fw + gap), fy, fw, fh);

  // hint or error
  if (!kpError.empty()) {
    text(kpError.c_str(), W / 2, fy + fh + 26, F_LABEL, BAD, textdatum_t::middle_center);
  } else {
    const NumField& f = kpFields[kpActive];
    char b[128];
    snprintf(b, sizeof(b), "%s%s%s to %s%s%s", kpHint.c_str(), kpHint.empty() ? "" : "  |  ",
             fmtNum(f.min, f.decimals).c_str(), fmtNum(f.max, f.decimals).c_str(),
             *f.unit ? " " : "", f.unit);
    text(b, W / 2, fy + fh + 26, F_BODY, MUTED, textdatum_t::middle_center);
  }

  // keys
  const int kw = 150, kh = 84, kg = 10;
  const int gridW = 3 * kw + 2 * kg, colW = 220;
  const int x0 = (W - (gridW + 16 + colW)) / 2, y0 = fy + fh + 50;
  static const char* const keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "Del"};
  const NumField& f = kpFields[kpActive];
  for (int k = 0; k < 12; k++) {
    int kx = x0 + (k % 3) * (kw + kg), ky = y0 + (k / 3) * (kh + kg);
    bool isDot = k == 9, isDel = k == 11;
    bool enabled = !(isDot && f.decimals == 0) && !(isDel && kpText[kpActive].empty());
    if (button(kx, ky, kw, kh, keys[k], Btn::Secondary, enabled, isDel ? F_LABEL : F_TITLE)) {
      if (isDel) {
        std::string& t = kpText[kpActive];
        if (kpSelected[kpActive]) t.clear(); else if (!t.empty()) t.pop_back();
        kpSelected[kpActive] = false;
        kpError.clear();
      } else {
        typeKey(keys[k][0]);
      }
    }
  }
  const int cx = x0 + gridW + 16;
  if (button(cx, y0, colW, kh, "Clear", Btn::Ghost, true, F_LABEL)) {
    kpText[kpActive].clear();
    kpSelected[kpActive] = false;
    kpError.clear();
  }
  if (button(cx, y0 + kh + kg, colW, 3 * kh + 2 * kg, "Done", Btn::Primary)) done();
}

}  // namespace ui
