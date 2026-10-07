#pragma once
#include <stdint.h>
#include <string>

// Backup and restore of settings and recipes (not the shot history, which lives
// on the SD card already). The Wi-Fi password is never included.
//
//   /coffeescale/settings-backup.json   written automatically a few seconds
//                                       after settings or recipes change
namespace backup {

std::string toJson();
// Validates and applies a backup (missing fields keep their current value) and
// saves everything. A restart afterwards applies it everywhere.
bool fromJson(const std::string& json, std::string* error = nullptr);

bool saveToSd();
bool restoreFromSd(std::string* error = nullptr);
bool sdBackupInfo(uint32_t& createdEpoch);   // backup file on the SD card?
uint32_t lastSavedMs();                      // millis() of the last SD backup, 0 = none yet

void markDirty();                            // settings / recipes changed
void loop(uint32_t now, bool busy);          // writes a pending auto backup when not busy
void restartSoon();                          // restart after a restore (device only)
bool restartPending();

}  // namespace backup
