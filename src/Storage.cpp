#include "Storage.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <mutex>

// Tab5 microSD slot in SPI mode (CS 42, SCK 43, MOSI 44, MISO 39).
// SD_MMC must not be used: the P4 has one SD host controller, and ESP-Hosted runs
// the link to the C6 radio chip on its other slot. Mounting (and especially failing
// to mount) a card through SD_MMC resets that controller and breaks Wi-Fi/Bluetooth.
namespace storage {

static std::recursive_mutex mtx;
static fs::FS* fsys = nullptr;
static SPIClass sdSpi(FSPI);

bool mount() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (fsys) return true;
  static bool spiStarted = false;
  if (!spiStarted) { sdSpi.begin(43, 39, 44, 42); spiStarted = true; }
  if (SD.begin(42, sdSpi, 25000000) && SD.cardType() != CARD_NONE) {
    fsys = &SD;
    Serial.printf("[SD] card mounted, %llu MB\n", SD.cardSize() >> 20);
    return true;
  }
  SD.end();
  return false;
}

bool mounted() { return fsys != nullptr; }

void unmount() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!fsys) return;
  SD.end();
  fsys = nullptr;
  log_w("SD card removed");
}

bool check() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!fsys) return false;
  File f = fsys->open("/");
  bool ok = (bool)f;
  if (f) f.close();
  if (!ok) unmount();
  return ok;
}

bool read(const char* path, std::string& out) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!fsys) return false;
  File f = fsys->open(path, FILE_READ);
  if (!f) return false;
  size_t n = f.size();
  out.resize(n);
  size_t got = n ? f.read((uint8_t*)&out[0], n) : 0;
  f.close();
  return got == n;
}

bool write(const char* path, const std::string& data) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!fsys) return false;
  std::string tmp = std::string(path) + ".tmp";
  File f = fsys->open(tmp.c_str(), FILE_WRITE);
  if (!f) { check(); return false; }
  size_t put = f.write((const uint8_t*)data.data(), data.size());
  f.close();
  if (put != data.size()) { fsys->remove(tmp.c_str()); check(); return false; }
  fsys->remove(path);
  return fsys->rename(tmp.c_str(), path);
}

bool remove(const char* path) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  return fsys && fsys->remove(path);
}

bool mkdirs(const char* path) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!fsys) return false;
  std::string p(path), cur;
  size_t pos = 1;
  while (pos <= p.size()) {
    size_t nx = p.find('/', pos);
    if (nx == std::string::npos) nx = p.size();
    cur = p.substr(0, nx);
    if (!fsys->exists(cur.c_str()) && !fsys->mkdir(cur.c_str())) return false;
    pos = nx + 1;
  }
  return true;
}

uint64_t freeBytes() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!fsys) return 0;
  return SD.totalBytes() - SD.usedBytes();
}

}  // namespace storage
