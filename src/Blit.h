#pragma once
#include <stddef.h>
#include <stdint.h>

// Hardware transfer of the UI canvas to the display.
//
// The Tab5 panel is portrait (720x1280) but the UI is landscape. Without help,
// every drawn pixel has to go through a rotated canvas, which is slow. With the
// ESP32-P4's PPA (pixel processing accelerator) the UI draws into a plain
// landscape canvas and the PPA rotates changed regions into the panel's frame
// buffer by DMA.
//
// begin() runs a self-test that rotates a test pattern and checks every pixel;
// it also finds out which PPA angle matches each screen orientation. If anything
// fails, it returns false and the caller keeps the software path.
namespace blit {
bool begin(int nativeW, int nativeH);   // true = hardware rotation available
bool active();
// Copies the logical rect (x, y, w, h) of a landscape RGB565 canvas to the panel.
// rotation: 1 or 3, the logical->panel mapping used for drawing and touch.
bool push(const void* canvas, int canvasW, int canvasH, int x, int y, int w, int h, int rotation);
// Fills a rectangle of an RGB565 picture by DMA. The buffer and its size must be
// cache-line aligned (64 bytes).
bool fill(void* buf, size_t bufSize, int picW, int picH, int x, int y, int w, int h, uint16_t rgb565);
}  // namespace blit
