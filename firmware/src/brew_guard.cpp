#include "brew_guard.h"

void BrewGuard::onRelease(uint8_t line, uint32_t pressed_at_ms) {
  if (line >= SwitchBank::N_LINES) return;
  last_press_ms[line] = pressed_at_ms;
  have_press[line]    = true;
}

bool BrewGuard::pressGuard(uint8_t line, uint32_t now_ms) const {
  if (line >= SwitchBank::N_LINES || !have_press[line]) return false;
  // Unsigned subtraction, correct across a single millis() wrap. It does alias
  // once every 49.7 days — a never-cleared timestamp re-enters the window and
  // produces one spurious 75 s of busy. Recorded rather than worked around:
  // the cost is 75 s of deferred uploads, twice a year, and the alternative is
  // a wider-than-32-bit clock this firmware does not otherwise need.
  return (now_ms - last_press_ms[line]) < BREW_GUARD_MS;
}

bool BrewGuard::busy(uint32_t now_ms, const Inputs& in) const {
  // 1. A LEVER HELD RIGHT NOW. SwitchBank emits an Event only on RELEASE, so
  //    for the duration of the press itself the pump is running and the
  //    event path knows nothing yet. (80-295 ms over the 2026-10-19 session;
  //    this cited "345-480 ms" until 2026-10-27, which is the superseded
  //    2026-10-08 range -- CLAUDE.md says do not quote it. Nothing here
  //    turns on the number.)
  //
  //    Two bounds, both learned the hard way. `pressed` includes a level
  //    ADOPTED at the first poll and never witnessed going high — a floating
  //    unwired line, a reboot with a lever held, one stuck divider leg — and a
  //    draft trusted it with no upper bound at all. That latched busy forever:
  //    no weighing ever reached flash, nothing uploaded, and Net::tick returns
  //    before configTzTime so every spooled record got time_valid = 0.
  //    Silently, with nothing logged.
  if (in.switches_on) {
    const uint8_t pump_lines[] = {SwitchBank::ONE_CUP, SwitchBank::TWO_CUP,
                                  SwitchBank::HOT_WATER};  // STEAM drives the
                                                           // heater, not the pump
    for (uint8_t i : pump_lines)
      if (in.pressed[i] && !in.synthetic[i] && in.held_ms[i] < BREW_GUARD_MS)
        return true;
  }

  // 2. BREW (1Cup / 2Cup). A usable display is authoritative in BOTH
  //    directions: Mode::TIMER is the machine's own pump-on to pump-off
  //    measurement, and a fresh reading that is NOT TIMER means the pump has
  //    stopped. Only without a usable display does the timer-less press guard
  //    apply — a draft OR-ed it unconditionally and blocked uploads for the
  //    full BREW_GUARD_MS after every 1Cup press, ~65 s of it needless, over
  //    exactly the minute when cups come off and the grinder runs.
  //
  //    NOTE THE DELIBERATE ASYMMETRY: branch 1 is capped by held_ms and this
  //    one is not. A TIMER frame that kept arriving would hold busy
  //    indefinitely. That is correct — TIMER is the machine stating the pump
  //    is running, and capping it would declare the machine idle mid-cycle,
  //    which is the invariant-2 breach this class exists to prevent. A
  //    reprogrammed 2Cup can legitimately exceed BREW_GUARD_MS.
  if (in.display_usable) {
    if (in.display_timer) return true;
  } else if (pressGuard(SwitchBank::ONE_CUP, now_ms) ||
             pressGuard(SwitchBank::TWO_CUP, now_ms)) {
    return true;
  }

  // 3. HOT WATER. The display can NEVER help here, fresh or not: CLAUDE.md
  //    records that the machine does not run its shot timer during a lever-B
  //    draw. So this guard applies unconditionally.
  //
  //    A draft returned false early whenever the display was fresh, which
  //    reads as "the display has the answer" but means only "the display is
  //    alive" — and during a water draw it is very much alive, showing boiler
  //    temperature at 41 ms a frame. That made this branch dead code in the
  //    deployed configuration, re-opening the exact hole the commit before had
  //    fixed. Hot water runs the pump and pulses the suction-side flowmeter,
  //    so a stall here still overflows the 64-entry flow ring.
  return pressGuard(SwitchBank::HOT_WATER, now_ms);
}
