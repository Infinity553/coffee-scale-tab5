// Shot history: list (recent / rating) + detail with curve, rating and actions.
//
// Drawing is expensive on the Tab5 (every pixel goes through the rotated canvas),
// so this screen avoids work: the sorted list is cached, the detail plot is drawn
// once per shot and then restored from a pixel cache, and a tap only redraws the
// panel(s) it actually changed.
#include "History.h"
#include "Net.h"
#include "Settings.h"
#include "UIKit.h"

namespace ui {

static bool byRating = false;
static int page = 0;
static uint32_t selectedId = 0;
static uint32_t deleteArmedId = 0;
static constexpr int ROWS = 5;

static const Rect R_LIST   = {24, 96, 540, 600};
static const Rect R_DETAIL = {584, 96, 1280 - 24 - 584, 600};

// --- timing (printed after every History frame) ---
static uint32_t tList, tRows, tDetail, tPlot, tLoad;
static bool plotFromCache;

static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

// ---------------------------------------------------------------------------
// cached data
// ---------------------------------------------------------------------------
static std::vector<ShotMeta> listCache;
static uint32_t listVer = 0;
static bool listByRating = false;
static bool listValid = false;

static std::vector<ShotMeta>& shots() {
  if (!listValid || listVer != history::version() || listByRating != byRating) {
    uint32_t t0 = micros();
    listCache = history::list(byRating);
    listVer = history::version();
    listByRating = byRating;
    listValid = true;
    tList += micros() - t0;
  }
  return listCache;
}

static ShotMeta* selected() {
  for (auto& m : shots())
    if (m.id == selectedId) return &m;
  return nullptr;
}

static uint32_t curveId = 0;
static std::vector<BrewSample> curve;
static std::vector<BrewSample> refCurve;
static uint32_t refCurveId = 0;
static std::string refRecipe;

// plot pixels, restored instead of re-rendering the curve
static std::vector<uint16_t> plotPixels;
static uint32_t plotKey = 0;

// ---------------------------------------------------------------------------
// list panel
// ---------------------------------------------------------------------------
static void drawRow(const ShotMeta& m, int x, int y, int w, int h, bool sel) {
  if (sel) {
    canvas.fillSmoothRoundRect(x, y, w, h, 16, ACCENT);
    canvas.fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, 14, SURFACE2);
  }
  char b[80], d[40], t[16];
  formatDate(d, sizeof(d), m.epoch, m.id);
  text(d, x + 22, y + 30, F_LABEL, TEXT);
  formatTimer(t, sizeof(t), m.time);
  if (m.dose > 0) snprintf(b, sizeof(b), "%s  |  %.1f g in, %.1f g out  |  %s", m.recipe.c_str(), m.dose, m.yield, t);
  else snprintf(b, sizeof(b), "%s  |  %.1f g  |  %s", m.recipe.c_str(), m.yield, t);
  textFit(b, x + 22, y + 62, w - 44, F_AXIS, MUTED);
  drawStars(x + w - 150, y + 30, 22, m.rating);
  if (m.id == settings.refShotId) chip(x + w - 160, y + 30, "REF", ON_ACCENT, ACCENT, textdatum_t::middle_right);
}

static uint32_t sigList();
static uint32_t sigDetail();
static uint32_t listDrawnSig = 0, detailDrawnSig = 0;   // state each panel last showed

static void drawList() {
  uint32_t t0 = micros();
  const int lx = R_LIST.x, lw = R_LIST.w, ly = R_LIST.y;
  const int rh = 92, pagerY = ly + R_LIST.h - 58;

  // handle row and pager taps first, so the list is drawn once, already updated
  {
    auto& pre = shots();
    int pages = max(1, (int)((pre.size() + ROWS - 1) / ROWS));
    for (int i = 0; i < ROWS; i++) {
      size_t k = page * ROWS + i;
      if (k < pre.size() && hit(lx + 12, ly + 80 + i * (rh + 4), lw - 24, rh)) {
        selectedId = pre[k].id;
        deleteArmedId = 0;
      }
    }
    if (page > 0 && hit(lx + 20, pagerY, 64, 48)) page--;
    if (page < pages - 1 && hit(lx + lw - 84, pagerY, 64, 48)) page++;
  }

  card(lx, ly, lw, R_LIST.h);
  char b[48];
  snprintf(b, sizeof(b), "%u SHOTS", (unsigned)shots().size());
  text(b, lx + 28, ly + 40, F_LABEL, MUTED);
  static const char* const sorts[] = {"Recent", "Rating"};
  int s = segmented(lx + lw - 20, ly + 40, sorts, 2, byRating ? 1 : 0, 120);
  if (s >= 0 && (s == 1) != byRating) {
    byRating = s == 1;
    page = 0;
  }
  auto& sorted = shots();

  if (sorted.empty()) {
    text("No shots yet.", lx + lw / 2, ly + 260, F_BTN, TEXT, textdatum_t::middle_center);
    text("They're saved here when a shot finishes.", lx + lw / 2, ly + 304, F_BODY, MUTED,
         textdatum_t::middle_center);
  }
  int pages = max(1, (int)((sorted.size() + ROWS - 1) / ROWS));
  page = constrain(page, 0, pages - 1);
  if (!selectedId && !sorted.empty()) selectedId = sorted[0].id;

  listDrawnSig = sigList();
  for (int i = 0; i < ROWS; i++) {
    size_t k = page * ROWS + i;
    if (k >= sorted.size()) break;
    drawRow(sorted[k], lx + 12, ly + 80 + i * (rh + 4), lw - 24, rh, sorted[k].id == selectedId);
  }
  // pager taps were handled above; these only draw (pressed look included)
  button(lx + 20, pagerY, 64, 48, "<", Btn::Secondary, page > 0, F_LABEL);
  snprintf(b, sizeof(b), "Page %d of %d", page + 1, pages);
  text(b, lx + lw / 2, pagerY + 24, F_BODY, MUTED, textdatum_t::middle_center);
  button(lx + lw - 84, pagerY, 64, 48, ">", Btn::Secondary, page < pages - 1, F_LABEL);
  tRows += micros() - t0;
}

// ---------------------------------------------------------------------------
// detail panel
// ---------------------------------------------------------------------------
static void drawPlotCached(const ShotMeta& m, int x, int y, int w) {
  uint32_t t0 = micros();
  if (curveId != m.id) {
    curveId = m.id;
    history::loadCurve(m.id, curve);
  }
  if (settings.refShotId && settings.refShotId != m.id && refCurveId != settings.refShotId) {
    refCurveId = settings.refShotId;
    history::loadCurve(refCurveId, refCurve);
    ShotMeta rm;
    refRecipe = history::get(refCurveId, rm) ? rm.recipe : "";
  }
  tLoad += micros() - t0;

  bool showRef = settings.refShotId && settings.refShotId != m.id && refCurve.size() > 1 &&
                 refRecipe == m.recipe;
  const int px = x + 64, py = y + 126, pw = w - 64 - 56, ph = 220;
  // plot incl. axis labels; inside the card, so the background is plain SURFACE
  const Rect area = {px - 62, py - 40, pw + 62 + 50, ph + 40 + 36};

  uint32_t key = mix(mix(mix(mix(2166136261u, m.id), showRef ? refCurveId : 0),
                         settings.colorScheme), (uint32_t)curve.size());
  t0 = micros();
  plotFromCache = key == plotKey && plotPixels.size() == (size_t)area.w * area.h;
  if (plotFromCache) {
    canvas.pushImage(area.x, area.y, area.w, area.h, plotPixels.data());
  } else {
    float maxW = 0, maxF = 0;
    for (auto& s : curve) { maxW = max(maxW, s.w); maxF = max(maxF, s.f); }
    PlotOpts o;
    o.ghost = showRef ? &refCurve : nullptr;
    drawPlotArea(px, py, pw, ph, curve.data(), (int)curve.size(), maxW, maxF, o);
    if (showRef) text("grey line: reference shot", px + pw, py - 24, F_AXIS, MUTED, textdatum_t::middle_right);
    if (curve.size() < 2) text("Curve not available", px + pw / 2, py + ph / 2, F_BODY, MUTED, textdatum_t::middle_center);
    plotPixels.resize((size_t)area.w * area.h);
    canvas.readRect(area.x, area.y, area.w, area.h, plotPixels.data());
    plotKey = key;
  }
  tPlot += micros() - t0;
}

static void drawDetailPanel() {
  uint32_t t0 = micros();
  const int x = R_DETAIL.x, y = R_DETAIL.y, w = R_DETAIL.w, h = R_DETAIL.h;
  detailDrawnSig = sigDetail();
  card(x, y, w, h);
  ShotMeta* sel = selected();
  if (!sel) {
    text("Select a shot", x + w / 2, y + h / 2, F_BODY, MUTED, textdatum_t::middle_center);
    tDetail += micros() - t0;
    return;
  }
  ShotMeta m = *sel;   // copy: actions below may change the list
  char b[96], d[40];
  formatDate(d, sizeof(d), m.epoch, m.id);
  text(m.recipe.c_str(), x + 28, y + 34, F_LABEL, ACCENT);
  text(d, x + 28, y + 70, F_BTN, TEXT);

  drawPlotCached(m, x, y, w);

  // figures
  int fy = y + 126 + 220 + 56;
  char t[16];
  formatTimer(t, sizeof(t), m.time);
  struct { const char* l; char v[24]; } f[5];
  int n = 0;
  f[n].l = "TIME"; snprintf(f[n++].v, 24, "%s", t);
  f[n].l = "YIELD"; snprintf(f[n++].v, 24, "%.1f g", m.yield);
  f[n].l = "RATIO"; if (m.dose > 0) snprintf(f[n].v, 24, "1:%.1f", m.ratio()); else snprintf(f[n].v, 24, "-"); n++;
  f[n].l = "PEAK FLOW"; snprintf(f[n++].v, 24, "%.1f g/s", m.peakFlow);
  f[n].l = "GRIND"; if (m.grind >= 0) snprintf(f[n].v, 24, "%.1f", m.grind); else snprintf(f[n].v, 24, "-"); n++;
  int colW = (w - 56) / n;
  for (int i = 0; i < n; i++) {
    text(f[i].l, x + 28 + i * colW, fy, F_AXIS, MUTED);
    text(f[i].v, x + 28 + i * colW, fy + 28, F_BTN, TEXT);
  }
  if (!m.notes.empty()) {
    snprintf(b, sizeof(b), "\"%s\"", m.notes.c_str());
    textFit(b, x + 28, fy + 64, w - 56, F_BODY, MUTED);
  }

  // rating (top right)
  text("YOUR RATING", x + w - 28, y + 34, F_AXIS, MUTED, textdatum_t::middle_right);
  int nr = starsInput(x + w - 28 - 5 * 46 + 6, y + 70, 38, m.rating);
  if (nr >= 0) { m.rating = nr; history::update(m); }

  // actions
  int ay = y + h - 84;
  int bx = x + w - 28;
  bool armed = deleteArmedId == m.id;
  bx -= 150;
  if (button(bx, ay, 150, 64, armed ? "Confirm" : "Delete", Btn::Danger, true, F_LABEL)) {
    if (armed) {
      history::remove(m.id);
      selectedId = 0;
      deleteArmedId = 0;
      requestRedraw();
      tDetail += micros() - t0;
      return;
    }
    deleteArmedId = m.id;
  }
  bx -= 160;
  if (button(bx, ay, 148, 64, "Notes", Btn::Secondary, true, F_LABEL)) {
    uint32_t id = m.id;
    openKeyboard("Notes for this shot", m.notes, 80, false, [id](const std::string& s) {
      ShotMeta mm;
      if (history::get(id, mm)) { mm.notes = s; history::update(mm); }
    }, Screen::History);
  }
  bool isRef = settings.refShotId == m.id;
  bx -= 196;
  if (button(bx, ay, 184, 64, isRef ? "Reference" : "Set reference", isRef ? Btn::Primary : Btn::Secondary,
             true, F_LABEL)) {
    history::setReference(isRef ? 0 : m.id);
  }
  tDetail += micros() - t0;
}

// ---------------------------------------------------------------------------
// screen
// ---------------------------------------------------------------------------
static void resetTimes() { tList = tRows = tDetail = tPlot = tLoad = 0; plotFromCache = false; }

static void reportTimes(const char* kind, uint32_t pushUs) {
  Serial.printf("[HIST] %s: list %lu ms, rows %lu ms, detail %lu ms (plot %s %lu ms, curve load %lu ms), push %lu ms\n",
                kind, (unsigned long)(tList / 1000), (unsigned long)(tRows / 1000),
                (unsigned long)(tDetail / 1000), plotFromCache ? "cached" : "drawn",
                (unsigned long)(tPlot / 1000), (unsigned long)(tLoad / 1000),
                (unsigned long)(pushUs / 1000));
}

void drawHistory() {
  resetTimes();
  topBar("History", Screen::Main);
  if (!history::available()) {
    card(240, 180, 800, 360);
    canvas.fillSmoothCircle(W / 2, 290, 54, SURFACE2);
    iconHistory(W / 2, 290, MUTED, SURFACE2);
    text("Insert an SD card to keep a shot history", W / 2, 390, F_BTN, TEXT, textdatum_t::middle_center);
    text("Shots, ratings and notes are saved on the Tab5's microSD card.", W / 2, 440, F_BODY,
         MUTED, textdatum_t::middle_center);
    text("The card is detected automatically.", W / 2, 472, F_BODY, MUTED, textdatum_t::middle_center);
    return;
  }
  drawList();
  drawDetailPanel();
  reportTimes("full screen", 0);
}

// What each panel shows, to find out which panels a tap changed.
static uint32_t sigList() {
  uint32_t h = 2166136261u;
  h = mix(h, page); h = mix(h, byRating); h = mix(h, selectedId);
  h = mix(h, history::version()); h = mix(h, settings.refShotId);
  return h;
}

static uint32_t sigDetail() {
  uint32_t h = 2166136261u;
  h = mix(h, selectedId); h = mix(h, history::version()); h = mix(h, settings.refShotId);
  h = mix(h, deleteArmedId);
  return h;
}

static bool inside(const Rect& r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void clearRect(const Rect& r) { canvas.fillRect(r.x, r.y, r.w, r.h, BG); }

bool historyPartialUpdate(bool flashEnd, uint32_t& drawUs, uint32_t& pushUs) {
  if (!history::available()) return false;
  int tx = -1, ty = -1;
  bool tapped = hasTap();
  if (tapped) tapPosition(tx, ty);
  else if (flashEnd) flashPoint(tx, ty);
  bool inList = inside(R_LIST, tx, ty), inDetail = inside(R_DETAIL, tx, ty);
  if ((tapped || flashEnd) && !inList && !inDetail) return false;   // top bar etc.: full repaint
  // no tap: only refresh panels whose content changed (e.g. a rating from the web UI)
  if (!tapped && !flashEnd && sigList() == listDrawnSig && sigDetail() == detailDrawnSig) return false;

  resetTimes();
  uint32_t t0 = micros();
  bool drewList = false, drewDetail = false;
  if (inList) { clearRect(R_LIST); drawList(); drewList = true; }
  else if (inDetail) { clearRect(R_DETAIL); drawDetailPanel(); drewDetail = true; }
  clearTap();
  if (currentScreen() != Screen::History) return true;   // e.g. opened the keyboard
  // a tap can change what the other panel (or a part drawn earlier) shows
  for (int pass = 0; pass < 2; pass++) {
    if (sigList() != listDrawnSig) { clearRect(R_LIST); drawList(); drewList = true; }
    if (sigDetail() != detailDrawnSig) { clearRect(R_DETAIL); drawDetailPanel(); drewDetail = true; }
  }
  uint32_t t1 = micros();
  drawUs += t1 - t0;
  if (drewList) pushRect(R_LIST);
  if (drewDetail) pushRect(R_DETAIL);
  uint32_t p = micros() - t1;
  pushUs += p;
  reportTimes(drewList && drewDetail ? "list+detail" : drewList ? "list" : "detail", p);
  return true;
}

}  // namespace ui
