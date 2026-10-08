#include "Brew.h"
#include "AcaiaScale.h"
#include "Recipes.h"
#include "Settings.h"
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif

Brew brew;

static constexpr float    AUTO_TARE_MIN   = 5.0f;   // g: weight change that counts as cup on/off
static constexpr uint32_t AUTO_TARE_SETTLE= 600;    // ms of stable weight before auto tare
static constexpr uint32_t TARE_TIMEOUT    = 3000;
static constexpr float    STABLE_RANGE    = 0.25f;  // g peak-to-peak
static constexpr uint32_t STABLE_WINDOW   = 800;    // ms
static constexpr uint32_t CUP_CHECK_MS    = 2000;   // classify fast jumps as cup placement
static constexpr float    CUP_RATE        = 40.0f;  // g/s
static constexpr float    CUP_MIN_JUMP    = 10.0f;  // g
static constexpr float    RISE_STEP       = 0.3f;   // g: counts as "still flowing"
static constexpr float    MIN_SHOT_YIELD  = 2.0f;   // g before auto stop may trigger
static constexpr uint32_t MIN_SHOT_MS     = 5000;

void Brew::begin() {
#ifdef MALLOC_CAP_SPIRAM
  samples_ = (BrewSample*)heap_caps_malloc(sizeof(BrewSample) * MAX_SAMPLES,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  if (!samples_) samples_ = (BrewSample*)malloc(sizeof(BrewSample) * MAX_SAMPLES);
  count_ = 0;
}

float Brew::timerSeconds(uint32_t ms) const {
  float t = timerAccum_;
  if (timerRunning_) t += (ms - timerStartMs_) / 1000.0f;
  return t;
}

void Brew::updateStability(float w, uint32_t ms) {
  histW_[histHead_] = w;
  histT_[histHead_] = ms;
  histHead_ = (histHead_ + 1) % HIST;
  if (histN_ < HIST) histN_++;

  float lo = w, hi = w;
  uint32_t span = 0;
  for (int i = 0; i < histN_; i++) {
    int idx = (histHead_ - 1 - i + HIST) % HIST;
    uint32_t age = ms - histT_[idx];
    if (age > STABLE_WINDOW) break;
    lo = min(lo, histW_[idx]);
    hi = max(hi, histW_[idx]);
    span = age;
  }
  bool nowStable = (hi - lo) < STABLE_RANGE && span >= STABLE_WINDOW * 3 / 4;
  if (nowStable && !stable_) stableSinceMs_ = ms;
  stable_ = nowStable;

  // flow over ~1 s, lightly smoothed
  uint32_t ago = 0;
  float wOld = w;
  for (int i = 0; i < histN_; i++) {
    int idx = (histHead_ - 1 - i + HIST) % HIST;
    wOld = histW_[idx];
    ago = ms - histT_[idx];
    if (ago >= 1000) break;
  }
  float raw = ago > 200 ? (w - wOld) * 1000.0f / ago : 0;
  flow_ = flow_ * 0.6f + raw * 0.4f;
  if (fabsf(flow_) < 0.05f) flow_ = 0;
}

void Brew::requestTare(uint32_t ms) {
  scale.tare();
  tareSentMs_ = ms;
  state_ = BrewState::TarePending;
}

void Brew::startRun(uint32_t ms) {
  count_ = 0;
  maxW_ = 0;
  maxF_ = 0;
  minDt_ = 0.08f;
  runStartMs_ = ms;
  lastRiseMs_ = ms;
  lastRiseW_ = weight_;
  targetHit_ = false;
  signalAtW_ = -1;
  firstDrop_ = -1;
  resetFlowGuide();
  state_ = BrewState::Running;
  evtRunStarted_ = true;
  addSample(0, baseline_);
}

void Brew::finishRun(uint32_t ms, float endTimer) {
  if (timerRunning_) {
    timerAccum_ = max(0.0f, endTimer);
    timerRunning_ = false;
    scale.stopTimer();
  }
  finalWeight_ = weight_;
  finalTime_ = timerAccum_;
  flowWarn_ = 0;
  state_ = BrewState::Finished;
  evtFinished_ = true;

  // Learn how much still ends up in the cup after the signal (reaction time +
  // drips). Ignore outliers, e.g. when the signal was ignored on purpose.
  if (settings.dripComp && signalAtW_ > 0) {
    float over = finalWeight_ - signalAtW_;
    if (over >= 0 && over < 8) {
      settings.dripGrams = constrain(settings.dripGrams * 0.7f + over * 0.3f, 0.0f, 6.0f);
      settings.save();
    }
  }
}

float Brew::signalWeight() const {
  float t = recipes::active().target();
  if (t <= 0) return 0;
  return settings.dripComp ? max(0.5f, t - settings.dripGrams) : t;
}

void Brew::addSample(float t, float w) {
  if (!samples_) return;
  if (count_ > 0 && t - samples_[count_ - 1].t < minDt_) return;
  if (count_ >= MAX_SAMPLES) {
    // decimate: keep every second sample, halve the resolution
    int j = 0;
    for (int i = 0; i < count_; i += 2) samples_[j++] = samples_[i];
    count_ = j;
    minDt_ *= 2;
  }
  float f = max(0.0f, flow_);
  samples_[count_++] = {t, w, f};
  maxW_ = max(maxW_, w);
  maxF_ = max(maxF_, f);
}

void Brew::onWeight(float w, uint32_t ms) {
  float dt = prevMs_ ? (ms - prevMs_) / 1000.0f : 0;
  float rate = dt > 0.04f ? (w - prevW_) / dt : 0;
  prevW_ = w;
  prevMs_ = ms;
  weight_ = w;
  updateStability(w, ms);

  bool settled = stable_ && (ms - stableSinceMs_) >= AUTO_TARE_SETTLE;

  switch (state_) {
    case BrewState::Armed:
      if (needSettle_) {
        if (!settled) break;
        needSettle_ = false;
        baseline_ = w;
      }
      if (dosing_) break;   // weighing beans: just show the weight
      if (settings.autoTare && settled && fabsf(w) >= AUTO_TARE_MIN) {
        requestTare(ms);
        break;
      }
      if (stable_ && (ms - stableSinceMs_) > 1500) baseline_ = w;  // follow resting weight
      if (w - baseline_ >= recipes::active().startThreshold) {
        // weight is changing: start plotting right away
        startRun(ms);
        cupCheck_ = true;
        if (settings.autoStart && !timerRunning_) {
          timerAccum_ = 0;
          timerStartMs_ = ms;
          timerRunning_ = true;
          autoStarted_ = true;
          scale.resetTimer();
          scale.startTimer();
        }
        addSample((ms - runStartMs_) / 1000.0f, w);
      }
      break;

    case BrewState::TarePending:
      if (fabsf(w) < 0.3f && stable_) {
        baseline_ = w;
        state_ = BrewState::Armed;
      }
      break;

    case BrewState::Running: {
      uint32_t runMs = ms - runStartMs_;
      if (cupCheck_) {
        if (runMs > CUP_CHECK_MS) {
          cupCheck_ = false;
        } else if (rate > CUP_RATE && w - baseline_ > CUP_MIN_JUMP) {
          // that was a cup being placed, not coffee: discard the run
          cupCheck_ = false;
          count_ = 0;
          maxW_ = maxF_ = 0;
          timerRunning_ = false;
          timerAccum_ = 0;
          scale.stopTimer();
          scale.resetTimer();
          state_ = BrewState::Armed;
          baseline_ = w;
          needSettle_ = true;
          break;
        }
      }
      addSample(runMs / 1000.0f, w);
      if (firstDrop_ < 0 && timerRunning_ && !autoStarted_ && w - baseline_ >= 0.3f) {
        firstDrop_ = timerSeconds(ms);
      }
      updateFlowGuide(w, ms);
      if (w > lastRiseW_ + RISE_STEP) {
        lastRiseW_ = w;
        lastRiseMs_ = ms;
      }
      float sig = signalWeight();
      if (sig > 0 && !targetHit_ && w >= sig) {
        targetHit_ = true;
        evtTarget_ = true;
        signalAtW_ = w;
      }
      // cup pulled off the scale mid-shot -> finish now
      if (!cupCheck_ && rate < -CUP_RATE && w < lastRiseW_ - CUP_MIN_JUMP) {
        finishRun(ms, timerSeconds(lastRiseMs_ > timerStartMs_ ? lastRiseMs_ : ms));
      }
      break;
    }

    case BrewState::Finished:
      if (settings.autoTare && settled && fabsf(w - finalWeight_) >= AUTO_TARE_MIN) {
        requestTare(ms);  // cup removed or swapped -> ready for next shot
      }
      break;
  }
}

void Brew::tick(uint32_t ms) {
  if (state_ == BrewState::TarePending && ms - tareSentMs_ > TARE_TIMEOUT) {
    baseline_ = weight_;
    state_ = BrewState::Armed;
  }
  uint8_t stopDelay = recipes::active().stopDelayS;
  if (state_ == BrewState::Running && settings.autoStop && stopDelay > 0) {
    bool flowed = (lastRiseW_ - baseline_) > MIN_SHOT_YIELD;
    // only with live readings: a gap in the data is not "the weight stopped rising"
    if (flowed && scale.weightFresh() && ms - runStartMs_ > MIN_SHOT_MS &&
        ms - lastRiseMs_ >= stopDelay * 1000UL) {
      // shot time ends when the weight stopped rising
      uint32_t end = lastRiseMs_ > timerStartMs_ ? lastRiseMs_ : ms;
      finishRun(ms, timerRunning_ ? timerAccum_ + (end - timerStartMs_) / 1000.0f : timerAccum_);
    }
  }
}

void Brew::toggleTimer() {
  uint32_t ms = millis();
  if (timerRunning_) {
    if (state_ == BrewState::Running) {
      finishRun(ms, timerSeconds(ms));
    } else {
      timerAccum_ = timerSeconds(ms);
      timerRunning_ = false;
      scale.stopTimer();
    }
    return;
  }
  if (state_ != BrewState::Running) {
    baseline_ = weight_;
    startRun(ms);
    cupCheck_ = false;
    timerAccum_ = 0;
    scale.resetTimer();
  }
  timerStartMs_ = ms;
  timerRunning_ = true;
  autoStarted_ = false;
  scale.startTimer();
}

void Brew::reset() {
  resetFlowGuide();
  timerRunning_ = false;
  timerAccum_ = 0;
  count_ = 0;
  maxW_ = maxF_ = 0;
  finalTime_ = 0;
  cupCheck_ = false;
  baseline_ = weight_;
  state_ = BrewState::Armed;
  scale.stopTimer();
  scale.resetTimer();
}

void Brew::manualTare() {
  reset();
  requestTare(millis());
}

bool Brew::takeTargetReached() { bool e = evtTarget_; evtTarget_ = false; return e; }
bool Brew::takeShotFinished()  { bool e = evtFinished_; evtFinished_ = false; return e; }
bool Brew::takeRunStarted()    { bool e = evtRunStarted_; evtRunStarted_ = false; return e; }
bool Brew::takeFlowWarning()   { bool e = evtFlowWarn_; evtFlowWarn_ = false; return e; }

// ---------------------------------------------------------------------------
// flow guide
// ---------------------------------------------------------------------------
static constexpr uint32_t FLOW_WARMUP_MS  = 4000;    // after the first drops
static constexpr uint32_t FLOW_REACH_MS   = 10000;   // never in the band by then = choking
static constexpr uint32_t FLOW_HIGH_MS    = 1500;    // out of band this long -> warn
static constexpr uint32_t FLOW_LOW_MS     = 2500;
static constexpr uint32_t FLOW_CLEAR_MS   = 1000;    // back inside this long -> clear
static constexpr float    FLOW_END_SHARE  = 0.85f;   // of the target: the natural slowdown

void Brew::resetFlowGuide() {
  flowT0_ = flowLastMs_ = 0;
  flowActiveMs_ = flowInMs_ = 0;
  flowOutSinceMs_ = flowInSinceMs_ = 0;
  flowOutDir_ = 0;
  flowWarn_ = 0;
  flowReached_ = false;
}

int Brew::inBandPercent() const {
  if (flowActiveMs_ < 3000) return -1;
  return (int)roundf(flowInMs_ * 100.0f / flowActiveMs_);
}

void Brew::updateFlowGuide(float w, uint32_t ms) {
  const Recipe& r = recipes::active();
  uint32_t dt = flowLastMs_ ? ms - flowLastMs_ : 0;
  flowLastMs_ = ms;
  float lo, hi;
  if (!r.flowBand(lo, hi)) { flowWarn_ = 0; return; }
  if (!flowT0_) {
    if (w - baseline_ >= 0.3f) flowT0_ = ms;
    return;
  }
  float target = r.target();
  if (target > 0 && w >= target * FLOW_END_SHARE) { flowWarn_ = 0; return; }
  if (!flowReached_ && flow_ >= lo && flow_ <= hi) flowReached_ = true;
  uint32_t since = ms - flowT0_;
  if (since < FLOW_WARMUP_MS || (!flowReached_ && since < FLOW_REACH_MS)) return;

  flowActiveMs_ += dt;
  bool inside = flow_ >= lo && flow_ <= hi;
  if (inside) flowInMs_ += dt;

  // a little tolerance so the warning doesn't flicker at the edges
  int dir = flow_ > hi * 1.1f ? 1 : flow_ < lo * 0.9f ? -1 : 0;
  if (dir != 0) {
    if (!flowOutSinceMs_ || flowOutDir_ != dir) { flowOutSinceMs_ = ms; flowOutDir_ = dir; }
    uint32_t need = dir > 0 ? FLOW_HIGH_MS : FLOW_LOW_MS;
    if (flowWarn_ != dir && ms - flowOutSinceMs_ >= need) {
      flowWarn_ = dir;
      evtFlowWarn_ = true;
    }
    flowInSinceMs_ = 0;
  } else {
    flowOutSinceMs_ = 0;
    if (flowWarn_ != 0) {
      if (!flowInSinceMs_) flowInSinceMs_ = ms;
      if (ms - flowInSinceMs_ >= FLOW_CLEAR_MS) flowWarn_ = 0;
    }
  }
}

void Brew::setDosing(bool on) {
  if (on == dosing_) return;
  dosing_ = on;
  if (!on) {
    // beans come off the scale next: wait until the weight settles again
    baseline_ = weight_;
    needSettle_ = true;
  } else if (state_ == BrewState::Running) {
    reset();
  }
}
