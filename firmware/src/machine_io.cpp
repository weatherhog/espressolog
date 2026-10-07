#include "machine_io.h"

// ---------------------------------------------------------------- 0d: flow
bool PulseCounter::feedEdge(uint32_t t_us) {
  if (have_last && (uint32_t)(t_us - last_us) < MIN_PERIOD_US) {
    // Deliberately does NOT advance last_us. The window is measured from the
    // last REAL pulse, so a noise burst costs at most one spurious count and
    // can never push the window far enough to reject the next real pulse.
    // Advancing it here would let a sustained train just under the threshold
    // walk the window forever and swallow genuine flow.
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
    s.stable   = raw;
    s.since_ms = s.cand_since;

    if (!raw) {   // release completes a press
      Event e = {};
      e.line          = i;
      e.pressed_at_ms = prev_since;
      e.duration_ms   = s.cand_since - prev_since;
      // ">= 2 s" against CLAUDE.md's "> ~2 s": the threshold is the
      // machine's observed behaviour, not a spec, so a press landing within
      // milliseconds of it is ambiguous either way. Flagging the ambiguous
      // case is the safe direction — a reprogram that goes unflagged is a
      // silent change to the machine's stored timers.
      e.reprogram     = e.duration_ms >= REPROGRAM_MS;
      push(e);
    }
  }
}
