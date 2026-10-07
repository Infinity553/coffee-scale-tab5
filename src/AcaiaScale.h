#pragma once
#include <Arduino.h>
#include <vector>

// BLE driver for Acaia scales (Lunar, Pearl S, Pyxis, ...).
// Runs its own FreeRTOS task: scanning, connecting, heartbeat and
// automatic reconnect as soon as the scale is switched on again.

enum class ScaleState : uint8_t {
  Idle,          // BLE stack not started / paused
  Searching,     // scanning for the paired scale
  Connecting,
  Connected,     // connected and receiving weight
  Discovering,   // setup mode: listing all nearby Acaia scales
};

struct FoundScale {
  String  name;
  String  address;
  int     rssi;
};

class AcaiaScale {
 public:
  void begin();

  // Pairing target ("" = connect to the first Acaia scale found).
  void setTarget(const String& address);

  // Setup mode: scan and list scales instead of connecting.
  void startDiscovery();
  void stopDiscovery();
  std::vector<FoundScale> discovered();

  // Commands (queued, executed on the BLE task).
  void tare();
  void startTimer();
  void stopTimer();
  void resetTimer();
  void disconnect();

  ScaleState state() const { return state_; }
  bool       bleReady() const { return bleReady_; }   // Bluetooth stack (and the C6 link) is up
  bool       connected() const { return state_ == ScaleState::Connected; }
  String     connectedName();
  String     connectedAddress();

  // Latest weight. Returns true when a new reading arrived since last call.
  bool  poll(float& grams, uint32_t& atMs);
  float weight() const { return weight_; }
  int   battery() const { return battery_; }   // -1 = unknown
  uint32_t lastPacketMs() const { return lastPacketMs_; }

  // Internal (called from BLE callbacks)
  void onNotify(const uint8_t* data, size_t len);
  void onDisconnected();
  void onAdvertised(const String& name, const String& addr, int rssi, void* dev);

 private:
  static void taskEntry(void* arg);
  void taskLoop();
  bool connectTo(void* advertisedDevice);
  void sendRaw(const uint8_t* data, size_t len);
  void sendMessage(uint8_t type, const uint8_t* payload, size_t len);
  void sendIdent();
  void handleMessage(const uint8_t* msg, size_t len);
  void decodeWeight(const uint8_t* p, size_t len);

  enum Cmd : uint8_t { CMD_TARE, CMD_TSTART, CMD_TSTOP, CMD_TRESET, CMD_DISCONNECT };

  void*           cmdQueue_ = nullptr;   // QueueHandle_t
  void*           lock_ = nullptr;       // SemaphoreHandle_t
  volatile ScaleState state_ = ScaleState::Idle;
  volatile bool   discoveryMode_ = false;
  volatile bool   linkLost_ = false;
  volatile bool   bleReady_ = false;
  String          target_;
  String          connName_, connAddr_;
  std::vector<FoundScale> found_;
  void*           pendingDevice_ = nullptr;   // BLEAdvertisedDevice* to connect to

  volatile float    weight_ = 0;
  volatile bool     newWeight_ = false;
  volatile uint32_t weightMs_ = 0;
  volatile int      battery_ = -1;
  volatile uint32_t lastPacketMs_ = 0;

  uint8_t  rx_[128];
  size_t   rxLen_ = 0;
};

extern AcaiaScale scale;
