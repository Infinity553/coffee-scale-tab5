#pragma once
#include <stddef.h>
#include <stdint.h>

// Firmware updates over Wi-Fi (upload from the web page).
// Uploads are only accepted for a while after "Allow web update" is tapped on
// the Tab5. A new firmware confirms itself after running cleanly for 30 s; if
// it crashes before that, the bootloader goes back to the previous firmware.
namespace ota {
void allow(uint32_t minutes);
void disallow();
bool allowed();
uint32_t allowedSecondsLeft();

// upload (called from the web server)
bool begin(size_t expectedSize, const char** error);
bool write(const uint8_t* data, size_t len);
bool finish(const char** error);
void abort();

bool active();             // an upload is being written
int  progress();           // 0..100
bool succeeded();          // finished, restart pending
const char* lastError();   // "" if none

void confirmIfHealthy(uint32_t now);   // call from loop()
bool rolledBack();         // the last update failed and the previous firmware runs
}  // namespace ota
