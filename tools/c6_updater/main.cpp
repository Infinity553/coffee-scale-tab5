// One-off utility: installs the ESP-Hosted firmware on the Tab5's ESP32-C6 that
// exactly matches this Arduino core (older or newer firmware on the C6 both get
// replaced: Bluetooth only works when both sides run the same version).
//
//   pio run -e c6-update -t upload && pio device monitor -e c6-update
//
// Preferred: put esp32c6-v<version>.bin (see the serial output) in the SD card root.
// Otherwise Wi-Fi credentials are typed into the serial monitor; nothing is stored.
// Afterwards flash the main firmware again: pio run -e m5stack-tab5 -t upload

#include <HTTPClient.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include "esp32-hal-hosted.h"

static void show(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  M5.Display.println(buf);
}

static String readLine(const char* prompt, bool secret) {
  Serial.print(prompt);
  String s;
  for (;;) {
    while (!Serial.available()) delay(10);
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (s.length()) break;
      continue;
    }
    if (c == 8 || c == 127) {  // backspace
      if (s.length()) { s.remove(s.length() - 1); Serial.print("\b \b"); }
      continue;
    }
    s += c;
    Serial.print(secret ? '*' : c);
  }
  Serial.println();
  return s;
}

// Writes one chunk to the C6, paced so its receive queue keeps up.
static bool writeChunk(uint8_t* buf, int n) {
  bool ok = hostedWriteUpdate(buf, n);
  delay(15);
  return ok;
}

static bool finishUpdate() {
  if (!hostedEndUpdate()) { show("ERROR: the C6 rejected the image"); return false; }
  if (!hostedActivateUpdate()) { show("ERROR: could not activate the new image"); return false; }
  return true;
}

// Preferred: image from the SD card (SPI), so only the write goes over the C6 link.
static bool flashFromSd(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  size_t total = f.size(), done = 0;
  show("Writing %s from the SD card (%u bytes)", path, (unsigned)total);
  if (!hostedBeginUpdate()) { show("ERROR: the C6 refused to start the update"); f.close(); return false; }
  static uint8_t buf[1024];
  int lastPct = -1;
  while (done < total) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) { show("ERROR: reading the SD card failed"); f.close(); return false; }
    if (!writeChunk(buf, n)) { show("ERROR: writing to the C6 failed at %u bytes", (unsigned)done); f.close(); return false; }
    done += n;
    int pct = done * 100 / total;
    if (pct / 10 != lastPct / 10) { show("  %d %%", pct); lastPct = pct; }
  }
  f.close();
  return finishUpdate();
}

// Fallback: downloads the C6 image for this host version and writes it to the C6.
static bool flashC6() {
  const char* url = hostedGetUpdateURL();
  show("Downloading %s", url);
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (!https.begin(client, url)) { show("ERROR: could not open the URL"); return false; }
  int code = https.GET();
  if (code != HTTP_CODE_OK) { show("ERROR: download failed (HTTP %d)", code); https.end(); return false; }
  int len = https.getSize();
  if (len <= 0) { show("ERROR: unknown download size"); https.end(); return false; }

  if (!hostedBeginUpdate()) { show("ERROR: the C6 refused to start the update"); https.end(); return false; }
  NetworkClient* stream = https.getStreamPtr();
  static uint8_t buf[1024];
  int total = len, lastPct = -1;
  uint32_t lastData = millis();
  while (len > 0 && millis() - lastData < 15000) {
    size_t avail = stream->available();
    if (!avail) { delay(2); continue; }
    int n = stream->readBytes(buf, min((size_t)sizeof(buf), min(avail, (size_t)len)));
    if (n <= 0) continue;
    if (!writeChunk(buf, n)) { show("ERROR: writing to the C6 failed"); https.end(); return false; }
    len -= n;
    lastData = millis();
    int pct = (total - len) * 100 / total;
    if (pct / 10 != lastPct / 10) { show("  %d %%", pct); lastPct = pct; }
  }
  https.end();
  if (len > 0) { show("ERROR: download stalled"); return false; }
  return finishUpdate();
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setFont(&fonts::FreeSans18pt7b);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 20);

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 10000) delay(10);
  delay(500);

  show("ESP32-C6 co-processor updater");
  WiFi.STA.begin();  // starts ESP-Hosted
  // The C6 may still hold a Wi-Fi network from its previous firmware and keep
  // trying to join it. Every attempt floods the link, so make it forget and stop.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);   // eraseap: clear the stored network on the C6
  delay(1500);
  hostedHasUpdate();  // queries the C6 version

  uint32_t hM, hm, hp, sM, sm, sp;
  hostedGetHostVersion(&hM, &hm, &hp);
  hostedGetSlaveVersion(&sM, &sm, &sp);
  show("Needed by this firmware: %lu.%lu.%lu", hM, hm, hp);
  show("Installed on the C6:     %lu.%lu.%lu", sM, sm, sp);

  if (hM == sM && hm == sm && hp == sp) {
    show("Versions match, nothing to do. Flash the Coffee Scale firmware again.");
    return;
  }
  show("The C6 will be %s to %lu.%lu.%lu.", (sM > hM || (sM == hM && sm > hm)) ? "changed back" : "updated",
       hM, hm, hp);
  Serial.println();
  Serial.println("==> Type yes and press Enter to continue <==");
  String ok = readLine("> ", false);
  if (!ok.equalsIgnoreCase("yes")) { show("Cancelled."); return; }

  // SD card first: no Wi-Fi traffic on the C6 link while the image is written
  char sdPath[48];
  snprintf(sdPath, sizeof(sdPath), "/esp32c6-v%lu.%lu.%lu.bin", hM, hm, hp);
  static SPIClass sdSpi(FSPI);
  sdSpi.begin(43, 39, 44, 42);
  if (SD.begin(42, sdSpi, 25000000) && SD.exists(sdPath)) {
    // Only disconnect: switching Wi-Fi off would also shut down the ESP-Hosted
    // link to the C6 (nothing else is using it here) and the update could not start.
    WiFi.disconnect(false, false);
    delay(500);
    if (flashFromSd(sdPath)) {
      show("SUCCESS. Restarting...");
      delay(2000);
      ESP.restart();
    }
    show("Update FAILED. The C6 keeps its current firmware.");
    return;
  }
  show("No %s on the SD card, downloading over Wi-Fi instead.", sdPath);

  for (;;) {
    String ssid = readLine("Wi-Fi SSID: ", false);
    String pass = readLine("Wi-Fi password: ", true);
    show("Connecting to %s ...", ssid.c_str());
    WiFi.STA.connect(ssid.c_str(), pass.c_str());
    t0 = millis();
    while (WiFi.STA.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
    if (WiFi.STA.status() == WL_CONNECTED) break;
    show("Wi-Fi connection failed, try again.");
    WiFi.STA.disconnect();
  }
  show("Connected. Writing the C6 firmware (about 1 minute)...");

  if (flashC6()) {
    show("SUCCESS. Restarting...");
    delay(2000);
    ESP.restart();
  } else {
    show("Update FAILED. The C6 keeps its current firmware.");
  }
}

void loop() { delay(1000); }
