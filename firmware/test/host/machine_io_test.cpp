// Host tests for the 0c/0d taps.
//
// MUTATIONS CHECKED (each must turn the suite red; re-run them if you change
// the logic). The claim "all four mutations were checked" was once made in a
// commit message with nothing in the repo to back it, which is not a record:
//
//   1. PulseCounter::feedEdge advances last_us on a reject   -> case 3
//   2. SwitchBank::feed sets since_ms = t_ms, not cand_since -> case 6
//   3. the DEBOUNCE_MS confirmation check removed            -> case 8
//   4. SwitchBank::push drops without counting `lost`        -> case 13
//
// Two further regressions, both found in review AFTER the above passed, are
// pinned by cases 11b and 11d. Both were reachable because the original tests
// were scoped so they could not reach them — 11 ran 500 ms against a 2 s
// threshold, and case 2 stopped one step short of MIN_PERIOD_US. That is the
// failure mode to watch for here: a test that exercises a path without ever
// reaching the condition it claims to check.
//
// Both milestones read physical wiring that cannot be exercised from a
// laptop, so everything that can be decided without the machine is decided
// here: the glitch filter, the debouncer, press duration, the reprogramming
// threshold, and the queue. What is left for the bench is only the part that
// genuinely needs voltage — whether the dividers land inside VIH/VIL and
// whether the pulse train on GPIO6 is the flowmeter or the pump triac.
#include "../../src/machine_io.h"
#include <cstdio>
#include <cassert>
#include <cstring>

// Mirrors loop(): the switch lines are polled, not interrupt-driven, so the
// tests poll too and never hand the debouncer an edge it could not have seen.
static constexpr uint32_t STEP_MS = 5;

struct Bench {
  SwitchBank sb;
  uint32_t t = 0;
  bool lv[SwitchBank::N_LINES] = { false, false, false, false };

  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += STEP_MS) { sb.feed(t, lv); t += STEP_MS; }
  }
  size_t drain(SwitchBank::Event* out, size_t max) {
    size_t n = 0;
    SwitchBank::Event e;
    while (n < max && sb.popEvent(e)) out[n++] = e;
    return n;
  }
};

int main() {
  SwitchBank::Event ev[8];

  // ---------------------------------------------------------- PulseCounter
  // 1. An ordinary pour. ~2 ml/s at ~2400 pulses/L is ~5 Hz; every pulse
  //    counts and none is mistaken for bounce.
  {
    PulseCounter p;
    uint32_t t = 0;
    for (int i = 0; i < 125; i++) { assert(p.feedEdge(t)); t += 200000; }   // 5 Hz, 25 s
    assert(p.total() == 125);
    assert(p.glitches() == 0);
    printf("1. a 25 s pour at 5 Hz counts every pulse OK\n");
  }

  // 2. Contact bounce / hash. WITHIN one window a burst yields one count —
  //    but the filter rate-limits rather than rejects, so SUSTAINED noise
  //    keeps scoring one count per MIN_PERIOD_US. The first half of this
  //    used to be the whole test, stopping at t<2000, one step short of the
  //    threshold — which made it structurally incapable of falsifying the
  //    "at most one spurious count" claim written beside it. That claim was
  //    false; this is what the code actually does.
  {
    PulseCounter p;
    assert(p.feedEdge(0));
    for (uint32_t t = 100; t < 2000; t += 100) assert(!p.feedEdge(t));
    assert(p.total() == 1 && p.glitches() == 19);

    PulseCounter q;                                   // 50 ms of 10 kHz hash
    for (uint32_t t = 0; t < 50000; t += 100) q.feedEdge(t);
    assert(q.total() == 25);                          // one per 2 ms, NOT one
    assert(q.glitches() == 475);
    printf("2. one count per window, and sustained noise scores 25 not 1 OK\n");
  }

  // 3. THE MUTATION TEST for feedEdge not advancing last_us on a reject.
  //    A real pulse 2.1 ms after the last REAL one must count even though it
  //    is only 200 us after the last rejected glitch. If the reject branch
  //    ever starts advancing last_us, noise silently eats real flow and the
  //    count reads low with nothing in the log to say so.
  {
    PulseCounter p;
    assert(p.feedEdge(0));
    for (uint32_t t = 100; t <= 1900; t += 100) assert(!p.feedEdge(t));
    assert(p.feedEdge(2100));
    assert(p.total() == 2);
    printf("3. noise never pushes the window past a real pulse OK\n");
  }

  // 4. micros() wraps every ~71 minutes and a shot can straddle it. The
  //    subtraction is unsigned on purpose; this is the test that says so.
  {
    PulseCounter p;
    uint32_t near_wrap = UINT32_MAX - 1000;
    assert(p.feedEdge(near_wrap));
    assert(!p.feedEdge(near_wrap + 500));     // 500 us later, still a glitch
    assert(p.feedEdge(near_wrap + 3000));     // 3 ms later, wrapped, counts
    assert(p.total() == 2);
    assert(p.glitches() == 1);
    printf("4. counting survives the micros() wrap OK\n");
  }

  // 5. "No pulse yet" is distinguishable from "a pulse ages ago" — the
  //    difference between a parked turbine and a tap that is not connected.
  {
    PulseCounter p;
    assert(p.sinceLastUs(5000000) == UINT32_MAX);
    p.feedEdge(1000000);
    assert(p.sinceLastUs(5000000) == 4000000);
    printf("5. silent-since-boot differs from merely idle OK\n");
  }

  // 5b. The runtime-settable filter, which exists to settle whether the
  //     measured 5900-6500 pulses/L counts water or ringing.
  //
  //     Case A, ringing that DIES inside the window (3 edges at 200 us
  //     spacing): the 2 ms filter already cleans it, and widening to 20 ms
  //     must not change the answer, because real pulses are 200 ms apart.
  //     If a wider filter moved the count on clean input, the experiment
  //     would be meaningless.
  {
    auto run = [](uint32_t width) {
      PulseCounter p;
      p.setMinPeriodUs(width);
      for (int i = 0; i < 50; i++) {
        uint32_t base = (uint32_t)i * 200000;              // 5 Hz
        p.feedEdge(base);
        for (int r = 1; r <= 3; r++) p.feedEdge(base + r * 200);
      }
      return p.total();
    };
    assert(run(PulseCounter::MIN_PERIOD_US) == 50);
    assert(run(20000) == 50);
    assert(run(0) == 200);          // filter off: every ringing edge scores

    // THE GETTER IS ASSERTED AND AN OFF-GRID WIDTH IS USED. Without this, a
    // setter that merely bucketed its argument into {0, 2000, 20000} passed
    // the whole suite — demonstrated in review — and minPeriodUs() was read by
    // no test at all, while being the only thing that tells an operator which
    // width a bench run used. The sweep below needs 5000 and 10000 to work.
    {
      PulseCounter p;
      assert(p.minPeriodUs() == PulseCounter::MIN_PERIOD_US);   // the default
      static const uint32_t widths[] = { 0, 1, 999, 5000, 10000, 37, 100000 };
      for (size_t i = 0; i < sizeof(widths)/sizeof(widths[0]); i++) {
        p.setMinPeriodUs(widths[i]);
        assert(p.minPeriodUs() == widths[i]);                   // exactly, not bucketed
      }
    }
    // An off-grid width must FILTER at that width, and the bound has to be
    // two-sided. `spaced6(5000) == 10` alone only requires the effective
    // width to be <= 6000, which every smaller bucket satisfies — a setter
    // that reported 5000 while filtering at 2000 passed the whole suite,
    // demonstrated in review, including the line that prints "not bucketed".
    // Pinning 5000 needs an upper bound (6 ms edges accepted) AND a lower one
    // (4 ms edges rejected), i.e. 4000 < W <= 6000.
    {
      auto spaced = [](uint32_t width, uint32_t gap_us) {
        PulseCounter p; p.setMinPeriodUs(width);
        for (int i = 0; i < 10; i++) p.feedEdge((uint32_t)i * gap_us);
        return p.total();
      };
      assert(spaced(5000, 6000) == 10);   // upper bound: W <= 6000
      assert(spaced(5000, 4000) == 5);    // LOWER bound: W > 4000
      assert(spaced(2000, 4000) == 10);   // ...and 2000 really is narrower
      assert(spaced(10000, 6000) == 5);   // 6000 < W <= 12000
      assert(spaced(10000, 12000) == 10);
    }
    printf("5b. clean input is width-invariant; the width is exact, not bucketed OK\n");
  }

  // 5c. Case B — THE SIGNATURE THE EXPERIMENT IS LOOKING FOR. Ringing that
  //     outlives the window: edges every 1 ms for 3 ms. The window is
  //     measured from the last COUNTED edge, so +1000 is rejected but +2000
  //     is exactly 2000 us away and counts, inflating the total. A 20 ms
  //     filter swallows the whole tail and recovers the true count.
  //
  //     This is what a flush will look like at two filter widths if the
  //     5900-6500 pulses/L figure is ringing rather than water — and it is the
  //     case that broke the first draft of test 5b, which assumed 1 ms
  //     ringing stayed inside a 2 ms window. It does not.
  {
    auto run = [](uint32_t width) {
      PulseCounter p;
      p.setMinPeriodUs(width);
      for (int i = 0; i < 50; i++) {
        uint32_t base = (uint32_t)i * 200000;
        p.feedEdge(base);
        for (int r = 1; r <= 3; r++) p.feedEdge(base + r * 1000);
      }
      return p.total();
    };
    uint32_t narrow = run(PulseCounter::MIN_PERIOD_US);
    uint32_t wide   = run(20000);
    assert(narrow == 100);          // inflated 2x by the surviving tail
    assert(wide == 50);             // the truth
    assert(narrow > wide);
    printf("5c. ringing past the window inflates at 2 ms, recovered at 20 ms OK\n");
  }

  // ------------------------------------------------------------ SwitchBank
  // 6. A short press: one event, backdated to the edge, not to confirmation.
  {
    Bench b;
    b.run(1000);
    b.lv[SwitchBank::TWO_CUP] = true;
    b.run(200);
    b.lv[SwitchBank::TWO_CUP] = false;
    b.run(200);
    assert(b.drain(ev, 8) == 1);
    assert(ev[0].line == SwitchBank::TWO_CUP);
    assert(ev[0].pressed_at_ms == 1000);     // NOT 1020, the confirmation
    assert(ev[0].duration_ms == 200);
    assert(!ev[0].reprogram);
    printf("6. a 200 ms 2Cup press backdates to the edge OK\n");
  }

  // 7. Bounce on both edges still yields exactly one event.
  {
    Bench b;
    b.run(1000);
    for (int i = 0; i < 3; i++) {            // 30 ms of chatter on the way in
      b.lv[SwitchBank::ONE_CUP] = true;  b.run(5);
      b.lv[SwitchBank::ONE_CUP] = false; b.run(5);
    }
    b.lv[SwitchBank::ONE_CUP] = true;
    b.run(300);
    for (int i = 0; i < 3; i++) {            // and on the way out
      b.lv[SwitchBank::ONE_CUP] = false; b.run(5);
      b.lv[SwitchBank::ONE_CUP] = true;  b.run(5);
    }
    b.lv[SwitchBank::ONE_CUP] = false;
    b.run(100);
    assert(b.drain(ev, 8) == 1);
    assert(ev[0].line == SwitchBank::ONE_CUP);
    assert(!ev[0].reprogram);
    printf("7. 30 ms of chatter on each edge yields one event OK\n");
  }

  // 8. A chatter burst that never settles pressed produces NO event. A lever
  //    brushed in passing is not a brew.
  {
    Bench b;
    b.run(500);
    b.lv[SwitchBank::STEAM] = true;
    b.run(10);                               // 10 ms, under DEBOUNCE_MS
    b.lv[SwitchBank::STEAM] = false;
    b.run(500);
    assert(b.drain(ev, 8) == 0);
    assert(!b.sb.pressed(SwitchBank::STEAM));
    printf("8. a 10 ms blip is not a press OK\n");
  }

  // 9. The reprogramming threshold, from both sides. CLAUDE.md: holding a
  //    direction past ~2 s rewrites that direction's stored timer, which is
  //    the machine changing its own configuration — this machine's 1Cup is
  //    ~10.5 s rather than the 15 s default for exactly this reason, and
  //    nothing recorded when it happened.
  {
    Bench b;
    b.run(1000);
    b.lv[SwitchBank::ONE_CUP] = true;
    b.run(1995);
    b.lv[SwitchBank::ONE_CUP] = false;
    b.run(100);
    assert(b.drain(ev, 8) == 1);
    assert(ev[0].duration_ms == 1995);
    assert(!ev[0].reprogram);

    b.lv[SwitchBank::ONE_CUP] = true;
    b.run(2000);
    b.lv[SwitchBank::ONE_CUP] = false;
    b.run(100);
    assert(b.drain(ev, 8) == 1);
    assert(ev[0].duration_ms == 2000);
    assert(ev[0].reprogram);
    printf("9. 1995 ms is a press, 2000 ms is a reprogram OK\n");
  }

  // 10. Both levers released in the same tick. A single event() accessor
  //     would keep one and lose the other without a trace — which is the
  //     whole reason this is a queue.
  {
    Bench b;
    b.run(1000);
    b.lv[SwitchBank::TWO_CUP]   = true;
    b.lv[SwitchBank::HOT_WATER] = true;
    b.run(300);
    b.lv[SwitchBank::TWO_CUP]   = false;
    b.lv[SwitchBank::HOT_WATER] = false;
    b.run(100);
    size_t n = b.drain(ev, 8);
    assert(n == 2);
    bool saw_2cup = false, saw_water = false;
    for (size_t i = 0; i < n; i++) {
      if (ev[i].line == SwitchBank::TWO_CUP)   saw_2cup = true;
      if (ev[i].line == SwitchBank::HOT_WATER) saw_water = true;
      assert(ev[i].duration_ms == 300);
    }
    assert(saw_2cup && saw_water);
    printf("10. two lines released in one tick give two events OK\n");
  }

  // 11. A line already high on the first poll is adopted silently. Inventing
  //     a press for it would stamp the log with a time nobody observed; it is
  //     still visible through pressed()/heldMs().
  {
    Bench b;
    b.lv[SwitchBank::STEAM] = true;
    b.run(500);
    assert(b.drain(ev, 8) == 0);
    assert(b.sb.pressed(SwitchBank::STEAM));
    assert(b.sb.heldMs(SwitchBank::STEAM, b.t) == 500);
    b.lv[SwitchBank::STEAM] = false;
    b.run(100);
    assert(b.drain(ev, 8) == 1);             // the release IS witnessed
    assert(ev[0].pressed_at_ms == 0);
    assert(ev[0].synthetic);                 // and is marked as unwitnessed
    printf("11. a line high at boot is adopted, not invented OK\n");
  }

  // 11b. THE REGRESSION. A line high at the first poll and released past the
  //      2 s threshold must NOT raise the reprogram alarm. Test 11 ran only
  //      500 ms, so it exercised this path without ever crossing the
  //      threshold, and the bug it was meant to cover shipped: an unwired
  //      (floating) line settling ten seconds later printed
  //      "*** HELD PAST 2 s: STORED TIMER REPROGRAMMED ***" for a press
  //      nobody made — the loudest message in the firmware, asserting the
  //      machine had rewritten its own stored timer. Found in review.
  {
    Bench b;
    b.lv[SwitchBank::STEAM] = true;          // already high when we start looking
    b.run(10000);                            // five times REPROGRAM_MS
    b.lv[SwitchBank::STEAM] = false;
    b.run(500);
    assert(b.drain(ev, 8) == 1);
    assert(ev[0].duration_ms == 10000);      // the span is reported honestly
    assert(ev[0].synthetic);                 // and graded as unwitnessed
    assert(!ev[0].reprogram);                // so the alarm stays silent
    printf("11b. an unwitnessed 10 s press raises no reprogram alarm OK\n");
  }

  // 11c. A REAL press after a witnessed rising edge still alarms — the fix
  //      must not have bought 11b by disabling the feature.
  {
    Bench b;
    b.run(500);                              // starts idle: the edge IS seen
    b.lv[SwitchBank::ONE_CUP] = true;
    b.run(3000);
    b.lv[SwitchBank::ONE_CUP] = false;
    b.run(100);
    assert(b.drain(ev, 8) == 1);
    assert(!ev[0].synthetic);
    assert(ev[0].reprogram);
    printf("11c. a witnessed 3 s press still alarms OK\n");
  }

  // 11d. reset() exists because the taps go in one at a time. Without it,
  //      state adopted from a floating line during an earlier `switches on`
  //      survives `switches off`, and the first poll after rewiring emits a
  //      release whose duration is the hours in between.
  {
    Bench b;
    b.lv[SwitchBank::TWO_CUP] = true;        // floating high, tap not yet wired
    b.run(5000);
    b.sb.reset();                            // switches off … wire J2 … on
    b.lv[SwitchBank::TWO_CUP] = false;       // now reads its true idle level
    b.run(500);
    assert(b.drain(ev, 8) == 0);             // no phantom 5 s release
    assert(!b.sb.pressed(SwitchBank::TWO_CUP));
    printf("11d. reset() drops state adopted from an unwired line OK\n");
  }

  // 11e. THE ORDERING 11d CANNOT REACH, and the one that actually happens.
  //      11d drops the line low before the next poll, so reset() never has to
  //      RE-adopt — which left it blind to a reset() that cleared state but
  //      kept `init`. That implementation would mark the next release as
  //      WITNESSED and raise the REPROGRAM alarm, i.e. re-introduce the first
  //      bug through the fix for the second, with 11d still green. Verified by
  //      mutation: 11d alone passes against exactly that.
  //
  //      The real sequence: `switches off`, the line is STILL floating high,
  //      `switches on`.
  {
    Bench b;
    b.lv[SwitchBank::TWO_CUP] = true;
    b.run(3000);
    b.sb.reset();                            // off … on, line never moved
    b.run(5000);                             // still high, well past 2 s
    b.lv[SwitchBank::TWO_CUP] = false;
    b.run(500);
    assert(b.drain(ev, 8) == 1);
    assert(ev[0].synthetic);                 // re-adopted, so still unwitnessed
    assert(!ev[0].reprogram);                // and still no alarm
    printf("11e. reset() RE-adopts a line that is still high OK\n");
  }

  // 11f. reset() drops queued events but keeps `lost`. Both halves are
  //      behavioural claims made in machine_io.cpp and neither was pinned:
  //      mutations removing the queue clear, and zeroing `lost`, both left
  //      the suite green. `lost` is a lifetime fault counter — zeroing it on
  //      every re-enable would hide a queue that overflows at every bring-up.
  {
    Bench b;
    b.run(100);
    for (int i = 0; i < 10; i++) {           // 10 presses into a queue of 8
      b.lv[SwitchBank::ONE_CUP] = true;  b.run(50);
      b.lv[SwitchBank::ONE_CUP] = false; b.run(50);
    }
    assert(b.sb.dropped() == 2);
    b.sb.reset();
    assert(b.drain(ev, 8) == 0);             // queued events are gone
    assert(b.sb.dropped() == 2);             // the fault count is NOT
    printf("11f. reset() clears the queue and keeps the drop count OK\n");
  }

  // 12. heldMs tracks a press in flight — how a stuck line shows up in
  //     status. Nothing synthesises a release for one.
  {
    Bench b;
    b.run(100);
    b.lv[SwitchBank::HOT_WATER] = true;
    b.run(5000);
    assert(b.drain(ev, 8) == 0);             // still held: no event yet
    assert(b.sb.pressed(SwitchBank::HOT_WATER));
    assert(b.sb.heldMs(SwitchBank::HOT_WATER, b.t) == 5000);
    assert(b.sb.heldMs(SwitchBank::ONE_CUP, b.t) == 0);
    printf("12. a held line reports its age and emits nothing OK\n");
  }

  // 13. Overflow is counted, never silent — same rule as the spool.
  {
    Bench b;
    b.run(100);
    for (int i = 0; i < 10; i++) {           // 10 presses, queue holds 8
      b.lv[SwitchBank::ONE_CUP] = true;  b.run(50);
      b.lv[SwitchBank::ONE_CUP] = false; b.run(50);
    }
    assert(b.drain(ev, 8) == 8);
    assert(b.sb.dropped() == 2);
    printf("13. a full event queue counts what it drops OK\n");
  }

  printf("\nall machine io tests passed\n");
  return 0;
}
