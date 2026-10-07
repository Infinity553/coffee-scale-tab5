#include <cstdlib>
#include "Diag.h"
namespace diag {
void begin() {}
void loop(uint32_t) {}
const char* shortReason() { return getenv("SIM_RESTARTED") ? "Restarted: radio chip reset" : ""; }
const char* detail() { return ""; }
bool bannerActive(uint32_t) { return getenv("SIM_RESTARTED") != nullptr; }
const char* firmwareId() { return "simulator"; }
}  // namespace diag
