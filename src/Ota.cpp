#include "Ota.h"
#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "Diag.h"

namespace ota {

static uint32_t allowUntil = 0;
static volatile bool isActive = false, isDone = false;
static size_t expected = 0, written = 0;
static char err[96] = "";
static bool confirmed = false;

void allow(uint32_t minutes) { allowUntil = millis() + minutes * 60000UL; if (!allowUntil) allowUntil = 1; }
void disallow() { allowUntil = 0; }
bool allowed() { return allowUntil && (int32_t)(allowUntil - millis()) > 0; }
uint32_t allowedSecondsLeft() { return allowed() ? (allowUntil - millis()) / 1000 : 0; }

bool begin(size_t expectedSize, const char** error) {
  err[0] = 0;
  isDone = false;
  written = 0;
  expected = expectedSize;
  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    snprintf(err, sizeof(err), "Can't start the update: %s", Update.errorString());
    *error = err;
    return false;
  }
  isActive = true;
  return true;
}

bool write(const uint8_t* data, size_t len) {
  if (!isActive) return false;
  if (Update.write(const_cast<uint8_t*>(data), len) != len) {
    snprintf(err, sizeof(err), "Writing failed: %s", Update.errorString());
    abort();
    return false;
  }
  written += len;
  return true;
}

bool finish(const char** error) {
  if (!isActive) { *error = err[0] ? err : "No update in progress"; return false; }
  isActive = false;
  // end() checks the image (header, chip, checksum) before it is made bootable
  if (!Update.end(true)) {
    snprintf(err, sizeof(err), "The file is not valid firmware: %s", Update.errorString());
    *error = err;
    return false;
  }
  isDone = true;
  disallow();
  return true;
}

void abort() {
  if (isActive) Update.abort();
  isActive = false;
}

bool active() { return isActive; }
int progress() {
  if (isDone) return 100;
  if (!expected) return 0;
  return (int)min<size_t>(99, written * 100 / expected);
}
bool succeeded() { return isDone; }
const char* lastError() { return err; }

void confirmIfHealthy(uint32_t now) {
  if (confirmed || now < 30000) return;
  confirmed = true;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] new firmware confirmed");
  }
}

bool rolledBack() { return esp_ota_get_last_invalid_partition() != nullptr; }

}  // namespace ota

// Arduino would confirm a new firmware right at boot; confirm it ourselves
// after 30 s of normal operation instead (see confirmIfHealthy).
extern "C" bool verifyRollbackLater() { return true; }
