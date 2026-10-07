#include "AcaiaScale.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
#include "esp32-hal-hosted.h"
#endif

AcaiaScale scale;

#define LOCK()   xSemaphoreTake((SemaphoreHandle_t)lock_, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive((SemaphoreHandle_t)lock_)
#define CMDQ     ((QueueHandle_t)cmdQueue_)

// --- Acaia BLE protocol -------------------------------------------------------
// Newer scales (Lunar 2021+, Pearl S, Pyxis): separate write / notify chars.
static BLEUUID NEW_SERVICE("49535343-fe7d-4ae5-8fa9-9fafd205e455");
static BLEUUID NEW_WRITE  ("49535343-8841-43f4-a8d4-ecbe34729bb3");
static BLEUUID NEW_NOTIFY ("49535343-1e4d-4bd9-ba61-23c647249616");
// Older scales (Lunar pre-2021, Pearl): one characteristic for both.
static BLEUUID OLD_SERVICE("00001820-0000-1000-8000-00805f9b34fb");
static BLEUUID OLD_CHAR   ("00002a80-0000-1000-8000-00805f9b34fb");

static constexpr uint8_t HDR1 = 0xEF, HDR2 = 0xDD;
static constexpr uint8_t MSG_HEARTBEAT = 0x00;
static constexpr uint8_t MSG_TARE      = 0x04;
static constexpr uint8_t MSG_STATUS    = 0x08;
static constexpr uint8_t MSG_IDENT     = 0x0B;
static constexpr uint8_t MSG_EVENT     = 0x0C;
static constexpr uint8_t MSG_TIMER     = 0x0D;

static constexpr uint32_t HEARTBEAT_MS   = 2750;
static constexpr uint32_t REIDENT_MS     = 6000;   // no data -> re-send ident
static constexpr uint32_t LINK_DEAD_MS   = 15000;  // no data -> drop link
static constexpr uint32_t SCAN_SECONDS   = 3;

// --- BLE objects (only touched from the BLE task) ------------------------------
static BLEClient*               client     = nullptr;
static BLERemoteCharacteristic* writeChar  = nullptr;
static BLERemoteCharacteristic* notifyChar = nullptr;
static bool                     writeWithResponse = false;

static bool isAcaiaName(const String& n) {
  String s = n;
  s.toUpperCase();
  return s.startsWith("LUNAR") || s.startsWith("ACAIA") || s.startsWith("PROCH") ||
         s.startsWith("PYXIS") || s.startsWith("PEARL") || s.startsWith("CINCO") ||
         s.startsWith("UMBRA");
}

class AdvCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (!dev.haveName()) return;
    String name = dev.getName().c_str();
    if (!isAcaiaName(name)) return;
    String addr = dev.getAddress().toString().c_str();
    scale.onAdvertised(name, addr, dev.getRSSI(), &dev);
  }
};

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override { scale.onDisconnected(); }
};

static void notifyCb(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  scale.onNotify(data, len);
}

static AdvCallbacks    advCallbacks;
static ClientCallbacks clientCallbacks;

// --- public API ---------------------------------------------------------------
void AcaiaScale::begin() {
  lock_ = xSemaphoreCreateMutex();
  cmdQueue_ = xQueueCreate(8, sizeof(uint8_t));
  xTaskCreatePinnedToCore(taskEntry, "acaia", 8192, this, 3, nullptr, 0);
}

void AcaiaScale::setTarget(const String& address) {
  LOCK();
  target_ = address;
  target_.toLowerCase();
  UNLOCK();
}

void AcaiaScale::startDiscovery() {
  LOCK();
  found_.clear();
  UNLOCK();
  discoveryMode_ = true;
  if (connected()) disconnect();
}

void AcaiaScale::stopDiscovery() { discoveryMode_ = false; }

std::vector<FoundScale> AcaiaScale::discovered() {
  LOCK();
  auto copy = found_;
  UNLOCK();
  return copy;
}

static void pushCmd(QueueHandle_t q, uint8_t c) { if (q) xQueueSend(q, &c, 0); }
void AcaiaScale::tare()       { pushCmd(CMDQ, CMD_TARE); }
void AcaiaScale::startTimer() { pushCmd(CMDQ, CMD_TSTART); }
void AcaiaScale::stopTimer()  { pushCmd(CMDQ, CMD_TSTOP); }
void AcaiaScale::resetTimer() { pushCmd(CMDQ, CMD_TRESET); }
void AcaiaScale::disconnect() { pushCmd(CMDQ, CMD_DISCONNECT); }

String AcaiaScale::connectedName() {
  LOCK();
  String s = connName_;
  UNLOCK();
  return s;
}

String AcaiaScale::connectedAddress() {
  LOCK();
  String s = connAddr_;
  UNLOCK();
  return s;
}

bool AcaiaScale::poll(float& grams, uint32_t& atMs) {
  if (!newWeight_) return false;
  newWeight_ = false;
  grams = weight_;
  atMs = weightMs_;
  return true;
}

// --- callbacks ----------------------------------------------------------------
void AcaiaScale::onAdvertised(const String& name, const String& addr, int rssi, void* dev) {
  if (discoveryMode_) {
    LOCK();
    bool known = false;
    for (auto& f : found_) {
      if (f.address == addr) { f.rssi = rssi; f.name = name; known = true; }
    }
    if (!known && found_.size() < 8) found_.push_back({name, addr, rssi});
    UNLOCK();
    return;
  }
  if (pendingDevice_) return;
  LOCK();
  bool match = target_.isEmpty() || target_.equalsIgnoreCase(addr);
  UNLOCK();
  if (match) {
    pendingDevice_ = new BLEAdvertisedDevice(*static_cast<BLEAdvertisedDevice*>(dev));
    BLEDevice::getScan()->stop();
  }
}

void AcaiaScale::onDisconnected() { linkLost_ = true; }

void AcaiaScale::onNotify(const uint8_t* data, size_t len) {
  lastPacketMs_ = millis();
  // Append to the reassembly buffer (messages may be split / concatenated).
  if (rxLen_ + len > sizeof(rx_)) rxLen_ = 0;
  memcpy(rx_ + rxLen_, data, len);
  rxLen_ += len;

  size_t pos = 0;
  while (true) {
    // find header
    while (pos + 1 < rxLen_ && !(rx_[pos] == HDR1 && rx_[pos + 1] == HDR2)) pos++;
    if (pos + 4 > rxLen_) break;  // need header, type, length
    uint8_t type = rx_[pos + 2];
    size_t msgLen;
    if (type == MSG_EVENT || type == MSG_STATUS) {
      msgLen = (size_t)rx_[pos + 3] + 5;
      if (msgLen > 64) { pos += 2; continue; }   // garbage
      if (pos + msgLen > rxLen_) break;          // wait for rest
    } else {
      // unknown framing: take everything up to the next header
      size_t nxt = pos + 2;
      while (nxt + 1 < rxLen_ && !(rx_[nxt] == HDR1 && rx_[nxt + 1] == HDR2)) nxt++;
      msgLen = (nxt + 1 < rxLen_) ? nxt - pos : rxLen_ - pos;
    }
    handleMessage(rx_ + pos, msgLen);
    pos += msgLen;
  }
  if (pos > 0) {
    memmove(rx_, rx_ + pos, rxLen_ - pos);
    rxLen_ -= pos;
  }
}

void AcaiaScale::decodeWeight(const uint8_t* p, size_t len) {
  if (len < 6) return;
  int32_t raw = ((int32_t)p[2] << 16) | ((int32_t)p[1] << 8) | p[0];
  uint8_t unit = p[4];
  float w = (float)raw;
  static const float div[] = {1, 10, 100, 1000, 10000};
  if (unit <= 4) w /= div[unit];
  if (p[5] & 0x02) w = -w;
  weight_ = w;
  weightMs_ = millis();
  newWeight_ = true;
}

void AcaiaScale::handleMessage(const uint8_t* m, size_t len) {
  if (len < 5) return;
  uint8_t type = m[2];
  if (type == MSG_EVENT) {
    uint8_t evt = m[4];
    const uint8_t* p = m + 5;
    size_t plen = len >= 7 ? len - 7 : 0;  // strip header/type/len/evt + checksum
    if (evt == 5) {                        // weight
      decodeWeight(p, plen);
    } else if (evt == 11) {                // heartbeat reply, may embed weight
      if (plen >= 9 && p[2] == 5) decodeWeight(p + 3, plen - 3);
    } else if (evt == 8) {                 // button / tare / timer events
      if (plen >= 8 && p[1] == 5) decodeWeight(p + 2, plen - 2);
    }
  } else if (type == MSG_STATUS) {
    uint8_t b = m[4] & 0x7F;
    if (b <= 100) battery_ = b;
  }
}

// --- BLE task -----------------------------------------------------------------
void AcaiaScale::taskEntry(void* arg) { static_cast<AcaiaScale*>(arg)->taskLoop(); }

void AcaiaScale::sendRaw(const uint8_t* data, size_t len) {
  if (!writeChar || !client || !client->isConnected()) return;
  writeChar->writeValue(const_cast<uint8_t*>(data), len, writeWithResponse);
}

void AcaiaScale::sendMessage(uint8_t type, const uint8_t* payload, size_t len) {
  uint8_t buf[40];
  if (len + 5 > sizeof(buf)) return;
  buf[0] = HDR1;
  buf[1] = HDR2;
  buf[2] = type;
  uint8_t ck1 = 0, ck2 = 0;
  for (size_t i = 0; i < len; i++) {
    buf[3 + i] = payload[i];
    if (i % 2 == 0) ck1 += payload[i]; else ck2 += payload[i];
  }
  buf[3 + len] = ck1;
  buf[4 + len] = ck2;
  sendRaw(buf, len + 5);
}

bool AcaiaScale::connectTo(void* advDev) {
  auto* dev = static_cast<BLEAdvertisedDevice*>(advDev);
  state_ = ScaleState::Connecting;
  writeChar = notifyChar = nullptr;

  if (!client) {
    client = BLEDevice::createClient();
    client->setClientCallbacks(&clientCallbacks);
  }
  linkLost_ = false;
  if (!client->connectTimeout(dev, 8000)) {
    log_w("connect failed");
    return false;
  }

  BLERemoteService* svc = client->getService(NEW_SERVICE);
  if (svc) {
    writeChar  = svc->getCharacteristic(NEW_WRITE);
    notifyChar = svc->getCharacteristic(NEW_NOTIFY);
  } else if ((svc = client->getService(OLD_SERVICE))) {
    writeChar = notifyChar = svc->getCharacteristic(OLD_CHAR);
  }
  if (!writeChar || !notifyChar) {
    log_w("Acaia service not found");
    client->disconnect();
    return false;
  }
  writeWithResponse = !writeChar->canWriteNoResponse();
  if (notifyChar->canNotify()) notifyChar->registerForNotify(notifyCb);

  rxLen_ = 0;
  LOCK();
  connName_ = dev->getName().c_str();
  connAddr_ = dev->getAddress().toString().c_str();
  UNLOCK();

  delay(100);
  sendIdent();
  lastPacketMs_ = millis();
  state_ = ScaleState::Connected;
  return true;
}

void AcaiaScale::sendIdent() {
  // "012345678901234" identifies us as an app
  static const uint8_t ident[] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                  0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34};
  // request weight (5), battery (2), timer (1), key (3) notifications
  static const uint8_t notifyReq[] = {9, 0, 1, 1, 2, 2, 5, 3, 4};
  sendMessage(MSG_IDENT, ident, sizeof(ident));
  delay(80);
  sendMessage(MSG_EVENT, notifyReq, sizeof(notifyReq));
}

void AcaiaScale::taskLoop() {
  // Bring up Bluetooth (on the Tab5 this also starts the link to the C6 radio
  // chip). If that fails, keep retrying instead of using a stack that isn't there.
  for (int attempt = 1; ; attempt++) {
    BLEDevice::init("CoffeeScaleTab5");
    if (BLEDevice::getInitialized()) break;
    Serial.printf("[BLE] Bluetooth start failed (attempt %d), retrying in 5 s\n", attempt);
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
    uint32_t hM, hm, hp, sM, sm, sp;
    hostedGetHostVersion(&hM, &hm, &hp);
    hostedGetSlaveVersion(&sM, &sm, &sp);
    if (hM != sM || hm != sm || hp != sp)
      Serial.printf("[BLE] the C6 runs ESP-Hosted %lu.%lu.%lu but this firmware needs %lu.%lu.%lu: "
                    "run the C6 updater (pio run -e c6-update -t upload)\n", sM, sm, sp, hM, hm, hp);
#endif
    BLEDevice::deinit(false);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  {
    uint32_t hM, hm, hp, sM, sm, sp;
    hostedGetHostVersion(&hM, &hm, &hp);
    hostedGetSlaveVersion(&sM, &sm, &sp);
    Serial.printf("[BLE] ESP-Hosted host %lu.%lu.%lu, C6 co-processor %lu.%lu.%lu, BLE %s\n",
                  hM, hm, hp, sM, sm, sp, hostedIsBLEActive() ? "active" : "NOT active");
    if (hostedHasUpdate()) {
      Serial.printf("[BLE] C6 firmware is older than the host; update from: %s\n",
                    hostedGetUpdateURL());
    }
  }
#endif
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&advCallbacks, true);
  scan->setActiveScan(true);
  // ~30 % duty cycle: the radio is shared with Wi-Fi on the C6, and the Lunar
  // advertises every ~100 ms, so it is still found within a second or two.
  scan->setInterval(160);
  scan->setWindow(48);
  bleReady_ = true;

  uint32_t lastHeartbeat = 0;
  uint32_t lastIdent = 0;

  for (;;) {
    // ---------------- not connected: discover / search ----------------
    if (state_ != ScaleState::Connected) {
      xQueueReset(CMDQ);
      if (discoveryMode_) {
        state_ = ScaleState::Discovering;
        scan->start(2, false);
        scan->clearResults();
        continue;
      }
      state_ = ScaleState::Searching;
      if (!pendingDevice_) {
        scan->start(SCAN_SECONDS, false);
        scan->clearResults();
      }
      if (pendingDevice_ && !discoveryMode_) {
        auto* dev = static_cast<BLEAdvertisedDevice*>(pendingDevice_);
        bool ok = connectTo(dev);
        delete dev;
        pendingDevice_ = nullptr;
        if (!ok) {
          state_ = ScaleState::Searching;
          vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
          lastHeartbeat = lastIdent = millis();
        }
      } else {
        delete static_cast<BLEAdvertisedDevice*>(pendingDevice_);
        pendingDevice_ = nullptr;
        vTaskDelay(pdMS_TO_TICKS(500));   // breathing room for Wi-Fi between scans
      }
      continue;
    }

    // ---------------- connected ----------------
    if (linkLost_ || !client->isConnected() || discoveryMode_) {
      if (client->isConnected()) client->disconnect();
      writeChar = notifyChar = nullptr;
      state_ = ScaleState::Searching;
      battery_ = -1;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    uint8_t cmd;
    while (xQueueReceive(CMDQ, &cmd, 0) == pdTRUE) {
      switch (cmd) {
        case CMD_TARE: {
          uint8_t z[17] = {0};
          sendMessage(MSG_TARE, z, sizeof(z));
          break;
        }
        case CMD_TSTART: { uint8_t p[] = {0, 0}; sendMessage(MSG_TIMER, p, 2); break; }
        case CMD_TRESET: { uint8_t p[] = {0, 1}; sendMessage(MSG_TIMER, p, 2); break; }
        case CMD_TSTOP:  { uint8_t p[] = {0, 2}; sendMessage(MSG_TIMER, p, 2); break; }
        case CMD_DISCONNECT:
          client->disconnect();
          linkLost_ = true;
          break;
      }
    }

    uint32_t now = millis();
    if (now - lastHeartbeat > HEARTBEAT_MS) {
      uint8_t hb[] = {2, 0};
      sendMessage(MSG_HEARTBEAT, hb, sizeof(hb));
      lastHeartbeat = now;
    }
    uint32_t silent = now - lastPacketMs_;
    if (silent > LINK_DEAD_MS) {
      log_w("scale silent, dropping link");
      client->disconnect();
      linkLost_ = true;
    } else if (silent > REIDENT_MS && now - lastIdent > REIDENT_MS) {
      sendIdent();
      lastIdent = now;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
