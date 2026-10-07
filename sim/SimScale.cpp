// Simulated Acaia scale for the desktop UI preview: plays a scripted
// espresso shot (cup placed, auto tare, ~30 s extraction, auto stop).
#include <cstdlib>
#include <random>
#include "AcaiaScale.h"

AcaiaScale scale;

static float tareOffset = 0;
static uint32_t lastSampleMs = 0;
static uint32_t t0 = 0;

static float rawWeight(float t) {
  float w = 0;
  if (t > 3.0f) w += std::min(1.0f, (t - 3.0f) / 0.15f) * 182.4f;  // cup placed
  const float shotStart = 7.0f;
  if (t > shotStart) {
    float s = t - shotStart;
    auto logistic = [](float x) { return 39.0f / (1 + expf(-(x - 15.0f) / 4.0f)); };
    float y = logistic(s) - logistic(0);
    y += 0.12f * s * expf(-s / 3.0f);  // first drops
    w += std::max(0.0f, std::min(y, 38.4f));
  }
  static std::mt19937 rng(1);
  static std::normal_distribution<float> noise(0, 0.03f);
  return w + noise(rng);
}

void AcaiaScale::begin() { t0 = millis(); }
void AcaiaScale::setTarget(const String&) {}
void AcaiaScale::startDiscovery() { discoveryMode_ = true; state_ = ScaleState::Discovering; }
void AcaiaScale::stopDiscovery() { discoveryMode_ = false; }
std::vector<FoundScale> AcaiaScale::discovered() {
  return {{"LUNAR-2B41", "c4:de:e2:19:2b:41", -58}, {"PEARLS-0A17", "d0:31:7a:55:0a:17", -77}};
}
void AcaiaScale::tare() { tareOffset = rawWeight((millis() - t0) / 1000.0f); }
void AcaiaScale::startTimer() {}
void AcaiaScale::stopTimer() {}
void AcaiaScale::resetTimer() {}
void AcaiaScale::disconnect() {}
String AcaiaScale::connectedName() { return "LUNAR-2B41"; }
String AcaiaScale::connectedAddress() { return "c4:de:e2:19:2b:41"; }

bool AcaiaScale::poll(float& grams, uint32_t& atMs) {
  uint32_t now = millis();
  float t = (now - t0) / 1000.0f;
  if (discoveryMode_) return false;
  if (t < 1.2f) { state_ = ScaleState::Searching; return false; }
  state_ = ScaleState::Connected;
  battery_ = 78;
  if (now - lastSampleMs < 100) return false;  // ~10 Hz like the Lunar
  lastSampleMs = now;
  grams = roundf((rawWeight(t) - tareOffset) * 10) / 10;
  atMs = now;
  weight_ = grams;
  return true;
}

void AcaiaScale::onNotify(const uint8_t*, size_t) {}
void AcaiaScale::onDisconnected() {}
void AcaiaScale::onAdvertised(const String&, const String&, int, void*) {}
