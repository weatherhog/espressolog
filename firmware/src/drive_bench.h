#pragma once
#include <stdint.h>

// Bench timing of the drive side's 74HC221 channels through their Q
// readbacks (docs/drive-bench.md). Pure logic, host-tested
// (test/host/drive_bench_test.cpp); main.cpp's `drive` command owns the
// pins and feeds this with sampled levels.
//
// THIS IS A BENCH TOOL, NOT `drivetest`. It times one channel per run and
// reports what it saw; it does not decide whether a press would latch, and
// it reads no machine line. The on-machine `drivetest` of
// docs/phase-0-plan.md still has open design defects (a positive control
// for the sense line, continuous sense sampling, the half-failed photoMOS)
// and is not built.

// One Q readback, sampled continuously from the trigger.
struct QTrace {
  int64_t rise_us = -1;   // first LOW->HIGH, µs after the trigger
  int64_t fall_us = -1;   // first HIGH->LOW after that rise
  uint16_t edges = 0;     // every level change seen
  bool level = false;

  void feed(bool lvl, int64_t t_us) {
    if (lvl == level) return;
    if (edges < 0xFFFF) edges++;
    if (lvl && rise_us < 0) rise_us = t_us;
    else if (!lvl && rise_us >= 0 && fall_us < 0) fall_us = t_us;
    level = lvl;
  }
  bool rose() const { return rise_us >= 0; }
  bool fell() const { return fall_us >= 0; }
  // High-time of the pulse, µs; -1 if it never completed.
  int64_t width_us() const { return rose() && fell() ? fall_us - rise_us : -1; }
};

// What one run found. Ordered most severe first.
enum class BenchVerdict : uint8_t {
  OK,
  HELD,          // Q still high at the cap: an open or high R_ext (a HOLD), reset by dropping the arm
  NO_RISE,       // Q never went high: open C_ext, dead channel, missing chip, unarmed
  OTHER_MOVED,   // a channel that was not fired changed level: a bridge or a cross-fire
  CUT_BY_GATE,   // the drive pulse ended with the gate's: the gate is narrower than the drive
  RETRIGGERED,   // more than one pulse on the fired channel: noise on A̅ or R̅, or a bad '221
};

inline const char* verdictName(BenchVerdict v) {
  switch (v) {
    case BenchVerdict::OK:          return "ok";
    case BenchVerdict::HELD:        return "HELD at the cap (open/high R_ext?)";
    case BenchVerdict::NO_RISE:     return "NO PULSE (open C_ext, chip missing, or not armed)";
    case BenchVerdict::OTHER_MOVED: return "ANOTHER CHANNEL MOVED (bridge or cross-fire)";
    case BenchVerdict::CUT_BY_GATE: return "CUT BY THE GATE (gate pulse shorter than drive)";
    case BenchVerdict::RETRIGGERED: return "RETRIGGERED (more than one pulse)";
  }
  return "?";
}

// A drive pulse whose fall lies within this of the gate's is taken to have
// been ended by the gate resetting it, not by its own RC.
static constexpr int64_t CUT_TOLERANCE_US = 200;

// Judge one run. `fired` is the channel triggered (may be the gate itself);
// `gate` is the gate's trace (nullptr when the run had no gate, or when the
// fired channel IS the gate); `others` are the channels that were not
// fired and must not have moved; `held_at_cap` says the cap was reached
// with `fired` still high.
inline BenchVerdict judge(const QTrace& fired, const QTrace* gate,
                          const QTrace* const* others, uint8_t n_others,
                          bool held_at_cap) {
  if (held_at_cap) return BenchVerdict::HELD;
  if (!fired.rose()) return BenchVerdict::NO_RISE;
  for (uint8_t i = 0; i < n_others; i++)
    if (others[i] && others[i]->edges) return BenchVerdict::OTHER_MOVED;
  if (gate && gate->fell() && fired.fell()) {
    int64_t d = fired.fall_us - gate->fall_us;
    if (d < 0) d = -d;
    if (d <= CUT_TOLERANCE_US) return BenchVerdict::CUT_BY_GATE;
  }
  if (fired.edges > 2) return BenchVerdict::RETRIGGERED;
  return BenchVerdict::OK;
}

// Pulse widths over repeated runs, for the sweep.
struct WidthStats {
  uint16_t n = 0;
  int64_t min_us = 0, max_us = 0, sum_us = 0;
  void add(int64_t w) {
    if (w < 0) return;
    if (n == 0 || w < min_us) min_us = w;
    if (n == 0 || w > max_us) max_us = w;
    sum_us += w;
    n++;
  }
  int64_t mean_us() const { return n ? sum_us / n : 0; }
};
