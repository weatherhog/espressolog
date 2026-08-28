#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <memory>

class RemoteScales;
class RemoteScalesScanner;

enum class ScaleRole : uint8_t { YIELD = 0, DOSE = 1 };

// Queue element, producer (NimBLE host task) → consumer (loop()). The BLE
// callback does nothing but convert g→mg, stamp millis() and enqueue —
// timestamps at arrival make the consumer latency-tolerant, and no detector
// or flash work ever runs on the BLE stack's task.
struct ScaleSample {
  uint32_t t_ms;
  int32_t  weight_mg;
  uint8_t  role;
};

// Dual-Bookoo manager around the vendored remote_scales library: slot per
// role, role bound to a MAC in NVS, reconnect with backoff (the Themis
// sleeps after ~15 min — reconnection is a steady state, not an error).
// Stage 1 uses only the YIELD slot; the DOSE slot arrives in stage 5.
class ScaleManager {
public:
  enum class SlotState : uint8_t { UNBOUND, SCANNING, CONNECTING, CONNECTED, LOST };

  ScaleManager();
  ~ScaleManager();   // out-of-line: unique_ptr<RemoteScales> needs the full type

  void begin(QueueHandle_t out_queue);
  void tick(uint32_t now_ms);
  bool connected(ScaleRole role) const;
  bool tare(ScaleRole role);
  SlotState slotState(ScaleRole role) const;
  String slotMac(ScaleRole role) const;
  void setVerbose(bool on) { verbose = on; }

  // CLI support: list Bookoos the scanner currently sees, and bind a role
  // to a MAC ("last" = most recently discovered). Binding persists to NVS
  // and enables the slot immediately.
  void listDiscovered(Stream& out);
  bool assign(ScaleRole role, const String& mac_or_last, Stream& out);

private:
  struct Slot {
    ScaleRole role;
    bool enabled = false;
    String bound_mac;                       // lowercase aa:bb:.. ; empty = unbound
    SlotState state = SlotState::UNBOUND;
    std::unique_ptr<RemoteScales> dev;
    volatile uint32_t last_sample_ms = 0;   // written from the BLE task
    uint32_t next_retry_ms = 0;
    uint32_t backoff_ms = 1000;
  };

  static constexpr uint32_t STALE_MS      = 3000;
  static constexpr uint32_t BACKOFF_MIN_MS = 1000;
  static constexpr uint32_t BACKOFF_MAX_MS = 30000;

  Slot slots[2];
  QueueHandle_t queue = nullptr;
  RemoteScalesScanner* scanner = nullptr;
  bool verbose = false;

  void tickSlot(Slot& s, uint32_t now);
  void dropConnection(Slot& s, uint32_t now, const char* why);
  bool tryConnect(Slot& s, uint32_t now);
  void updateScanState();

  static ScaleManager* self;                // for the C-style lib callbacks
  static void onWeightYield(float grams);
  static void onWeightDose(float grams);
  static void onLog(std::string msg);
  void enqueue(ScaleRole role, float grams);
};
