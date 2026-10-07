#include "Blit.h"

#if defined(ESP_PLATFORM) && __has_include(<driver/ppa.h>)
#define BLIT_PPA 1
#include <M5Unified.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#endif

namespace blit {

#if BLIT_PPA
static ppa_client_handle_t client = nullptr;
static ppa_client_handle_t fillClient = nullptr;
static void* fb = nullptr;
static int fbW = 0, fbH = 0;
static ppa_srm_rotation_angle_t angleRot1, angleRot3;   // PPA angle for screen rotation 1 / 3
static bool ok = false;

static bool rotateBlock(const void* src, int srcW, int srcH, int bx, int by, int bw, int bh,
                        void* dst, size_t dstSize, int dstW, int dstH, int ox, int oy,
                        ppa_srm_rotation_angle_t angle) {
  ppa_srm_oper_config_t op = {};
  op.in.buffer = src;
  op.in.pic_w = srcW;
  op.in.pic_h = srcH;
  op.in.block_w = bw;
  op.in.block_h = bh;
  op.in.block_offset_x = bx;
  op.in.block_offset_y = by;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.out.buffer = dst;
  op.out.buffer_size = dstSize;
  op.out.pic_w = dstW;
  op.out.pic_h = dstH;
  op.out.block_offset_x = ox;
  op.out.block_offset_y = oy;
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.rotation_angle = angle;
  op.scale_x = 1.0f;
  op.scale_y = 1.0f;
  op.mode = PPA_TRANS_MODE_BLOCKING;
  return ppa_do_scale_rotate_mirror(client, &op) == ESP_OK;
}

// Rotates a 64x32 test pattern and reports which screen rotation the PPA angle
// produced: 1 (logical (x,y) -> panel (W-1-y, x)), 3 (-> panel (y, H-1-x)), or 0.
static int probe(ppa_srm_rotation_angle_t angle) {
  const int IW = 64, IH = 32;              // landscape input, portrait output (IH x IW)
  const size_t size = IW * IH * 2;
  auto* in = (uint16_t*)heap_caps_aligned_calloc(64, 1, size, MALLOC_CAP_SPIRAM);
  auto* out = (uint16_t*)heap_caps_aligned_calloc(64, 1, size, MALLOC_CAP_SPIRAM);
  int result = 0;
  if (in && out) {
    for (int y = 0; y < IH; y++)
      for (int x = 0; x < IW; x++) in[y * IW + x] = (uint16_t)(((x << 8) | y) + 1);
    esp_cache_msync(in, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (rotateBlock(in, IW, IH, 0, 0, IW, IH, out, size, IH, IW, 0, 0, angle)) {
      esp_cache_msync(out, size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
      bool r1 = true, r3 = true;
      for (int y = 0; y < IH; y++) {
        for (int x = 0; x < IW; x++) {
          uint16_t v = in[y * IW + x];
          r1 = r1 && out[x * IH + (IH - 1 - y)] == v;
          r3 = r3 && out[(IW - 1 - x) * IH + y] == v;
        }
      }
      result = r1 ? 1 : r3 ? 3 : 0;
    }
  }
  heap_caps_free(in);
  heap_caps_free(out);
  return result;
}
#endif

bool begin(int nativeW, int nativeH) {
#if BLIT_PPA
  if (M5.getBoard() != m5::board_t::board_M5Tab5) return false;
  auto* dsi = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
  fb = dsi ? dsi->config_detail().buffer : nullptr;
  if (!fb) { Serial.println("[UI] display: no frame buffer, software rotation"); return false; }
  fbW = nativeW;
  fbH = nativeH;

  ppa_client_config_t cfg = {};
  cfg.oper_type = PPA_OPERATION_SRM;
  cfg.max_pending_trans_num = 1;
  if (ppa_register_client(&cfg, &client) != ESP_OK) {
    Serial.println("[UI] display: PPA not available, software rotation");
    return false;
  }
  int a90 = probe(PPA_SRM_ROTATION_ANGLE_90);
  int a270 = probe(PPA_SRM_ROTATION_ANGLE_270);
  if (a90 == 1 && a270 == 3) { angleRot1 = PPA_SRM_ROTATION_ANGLE_90; angleRot3 = PPA_SRM_ROTATION_ANGLE_270; }
  else if (a90 == 3 && a270 == 1) { angleRot1 = PPA_SRM_ROTATION_ANGLE_270; angleRot3 = PPA_SRM_ROTATION_ANGLE_90; }
  else {
    Serial.printf("[UI] display: PPA self-test failed (%d/%d), software rotation\n", a90, a270);
    ppa_unregister_client(client);
    client = nullptr;
    return false;
  }
  // From now on only the PPA writes the frame buffer: flush anything the CPU
  // cache still holds for it, so it can't overwrite PPA output later.
  esp_cache_msync(fb, (size_t)fbW * fbH * 2,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
  ok = true;
  Serial.println("[UI] display: PPA hardware rotation (self-test passed)");
  return true;
#else
  (void)nativeW;
  (void)nativeH;
  return false;
#endif
}

bool active() {
#if BLIT_PPA
  return ok;
#else
  return false;
#endif
}

bool push(const void* canvas, int canvasW, int canvasH, int x, int y, int w, int h, int rotation) {
#if BLIT_PPA
  if (!ok || w <= 0 || h <= 0) return false;
  // panel position of the rotated block (same mapping as touch / software path)
  int ox, oy;
  if (rotation == 1) { ox = fbW - y - h; oy = x; }
  else               { ox = y;           oy = fbH - x - w; }
  // the CPU drew these rows through its cache: make sure they are in PSRAM
  const uint8_t* first = (const uint8_t*)canvas + ((size_t)y * canvasW + x) * 2;
  size_t span = ((size_t)(h - 1) * canvasW + w) * 2;
  esp_cache_msync((void*)first, span, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  return rotateBlock(canvas, canvasW, canvasH, x, y, w, h, fb, (size_t)fbW * fbH * 2, fbW, fbH,
                     ox, oy, rotation == 1 ? angleRot1 : angleRot3);
#else
  (void)canvas; (void)canvasW; (void)canvasH; (void)x; (void)y; (void)w; (void)h; (void)rotation;
  return false;
#endif
}

bool fill(void* buf, size_t bufSize, int picW, int picH, int x, int y, int w, int h, uint16_t c) {
#if BLIT_PPA
  if (!fillClient) {
    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_FILL;
    cfg.max_pending_trans_num = 1;
    if (ppa_register_client(&cfg, &fillClient) != ESP_OK) return false;
  }
  // The CPU may hold cached pixels of these rows: write them back and drop them,
  // so nothing stale overwrites the DMA fill later. Invalidating needs whole
  // cache lines, so round the span out to 64 bytes.
  uintptr_t a = (uintptr_t)buf + ((size_t)y * picW + x) * 2;
  uintptr_t e = (uintptr_t)buf + ((size_t)(y + h - 1) * picW + x + w) * 2;
  a &= ~(uintptr_t)63;
  e = (e + 63) & ~(uintptr_t)63;
  esp_cache_msync((void*)a, e - a, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

  ppa_fill_oper_config_t op = {};
  op.out.buffer = buf;
  op.out.buffer_size = bufSize;
  op.out.pic_w = picW;
  op.out.pic_h = picH;
  op.out.block_offset_x = x;
  op.out.block_offset_y = y;
  op.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
  op.fill_block_w = w;
  op.fill_block_h = h;
  op.fill_argb_color.a = 255;
  op.fill_argb_color.r = ((c >> 11) & 0x1F) << 3;
  op.fill_argb_color.g = ((c >> 5) & 0x3F) << 2;
  op.fill_argb_color.b = (c & 0x1F) << 3;
  op.mode = PPA_TRANS_MODE_BLOCKING;
  return ppa_do_fill(fillClient, &op) == ESP_OK;
#else
  (void)buf; (void)bufSize; (void)picW; (void)picH; (void)x; (void)y; (void)w; (void)h; (void)c;
  return false;
#endif
}

}  // namespace blit
