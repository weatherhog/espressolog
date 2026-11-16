// Host test for drive_bench.h — the bench timing of the drive '221s.
#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include "../../src/drive_bench.h"
#include <cstdio>
#include <cassert>

static QTrace pulse(int64_t rise, int64_t fall) {
  QTrace q;
  q.feed(true, rise);
  if (fall >= 0) q.feed(false, fall);
  return q;
}

int main() {
  // 1. A clean 220 ms pulse: one rise, one fall, the width between them;
  //    repeated levels change nothing.
  {
    QTrace q;
    q.feed(false, 0);
    q.feed(true, 12);
    q.feed(true, 500);
    q.feed(false, 220012);
    q.feed(false, 300000);
    assert(q.rose() && q.fell() && q.width_us() == 220000 && q.edges == 2);
    printf("1. a clean pulse: width and edges OK\n");
  }

  // 2. Verdicts, most severe first.
  {
    QTrace quiet;                                  // a channel that never moved
    const QTrace* others[] = { &quiet, &quiet, &quiet };
    QTrace drive = pulse(10, 220010);
    QTrace gate  = pulse(0, 315000);
    assert(judge(drive, &gate, others, 3, false) == BenchVerdict::OK);

    // Held at the cap beats everything else.
    QTrace held = pulse(10, -1);
    assert(judge(held, &gate, others, 3, true) == BenchVerdict::HELD);

    // Never rose.
    QTrace none;
    assert(judge(none, &gate, others, 3, false) == BenchVerdict::NO_RISE);

    // A neighbour moved.
    QTrace moved = pulse(50, 60);
    const QTrace* bad[] = { &quiet, &moved, &quiet };
    assert(judge(drive, &gate, bad, 3, false) == BenchVerdict::OTHER_MOVED);

    // The drive pulse ended where the gate's did: the gate's reset ended it
    // (an open drive R_ext held until then, or the gate is the shorter).
    QTrace cut = pulse(10, 150100);
    QTrace short_gate = pulse(0, 150000);
    assert(judge(cut, &short_gate, others, 3, false) == BenchVerdict::ENDED_WITH_GATE);
    // ...but not when the drive ends on its own well before the gate.
    assert(judge(drive, &short_gate, others, 3, false) == BenchVerdict::OK);

    // Two pulses on the fired channel.
    QTrace twice = pulse(10, 1000);
    twice.feed(true, 2000);
    twice.feed(false, 3000);
    assert(twice.width_us() == 990);               // the first pulse is reported
    assert(judge(twice, nullptr, others, 3, false) == BenchVerdict::RETRIGGERED);

    // Timing the gate alone: no gate trace to compare with.
    assert(judge(gate, nullptr, others, 3, false) == BenchVerdict::OK);
    printf("2. verdicts: held, no pulse, neighbour moved, ended with gate, retriggered OK\n");
  }

  // 3. Stats ignore runs with no width.
  {
    WidthStats s;
    s.add(219800);
    s.add(221000);
    s.add(-1);
    s.add(220400);
    assert(s.n == 3 && s.min_us == 219800 && s.max_us == 221000 && s.mean_us() == 220400);
    printf("3. sweep stats OK\n");
  }

  printf("\nall drive bench tests passed\n");
  return 0;
}
