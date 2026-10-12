// ARMED is "stable at ANY value for 1 s", not "near zero".
//
// This test exists because its absence cost a real finding. CLAUDE.md
// described ARMED as "weight within +/-0.5 g of zero (i.e. tared with the cup
// on)" -- stale against detector 0a.6 -- and that description was used to
// WITHDRAW a correct invariant-2 report, on the reasoning that a weighing
// episode needs w > 5 g and so could never complete while ARMED. ARM_BAND_MG
// is a FLATNESS band measured against the current reading, so an un-tared cup
// of any mass arms fine and the withdrawal was wrong.
//
// Pin the behaviour so the next person reads the code, not the prose.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "../../src/detector.h"

static void feedStable(ShotDetector& d, int32_t mg, uint32_t from_ms,
                       uint32_t to_ms, uint32_t step = 100) {
  for (uint32_t t = from_ms; t <= to_ms; t += step) d.feed(t, mg);
}

int main() {
  // 1. An UN-TARED cup at 212 g arms. If ARM_BAND_MG were a band around zero
  //    this could not happen, and the cup-swap workflow would be broken.
  {
    ShotDetector d;
    feedStable(d, 212000, 0, 2000);
    assert(d.state() == ShotState::ARMED);
    printf("1. un-tared cup at 212 g reaches ARMED OK\n");
  }

  // 2. A tared scale arms too -- the zero case still works.
  {
    ShotDetector d;
    feedStable(d, 0, 0, 2000);
    assert(d.state() == ShotState::ARMED);
    printf("2. tared scale at 0 g reaches ARMED OK\n");
  }

  // 3. Flatness, not magnitude, is what ARM_BAND_MG bounds: a reading drifting
  //    by more than the band does NOT arm, however small it is.
  {
    ShotDetector d;
    for (uint32_t t = 0; t <= 2000; t += 100) d.feed(t, (int32_t)(t * 2));  // 2 mg/ms = 2 g/s
    assert(d.state() != ShotState::ARMED);
    printf("3. a drifting reading does not arm OK\n");
  }

  // 4. THE ONE THAT MATTERS FOR INVARIANT 2: the weighing threshold (5 g) and
  //    the arming band (0.5 g) are not mutually exclusive, because they are
  //    not measured against the same thing. A mass above the weighing
  //    threshold is ARMED, so a weighing episode CAN complete in that state --
  //    which is why main.cpp must not treat ARMED as quiet.
  {
    ShotDetector d;
    feedStable(d, 18000, 0, 2000);   // a dose cup, well over 5 g
    assert(d.state() == ShotState::ARMED);
    printf("4. 18 g is ARMED -- ARMED is not mutually exclusive with a weighing OK\n");
  }

  printf("\nall armed tests passed\n");
  return 0;
}
