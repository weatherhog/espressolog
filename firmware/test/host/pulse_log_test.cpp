// Host test for PulseLog — the pulse train from lever press to pour
// confirmation (the pre-infusion phase).
#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include "../../src/pulse_log.h"
#include <cstdio>
#include <cassert>

int main() {
  // 1. Only the window [press, t0] is copied, as offsets from the press:
  //    pulses from an earlier flush and from after first drip are left out.
  {
    PulseLog p;
    for (uint32_t t = 1000; t < 3000; t += 50) p.push(t);       // earlier flush
    for (uint32_t t = 10100; t <= 12000; t += 100) p.push(t);   // pre-infusion
    for (uint32_t t = 15000; t <= 18000; t += 40) p.push(t);    // after the pause
    for (uint32_t t = 18100; t < 20000; t += 40) p.push(t);     // after t0
    uint16_t out[256], copied; bool complete;
    uint16_t total = p.extract(10000, 18000, out, 256, &copied, &complete);
    assert(total == 20 + 76 && copied == total && complete);
    assert(out[0] == 100);                        // first pulse 100 ms after the press
    assert(out[19] == 2000);                      // last pre-infusion pulse
    assert(out[20] == 5000);                      // the 3 s pause shows as a gap
    assert(out[copied - 1] == 8000);              // t0 itself is inside
    printf("1. the press-to-confirmation window, as offsets from the press OK\n");
  }

  // 2. More pulses than max_out: the first max_out are kept, the total says
  //    how many there were.
  {
    PulseLog p;
    for (uint32_t t = 0; t < 300; t++) p.push(1000 + t * 10);
    uint16_t out[256], copied; bool complete;
    uint16_t total = p.extract(1000, 5000, out, 256, &copied, &complete);
    assert(total == 300 && copied == 256 && complete);
    assert(out[0] == 0 && out[255] == 2550);
    printf("2. capped at max_out, total still counted OK\n");
  }

  // 3. The ring overflowed inside the window: flagged incomplete, so the
  //    total is known to be a lower bound.
  {
    PulseLog p;
    for (uint32_t t = 0; t < PulseLog::N + 100; t++) p.push(1000 + t * 10);
    uint16_t out[256], copied; bool complete;
    p.extract(1000, 1000 + (PulseLog::N + 100) * 10, out, 256, &copied, &complete);
    assert(!complete);
    // A window that starts after the ring's oldest entry is complete.
    p.extract(1000 + 400 * 10, 1000 + 500 * 10, out, 256, &copied, &complete);
    assert(complete);
    printf("3. ring overflow inside the window is flagged OK\n");
  }

  // 4. Across a millis() wrap.
  {
    PulseLog p;
    p.push(0xFFFFFF00u);
    p.push(0x00000100u);
    uint16_t out[8], copied; bool complete;
    uint16_t total = p.extract(0xFFFFFE00u, 0x00000200u, out, 8, &copied, &complete);
    assert(total == 2 && out[0] == 0x100 && out[1] == 0x300);
    printf("4. wrap-safe OK\n");
  }

  // 5. Nothing in the window, and a cleared log.
  {
    PulseLog p;
    p.push(500);
    uint16_t out[8], copied; bool complete;
    assert(p.extract(1000, 2000, out, 8, &copied, &complete) == 0 && copied == 0 && complete);
    p.push(1500);
    p.clear(1600);
    assert(p.extract(1000, 2000, out, 8, &copied, &complete) == 0);
    assert(!complete);                   // the clear fell inside the window
    printf("5. empty window and clear OK\n");
  }

  // 6. The whole window overwritten — the ring's oldest entry is AFTER the
  //    window (a capture made long after first drip). Reproduced by review
  //    2026-11-15: it read "complete, 0 pulses".
  {
    PulseLog p;
    for (uint32_t i = 0; i < 600; i++) p.push(114700 + i * 40);
    uint16_t out[8], copied; bool complete;
    uint16_t total = p.extract(100000, 100250, out, 8, &copied, &complete);
    assert(total == 0 && !complete);
    printf("6. a window overwritten entirely is incomplete OK\n");
  }

  // 7. Pulses lost upstream (ISR ring overflow) after the window opened make
  //    it incomplete; a gap before the window does not.
  {
    PulseLog p;
    for (uint32_t t = 1000; t < 2000; t += 50) p.push(t);
    p.markGap(900);
    uint16_t out[64], copied; bool complete;
    p.extract(1000, 2000, out, 64, &copied, &complete);
    assert(complete);
    p.markGap(1500);
    p.extract(1000, 2000, out, 64, &copied, &complete);
    assert(!complete);
    printf("7. an upstream gap inside the window is incomplete OK\n");
  }

  // 8. A gap long after the window ended (an overflow during the pour, seen
  //    by a capture made at completion) does not condemn it; one just after
  //    its end does (the drain notices late). And a gap 25+ days old does
  //    not flip sign and condemn later windows.
  {
    PulseLog p;
    for (uint32_t t = 1000; t < 2000; t += 50) p.push(t);
    uint16_t out[64], copied; bool complete;
    p.markGap(2000 + PulseLog::GAP_SLACK_MS + 30000);
    p.extract(1000, 2000, out, 64, &copied, &complete);
    assert(complete);
    p.markGap(2500);
    p.extract(1000, 2000, out, 64, &copied, &complete);
    assert(!complete);

    PulseLog q;
    q.markGap(1000);
    uint32_t later = 1000u + 2200000000u;          // ~25.5 days on
    q.push(later + 100);
    q.extract(later, later + 1000, out, 64, &copied, &complete);
    assert(complete);
    printf("8. gaps count only near the window, and never flip sign OK\n");
  }

  // v6: idle time before a press = gap to the newest pulse strictly before it.
  {
    PulseLog p;
    for (uint32_t t = 1000; t < 3000; t += 50) p.push(t);       // a flush, last pulse 2950
    for (uint32_t t = 40100; t < 41000; t += 50) p.push(t);     // this shot's pump, already logged
    uint32_t idle = 0;
    assert(p.idleBefore(40000, &idle) && idle == 40000 - 2950);
    PulseLog e;                                                 // nothing logged: not known
    assert(!e.idleBefore(40000, &idle));
    PulseLog g;                                                 // pulses lost after the flush
    for (uint32_t t = 1000; t < 3000; t += 50) g.push(t);
    g.markGap(20000);
    assert(!g.idleBefore(40000, &idle));
    PulseLog w;                                                 // a wrapped ring keeps the newest
    for (uint32_t i = 0; i < 2 * PulseLog::N; i++) w.push(100000 + i * 20);
    assert(w.idleBefore(200000, &idle) && idle == 200000 - (100000 + (2 * PulseLog::N - 1) * 20));
    printf("idle time before a press OK\n");
  }

  printf("\nall pulse log tests passed\n");
  return 0;
}
