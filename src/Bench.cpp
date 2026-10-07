// TEMPORARY: one-time drawing benchmark at boot, to find where full-screen
// redraw time goes on the Tab5 (PSRAM vs internal RAM, CPU vs PPA).
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include "Blit.h"
#include "UIKit.h"

namespace ui {

template <class F>
static void timeIt(const char* name, int reps, F fn) {
  uint32_t t0 = micros();
  for (int i = 0; i < reps; i++) fn();
  Serial.printf("[BENCH] %-34s %7.2f ms\n", name, (micros() - t0) / 1000.0f / reps);
}

void runBenchmark() {
  Serial.println("[BENCH] ---- drawing benchmark (canvas in PSRAM) ----");
  const size_t full = (size_t)W * H * 2;
  timeIt("fillScreen 1280x720", 3, [] { canvas.fillScreen(BG); });
  timeIt("fillRect 740x600", 3, [] { canvas.fillRect(24, 96, 740, 600, SURFACE); });
  timeIt("card 740x600 (2 smooth round rects)", 3, [] { card(24, 96, 740, 600); });
  timeIt("30 lines of 12pt text", 3, [] {
    for (int i = 0; i < 30; i++) text("Seconds without weight gain 0123", 40, 110 + i * 18, F_BODY, TEXT);
  });
  timeIt("segment digits 188.8 (148 px)", 3, [] { segText("188.8", 600, 150, 148, TEXT, true); });
  timeIt("300 wide lines", 3, [] {
    for (int i = 0; i < 300; i++) canvas.drawWideLine(600 + i, 300, 601 + i, 320 + (i % 40), 1.4f, ACCENT);
  });
  timeIt("memset 1.8 MB canvas", 3, [&] { memset(canvas.getBuffer(), 0, full); });

  // same work in internal RAM
  LGFX_Sprite small;
  small.setPsram(false);
  small.setColorDepth(canvas.getColorDepth());
  if (small.createSprite(400, 300)) {
    Serial.println("[BENCH] ---- 400x300 sprite: internal RAM vs PSRAM canvas ----");
    timeIt("internal: smooth round rect 400x300", 5, [&] { small.fillSmoothRoundRect(0, 0, 400, 300, 22, SURFACE); });
    timeIt("PSRAM:    smooth round rect 400x300", 5, [] { canvas.fillSmoothRoundRect(0, 0, 400, 300, 22, SURFACE); });
    timeIt("internal: 30 text lines", 3, [&] {
      small.setFont(F_BODY);
      for (int i = 0; i < 15; i++) small.drawString("Seconds without weight gain 0123", 4, i * 18);
    });
    timeIt("PSRAM:    30 text lines (same 15)", 3, [] {
      canvas.setFont(F_BODY);
      for (int i = 0; i < 15; i++) canvas.drawString("Seconds without weight gain 0123", 4, i * 18);
    });
    small.deleteSprite();
  } else {
    Serial.println("[BENCH] no internal RAM for a 400x300 sprite");
  }

  // raw memory bandwidth
  void* ib = heap_caps_malloc(200 * 1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void* pb = heap_caps_malloc(200 * 1024, MALLOC_CAP_SPIRAM);
  if (ib && pb) {
    timeIt("memset 200 KB internal", 5, [&] { memset(ib, 1, 200 * 1024); });
    timeIt("memset 200 KB PSRAM", 5, [&] { memset(pb, 1, 200 * 1024); });
  }
  heap_caps_free(ib);
  heap_caps_free(pb);
  Serial.printf("[BENCH] free internal RAM: %u KB (largest block %u KB)\n",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));

  // PPA (DMA) fill and full-screen push
  void* ab = heap_caps_aligned_alloc(64, full, MALLOC_CAP_SPIRAM);
  if (ab) {
    timeIt("PPA fill 1280x720", 3, [&] { blit::fill(ab, full, W, H, 0, 0, W, H, BG); });
    timeIt("PPA fill 740x600", 3, [&] { blit::fill(ab, full, W, H, 24, 96, 740, 600, BG); });
    heap_caps_free(ab);
  }
  if (blit::active()) timeIt("PPA push full screen", 3, [] { blit::push(canvas.getBuffer(), W, H, 0, 0, W, H, 1); });
  Serial.println("[BENCH] ---- done ----");
}

}  // namespace ui
#endif
