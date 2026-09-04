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
  // Stability is judged per sample (|Δ| ≤ STEP_MG — Bookoo jitter flips
  // ±0.1 g between samples, i.e. 0.2 g steps) plus a span range check
  // (max−min ≤ RANGE_MG), so a cup landing costs no settle window and a
  // grinder filling the cup (~1 g/s) breaks the range within 0.4 s. A dose
  // cup needs ~1.1 s on the scale. (The old flow-window rule needed 2.2 s;
  // Lucas's dose cup sits for ~2.0 s — shot 20's dose was missed by 0.2 s.)
  static constexpr int32_t  STEP_MG          = 250;
  static constexpr int32_t  RANGE_MG         = 300;
  static constexpr uint32_t STABLE_HOLD_MS   = 1000;
  static constexpr uint32_t STABLE_CAP_MS    = 30000;
  static constexpr int32_t  MIN_WEIGHT_MG    = 5000;

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
  uint32_t stable_since = 0;   // 0 = not stable
  int64_t  sum_mg = 0;
  uint32_t n = 0;
  int32_t  span_min = 0, span_max = 0;
  int32_t  prev_w = 0;
  bool     have_prev = false;
  bool     emitted = false;
  Event    ev = {};

  bool emit(uint32_t now);
};

class ShotDetector {
public:
  static constexpr size_t   MAX_SAMPLES     = 2048;   // >3 min @ 10 Hz, 20 KB SRAM
  static constexpr int32_t  ARM_BAND_MG     = 500;
  static constexpr uint32_t ARM_HOLD_MS     = 1000;
  static constexpr int32_t  POUR_FLOW_MGPS  = 300;
  static constexpr uint32_t POUR_HOLD_MS    = 300;
  // Stop (0a.5): the weight is FLAT — within STOP_DELTA_MG over the last
  // STOP_WINDOW_MS. Not "instantaneous flow < 0.1 g/s": the Bookoo's ±0.1 g
  // jitter over the 700 ms flow window reads as ±0.2–0.8 g/s while the cup
  // sits dead still, so that rule almost never confirmed (shot 19,
  // 2026-09-04: 13 s of flat weight, stop never fired). A 2 s net delta
  // averages the jitter out. Only checked once the shot has substance
  // (running_max ≥ REJECT_MIN_MG) so a pre-infusion soak isn't a stop.
  static constexpr uint32_t STOP_WINDOW_MS  = 2000;
  static constexpr int32_t  STOP_DELTA_MG   = 300;
  // Below REJECT_MIN_MG the stop rule doesn't apply (a pre-infusion soak is
  // flat and sub-5 g). So a false trigger — a knock, a drip — needs its own
  // exit or it would sit in POURING and swallow the next real shot into its
  // record: flat for ABORT_FLAT_MS while still under 5 g → reject. Longer
  // than any soak on this machine.
  static constexpr uint32_t ABORT_FLAT_MS   = 8000;
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
  static constexpr size_t RING_N = 128;  // ~12 s @ 10 Hz: covers ABORT_FLAT_MS + backdating

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
  uint32_t lift_since = 0;       // 0 = not currently dropped below peak
  uint16_t lift_buf = 0;         // buf_count at the moment the drop began
  uint32_t last_step_at = 0;     // last >5 g single-sample step (cup moved); 0 = none
  uint32_t settle_until = 0;
  int32_t  running_max = 0;
  int32_t  last_w = 0;

  void ringPush(uint32_t t, int32_t w);
  int32_t computeFlow(uint32_t t, int32_t w) const;
  // Newest ring sample at or before target_t (oldest if none is that old).
  bool ringAt(uint32_t target_t, uint32_t& t_out, int32_t& w_out) const;
  // Shared ARMED/IDLE pour-onset logic. Returns true when a pour began.
  bool tryPourOnset(uint32_t t, int32_t w, int32_t prev_w, bool armed);
  void appendSample(uint32_t t, int32_t w);
  void beginPour(uint32_t crossing_t);
  void enterSettling(uint32_t t, int32_t w_at_stop, uint32_t stop_t, bool truncated);
  bool finishShot();
  bool finalizeLifted();   // cup removed → finalize at the peak weight
  void computeFlowWindow();
  void resetToIdle();
};
