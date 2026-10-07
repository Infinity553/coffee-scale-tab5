#include "Diag.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_system.h>

namespace diag {

static char shortText[64] = "";
static char detailText[900] = "";
static uint32_t lastPrint = 0;

// Code addresses on the ESP32-P4: flash (XIP) and internal RAM.
static bool isCodeAddr(uint32_t a) {
  return (a >= 0x40000000 && a < 0x44000000) || (a >= 0x4FF00000 && a < 0x4FFC0000);
}

void begin() {
  esp_reset_reason_t r = esp_reset_reason();
  const char* why = "";      // short, for the screen
  const char* explain = "";  // longer, for serial
  switch (r) {
    case ESP_RST_PANIC:
      why = "crash"; explain = "firmware crash"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      why = "watchdog"; explain = "watchdog (also used after a crash whose dump took too long to save)"; break;
    case ESP_RST_BROWNOUT:
      why = "power dip"; explain = "brownout: supply voltage dropped"; break;
    case ESP_RST_SW:
      // ESP-Hosted restarts the P4 when the C6 (Wi-Fi/Bluetooth chip) resets
      why = "radio chip reset";
      explain = "software restart, most likely ESP-Hosted after the C6 radio chip reset"; break;
    default: break;
  }
  if (*why) snprintf(shortText, sizeof(shortText), "Restarted: %s", why);

  char mySha[17] = "";
  esp_app_get_elf_sha256(mySha, sizeof(mySha));
  int n = snprintf(detailText, sizeof(detailText), "[DIAG] firmware %s, reset reason %d (%s)",
                   mySha, (int)r, *explain ? explain : "normal start");

  // A crash dump stays in flash until the next crash overwrites it; report each one once.
  if (esp_core_dump_image_check() == ESP_OK) {
    esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
    if (s && esp_core_dump_get_summary(s) == ESP_OK) {
      char dumpSha[17];
      memcpy(dumpSha, s->app_elf_sha256, 16);
      dumpSha[16] = 0;
      char key[40];
      snprintf(key, sizeof(key), "%s%08lx", dumpSha, (unsigned long)s->exc_pc);

      Preferences p;
      p.begin("diag", false);
      bool known = p.isKey("dumpKey") && p.getString("dumpKey", "") == key;
      if (!known) p.putString("dumpKey", key);
      p.end();

      if (!known) {
        char reason[120] = "";
        esp_core_dump_get_panic_reason(reason, sizeof(reason));
        n += snprintf(detailText + n, sizeof(detailText) - n,
                      "\n[DIAG] crash in task '%s'%s%s (firmware %s%s)"
                      "\n[DIAG] pc 0x%08lx ra 0x%08lx mcause 0x%lx mtval 0x%08lx\n[DIAG] stack:",
                      s->exc_task, *reason ? ": " : "", reason, dumpSha,
                      strncmp(dumpSha, mySha, 16) ? ", an older build" : ", this build",
                      (unsigned long)s->exc_pc, (unsigned long)s->ex_info.ra,
                      (unsigned long)s->ex_info.mcause, (unsigned long)s->ex_info.mtval);
        // code addresses found on the crashed task's stack: the likely call chain
        const uint32_t* w = (const uint32_t*)s->exc_bt_info.stackdump;
        int found = 0;
        for (uint32_t i = 0; i < s->exc_bt_info.dump_size / 4 && found < 16; i++) {
          if (isCodeAddr(w[i]) && n < (int)sizeof(detailText) - 12) {
            n += snprintf(detailText + n, sizeof(detailText) - n, " 0x%08lx", (unsigned long)w[i]);
            found++;
          }
        }
        snprintf(shortText, sizeof(shortText), "Restarted: crash in %s", s->exc_task);
      }
    }
    free(s);
  }

  if (*shortText) {
    Preferences p;
    p.begin("diag", false);
    p.putString("last", detailText);
    p.end();
  }
}

void loop(uint32_t now) {
  if (!*shortText || now > 120000) return;
  if (lastPrint && now - lastPrint < 10000) return;
  lastPrint = now;
  Serial.println(detailText);
}

const char* shortReason() { return shortText; }
const char* detail() { return detailText; }
bool bannerActive(uint32_t now) { return *shortText && now < 30000; }

}  // namespace diag
