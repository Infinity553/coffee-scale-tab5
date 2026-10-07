#pragma once
#include <stdint.h>

// Colour schemes. The active palette is copied into the variables below, so
// drawing code just uses theme::ACCENT etc. and follows the current scheme.
namespace theme {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Palette {
  const char* name;
  uint16_t bg, surface, surface2, stroke, grid;
  uint16_t accent, accentHi, accentLo, accentDim, onAccent;
  uint16_t text, muted, flow, good, warn, bad;
  uint16_t highlight;                       // value readouts (flow, %, settings values)
  uint16_t chipOn, chipOnFg, chipOnLine;    // quick-toggle chips when on
};

enum : uint8_t { ROAST = 0, RACER = 1, COUNT = 2 };
extern const Palette PALETTES[COUNT];

void apply(uint8_t id);

extern uint16_t BG;         // screen background
extern uint16_t SURFACE;    // card
extern uint16_t SURFACE2;   // raised / buttons
extern uint16_t STROKE;     // outlines
extern uint16_t GRID;       // plot grid
extern uint16_t ACCENT;     // main accent
extern uint16_t ACCENT_HI;  // highlight
extern uint16_t ACCENT_LO;  // area fill under curve
extern uint16_t ACCENT_DIM;
extern uint16_t ON_ACCENT;  // text on accent-coloured fills
extern uint16_t TEXT;
extern uint16_t MUTED;
extern uint16_t FLOW;       // flow line
extern uint16_t GOOD;
extern uint16_t WARN;
extern uint16_t BAD;
extern uint16_t HIGHLIGHT;
extern uint16_t CHIP_ON;
extern uint16_t CHIP_ON_FG;
extern uint16_t CHIP_ON_LINE;

}  // namespace theme
