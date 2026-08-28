// Host-side smoke test for ShotDetector: synthetic 10 Hz curves.
#include "../../src/detector.h"
#include <cstdio>
#include <cmath>
#include <cassert>

static ShotDetector det;

static bool feed(uint32_t t, double grams, const ShotResult** out) {
  bool done = det.feed(t, (int32_t)llround(grams * 1000.0));
  if (done && out) *out = &det.result();
  return done;
}

// Weight profile of a plausible shot: flat 0, first drip at +8s after arm,
// ramp to ~36 g by ~33 s, drip settle +0.9 g.
static double shotCurve(double s) {
  if (s < 8.0) return 0.0;
  if (s < 30.0) {
    double p = (s - 8.0) / 22.0;                 // 0..1
    return 36.0 * (p * p * 0.3 + p * 0.7) * 0.98; // slightly convex ramp
  }
  return 35.3;                                   // stopped; settle handled below
}

int main() {
  const ShotResult* r = nullptr;
  uint32_t t = 100000;  // arbitrary millis() origin

  // Phase A: 3 s of noise-free zero → must pass IDLE→ARMED.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  printf("armed OK\n");

  // Phase B: the pour, 30 s.
  uint32_t pour_t0_wall = t;
  bool done = false;
  for (int i = 0; i <= 300 && !done; i++, t += 100) {
    done = feed(t, shotCurve(i / 10.0), &r);
  }
  // Phase C: settle tail — drips creep in at 0.09 g/s (below the 0.1 g/s
  // stop threshold), capping at +0.9 g.
  for (int i = 0; i < 200 && !done; i++, t += 100) {
    double drip = 0.09 * (i / 10.0);
    done = feed(t, 35.3 + (drip < 0.9 ? drip : 0.9), &r);
  }
  assert(done && r);
  printf("shot complete: valid=%d fault=%d trunc=%d\n", r->valid, r->fault, r->truncated);
  printf("  t0 backdated to wall %u (pour began ~%u + 8000)\n", r->started_at_ms, pour_t0_wall);
  printf("  stop_ms=%u yield_at_stop=%d final=%d settle_offset=%d\n",
         r->stop_ms, r->yield_at_stop_mg, r->yield_final_mg, r->settle_offset_mg);
  printf("  peak=%d mgps mean=%d mgps win=%u..%u samples=%u\n",
         r->peak_flow_mgps, r->mean_flow_mgps, r->flow_win_start_ms, r->flow_win_end_ms,
         r->sample_count);
  assert(r->valid && !r->fault);
  assert(r->yield_final_mg > 35000 && r->yield_final_mg < 37000);
  assert(r->settle_offset_mg > 300 && r->settle_offset_mg < 1200);
  assert(r->sample_count > 200);
  // t=0 must be backdated near the true flow crossing (~8 s after pour start)
  uint32_t expected_t0 = pour_t0_wall + 8000;
  assert(r->started_at_ms > expected_t0 - 1500 && r->started_at_ms < expected_t0 + 2500);
  assert(det.state() == ShotState::IDLE);

  // Scenario 2: reject — a 2 g dribble.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 300 && !done; i++, t += 100) {
    double s = i / 10.0;
    double w = s < 3.0 ? s * 0.6 : 1.8;   // 0.6 g/s for 3 s then stop
    done = feed(t, w, &r);
  }
  assert(done && !r->valid && !r->fault);
  printf("reject OK (final=%d mg)\n", r->yield_final_mg);

  // Scenario 3: fault — cup lifted mid-pour.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 200 && !done; i++, t += 100) {
    double s = i / 10.0;
    double w = s < 10.0 ? s * 2.0 : -5.0;  // 2 g/s then cup gone
    done = feed(t, w, &r);
  }
  assert(done && r->fault && !r->valid);
  printf("fault OK\n");

  // Scenario 4 (real shot lost 2026-08-28): slow pre-infusion drips crossed
  // the arming band below the pour threshold — must STAY ARMED, then record
  // the shot once flow builds.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 400 && !done; i++, t += 100) {
    double s = i / 10.0;
    double w;
    if (s < 8.0)       w = s * 0.15;                    // 0.15 g/s creep: drips
    else if (s < 22.0) w = 1.2 + (s - 8.0) * 2.0;      // pour proper, 2 g/s
    else               w = 29.2;                        // stopped
    done = feed(t, w, &r);
  }
  assert(done && r->valid && !r->fault);
  printf("slow-drip shot OK (final=%d mg)\n", r->yield_final_mg);

  // Scenario 5 (fault noise, both real sessions): a 411 g cup landing in
  // one sample must disarm quietly, never spool a fault.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 60 && !done; i++, t += 100) {
    done = feed(t, i < 3 ? 0.0 : 411.8, &r);   // lands between samples 2 and 3
  }
  assert(!done);
  assert(det.state() == ShotState::IDLE);
  printf("cup placement OK (disarmed, no record)\n");

  // Scenario 6: a parked object (spoon, 20 g) placed gently must disarm
  // after ~2 s of sitting flat, not wedge ARMED forever.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  for (int i = 0; i < 80; i++, t += 100) {
    double s = i / 10.0;
    feed(t, s < 4.0 ? s * 0.25 : 1.0, nullptr);  // slow 0.25 g/s to 1 g, then parked
  }
  assert(det.state() == ShotState::IDLE);
  printf("parked object OK (disarmed)\n");

  printf("all scenarios passed\n");
  return 0;
}
