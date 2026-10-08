#include "DialIn.h"
#include <cmath>
#include "Settings.h"

namespace dialin {

static constexpr float GRIND_STEP = 0.5f;   // the grind stepper's step

static float roundStep(float g) { return roundf(g / GRIND_STEP) * GRIND_STEP; }

// Linear fit of shot time over grind setting, from earlier shots of the same
// recipe and (about) the same dose. slope = seconds per grind unit.
struct GrindModel {
  bool  ok = false;
  float slope = 0;
  int   n = 0;
};

static GrindModel learnGrinder(const ShotMeta& shot) {
  GrindModel m;
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  float gMin = 1e9f, gMax = -1e9f;
  int n = 0;
  for (const auto& s : history::list(false)) {   // most recent first
    if (n >= 30) break;
    if (s.recipe != shot.recipe || s.grind < 0 || s.time <= 0) continue;
    if (fabsf(s.dose - shot.dose) > 0.6f) continue;
    sx += s.grind; sy += s.time;
    sxx += s.grind * s.grind; syy += s.time * s.time; sxy += s.grind * s.time;
    gMin = fminf(gMin, s.grind); gMax = fmaxf(gMax, s.grind);
    n++;
  }
  m.n = n;
  if (n < 4 || gMax - gMin < GRIND_STEP) return m;   // needs at least two settings
  double vx = sxx - sx * sx / n, vy = syy - sy * sy / n, cxy = sxy - sx * sy / n;
  if (vx <= 0 || vy <= 0) return m;
  double r = cxy / sqrt(vx * vy);
  m.slope = (float)(cxy / vx);
  m.ok = fabs(r) >= 0.5 && fabsf(m.slope) >= 0.2f;
  return m;
}

bool targetWindow(const ShotMeta& shot, const Recipe& r, float& lo, float& hi, bool& fromReference) {
  fromReference = false;
  ShotMeta ref;
  if (settings.refShotId && settings.refShotId != shot.id && history::get(settings.refShotId, ref) &&
      ref.recipe == shot.recipe && ref.time > 0) {
    lo = ref.time - 2;
    hi = ref.time + 2;
    fromReference = true;
    return true;
  }
  if (r.timeMax <= 0) return false;
  lo = r.timeMin;
  hi = r.timeMax;
  return true;
}

static std::string fmt(const char* f, double a = 0, double b = 0, double c = 0) {
  char buf[160];
  snprintf(buf, sizeof(buf), f, a, b, c);
  return buf;
}

Advice advise(const ShotMeta& shot, const Recipe& r) {
  Advice a;
  float lo, hi;
  bool fromRef;
  if (!targetWindow(shot, r, lo, hi, fromRef) || shot.time <= 0) return a;
  a.valid = true;
  const float center = (lo + hi) / 2;
  const bool fast = shot.time < lo, slow = shot.time > hi;

  // taste first where it changes the diagnosis
  if (fast && shot.taste == TASTE_BITTER) {
    a.headline = "Fast but bitter: likely channeling";
    a.detail = "Check distribution and tamping before changing the grind.";
    return a;
  }
  if (slow && shot.taste == TASTE_SOUR) {
    a.headline = "Slow but sour: uneven extraction";
    a.detail = "Check puck prep, or the beans may be too fresh or too old.";
    return a;
  }

  if (fast || slow) {
    float need = center - shot.time;                 // + = needs to run longer
    int secs = (int)roundf(fabsf(fast ? lo - shot.time : shot.time - hi));
    if (secs < 1) secs = 1;
    a.headline = fmt(fast ? "Ran %.0f s fast: grind finer" : "Ran %.0f s slow: grind coarser", secs);
    if (shot.grind < 0) {
      a.detail = "Set the grind below to get an exact suggestion.";
      return a;
    }
    GrindModel m = learnGrinder(shot);
    float delta;
    if (m.ok) {
      delta = need / m.slope;                        // grind units to move
      delta = fmaxf(-3, fminf(3, delta));
      delta = roundStep(delta);
      // at least one step, in the direction that moves the time towards the window
      if (fabsf(delta) < GRIND_STEP) delta = (need / m.slope) > 0 ? GRIND_STEP : -GRIND_STEP;
    } else {
      float steps = fminf(3, fmaxf(1, roundf(fabsf(need) / 4)));   // ~4 s per step as a start
      float finerSign = settings.finerIsLower ? -1 : 1;
      delta = (need > 0 ? finerSign : -finerSign) * steps * GRIND_STEP;
    }
    a.grind = fmaxf(0, shot.grind + delta);
    char b[160];
    if (m.ok) {
      snprintf(b, sizeof(b), "Try %.1f (now %.1f): about %+.0f s, learned from %d of your shots.",
               a.grind, shot.grind, delta * m.slope, m.n);
    } else {
      snprintf(b, sizeof(b), "Try %.1f (now %.1f). It learns your grinder after a few shots "
               "at different settings.", a.grind, shot.grind);
    }
    a.detail = b;
    return a;
  }

  // on time: taste decides
  switch (shot.taste) {
    case TASTE_SOUR:
      if (r.ratio > 0) {
        a.ratio = roundf((r.ratio + (r.pourOver ? 0.5f : 0.2f)) * 10) / 10;
        a.headline = "On time but sour: extract more";
        a.detail = fmt("Try a longer ratio 1:%.1f (%.0f g out).", a.ratio, r.dose * a.ratio);
      } else {
        a.headline = "On time but sour: extract more";
        a.detail = "Grind a little finer or let it run longer.";
      }
      return a;
    case TASTE_BITTER:
      if (r.ratio > 0) {
        a.ratio = roundf((r.ratio - (r.pourOver ? 0.5f : 0.2f)) * 10) / 10;
        a.headline = "On time but bitter: extract less";
        a.detail = fmt("Try a shorter ratio 1:%.1f (%.0f g out).", a.ratio, r.dose * a.ratio);
      } else {
        a.headline = "On time but bitter: extract less";
        a.detail = "Grind a little coarser or stop earlier.";
      }
      return a;
    case TASTE_BALANCED:
      a.good = true;
      a.headline = "Dialed in";
      a.detail = "Keep these settings.";
      if (settings.refShotId != shot.id) a.detail += " Consider using it as your reference.";
      return a;
    default:
      if (shot.inBand >= 0 && shot.inBand < 50) {
        a.headline = "On time, but the flow was uneven";
        a.detail = fmt("Only %.0f %% inside the flow band: check distribution and tamping.", shot.inBand);
        return a;
      }
      a.good = true;
      a.headline = fmt("On time (%.0f-%.0f s)", lo, hi);
      a.detail = fromRef ? "Same as your reference. How did it taste?"
                         : "How did it taste? Pick sour, balanced or bitter above.";
      return a;
  }
}

}  // namespace dialin
