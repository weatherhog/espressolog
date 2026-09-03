#pragma once
#include <stdint.h>
#include <stddef.h>

// On-flash sample record, per CLAUDE.md. In 0a inlet_pulses is always 0 and
// temp_dc is always INT16_MIN (no reading) — the fields exist so the flash
// format never changes when 0c/0d/0e arrive.
typedef struct __attribute__((packed)) {
  uint16_t t_ms;          // offset from shot start
  int32_t  weight_mg;     // signed; tare drift goes negative
  uint16_t inlet_pulses;  // cumulative
  int16_t  temp_dc;       // decidegrees C; INT16_MIN = no reading
} sample_t;               // 10 bytes

enum class ShotState : uint8_t { IDLE, ARMED, POURING, SETTLING };

// Everything integer: mg, ms, mg/s. detector_version is stamped by the
// caller (spool/upload layer) — this struct is measurement only.
struct ShotResult {
  bool     valid;          // false: rejected (<5 g) or faulted
  bool     fault;          // cup removed mid-pour
  bool     truncated;      // hit the uint16 t_ms ceiling, stop was forced
  uint32_t started_at_ms;  // millis() of t=0 (backdated to the flow crossing)
  uint32_t stop_ms;        // t of first sample below the stop threshold
  int32_t  yield_at_stop_mg;
  int32_t  yield_final_mg;
  int32_t  settle_offset_mg;   // final − at_stop. THE PHASE-1 CALIBRATION.
  int32_t  peak_flow_mgps;
  int32_t  mean_flow_mgps;     // over 5 g-out .. 80 % of final
  uint32_t flow_win_start_ms;
  uint32_t flow_win_end_ms;
  uint16_t sample_count;
  const sample_t* samples;
};

// Shot state machine, thresholds per CLAUDE.md. Pure logic: no I/O, no
// millis() of its own — it consumes samples timestamped at BLE arrival, so
// consumer latency can never distort the curve and dt is never assumed
// uniform. feed() returns true when a completed ShotResult is available
// (including rejects and faults — the caller decides their disposition).
//
// 0a.3: arms on ANY stable weight, not just zero — the stable value becomes
// the baseline and the curve is recorded relative to it. Taring stopped
// being load-bearing after two real shots were lost to a skipped tare
// (2026-08-28 and 2026-09-02); a tare is just a baseline the scale applied
// for you, and detection never needed the absolute value.
// Anonymous stable-weight events, per CLAUDE.md's weighing attribution: the
// UI never asks "are you about to weigh a dose?" — every stable reading is
// recorded blind and a closing shot claims the last one retroactively on
// the server. Runs per scale, both roles: one scale doing both jobs must
// work (a flat battery degrades the data, never the workflow).
//
// An episode emits once: either when stability breaks (dose lifted off) or
// at a 30 s cap (parked cup — capping means the event exists even if the
// scale then sleeps). More weight landing later starts a new episode; the
// last event wins at attribution, earlier ones get superseded there.
// The 12–24 g dose plausibility window is applied at attribution time, not
// here — the schema wants everything the scale actually saw.
class WeighingDetector {
public:
  static constexpr int32_t  STABLE_FLOW_MGPS = 50;    // |dw/dt| < 0.05 g/s
  static constexpr uint32_t STABLE_HOLD_MS   = 1500;
  static constexpr uint32_t STABLE_CAP_MS    = 30000;
  static constexpr int32_t  MIN_WEIGHT_MG    = 5000;
  static constexpr uint32_t FLOW_WINDOW_MS   = 700;

  struct Event {
    uint32_t started_at_ms;  // millis() when stability began
    int32_t  grams_mg;       // mean over the stable span
    uint32_t stable_ms;
  };

  // shot_active: a shot is POURING/SETTLING anywhere in the system.
  // Returns true when an event completed; read it via event().
  bool feed(uint32_t t_ms, int32_t weight_mg, bool shot_active);
  const Event& event() const { return ev; }

private:
  struct RingEntry { uint32_t t; int32_t w; };
  static constexpr size_t RING_N = 16;

  RingEntry ring[RING_N] = {};
  size_t ring_head = 0, ring_count = 0;

  uint32_t stable_since = 0;   // 0 = not stable
  int64_t  sum_mg = 0;
  uint32_t n = 0;
  bool     emitted = false;
  Event    ev = {};

  int32_t computeFlow(uint32_t t, int32_t w) const;
  bool emit(uint32_t now);
};

class ShotDetector {
public:
  static constexpr size_t   MAX_SAMPLES     = 2048;   // >3 min @ 10 Hz, 20 KB SRAM
  static constexpr int32_t  ARM_BAND_MG     = 500;
  static constexpr uint32_t ARM_HOLD_MS     = 1000;
  static constexpr int32_t  POUR_FLOW_MGPS  = 300;
  static constexpr uint32_t POUR_HOLD_MS    = 300;
  static constexpr int32_t  STOP_FLOW_MGPS  = 100;
  static constexpr uint32_t STOP_HOLD_MS    = 1500;
  static constexpr uint32_t SETTLE_MS       = 5000;
  static constexpr int32_t  REJECT_MIN_MG   = 5000;
  static constexpr int32_t  LIFT_DROP_MG    = 5000;   // cup this far below peak = being lifted
  static constexpr uint32_t LIFT_HOLD_MS    = 2000;   // sustained this long = really lifted, not a bump
  static constexpr uint32_t FLOW_WINDOW_MS  = 700;
  static constexpr uint32_t MAX_SHOT_MS     = 60000;  // force-stop before uint16 t_ms wraps
  // ARMED-state discrimination (0a.2/0a.3, learned from real shots):
  // a cup landing/lifting arrives as one huge sample step; first drips
  // arrive as a slow positive creep; a parked mass drifting from the
  // baseline is a new baseline, not a shot.
  static constexpr int32_t  PLACEMENT_STEP_MG = 5000;  // per-sample jump = cup moved
  static constexpr int32_t  PARKED_FLOW_MGPS  = 50;
  static constexpr uint32_t PARKED_HOLD_MS    = 2000;

  // The armed baseline (mg, absolute scale reading). Meaningful in ARMED
  // and during a shot; recorded weights are relative to it.
  int32_t baselineMg() const { return baseline; }

  bool feed(uint32_t t_ms, int32_t weight_mg);
  ShotState state() const { return st; }
  const ShotResult& result() const { return res; }
  int32_t flowNowMgps() const { return flow_now; }

private:
  struct RingEntry { uint32_t t; int32_t w; };
  static constexpr size_t RING_N = 64;   // ~6 s of pre-history @ 10 Hz

  ShotState st = ShotState::IDLE;
  ShotResult res = {};

  RingEntry ring[RING_N] = {};
  size_t ring_head = 0, ring_count = 0;
  int32_t flow_now = 0;

  sample_t buf[MAX_SAMPLES];
  uint16_t buf_count = 0;
  uint32_t t0 = 0;               // millis() of shot start
  int32_t  baseline = 0;         // absolute reading the curve is relative to
  uint32_t pour_cand_since = 0;  // 0 = no candidate
  uint32_t parked_since = 0;     // 0 = not parked at a new value
  uint32_t stop_cand_at = 0;     // 0 = no candidate
  uint32_t lift_since = 0;       // 0 = not currently dropped below peak
  uint16_t lift_buf = 0;         // buf_count at the moment the drop began
  int32_t  stop_cand_w = 0;
  uint32_t settle_until = 0;
  int32_t  running_max = 0;
  int32_t  last_w = 0;

  void ringPush(uint32_t t, int32_t w);
  int32_t computeFlow(uint32_t t, int32_t w) const;
  void appendSample(uint32_t t, int32_t w);
  void beginPour(uint32_t crossing_t);
  void enterSettling(uint32_t t, int32_t w_at_stop, uint32_t stop_t, bool truncated);
  bool finishShot();
  bool finalizeLifted();   // cup removed → finalize at the peak weight
  void computeFlowWindow();
  void resetToIdle();
};
