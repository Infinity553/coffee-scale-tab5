#pragma once
#include <Arduino.h>

// Shot logic: auto tare, shot timer, auto start/stop, dosing, drip-compensated
// target signal and the weight log used for the extraction plot.

enum class BrewState : uint8_t {
  Armed,      // waiting for weight to change (shows last shot dimmed)
  TarePending,// auto tare sent, waiting for the scale to settle at zero
  Running,    // recording; plot started on first weight change
  Finished,   // shot done, result shown
};

struct BrewSample {
  float t;  // seconds since plot start
  float w;  // grams
  float f;  // flow g/s (smoothed)
};

class Brew {
 public:
  static constexpr int MAX_SAMPLES = 3000;

  void begin();
  // Feed every new weight reading from the scale.
  void onWeight(float grams, uint32_t ms);
  // Call regularly (handles timeouts even without new readings).
  void tick(uint32_t ms);

  // User actions
  void manualTare();        // tares scale and re-arms
  void toggleTimer();       // start / stop
  void reset();             // clear timer and plot, re-arm
  void setDosing(bool on);  // weighing beans: no auto tare, no auto start
  bool dosing() const { return dosing_; }

  // Events consumed by the UI
  bool takeTargetReached();
  bool takeShotFinished();
  bool takeRunStarted();
  bool takeFlowWarning();   // a flow warning just started (for the beep)

  BrewState state() const { return state_; }
  float weight() const { return weight_; }
  float flow() const { return flow_; }
  bool  timerRunning() const { return timerRunning_; }
  float timerSeconds(uint32_t ms) const;
  bool  stable() const { return stable_; }

  // Weight at which the target signal fires (target minus learned drip), 0 = none.
  float signalWeight() const;

  int  sampleCount() const { return count_; }
  const BrewSample& sample(int i) const { return samples_[i]; }
  const BrewSample* samples() const { return samples_; }
  float maxWeight() const { return maxW_; }
  float maxFlow() const { return maxF_; }
  float lastShotSeconds() const { return finalTime_; }
  float finalWeight() const { return finalWeight_; }
  float firstDropSeconds() const { return firstDrop_; }  // -1 = unknown
  // Flow guide (recipe flow band): +1 flow too high, -1 too low, 0 fine / off.
  int   flowWarning() const { return flowWarn_; }
  // Share of the main extraction spent inside the band, -1 if not measured.
  int   inBandPercent() const;
  bool  hasPlot() const { return count_ > 1; }

 private:
  void startRun(uint32_t ms);
  void finishRun(uint32_t ms, float endTime);
  void addSample(float t, float w);
  void requestTare(uint32_t ms);
  void updateStability(float w, uint32_t ms);
  void updateFlowGuide(float w, uint32_t ms);
  void resetFlowGuide();

  BrewState state_ = BrewState::Armed;
  float    weight_ = 0, flow_ = 0;
  float    baseline_ = 0;      // weight when armed
  float    finalWeight_ = 0;
  float    finalTime_ = 0;
  float    firstDrop_ = -1;
  bool     dosing_ = false;

  // timer
  bool     timerRunning_ = false;
  bool     autoStarted_ = false;   // timer started by the first drops (no first-drop time)
  uint32_t timerStartMs_ = 0;
  float    timerAccum_ = 0;

  // run tracking
  uint32_t runStartMs_ = 0;
  uint32_t lastRiseMs_ = 0;
  float    lastRiseW_ = 0;
  bool     cupCheck_ = false;
  bool     needSettle_ = false;  // ignore triggers until weight is stable
  uint32_t tareSentMs_ = 0;
  float    signalAtW_ = -1;      // weight when the target signal fired

  // stability / flow history (ring buffer, ~3 s)
  static constexpr int HIST = 64;
  float    histW_[HIST];
  uint32_t histT_[HIST];
  int      histHead_ = 0, histN_ = 0;
  bool     stable_ = false;
  uint32_t stableSinceMs_ = 0;
  float    prevW_ = 0;
  uint32_t prevMs_ = 0;

  // plot
  BrewSample* samples_ = nullptr;
  int      count_ = 0;
  float    maxW_ = 0, maxF_ = 0;
  float    minDt_ = 0.08f;

  bool     evtTarget_ = false, evtFinished_ = false, evtRunStarted_ = false, targetHit_ = false;

  // flow guide
  uint32_t flowT0_ = 0;          // first drops (ms), 0 = not yet
  uint32_t flowLastMs_ = 0;
  uint32_t flowActiveMs_ = 0, flowInMs_ = 0;
  uint32_t flowOutSinceMs_ = 0, flowInSinceMs_ = 0;
  int8_t   flowOutDir_ = 0;
  int8_t   flowWarn_ = 0;
  bool     flowReached_ = false;  // flow has been inside the band at least once
  bool     evtFlowWarn_ = false;
};

extern Brew brew;
