#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include <cassert>
#include <cstdio>
#include "../../src/brew_guard.h"

// Every case here is a bug this logic actually shipped, or a property a fix
// depended on. `machineBusy()` was rewritten three times in one day and each
// rewrite introduced a defect a review had to find by reading.

using L = SwitchBank;

static BrewGuard::Inputs bothTapsIdle() {
  BrewGuard::Inputs in;
  in.switches_on = true;
  in.display_usable = true;
  in.display_timer = false;      // showing boiler temperature
  return in;
}

int main() {
  // 1. THE HOT-WATER HOLE, opened, closed, and re-opened across three
  //    rewrites. The display is perfectly alive during a lever-B draw —
  //    showing temperature at 41 ms a frame — and never shows a timer for one.
  //    A draft returned false early on "display is fresh", which made this
  //    branch dead code in the deployed configuration.
  {
    BrewGuard g;
    g.onRelease(L::HOT_WATER, 1000);
    auto in = bothTapsIdle();                 // display fresh, NOT timer
    assert(g.busy(2000, in));
    printf("1. a water draw is busy even while the display is fresh OK\n");
  }

  // 2. ...and the brew lines must NOT behave that way, or every 1Cup press
  //    blocks flash and uploads for the full 75 s while the display sits there
  //    reporting that the timer stopped ~10 s in.
  {
    BrewGuard g;
    g.onRelease(L::ONE_CUP, 1000);
    auto in = bothTapsIdle();
    assert(!g.busy(2000, in));
    printf("2. a fresh non-TIMER display releases the 1Cup guard at once OK\n");
  }

  // 3. ...but with no usable display the press guard is all there is.
  {
    BrewGuard g;
    g.onRelease(L::ONE_CUP, 1000);
    auto in = bothTapsIdle();
    in.display_usable = false;
    assert(g.busy(2000, in));
    assert(!g.busy(1000 + BrewGuard::BREW_GUARD_MS + 1, in));   // and it expires
    printf("3. without a usable display the 1Cup press guard applies, and expires OK\n");
  }

  // 4. A SYNTHETIC PRESS MUST NEVER ARM ANYTHING. A level adopted at the first
  //    poll was never witnessed going high — a floating unwired line, a reboot
  //    with a lever held. A draft trusted it with no upper bound and latched
  //    busy forever: no flash, no uploads, and no SNTP, so every spooled
  //    record got time_valid = 0. Silently.
  {
    BrewGuard g;
    auto in = bothTapsIdle();
    in.pressed[L::ONE_CUP] = true;
    in.synthetic[L::ONE_CUP] = true;
    in.held_ms[L::ONE_CUP] = 10;
    assert(!g.busy(2000, in));
    printf("4. a synthetic (adopted) level never makes the guard busy OK\n");
  }

  // 5. A genuine held lever IS busy — the event path knows nothing until
  //    release, and a press runs 345-480 ms by hand on this machine.
  {
    BrewGuard g;
    auto in = bothTapsIdle();
    in.pressed[L::ONE_CUP] = true;
    in.held_ms[L::ONE_CUP] = 400;
    assert(g.busy(2000, in));
    printf("5. a lever held right now is busy, before any event fires OK\n");
  }

  // 6. ...bounded, so a stuck line cannot latch it.
  {
    BrewGuard g;
    auto in = bothTapsIdle();
    in.pressed[L::ONE_CUP] = true;
    in.held_ms[L::ONE_CUP] = BrewGuard::BREW_GUARD_MS + 1;
    assert(!g.busy(2000, in));
    printf("6. a line held past BREW_GUARD_MS stops counting OK\n");
  }

  // 7. STEAM drives the heater, not the pump, so it is not an invariant-2
  //    hazard and must not block uploads.
  {
    BrewGuard g;
    auto in = bothTapsIdle();
    in.pressed[L::STEAM] = true;
    in.held_ms[L::STEAM] = 400;
    assert(!g.busy(2000, in));
    printf("7. steam does not make the guard busy OK\n");
  }

  // 8. TIMER is busy, and DELIBERATELY unbounded. Capping it would declare the
  //    machine idle mid-cycle, which is the breach this class prevents; a
  //    reprogrammed 2Cup can legitimately run longer than BREW_GUARD_MS.
  {
    BrewGuard g;
    auto in = bothTapsIdle();
    in.display_timer = true;
    assert(g.busy(2000, in));
    assert(g.busy(10 * BrewGuard::BREW_GUARD_MS, in));
    printf("8. a TIMER reading is busy, with no upper bound (deliberate) OK\n");
  }

  // 9. ASKING MUST NOT CHANGE THE ANSWER. A draft cleared a press guard as a
  //    side effect of being asked, so a 2 s window of apparent display
  //    freshness — the uint32_t micros() alias, once every 71.58 min —
  //    permanently erased the guard for the rest of a brew.
  {
    BrewGuard g;
    g.onRelease(L::HOT_WATER, 1000);
    auto in = bothTapsIdle();
    for (int i = 0; i < 1000; i++) (void)g.busy(2000, in);
    assert(g.busy(2000, in));
    printf("9. busy() has no side effects — 1000 calls, same answer OK\n");
  }

  // 10. A press at millis() == 0 still arms. Using 0 as "no press" loses the
  //     first millisecond after boot; this file's neighbours use a flag.
  {
    BrewGuard g;
    g.onRelease(L::HOT_WATER, 0);
    auto in = bothTapsIdle();
    assert(g.busy(1, in));
    printf("10. a press at millis()==0 arms the guard OK\n");
  }

  // 11. The guard survives a millis() wrap.
  {
    BrewGuard g;
    g.onRelease(L::HOT_WATER, 0xFFFFF000);
    auto in = bothTapsIdle();
    assert(g.busy(0x00000100, in));      // 4.3 s later in real time
    printf("11. the press guard is correct across a millis() wrap OK\n");
  }

  // 12. WITH NO TAP ENABLED AND NOTHING EVER WITNESSED, the guard is always
  //     false — which means the invariant-2 fix is CONDITIONAL on hardware the
  //     firmware does not require. display_on and switches_on are
  //     NVS-persisted and default OFF, so a fresh board restores the full
  //     pre-fix behaviour. Documented in the deploy note; pinned here so
  //     nobody reads the guard as unconditional.
  {
    BrewGuard g;
    BrewGuard::Inputs in;                 // both taps off, no press recorded
    assert(!g.busy(2000, in));
    printf("12. with no tap enabled and no press seen, never busy (by design) OK\n");
  }

  // 13. BUT A PRESS ALREADY WITNESSED OUTLIVES THE TAP BEING DISABLED, and
  //     that is deliberate rather than an oversight. A first version of case
  //     12 asserted the opposite and failed — correctly. main.cpp only calls
  //     onRelease from pollSwitches, which returns early when the tap is off,
  //     so a recorded press means the lever really was seen. The pump it
  //     started does not stop because someone typed `switches off`.
  {
    BrewGuard g;
    g.onRelease(L::HOT_WATER, 1000);      // witnessed while the tap was live
    BrewGuard::Inputs in;                 // ...then the tap is disabled
    assert(g.busy(2000, in));
    assert(!g.busy(1000 + BrewGuard::BREW_GUARD_MS + 1, in));   // still expires
    printf("13. a witnessed press outlives the tap being disabled OK\n");
  }

  printf("\nall brew guard tests passed\n");
  return 0;
}
