#pragma once
#include <stdint.h>
#include "machine_io.h"

// Is the machine's PUMP running? Pure logic, no Arduino, no clock of its own —
// main.cpp owns the pin reads, millis()/micros() and the NVS tap flags, this
// owns every decision.
//
// That split is not stylistic, and this object is the reason it is not.
// machine_context.h and machine_io.h both record that the first version of
// THEIR logic lived inline in main.cpp, had no tests, and shipped two bugs a
// green suite could not see. This logic lived in main.cpp as `machineBusy()`
// through three rewrites across 2026-10-12 and shipped NINE findings, two of
// which re-opened a hole the commit before had closed. Same shape, same split,
// a round later than it should have been.
//
// WHY IT EXISTS AT ALL. Hard invariant 2 forbids writing flash during a shot,
// and the shot detector cannot answer "is a shot happening" in the window that
// matters: it is scale-driven, so it reads IDLE or ARMED from pump-on until
// first drip plus POUR_HOLD_MS. That window covers all of pre-infusion with
// the pump running, the flow ISR counting and the display bus decoding. So ask
// the machine instead. It answers two ways, and they are not interchangeable.
class BrewGuard {
public:
  // How long a press keeps the guard up once the lever has sprung back.
  //
  // GRADE THIS HONESTLY: it is sized from 2Cup's factory-default 60 s plus
  // margin. CLAUDE.md recorded this machine's stored 2Cup duration as UNKNOWN
  // until 2026-10-19, when the operator reported it set to 45 s (1Cup is
  // ~10.5 s; both are user-modifiable). Note 45 < 75, so 2Cup is NOT what
  // makes this constant doubtful -- a hot-water draw is, see CLAUDE.md. It is a
  // conservative guess, not a measurement, and it is the dominant cost of the
  // hot-water path — see busy(). Measuring a real draw would let it shrink.
  //
  // 75000 ms = 75 s. Every prose site that quotes this constant quotes the
  // seconds; this header is the source of truth for both.
  // fact:brew-guard-ms 75 s
  static constexpr uint32_t BREW_GUARD_MS = 75000;

  // Everything main.cpp knows, passed in. Nothing is read from a global.
  struct Inputs {
    bool switches_on   = false;  // the J2 tap is enabled
    bool display_usable = false; // the J5 tap is enabled AND a frame is fresh
    bool display_timer  = false; // ...and that frame is Mode::TIMER
    bool     pressed[SwitchBank::N_LINES]   = {};
    bool     synthetic[SwitchBank::N_LINES] = {};
    uint32_t held_ms[SwitchBank::N_LINES]   = {};
  };

  // A lever was released. Backdate to the press: the pump started then, and
  // SwitchBank only emits on release. main.cpp filters synthetic events and
  // STEAM before calling this; busy() does not depend on that filtering.
  void onRelease(uint8_t line, uint32_t pressed_at_ms);

  // CONST ON PURPOSE. A draft of this cleared a press guard as a side effect
  // of being asked, so calling it twice in one iteration gave two answers —
  // and because a stale display reads "fresh" for 2 s out of every 71.58 min
  // (a uint32_t micros() alias), that momentary condition permanently erased
  // the guard for the rest of a brew. A question must not change the answer.
  bool busy(uint32_t now_ms, const Inputs& in) const;

private:
  uint32_t last_press_ms[SwitchBank::N_LINES] = {};
  // Explicit, rather than treating 0 as "no press". millis() really is 0 for
  // one millisecond after boot, and this file's neighbours (PulseCounter's
  // have_last, main.cpp's flow_ever) use a flag for exactly this reason.
  bool     have_press[SwitchBank::N_LINES] = {};

  bool pressGuard(uint8_t line, uint32_t now_ms) const;
};
