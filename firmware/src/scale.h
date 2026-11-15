#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <memory>
#include <atomic>

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
// sleeps after ~9 min idle, MEASURED — reconnection is a steady state, not
// an error. This said ~15 min until 2026-10-27; that figure's source was
// never recorded and docs/phase-0-status.md already carried the measured 9).
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
    // CONNECTING runs in connectTask, not loop(): `dev` belongs to that task
    // until `connect_result` leaves PENDING (release/acquire), and loop()
    // must not touch it before.
    std::atomic<uint8_t> connect_result{0};   // ConnectResult
    uint32_t connect_started_ms = 0;
    bool     connect_slow_logged = false;
    uint32_t connected_at_ms = 0;
  };
  enum ConnectResult : uint8_t { CONNECT_IDLE = 0, CONNECT_PENDING, CONNECT_OK, CONNECT_FAIL };

  static constexpr uint32_t STALE_MS      = 3000;
  static constexpr uint32_t BACKOFF_MIN_MS = 1000;
  static constexpr uint32_t BACKOFF_MAX_MS = 30000;
  // A link this healthy for this long earns a fresh minimum backoff.
  static constexpr uint32_t HEALTHY_MS     = 10000;
  // NimBLE's own connect timeout is 30 s and its service discovery waits
  // without one; past this, say so — a hung connect blocks the other slot.
  static constexpr uint32_t CONNECT_SLOW_MS = 45000;

  Slot slots[2];
  QueueHandle_t queue = nullptr;
  RemoteScalesScanner* scanner = nullptr;
  bool verbose = false;

  void tickSlot(Slot& s, uint32_t now);
  void dropConnection(Slot& s, uint32_t now, const char* why);
  bool startConnect(Slot& s, uint32_t now);
  void updateScanState();
  bool anyConnecting() const;

  // The blocking BLE connect, off loop(). Until 2026-11-15 tryConnect() ran
  // dev->connect() inline, and with the grinder scale gone loop() stalled
  // ~1.8 s every few seconds retrying it: switch presses fell inside the
  // stalls and were missed outright (shot 181), and the brew guard — which
  // needs those presses — went blind with them (invariant 2). Why "every
  // few seconds" rather than a growing backoff is INFERRED, not observed: a
  // scale that accepts the connection and then sends nothing went stale in
  // 3 s, and dropConnection() reset the backoff to 1 s each time. It now
  // keeps growing until a link has been healthy for HEALTHY_MS. (A draft of
  // this comment blamed the scanner for never forgetting a scale; it does —
  // every scan restart clears its list.)
  QueueHandle_t connect_queue = nullptr;
  static void connectTask(void* arg);

  static ScaleManager* self;                // for the C-style lib callbacks
  static void onWeightYield(float grams);
  static void onWeightDose(float grams);
  static void onLog(std::string msg);
  void enqueue(ScaleRole role, float grams);
};
