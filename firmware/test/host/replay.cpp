// Offline replay: feed a captured serial log (t_ms,weight_mg[,state]) through
// the real ShotDetector and print every completed record machine-readably.
#include "../../src/detector.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print(const ShotResult& r);

// The board also calls tick() from loop() (0a.8): a shot ends on time when
// the yield scale goes quiet. What tick() stores does not depend on WHEN it
// fires, only on the samples before it, so ticking just before each sample
// -- and once, far on, after the last -- reproduces the quiet-scale endings
// the capture SHOWS. Not every one the board made: the board defers tick()
// while the brew guard says the pump runs (no machine state here), a
// loop() stall that overflows the 64-entry sample queue loses samples the
// capture never had either, and a sample stamped before a tick but drained
// after it is fed one pass late. A capture of the samples is not a capture
// of the board's clock. Only yield-scale lines count: main.cpp also prints
// the dose scale as "t,w,DOSE", which is not this detector's input.
int main() {
  ShotDetector det;
  char line[128];
  uint32_t last = 0;
  bool any = false;
  while (fgets(line, sizeof(line), stdin)) {
    unsigned long t;
    long w;
    if (sscanf(line, "%lu,%ld", &t, &w) != 2) continue;
    if (strstr(line, ",DOSE")) continue;           // the dose scale
    if (any && det.tick((uint32_t)t - 1)) print(det.result());
    if (det.feed((uint32_t)t, (int32_t)w)) print(det.result());
    last = (uint32_t)t;
    any = true;
  }
  if (any && det.tick(last + 3600000u)) print(det.result());
  return 0;
}

static void print(const ShotResult& r) {
  printf("RECORD valid=%d fault=%d trunc=%d placement=%d disturbed=%d t0=%u stop_ms=%u at_stop=%d final=%d settle=%d peak=%d mean=%d win0=%u win1=%u n=%u\n",
         r.valid, r.fault, r.truncated, r.placement, r.disturbed, r.started_at_ms, r.stop_ms,
         r.yield_at_stop_mg, r.yield_final_mg, r.settle_offset_mg,
         r.peak_flow_mgps, r.mean_flow_mgps,
         r.flow_win_start_ms, r.flow_win_end_ms, r.sample_count);
  for (uint16_t i = 0; i < r.sample_count; i++)
    printf("S %u,%d\n", r.samples[i].t_ms, r.samples[i].weight_mg);
  printf("END\n");
}
