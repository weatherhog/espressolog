// Host test for PulseLog — the pulse train from lever press to first drip.
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
    printf("1. the press-to-drip window, as offsets from the press OK\n");
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
    p.clear();
    assert(p.extract(1000, 2000, out, 8, &copied, &complete) == 0);
    printf("5. empty window and clear OK\n");
  }

  printf("\nall pulse log tests passed\n");
  return 0;
}
