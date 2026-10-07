// Coffee Scale Display
// M5Stack Tab5 (ESP32-P4) as a large display for an Acaia Lunar scale.

#include <M5Unified.h>
#include <sys/time.h>
#include <ctime>
#include "AcaiaScale.h"
#include "Backup.h"
#include "Brew.h"
#include "Diag.h"
#include "History.h"
#include "Net.h"
#include "Recipes.h"
#include "Settings.h"
#include "UI.h"

// days since 1970-01-01 for a civil date (UTC), no timezone involved
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

// The RTC keeps UTC. Restore the system clock from it at boot.
static void clockFromRtc() {
  if (!M5.Rtc.isEnabled()) return;
  m5::rtc_datetime_t dt;
  if (!M5.Rtc.getDateTime(&dt) || dt.date.year < 2024) return;
  int64_t days = daysFromCivil(dt.date.year, dt.date.month, dt.date.date);
  timeval tv = {(time_t)(days * 86400 + dt.time.hours * 3600 + dt.time.minutes * 60 + dt.time.seconds), 0};
  settimeofday(&tv, nullptr);
}

static void clockToRtc() {
  if (!M5.Rtc.isEnabled()) return;
  time_t now = time(nullptr);
  struct tm g;
  gmtime_r(&now, &g);
  M5.Rtc.setDateTime(&g);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  diag::begin();
  Serial.println(diag::detail());
  setenv("TZ", DEVICE_TZ, 1);
  tzset();
  clockFromRtc();

  settings.load();
  recipes::load();
  brew.begin();
  ui::begin();

  scale.begin();   // BLE task: scans, connects and reconnects automatically
  scale.setTarget(settings.scaleAddress);
  if (!settings.configured) scale.startDiscovery();

  history::begin();   // SD card (watched in the background)
  net::begin();       // Wi-Fi / web UI if enabled
}

void loop() {
  M5.update();

  float grams;
  uint32_t at;
  if (scale.poll(grams, at)) brew.onWeight(grams, at);
  brew.tick(millis());

  static ScaleState lastState = ScaleState::Idle;
  if (scale.state() != lastState) {
    lastState = scale.state();
    ui::invalidate();
  }
  if (net::takeTimeSynced()) clockToRtc();
  diag::loop(millis());
  backup::loop(millis(), brew.state() == BrewState::Running);

  ui::update();
  delay(2);
}
