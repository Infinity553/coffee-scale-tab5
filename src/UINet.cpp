// Wi-Fi & web screens and the on-screen keyboard.
#include "History.h"
#include "Net.h"
#include "Settings.h"
#include "UIKit.h"

namespace ui {

// ---------------------------------------------------------------------------
// Wi-Fi & web
// ---------------------------------------------------------------------------
static void qr(const char* content, int x, int y, int size) {
  canvas.fillSmoothRoundRect(x - 14, y - 14, size + 28, size + 28, 16, 0xFFFF);
  canvas.qrcode(content, x, y, size, 4);
}

static void saveNetwork(const std::string& ssid, const std::string& pass) {
  settings.wifiSsid = ssid.c_str();
  settings.wifiPass = pass.c_str();
  settings.wifiMode = WIFI_HOME;
  settings.wifiEnabled = true;
  settings.save();
  net::apply();
}

void drawWifi() {
  topBar("Wi-Fi & web", Screen::Settings);
  char b[128];

  // --- left: configuration ---
  const int lx = 24, lw = 600, top = 96;
  card(lx, top, lw, 600);
  text("CONNECTION", lx + 28, top + 34, F_LABEL, MUTED);
  const int rh = 84;
  int y = top + 56 + rh / 2;
  if (toggleRow(lx, y, lw, "Web access", "Browse and download your shots", settings.wifiEnabled))
    net::apply();
  divider(lx, y + rh / 2, lw); y += rh;

  settingRowLabel(lx + 28, y, "Connect via", nullptr);
  static const char* const modes[] = {"Home Wi-Fi", "Hotspot"};
  int m = segmented(lx + lw - 28, y, modes, 2, settings.wifiMode, 172);
  if (m >= 0 && m != settings.wifiMode) {
    settings.wifiMode = m;
    settings.save();
    net::apply();
  }
  divider(lx, y + rh / 2, lw); y += rh;

  if (settings.wifiMode == WIFI_HOME) {
    settingRowLabel(lx + 28, y, "Network",
                    settings.wifiSsid.length() ? settings.wifiSsid.c_str() : "Not chosen yet");
    if (button(lx + lw - 28 - 160, y - 30, 160, 60, "Choose", Btn::Secondary, true, F_LABEL)) {
      net::startScan();
      setScreen(Screen::WifiNetworks);
    }
    divider(lx, y + rh / 2, lw); y += rh;
    text("The Tab5 joins your home network. Any phone or", lx + 28, y, F_BODY, MUTED);
    text("computer on that network can open the web page.", lx + 28, y + 30, F_BODY, MUTED);
  } else {
    settingRowLabel(lx + 28, y, "Network name", nullptr);
    text(net::hotspotName(), lx + lw - 28, y, F_LABEL, HIGHLIGHT, textdatum_t::middle_right);
    divider(lx, y + rh / 2, lw); y += rh;
    settingRowLabel(lx + 28, y, "Password", nullptr);
    text(net::hotspotPassword(), lx + lw - 28, y, F_LABEL, HIGHLIGHT, textdatum_t::middle_right);
    divider(lx, y + rh / 2, lw); y += rh;
    text("The Tab5 makes its own Wi-Fi. Join it with your", lx + 28, y, F_BODY, MUTED);
    text("phone to open the web page, no router needed.", lx + 28, y + 30, F_BODY, MUTED);
  }

  // SD status
  if (history::available()) snprintf(b, sizeof(b), "SD card: %u shots saved", (unsigned)history::count());
  else snprintf(b, sizeof(b), "No SD card: shot history is off");
  canvas.fillSmoothCircle(lx + 36, top + 600 - 40, 6, history::available() ? GOOD : WARN);
  text(b, lx + 52, top + 600 - 40, F_BODY, MUTED);

  // --- right: status + how to open ---
  const int rx = 644, rw = W - 24 - rx;
  card(rx, top, rw, 600);
  text("STATUS", rx + 28, top + 34, F_LABEL, MUTED);
  net::Status s = net::status();
  uint16_t dot = !s.enabled ? STROKE : s.connected ? GOOD : s.passwordRejected ? BAD : WARN;
  const char* head;
  if (!s.enabled) head = "Web access is off";
  else if (s.hotspot) head = s.connected ? "Hotspot is on" : "Starting hotspot...";
  else if (s.connected) { snprintf(b, sizeof(b), "Connected to %s", s.ssid.c_str()); head = b; }
  else if (s.passwordRejected) { snprintf(b, sizeof(b), "Couldn't join %s", settings.wifiSsid.c_str()); head = b; }
  else if (settings.wifiSsid.length()) { snprintf(b, sizeof(b), "Connecting to %s...", settings.wifiSsid.c_str()); head = b; }
  else head = "Choose a network";
  canvas.fillSmoothCircle(rx + 38, top + 90, 8, dot);
  std::string headS = head;
  textFit(headS.c_str(), rx + 58, top + 90, rw - 86, F_BTN, TEXT);

  if (!s.enabled) {
    text("Turn on web access to see your shot", rx + 28, top + 150, F_BODY, MUTED);
    text("history on a phone or computer.", rx + 28, top + 180, F_BODY, MUTED);
    return;
  }
  if (!s.connected) {
    if (!s.problem.empty()) {
      text(s.problem.c_str(), rx + 28, top + 150, F_LABEL, s.passwordRejected ? BAD : WARN);
    }
    if (s.passwordRejected) {
      text("Check the password, including capital letters", rx + 28, top + 190, F_BODY, MUTED);
      text("and symbols. Tap Show to see what you typed.", rx + 28, top + 220, F_BODY, MUTED);
      if (button(rx + 28, top + 260, 300, 68, "Re-enter password", Btn::Primary, true, F_LABEL)) {
        std::string ssid = settings.wifiSsid.c_str();
        std::string title = "Password for " + ssid;
        openKeyboard(title.c_str(), settings.wifiPass.c_str(), 63, true,
                     [ssid](const std::string& pass) { saveNetwork(ssid, pass); }, Screen::Wifi);
      }
    } else if (!s.problem.empty()) {
      text("Still trying. Make sure the Tab5 is in range.", rx + 28, top + 190, F_BODY, MUTED);
    }
    return;
  }

  char url[64], url2[64];
  snprintf(url, sizeof(url), "http://%s", s.ip.c_str());
  snprintf(url2, sizeof(url2), "http://%s.local", net::hostname());
  const int qs = 220, qx = rx + 42, qy = top + 150;
  if (s.hotspot) {
    char join[96];
    snprintf(join, sizeof(join), "WIFI:T:WPA;S:%s;P:%s;;", net::hotspotName(), net::hotspotPassword());
    qr(join, qx, qy, qs);
    int tx = qx + qs + 40;
    text("1. Scan to join", tx, qy + 20, F_LABEL, TEXT);
    text(net::hotspotName(), tx, qy + 52, F_BODY, MUTED);
    text("2. Open in a browser", tx, qy + 110, F_LABEL, TEXT);
    text(url, tx, qy + 142, F_LABEL, HIGHLIGHT);
  } else {
    qr(url, qx, qy, qs);
    int tx = qx + qs + 40;
    text("Scan or open", tx, qy + 20, F_LABEL, TEXT);
    text(url2, tx, qy + 60, F_LABEL, HIGHLIGHT);
    text("or", tx, qy + 96, F_BODY, MUTED);
    text(url, tx, qy + 132, F_LABEL, HIGHLIGHT);
  }
  text("Sort by rating, open a shot for its full curve,", rx + 28, top + 450, F_BODY, MUTED);
  text("and download single shots or everything as", rx + 28, top + 480, F_BODY, MUTED);
  text("CSV or JSON.", rx + 28, top + 510, F_BODY, MUTED);
}

void drawNetworks() {
  topBar("Choose network", Screen::Wifi);
  const int lx = 260, lw = 760, ly = 100, rh = 76;
  auto list = net::networks();
  bool busy = net::scanning();
  card(lx, ly, lw, rh * 6 + 24);
  if (list.empty()) {
    char b[64];
    if (busy) snprintf(b, sizeof(b), "Looking for networks...");
    else if (net::lastScanResult() < 0) snprintf(b, sizeof(b), "The Wi-Fi scan didn't complete. Tap Scan again.");
    else snprintf(b, sizeof(b), "No networks found. Move closer to your router and scan again.");
    text(b, W / 2, ly + rh * 3, F_BODY, MUTED, textdatum_t::middle_center);
  }
  for (size_t i = 0; i < list.size() && i < 6; i++) {
    int ry = ly + 12 + i * rh;
    if (hit(lx + 12, ry, lw - 24, rh - 6)) {
      std::string ssid = list[i].ssid;
      if (list[i].secure) {
        std::string title = "Password for " + ssid;
        std::string initial = settings.wifiSsid == String(ssid.c_str()) ? settings.wifiPass.c_str() : "";
        openKeyboard(title.c_str(), initial, 63, true,
                     [ssid](const std::string& pass) { saveNetwork(ssid, pass); }, Screen::Wifi);
      } else {
        saveNetwork(ssid, "");
        setScreen(Screen::Wifi);
      }
      return;
    }
    if (flashing(lx + 12, ry)) canvas.fillSmoothRoundRect(lx + 12, ry, lw - 24, rh - 6, 14, SURFACE2);
    textFit(list[i].ssid.c_str(), lx + 44, ry + rh / 2 - 3, lw - 220, F_LABEL, TEXT);
    if (list[i].secure) iconLock(lx + lw - 120, ry + rh / 2 - 5, MUTED, SURFACE);
    rssiBars(lx + lw - 80, ry + rh / 2 + 12, list[i].rssi);
    if (i + 1 < list.size() && i < 5) canvas.drawFastHLine(lx + 40, ry + rh - 3, lw - 80, GRID);
  }
  if (button(W / 2 - 150, 600, 300, 72, busy ? "Scanning..." : "Scan again", Btn::Secondary, !busy))
    net::startScan();
}

// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------
static std::string kbTitle, kbText;
static size_t kbMax = 64;
static bool kbSecret = false, kbShow = false, kbShift = false;
static int kbPage = 0;   // 0 letters, 1 digits & symbols, 2 more symbols
static std::function<void(const std::string&)> kbDone;
static Screen kbReturn = Screen::Main;

void openKeyboard(const char* title, const std::string& initial, size_t maxLen, bool secret,
                  std::function<void(const std::string&)> done, Screen returnTo) {
  kbTitle = title;
  kbText = initial;
  kbMax = maxLen;
  kbSecret = secret;
  kbShow = false;
  kbShift = false;
  kbPage = 0;
  kbDone = std::move(done);
  kbReturn = returnTo;
  setScreen(Screen::Keyboard);
}

static void typeChar(char c) {
  if (kbText.size() < kbMax) kbText += c;
  kbShift = false;
}

static void keyRow(const char* keys, int x, int y, int kw, int kh, int gap) {
  for (int i = 0; keys[i]; i++) {
    char c = keys[i];
    if (kbShift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    char label[2] = {c, 0};
    if (button(x + i * (kw + gap), y, kw, kh, label, Btn::Secondary)) typeChar(c);
  }
}

void drawKeyboard() {
  topBar(kbTitle.c_str(), kbReturn);
  if (currentScreen() != Screen::Keyboard) return;   // cancelled

  // input field
  const int fx = 24, fy = 100, fw = W - 48, fh = 84;
  canvas.fillSmoothRoundRect(fx, fy, fw, fh, 20, ACCENT);
  canvas.fillSmoothRoundRect(fx + 2, fy + 2, fw - 4, fh - 4, 18, SURFACE);
  std::string shown = (kbSecret && !kbShow) ? std::string(kbText.size(), '*') : kbText;
  int maxW = fw - (kbSecret ? 200 : 60);
  while (!shown.empty() && textWidth(shown.c_str(), F_BTN) > maxW) shown.erase(0, 1);
  text(shown.c_str(), fx + 28, fy + fh / 2, F_BTN, TEXT);
  int cx = fx + 30 + textWidth(shown.c_str(), F_BTN);
  canvas.fillRect(cx, fy + 22, 3, fh - 44, ACCENT);   // steady cursor: no redraws needed
  if (kbSecret && button(fx + fw - 150, fy + 14, 130, fh - 28, kbShow ? "Hide" : "Show",
                         Btn::Ghost, true, F_LABEL))
    kbShow = !kbShow;

  // keys
  const int kw = 100, kh = 100, gap = 12, x0 = 24;
  const int y1 = 214, y2 = y1 + kh + 14, y3 = y2 + kh + 14, y4 = y3 + kh + 14;
  if (kbPage == 0) {
    keyRow("qwertyuiop", x0, y1, kw, kh, gap);
    keyRow("asdfghjkl", x0 + 56, y2, kw, kh, gap);
    keyRow("zxcvbnm", x0 + 168, y3, kw, kh, gap);
    if (button(x0, y3, 156, kh, kbShift ? "SHIFT" : "Shift", kbShift ? Btn::Primary : Btn::Secondary,
               true, F_LABEL))
      kbShift = !kbShift;
    keyRow("-.", x0 + 168 + 7 * (kw + gap), y3, kw, kh, gap);
  } else if (kbPage == 1) {
    keyRow("1234567890", x0, y1, kw, kh, gap);
    keyRow("@#$%&*()'", x0 + 56, y2, kw, kh, gap);
    keyRow("!?+=/:;,\"_", x0 + 56, y3, kw, kh, gap);
  } else {
    keyRow("[]{}<>^~`|", x0, y1, kw, kh, gap);
    keyRow("\\-._", x0 + 56, y2, kw, kh, gap);
  }
  // backspace
  int bx = x0 + 10 * (kw + gap);
  if (button(bx, y1, W - 24 - bx, kh, "Del", Btn::Secondary, !kbText.empty(), F_LABEL))
    kbText.pop_back();
  // bottom row
  static const char* const pageKey[] = {"123", "#+=", "ABC"};
  if (button(x0, y4, 200, kh - 10, pageKey[kbPage], Btn::Secondary, true, F_LABEL))
    kbPage = (kbPage + 1) % 3;
  if (button(x0 + 212, y4, W - 48 - 212 - 272, kh - 10, "space", Btn::Secondary, true, F_LABEL))
    typeChar(' ');
  if (button(W - 24 - 260, y4, 260, kh - 10, "Done", Btn::Primary)) {
    auto cb = kbDone;
    std::string result = kbText;
    setScreen(kbReturn);
    if (cb) cb(result);
  }
}

}  // namespace ui
