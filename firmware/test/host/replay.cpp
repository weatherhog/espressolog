// Offline replay: feed a captured serial log (t_ms,weight_mg[,state]) through
// the real ShotDetector and print every completed record machine-readably.
#include "../../src/detector.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
  ShotDetector det;
  char line[128];
  while (fgets(line, sizeof(line), stdin)) {
    unsigned long t;
    long w;
    if (sscanf(line, "%lu,%ld", &t, &w) != 2) continue;
    if (det.feed((uint32_t)t, (int32_t)w)) {
      const ShotResult& r = det.result();
      printf("RECORD valid=%d fault=%d trunc=%d t0=%u stop_ms=%u at_stop=%d final=%d settle=%d peak=%d mean=%d win0=%u win1=%u n=%u\n",
             r.valid, r.fault, r.truncated, r.started_at_ms, r.stop_ms,
             r.yield_at_stop_mg, r.yield_final_mg, r.settle_offset_mg,
             r.peak_flow_mgps, r.mean_flow_mgps,
             r.flow_win_start_ms, r.flow_win_end_ms, r.sample_count);
      for (uint16_t i = 0; i < r.sample_count; i++)
        printf("S %u,%d\n", r.samples[i].t_ms, r.samples[i].weight_mg);
      printf("END\n");
    }
  }
  return 0;
}
