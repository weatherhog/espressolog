#pragma once
#include <stdint.h>

// The most recent counted flowmeter pulses, as millis() timestamps, so a shot
// can carry the pulse train from the lever press to first drip (spool v4):
// the pre-infusion phase, which the per-sample inlet_pulses window — opened
// at first drip — never sees.
//
// Pure logic, host-tested (test/host/pulse_log_test.cpp). Fed from
// pollFlow() with each pulse the glitch filter COUNTED, so the train matches
// the counts the shot already stores.
//
// 512 entries is ~22 s at the ~23 pulses/s a 2026-11-13 flush showed: more
// than a press-to-drip phase (seconds) needs, so the pulses are still here
// at pour start, when they are copied out.
class PulseLog {
public:
  static constexpr uint16_t N = 512;

  void push(uint32_t ms) {
    ring[head] = ms;
    head = (uint16_t)((head + 1) % N);
    if (count < N) count++;
  }

  void clear() { head = count = 0; }

  // Copies the pulses in [from_ms, to_ms] into out[] as offsets from from_ms,
  // oldest first, at most max_out of them. Returns how many pulses lay in the
  // window in all — more than were copied if max_out was too small. If the
  // ring may have lost pulses from the start of the window (it is full and
  // its oldest entry is already inside it), `complete` is set false: the
  // total is then a lower bound. Offsets saturate at 65535 ms; callers keep
  // windows shorter than that.
  uint16_t extract(uint32_t from_ms, uint32_t to_ms, uint16_t* out, uint16_t max_out,
                   uint16_t* copied, bool* complete) const {
    uint16_t total = 0, n = 0;
    const uint32_t span = to_ms - from_ms;
    const uint16_t oldest = (uint16_t)((head + N - count) % N);
    for (uint16_t i = 0; i < count; i++) {
      uint32_t t = ring[(oldest + i) % N];
      uint32_t off = t - from_ms;                    // unsigned: wrap-safe
      if (off > span) continue;                      // before from_ms or after to_ms
      if (total < 0xFFFF) total++;
      if (n < max_out) out[n++] = off > 0xFFFF ? 0xFFFF : (uint16_t)off;
    }
    *copied = n;
    // Full ring whose oldest pulse is already inside the window: anything
    // older than that, also inside the window, has been overwritten.
    *complete = !(count == N && (uint32_t)(ring[oldest] - from_ms) <= span);
    return total;
  }

private:
  uint32_t ring[N] = {};
  uint16_t head = 0;
  uint16_t count = 0;
};
