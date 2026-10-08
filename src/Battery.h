#pragma once
#include <stdint.h>

// The Tab5's own battery (2S Li-Po, read through M5Unified). Polled every few
// seconds from the main loop; the getters return cached values.
namespace battery {
void poll(uint32_t now);
bool present();      // false when no battery is fitted (USB power only)
int  level();        // 0..100
bool charging();
int  voltageMv();    // pack voltage
}  // namespace battery
