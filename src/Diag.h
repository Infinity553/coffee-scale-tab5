#pragma once
#include <stdint.h>

// Why did we (re)start? Reads the reset reason and, after a crash, the core dump
// summary from flash. Shown briefly on screen and printed to serial.
namespace diag {
void begin();                 // call first thing in setup()
void loop(uint32_t now);      // repeats the report on serial for 2 minutes
const char* shortReason();    // "" after a normal power-on
const char* detail();         // full text (also printed to serial)
bool bannerActive(uint32_t now);
}  // namespace diag
