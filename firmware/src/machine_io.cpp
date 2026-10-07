#include "machine_io.h"

// ---------------------------------------------------------------- 0d: flow
bool PulseCounter::feedEdge(uint32_t t_us) {
  if (have_last && (uint32_t)(t_us - last_us) < MIN_PERIOD_US) {
    // Deliberately does NOT advance last_us: the window is measured from the
    // last REAL pulse, so noise can never push it far enough to reject the
    // next genuine one. Advancing it here would let a sustained train just
    // under the threshold walk the window forever and swallow real flow.
    //
    // The cost of that choice is that sustained noise IS counted, once per
    // MIN_PERIOD_US — see the header. This branch rate-limits; it does not
    // reject. The trade is deliberate: swallowing real flow is silent, and
    // phantom counts show up next to a large `glitches()`.
    rejected++;
    return false;
  }
  last_us = t_us;
  have_last = true;
  counted++;
  return true;
}

// ------------------------------------------------------------ 0c: switches
const char* SwitchBank::lineName(uint8_t line) {
  switch (line) {
    case ONE_CUP:   return "1cup";
    case TWO_CUP:   return "2cup";
    case STEAM:     return "steam";
    case HOT_WATER: return "water";
  }
  return "?";
}

void SwitchBank::reset() {
  for (uint8_t i = 0; i < N_LINES; i++) st[i] = LineState{};
  q_head = q_count = 0;
  // `lost` deliberately survives: it is a lifetime fault counter, and zeroing
  // it on every re-enable would hide a queue that overflows at every bring-up.
}

void SwitchBank::push(const Event& e) {
  if (q_count >= EV_N) { lost++; return; }   // counted, never silently dropped
  q[(q_head + q_count) % EV_N] = e;
  q_count++;
}

bool SwitchBank::popEvent(Event& out) {
  if (q_count == 0) return false;
  out = q[q_head];
  q_head = (q_head + 1) % EV_N;
  q_count--;
  return true;
}

void SwitchBank::feed(uint32_t t_ms, const bool level[N_LINES]) {
  for (uint8_t i = 0; i < N_LINES; i++) {
    LineState& s = st[i];
    bool raw = level[i];

    if (!s.init) {
      // Adopt whatever the line reads on the first poll and emit nothing.
      // A line already high at boot is a held lever or a fault, not a press
      // this firmware witnessed; inventing a press for it would put a
      // timestamp in the log that nobody observed. It still shows up in
      // pressed()/heldMs(), and its release still emits — with pressed_at
      // honestly set to the first poll, which is as far back as we saw.
      s.init = true;
      s.stable = s.cand = raw;
      s.cand_since = s.since_ms = t_ms;
      s.adopted = raw;   // high at first sight = a press we did not witness
      continue;
    }

    if (raw != s.cand) {      // level moved: restart the confirmation clock
      s.cand = raw;
      s.cand_since = t_ms;
      continue;
    }
    if (raw == s.stable) continue;                                 // nothing pending
    if ((uint32_t)(t_ms - s.cand_since) < DEBOUNCE_MS) continue;   // not yet confirmed

    // Confirmed. Backdate to the edge that started it — debounce is
    // confirmation, not the measurement, the same reason the shot detector
    // backdates t=0 to the flow crossing rather than to POUR_HOLD_MS later.
    uint32_t prev_since = s.since_ms;
    bool     was_adopted = s.adopted;
    s.stable   = raw;
    s.since_ms = s.cand_since;
    if (raw) s.adopted = false;   // a witnessed rising edge: real from here on

    if (!raw) {   // release completes a press
      s.adopted = false;
      Event e = {};
      e.line          = i;
      e.pressed_at_ms = prev_since;
      e.duration_ms   = s.cand_since - prev_since;
      e.synthetic     = was_adopted;
      // ">= 2 s" against CLAUDE.md's "> ~2 s": the threshold is the
      // machine's observed behaviour, not a spec, so a press landing within
      // milliseconds of it is ambiguous either way. Flagging the ambiguous
      // case is the safe direction — a reprogram that goes unflagged is a
      // silent change to the machine's stored timers.
      // Never on a press whose start was never observed — see Event::synthetic.
      e.reprogram     = !was_adopted && e.duration_ms >= REPROGRAM_MS;
      push(e);
    }
  }
}
