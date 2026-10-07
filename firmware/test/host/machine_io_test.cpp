// Host tests for the 0c/0d taps.
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

  // 2. Contact bounce / hash: a burst 100 us apart yields exactly one count.
  {
    PulseCounter p;
    assert(p.feedEdge(0));
    for (uint32_t t = 100; t < 2000; t += 100) assert(!p.feedEdge(t));
    assert(p.total() == 1);
    assert(p.glitches() == 19);
    printf("2. a 100 us burst counts once, 19 rejected OK\n");
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
    printf("11. a line high at boot is adopted, not invented OK\n");
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
