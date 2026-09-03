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

  // Scenario 3 (0a.4): cup lifted at the END of a pour is a normal stop, not
  // a fault. Real regression: on 0a.3 this excluded 3 real shots (2026-09-03).
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 400 && !done; i++, t += 100) {
    double s = i / 10.0;
    double w;
    if (s < 18.0) w = s * 2.0;          // 2 g/s pour to 36 g
    else          w = -180.0;           // cup lifted off the scale, held off
    done = feed(t, w, &r);
  }
  assert(done && r->valid && !r->fault);
  assert(r->yield_final_mg > 34000 && r->yield_final_mg < 37000);  // peak, not the lift
  printf("end-lift finalizes valid (%d mg)\n", r->yield_final_mg);

  // Scenario 3b: a momentary bump/dip (<2 s) must NOT end the shot.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 400 && !done; i++, t += 100) {
    double s = i / 10.0;
    double w;
    if (s < 10.0)      w = s * 2.0;                 // pour to 20 g
    else if (s < 11.0) w = 6.0;                     // 1 s dip (bumped) — must recover
    else if (s < 24.0) w = 20.0 + (s - 11.0) * 1.4; // resumes to ~38 g
    else               w = 38.2;                     // settles, cup stays
    done = feed(t, w, &r);
  }
  assert(done && r->valid && !r->fault);
  assert(r->yield_final_mg > 37000);   // full shot, the bump didn't truncate it
  printf("bump-during-pour ignored (%d mg)\n", r->yield_final_mg);

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
  // one sample must never produce a record. Under 0a.3 it re-arms on the
  // new mass a second later — which is the desired behavior.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 60 && !done; i++, t += 100) {
    done = feed(t, i < 3 ? 0.0 : 411.8, &r);   // lands between samples 2 and 3
  }
  assert(!done);
  assert(det.state() == ShotState::ARMED);   // re-armed at the 411.8 g baseline
  printf("cup placement OK (no record, re-armed at new baseline)\n");

  // Scenario 6 (0a.3): slow drift away from the baseline that then parks
  // becomes the new baseline — armed throughout, never a phantom pour.
  {
    // continue from the 411.8 baseline of scenario 5
    for (int i = 0; i < 80; i++, t += 100) {
      double s = i / 10.0;
      done = feed(t, 411.8 + (s < 4.0 ? s * 0.25 : 1.0), &r);  // +0.25 g/s to +1 g, parks
      assert(!done);
    }
    assert(det.state() == ShotState::ARMED);
    printf("parked drift OK (re-baselined, armed)\n");
  }

  // Scenario 7 (the 2026-09-02 lost shot): back-to-back shots with NO tare.
  // Drink #1 (37.3 g) leaves, cup #2 (51.2 g) lands un-tared, the pour goes
  // on top of it — must record, relative to the cup's own baseline.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 37.3, nullptr);   // drink resting
  assert(det.state() == ShotState::ARMED);
  for (int i = 0; i < 8; i++, t += 100) feed(t, -180.0, nullptr);  // drink lifted (scale far negative)
  for (int i = 0; i < 25; i++, t += 100) feed(t, 51.2, nullptr);   // cup #2, never tared
  assert(det.state() == ShotState::ARMED);
  done = false;
  for (int i = 0; i < 500 && !done; i++, t += 100) {
    double s = i / 10.0;
    double w;
    if (s < 6.0)       w = 51.2 + s * 0.12;                 // slow drips
    else if (s < 24.0) w = 51.9 + (s - 6.0) * 2.0;         // 2 g/s pour
    else               w = 87.9 + (s < 26 ? (s - 24) * 0.04 : 0.08);  // settle creep
    done = feed(t, w, &r);
  }
  assert(done && r->valid && !r->fault);
  assert(r->yield_final_mg > 35500 && r->yield_final_mg < 38000);  // relative, not 88 g
  printf("un-tared cup swap OK (final=%d mg relative)\n", r->yield_final_mg);

  // ---- WeighingDetector ----------------------------------------------------

  // Scenario W1: the dose workflow — cup on grinder scale, grounds land,
  // stabilizes at 18.1 g, lifted off. One event, emitted at the lift.
  {
    WeighingDetector wd;
    WeighingDetector::Event got{};
    int events = 0;
    uint32_t t = 500000;
    auto step = [&](double g, bool shot = false) {
      if (wd.feed(t, (long long)(g * 1000), shot)) { got = wd.event(); events++; }
      t += 100;
    };
    for (int i = 0; i < 20; i++) step(0.0);                    // empty scale
    for (int i = 0; i < 30; i++) step(i * 0.6);                // grinding: rising
    for (int i = 0; i < 40; i++) step(18.1);                   // stable 4 s
    for (int i = 0; i < 5; i++) step(0.0);                     // lifted off
    assert(events == 1);
    assert(got.grams_mg > 17500 && got.grams_mg < 18500);
    assert(got.stable_ms >= 3000);
    printf("weighing dose OK (%d mg over %u ms)\n", got.grams_mg, got.stable_ms);
  }

  // Scenario W2: parked past the cap emits exactly once, no second event on
  // the eventual lift.
  {
    WeighingDetector wd;
    int events = 0;
    uint32_t t = 600000;
    for (int i = 0; i < 400; i++, t += 100)                    // 40 s parked at 18 g
      if (wd.feed(t, 18000, false)) events++;
    assert(events == 1);
    for (int i = 0; i < 10; i++, t += 100)                     // lifted
      if (wd.feed(t, 0, false)) events++;
    assert(events == 1);
    printf("weighing parked-cap OK\n");
  }

  // Scenario W3: a shot in progress suppresses weighing events entirely
  // (the yield scale mid-pour must not produce phantom weighings).
  {
    WeighingDetector wd;
    int events = 0;
    uint32_t t = 700000;
    for (int i = 0; i < 100; i++, t += 100)
      if (wd.feed(t, 20000, true)) events++;
    assert(events == 0);
    printf("weighing shot-suppression OK\n");
  }

  // Scenario W4: re-placing the cup produces one event per placement — the
  // attribution rule "last one wins" needs them all.
  {
    WeighingDetector wd;
    int events = 0;
    uint32_t t = 800000;
    auto hold = [&](double g, int samples) {
      for (int i = 0; i < samples; i++, t += 100)
        if (wd.feed(t, (long long)(g * 1000), false)) events++;
    };
    hold(18.1, 30); hold(0, 5);      // first placement, lifted
    hold(18.3, 30); hold(0, 5);      // nudged and re-placed
    assert(events == 2);
    printf("weighing re-place OK (%d events)\n", events);
  }

  printf("all scenarios passed\n");
  return 0;
}
