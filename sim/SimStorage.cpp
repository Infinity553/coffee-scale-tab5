// Simulator storage: a folder stands in for the SD card.
//   SIM_SD=path   folder to use (default sim/sdcard)
//   SIM_NO_SD=1   behave as if no card is inserted
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include "Storage.h"

namespace fsx = std::filesystem;

namespace storage {
static std::recursive_mutex mtx;
static bool isMounted = false;

static fsx::path root() { return getenv("SIM_SD") ? getenv("SIM_SD") : "sim/sdcard"; }
static fsx::path full(const char* p) { return root() / fsx::path(p).relative_path(); }

bool mount() {
  if (getenv("SIM_NO_SD")) return false;
  fsx::create_directories(root());
  return isMounted = true;
}
bool mounted() { return isMounted; }
void unmount() { isMounted = false; }
bool check() { return isMounted; }

bool read(const char* path, std::string& out) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!isMounted) return false;
  std::ifstream f(full(path), std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool write(const char* path, const std::string& data) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (!isMounted) return false;
  fsx::path p = full(path), tmp = p;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary);
    if (!f) return false;
    f << data;
  }
  std::error_code ec;
  fsx::rename(tmp, p, ec);
  return !ec;
}

bool remove(const char* path) {
  std::error_code ec;
  return isMounted && fsx::remove(full(path), ec);
}

bool mkdirs(const char* path) {
  std::error_code ec;
  fsx::create_directories(full(path), ec);
  return isMounted && !ec;
}

uint64_t freeBytes() { return isMounted ? fsx::space(root()).available : 0; }
}  // namespace storage
