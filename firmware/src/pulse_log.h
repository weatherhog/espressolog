#pragma once
#include <stdint.h>

// The most recent counted flowmeter pulses, as millis() timestamps, so a shot
// can carry the pulse train from the lever press to pour confirmation (spool
// v4): the pre-infusion phase, which the per-sample inlet_pulses window —
// opened at pour confirmation — never sees.
//
// Pure logic, host-tested (test/host/pulse_log_test.cpp). Fed from
// pollFlow() with each pulse the glitch filter COUNTED, so the train matches
// the counts the shot already stores.
//
// 512 entries is ~22 s at the ~23 pulses/s a 2026-11-13 flush showed: more
// than a press-to-confirmation phase (seconds) needs, so the pulses are still here
// at pour start, when they are copied out.
class PulseLog {
public:
  static constexpr uint16_t N = 512;
  static constexpr uint32_t GAP_SLACK_MS = 2000;
  static constexpr uint32_t RUNNING_MS = 1000;   // idleBefore(): see there

  void push(uint32_t ms) {
    ring[head] = ms;
    head = (uint16_t)((head + 1) % N);
    if (count < N) count++;
  }

  // The log was emptied at now_ms (flow re-enabled): pulses before it are
  // gone, and a window reaching back past it is incomplete.
  void clear(uint32_t now_ms) {
    head = count = 0;
    markGap(now_ms);
  }

  // Pulses were lost upstream (the ISR's ring overflowed) and noticed at
  // now_ms — by the next drain, so the loss itself was up to one loop (or
  // one stall) earlier. A window it falls in, or ends shortly before, may be
  // missing some.
  void markGap(uint32_t now_ms) {
    gap_ms = now_ms;
    have_gap = true;
  }

  // Copies the pulses in [from_ms, to_ms] into out[] as offsets from from_ms,
  // oldest first, at most max_out of them. Returns how many pulses lay in the
  // window in all — more than were copied if max_out was too small.
  // `complete` is false when pulses in the window may be missing, so the
  // total is only a lower bound: the ring is full and its oldest entry is
  // not before the window (it has overwritten the start of it — or all of
  // it, review 2026-11-15), or a gap (clear(), markGap()) was recorded at or
  // in the window or within GAP_SLACK_MS after its end (the drain notices a
  // loss late). Offsets saturate at 65535 ms; callers keep windows shorter
  // than that.
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
    // A full ring whose oldest entry is at or after from_ms has overwritten
    // whatever lay between from_ms and it. (Until 2026-11-15 this only
    // caught an oldest entry INSIDE the window, so a window overwritten
    // entirely read "complete, 0 pulses".)
    const bool overwritten = count == N && (int32_t)(ring[oldest] - from_ms) >= 0;
    // Unsigned offset from the window's start, like the pulses: a gap long
    // before the window is a huge offset and does not count, and — unlike the
    // signed compare used until review 2026-11-15 — a gap 25 days old does
    // not flip sign and condemn every window after it.
    const bool gapped = have_gap && (uint32_t)(gap_ms - from_ms) <= span + GAP_SLACK_MS;
    *complete = !overwritten && !gapped;
    return total;
  }

  // v6: how long the flowmeter had been silent before press_ms -- the newest
  // pulse strictly before it, i.e. the end of the previous pump run. False
  // (not known) when no pulse before it is logged, or a gap (clear(),
  // markGap()) lies between that pulse and the press: pulses may be missing
  // there, so the idle time could be shorter than it looks. A ring that has
  // wrapped still holds its newest entries, which is all this needs.
  //   Also false when that pulse is within RUNNING_MS of the press: press_ms
  // is the first POLL that saw the lever, and pulses carry their interrupt
  // time, so after a loop() stall across the press the new run's own first
  // pulses sit just before it (review 2026-11-22). An idle under a second
  // is not told apart from that and is reported as not known.
  bool idleBefore(uint32_t press_ms, uint32_t* idle_ms) const {
    const uint16_t oldest = (uint16_t)((head + N - count) % N);
    bool found = false;
    uint32_t best = 0;
    for (uint16_t i = 0; i < count; i++) {
      uint32_t t = ring[(oldest + i) % N];
      uint32_t before = press_ms - t;               // unsigned: wrap-safe
      if (t == press_ms || (int32_t)before <= 0) continue;   // at or after the press
      if (!found || before < best) { found = true; best = before; }
    }
    if (!found || best < RUNNING_MS) return false;
    const uint32_t last = press_ms - best;
    // Slack as in extract(): a loss is noticed by the next drain, which can
    // come after the press.
    if (have_gap && (uint32_t)(gap_ms - last) <= best + GAP_SLACK_MS) return false;
    *idle_ms = best;
    return true;
  }

private:
  uint32_t ring[N] = {};
  uint16_t head = 0;
  uint16_t count = 0;
  uint32_t gap_ms = 0;
  bool     have_gap = false;
};
