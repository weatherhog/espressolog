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

  // 3. FLATNESS is what ARM_BAND_MG bounds, and finding a case that proves it
  //    takes care. Two earlier attempts did not:
  //
  //      a 2 g/s ramp          -> ends in POURING, so "not ARMED" holds with
  //                               the flatness check DELETED (verified)
  //      one step past the band -> also trips pour onset, same problem
  //
  //    The reason is structural and worth knowing: ARM_BAND_MG is 500 mg
  //    across a 1 s window, i.e. an effective 500 mg/s, which is ABOVE the
  //    300 mg/s pour threshold. Any MONOTONIC drift fast enough to break
  //    flatness is therefore also fast enough to be detected as a pour. The
  //    flatness check only does unique work where pour onset cannot see:
  //    a reading that moves a lot without going anywhere.
  //
  //    So: oscillate. Spread 800 mg breaks the band; net flow is ~0 so no
  //    pour is detected. IDLE with the check, ARMED without it.
  {
    ShotDetector d;
    for (uint32_t t = 0; t <= 3000; t += 100)
      d.feed(t, 20000 + ((t / 100) % 2 ? 400 : -400));
    assert(d.state() == ShotState::IDLE);
    printf("3. an oscillating reading does not arm (spread > band, no net flow) OK\n");
  }

  // 4. THE ONE THAT MATTERS FOR INVARIANT 2, and it has to be DEMONSTRATED.
  //    A first version asserted only "18 g is ARMED" and never touched
  //    WeighingDetector, so its claim -- that a weighing episode can complete
  //    while the detector is ARMED -- was reasoning wearing a test's clothes.
  //    That claim is the entire justification for narrowing the quiet window,
  //    so run the real sequence.
  //
  //    An un-tared dose cup sits still (stable, and above MIN_WEIGHT_MG), then
  //    the first drips arrive at 200 mg/s. That breaks the weighing episode's
  //    RANGE_MG (300 mg) -- which is what makes it EMIT -- while staying under
  //    tryPourOnset's 300 mg/s, so the shot detector has not reached POURING.
  //    The event is queued and, if `quiet` were true, flushed to flash mid
  //    pre-infusion.
  {
    ShotDetector shot;
    WeighingDetector weigh;
    int32_t w = 18000;
    bool completed = false, armed_at_completion = false;

    for (uint32_t t = 0; t <= 4000; t += 100) {
      if (t > 1500) w += 20;                      // 200 mg/s of drips
      shot.feed(t, w);
      bool shot_active = shot.state() == ShotState::POURING ||
                         shot.state() == ShotState::SETTLING;
      if (weigh.feed(t, w, shot_active)) {
        completed = true;
        armed_at_completion = (shot.state() == ShotState::ARMED);
        break;
      }
    }
    assert(completed);              // the episode really did finish
    assert(armed_at_completion);    // ...with the shot detector still ARMED
    printf("4. a weighing episode COMPLETES while the detector is ARMED OK\n");
    printf("   (so ARMED is not quiet -- main.cpp must not flush flash there)\n");
  }

  printf("\nall armed tests passed\n");
  return 0;
}
