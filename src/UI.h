#pragma once
#include <Arduino.h>

enum class Screen : uint8_t {
  SetupWelcome,
  SetupScan,
  SetupPrefs,
  Main,
  Settings,
  Scan,          // change scale from the settings screen
  Recipes,
  History,
  Wifi,
  WifiNetworks,
  Keyboard,
  System,        // backup / restore, about
  Keypad,        // numeric input
};

namespace ui {
void begin();
void applyDisplaySettings();
void setScreen(Screen s);
Screen screen();
// Polls touch, handles brew events, renders a frame when needed. Call from loop().
void update();
void invalidate();
// PNG of the current frame (simulator snapshots); caller frees.
void* snapshotPng(size_t* len);
// Simulator: inject a tap at logical coordinates.
void simulateTap(int x, int y);
}  // namespace ui
