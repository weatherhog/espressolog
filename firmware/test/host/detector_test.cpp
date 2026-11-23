// Host-side smoke test for ShotDetector: synthetic 10 Hz curves.
#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include "../../src/detector.h"
#include "shot_curves.h"
#include <cstdio>
#include <cmath>
#include <cassert>
#include <initializer_list>

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
  assert(!r->placement);                          // a real pour is not a cup landing

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
  assert(!r->disturbed);               // 0a.7: the recovery is not a disturbance
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

  // Scenario W1b (shot 20's missed dose, 2026-09-04): the dose cup sits for
  // only ~1.3 s. Must emit — Lucas works fast.
  {
    WeighingDetector wd;
    int events = 0;
    uint32_t t = 550000;
    auto step = [&](double g) { if (wd.feed(t, (long long)(g * 1000), false)) events++; t += 100; };
    for (int i = 0; i < 10; i++) step(-256.7);   // scale tared with cup, cup off
    step(18.1);                                   // cup+grounds lands
    for (int i = 0; i < 12; i++) step(18.1 + ((i % 2) ? 0.1 : -0.1));  // 1.2 s of jitter
    step(25.2); step(-539.3);                     // nudged, then everything off
    assert(events == 1);
    printf("weighing short-sit OK (%d event in 1.3 s)\n", events);
  }

  // Scenario W1c: grinding INTO the cup on the scale (~1 g/s) must not read
  // as a stable weighing while it's still filling.
  {
    WeighingDetector wd;
    int events = 0;
    uint32_t t = 560000;
    for (int i = 0; i < 40; i++, t += 100)
      if (wd.feed(t, 100 * i, false)) events++;     // 0 → 4 g at 1 g/s
    for (int i = 0; i < 160; i++, t += 100)
      if (wd.feed(t, 4000 + 100 * i, false)) events++;  // 4 → 20 g at 1 g/s
    assert(events == 0);
    for (int i = 0; i < 15; i++, t += 100)
      if (wd.feed(t, 20000, false)) events++;       // done, sits 1.5 s
    if (wd.feed(t, -200000, false)) events++;       // lifted
    assert(events == 1);
    printf("weighing grind-in-place OK (no event while filling, 1 after)\n");
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

  // Scenario 8 (shot 19, 2026-09-04): the pour ends and the cup SITS with
  // real Bookoo jitter (±0.15 g). The stop must confirm within a few seconds
  // of the plateau, not wait for the lift 13 s later.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  assert(det.state() == ShotState::ARMED);
  done = false;
  {
    uint32_t pour_start = t;
    int jit = 0;
    for (int i = 0; i < 500 && !done; i++, t += 100) {
      double s = i / 10.0;
      double w;
      if (s < 20.0)      w = s * 2.0;                                  // pour to 40 g
      else               w = 40.0 + ((jit++ % 3) - 1) * 0.15;         // flat + jitter
      done = feed(t, w, &r);
    }
    assert(done && r->valid);
    // stop must land near the plateau start (20 s), NOT at the end of sitting
    assert(r->stop_ms > 18000 && r->stop_ms < 24000);
    (void)pour_start;
    printf("noisy plateau stop OK (stop_ms=%u, final=%d mg)\n", r->stop_ms, r->yield_final_mg);
  }

  // Scenario 9 (missing shot B hypothesis, 2026-09-04): cup down and the
  // switch hit immediately — no second of stillness, so ARMED never happens.
  // The pour must still be detected from IDLE with the pre-rise baseline.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);   // empty scale, armed at 0
  for (int i = 0; i < 3; i++, t += 100) feed(t, 210.0, nullptr);   // cup lands: step → IDLE
  assert(det.state() == ShotState::IDLE);
  done = false;
  for (int i = 0; i < 500 && !done; i++, t += 100) {
    double s = i / 10.0;
    double w;
    if (s < 18.0)      w = 210.0 + s * 2.0;     // pour starts within 0.3 s of placement
    else if (s < 24.0) w = 246.0;               // flat
    else               w = -5.0;                // lifted
    done = feed(t, w, &r);
  }
  assert(done && r->valid);
  assert(r->yield_final_mg > 34000 && r->yield_final_mg < 38000);  // ~36 g relative to the 210 g cup
  printf("cup-down-and-go from IDLE OK (final=%d mg)\n", r->yield_final_mg);

  // Scenario 10 (the real workflow): cup preheated, machine already
  // running. Cup lands during pre-infusion, drips begin, THEN the user
  // tares (-cup step while ~1.5 g is out). Must be one continuous shot —
  // no reject, no restart, yield relative to the tare.
  for (int i = 0; i < 30; i++, t += 100) feed(t, 0.0, nullptr);
  done = false;
  {
    int records = 0;
    for (int i = 0; i < 600 && !done; i++, t += 100) {
      double s = i / 10.0;
      double w;
      if (s < 0.3)        w = 0.0;
      else if (s < 2.0)   w = 205.0;                    // cup lands (empty, wet)
      else if (s < 3.5)   w = 205.0 + (s - 2.0) * 1.0;  // drips start: 0..1.5 g
      else if (s < 24.0)  w = (s - 3.5) * 1.8 + 1.5 - 1.5;  // TARED at 3.5 s: reads from 0, pour 1.8 g/s
      else if (s < 30.0)  w = 36.9;                     // flat
      else                w = -205.0;                   // lifted
      if (feed(t, w, &r)) { records++; done = r->valid; }
    }
    assert(done && r->valid);
    assert(records == 1);                               // no reject split off
    // True yield = 1.5 g pre-tare drips + 36.9 g after = 38.4 g. The scale's
    // display forgot the first 1.5 g when tared; the rebased baseline didn't.
    assert(r->yield_final_mg > 37500 && r->yield_final_mg < 39500);
    printf("tare-during-drips OK (1 record, final=%d mg incl. pre-tare drips)\n", r->yield_final_mg);
  }


  // 0d: the flowmeter count is windowed at t=0, so a hot-water draw pulled
  // before the shot cannot be attributed to it. CLAUDE.md flags that exactly
  // this would happen without scoping — lever B runs the pump, and whether
  // that water crosses the flowmeter is untraced, so the firmware must not
  // depend on the answer.
  {
    ShotDetector d;
    uint32_t tt = 500000;
    uint32_t pulses = 777;          // an earlier draw already moved the counter

    for (int i = 0; i < 30; i++) { d.feed(tt, 0, pulses); tt += 100; }   // 3 s armed, no flow
    assert(d.state() == ShotState::ARMED);

    const ShotResult* rr = nullptr;
    bool done = false;
    for (int i = 0; i < 250 && !done; i++) {                             // 25 s ramp to 36 g
      pulses += (i % 2) ? 1 : 0;                                         // 5 pulses/s
      done = d.feed(tt, (int32_t)(36000.0 * i / 250.0), pulses);
      tt += 100;
    }
    for (int i = 0; i < 100 && !done; i++) {                             // pump off: count frozen
      done = d.feed(tt, 36000, pulses);
      tt += 100;
    }
    assert(done);
    rr = &d.result();
    assert(rr->valid && rr->sample_count > 10);

    assert(rr->samples[0].inlet_pulses == 0);        // the window opens at zero
    uint16_t last = rr->samples[rr->sample_count - 1].inlet_pulses;
    // ~125 flowed during the pour. The band is tight enough to also exclude
    // the 777 carried in beforehand — an earlier version asserted `last < 700`
    // on the next line as if that were an independent check; it was implied by
    // this one and proved nothing.
    assert(last > 110 && last < 130);
    for (uint16_t i = 1; i < rr->sample_count; i++)  // cumulative never goes backwards
      assert(rr->samples[i].inlet_pulses >= rr->samples[i - 1].inlet_pulses);
    printf("flowmeter windowing OK (%u pulses in-shot, 777 pre-shot excluded)\n", last);
  }

  // The cup put back on the scale, 2026-11-15 (shot 180): the device's own
  // timestamped weight stream from the server's live journal, mg. The scale
  // read -151 g with the cup off (it had been tared with the cup on); the
  // cup came down slowly enough — 3-4 g per sample — to pass the per-sample
  // step guard into POURING, then rose 151 g in ~0.6 s. Recorded, but
  // flagged as a placement, so the server excludes it.
  {
    static const struct { uint32_t t; int32_t w; } s180[] = {
      {353634,-151390}, {353754,-151390}, {353845,-151390}, {353964,-151390}, {354055,-151390}, {354144,-151390}, {354264,-151390}, {354354,-151390}, {354474,-151390}, {354564,-151390}, {354654,-151390}, {354774,-151390}, {354864,-151390}, {354985,-151390}, {355104,-151390}, {355135,-151390}, {355285,-151390}, {355374,-151390}, {355434,-151390}, {355584,-151390}, {355645,-151390}, {355734,-151390}, {355884,-151390}, {355944,-151390}, {356095,-151390}, {356155,-151390}, {356244,-151390}, {356395,-151390}, {356455,-151390}, {356544,-151390}, {356664,-151390}, {356754,-151390}, {356844,-151390}, {356964,-151390}, {357054,-151390}, {357144,-151390}, {357265,-151390}, {357354,-151390}, {357474,-151390}, {357564,-151390}, {357654,-151390}, {357775,-151390}, {357864,-151390}, {357954,-151390}, {358075,-151390}, {358164,-151390}, {358254,-151390}, {358374,-151390}, {358464,-151390}, {358554,-151390}, {358674,-151390}, {358764,-151390}, {358884,-151390}, {358975,-151390}, {359064,-151390}, {359185,-151390}, {359274,-151390}, {359365,-151390}, {359484,-151390}, {359575,-151390}, {359694,-151390}, {359784,-151390}, {359904,-151390}, {359994,-151390}, {360084,-151390}, {360205,-151390}, {360294,-151390}, {360415,-151390}, {360504,-151390}, {360594,-151390}, {360684,-151390}, {360804,-151390}, {360894,-151390}, {361014,-151390}, {361104,-151390}, {361224,-151390}, {361314,-151390}, {361374,-151390}, {361524,-151390}, {361584,-151390}, {361735,-151390}, {361824,-151390}, {361884,-151390}, {362034,-151390}, {362094,-147700}, {362184,-144800}, {362334,-144800}, {362394,-148100}, {362484,-138300}, {362604,-137300}, {362695,-149600}, {362785,-104400}, {362905,-41800}, {362994,-49300}, {363114,-13800}, {363204,-1200}, {363294,-800}, {363415,-800}, {363504,-800}, {363594,-800}, {363714,-600}, {363804,-600}, {363894,-600}, {364015,-600}, {364134,-600}, {364195,-600}, {364314,-600}, {364404,-600}, {364524,-600}, {364614,0}, {364704,0}, {364824,0}, {364914,0}, {365004,0}, {365124,0}, {365214,0}, {365304,0}, {365424,0}, {365514,0}, {365604,0}, {365724,0}, {365814,0}, {365934,0}, {366024,0}, {366114,0}, {366234,0}, {366355,0}, {366445,0}, {366534,0}, {366654,0}, {366744,0}, {366865,0}, {366954,0}, {367044,0}, {367164,0}, {367255,0}, {367375,0}, {367464,0}, {367525,0}, {367675,0}, {367764,0}, {367825,0}, {367974,0}, {368034,0}, {368124,0}, {368274,0}, {368334,0}, {368425,0}, {368544,0}, {368634,0}, {368784,0}, {368844,0}, {368934,0}, {369054,0}, {369144,0}, {369235,0}, {369354,0}, {369445,0}, {369534,0}, {369654,0}, {369745,0}, {369834,0}, {369954,0}, {370044,0}, {370134,0}, {370254,0}, {370344,0}, {370464,0}, {370555,0}, {370645,0}, {370764,0}, {370855,0}, {370945,0}, {371064,0}, {371154,0}, {371244,0}, {371364,0}, {371454,0}, {371574,0}, {371664,0}, {371754,0}, {371874,0}, {371964,0}, {372085,0}, {372174,0}, {372294,0}, {372385,0}, {372475,0}
    };
    ShotDetector d;
    const ShotResult* rr = nullptr;
    for (const auto& x : s180) {
      if (d.feed(x.t, x.w)) { rr = &d.result(); break; }
    }
    // Feed flat until it completes, if the journal window ended first.
    for (uint32_t t2 = s180[sizeof(s180) / sizeof(s180[0]) - 1].t + 100; !rr && t2 < 500000; t2 += 100)
      if (d.feed(t2, 600)) rr = &d.result();
    assert(rr);
    printf("shot 180 replay: placement=%d disturbed=%d final=%d\n",
           rr->placement, rr->disturbed, rr->yield_final_mg);
    // It lands mid-pour, where 0a.7 only flags: the yield still claims the
    // whole ~151 g, and the placement flag is what excludes it.
    assert(rr->disturbed);
    assert(rr->yield_final_mg > 140000);              // what it claims
    assert(rr->placement);
    printf("cup put back (real stream, shot 180) is flagged a placement OK\n");
  }

  // A REAL shot whose weight dips by more than 20 g and recovers must NOT be
  // flagged a placement (review, 2026-11-15: the first rule scanned the
  // whole curve, so the recovery from a dip read as a 20 g rise, and good
  // shots were excluded). Three dips, each on a pour already at 30 g: a
  // brief lift, a bump, and the single 0 g sample a cup-scale reconnect
  // injects mid-shot (remote_scales' bookoo connect() ends in setWeight(0)).
  {
    struct Dip { const char* name; uint32_t start_s10; uint32_t len_s10; double w; };
    const Dip dips[] = {
      {"1 s lift to -170 g", 200, 10, -170.0},
      {"1 s bump down to 9 g", 200, 10, 9.0},
      {"one 0 g sample (reconnect)", 200, 1, 0.0},
    };
    for (const Dip& dp : dips) {
      ShotDetector d;
      const ShotResult* rr = nullptr;
      uint32_t tt = 100000;
      for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
      bool fin = false;
      for (int i = 0; i <= 300 && !fin; i++, tt += 100) {
        double w = shotCurve(i / 10.0);
        if ((uint32_t)i >= dp.start_s10 && (uint32_t)i < dp.start_s10 + dp.len_s10) w = dp.w;
        fin = d.feed(tt, (int32_t)llround(w * 1000.0));
      }
      for (int i = 0; i < 200 && !fin; i++, tt += 100) fin = d.feed(tt, 35300);
      assert(fin);
      rr = &d.result();
      if (rr->placement) printf("FLAGGED: %s\n", dp.name);
      if (rr->disturbed) printf("DISTURBED: %s\n", dp.name);
      assert(!rr->placement);
      assert(!rr->disturbed);   // 0a.7: climbing back is not a jump above the peak
      assert(rr->yield_final_mg > 35000);
    }
    printf("dips that recover (lift, bump, reconnect 0 g) are not placements OK\n");
  }

  // A scale reconnect mid-pour: the samples stop for several seconds, then
  // resume with the injected 0 g and the weight the pour reached meanwhile.
  // Measured against the pre-gap peak that read as a 20 g rise in 1 s once
  // the gap passed ~8 s (review, 2026-11-15). Never a placement.
  {
    for (uint32_t gap_s : {4u, 8u, 10u, 12u}) {
      ShotDetector d;
      uint32_t tt = 100000;
      for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
      const uint32_t pour0 = tt;
      bool fin = false;
      auto w_at = [&](uint32_t now) {            // 2.5 g/s for 16 s, then flat
        double s = (now - pour0) / 1000.0;
        return s < 16.0 + gap_s ? 2.5 * s : 2.5 * (16.0 + gap_s);
      };
      for (; tt < pour0 + 8000 && !fin; tt += 100) fin = d.feed(tt, (int32_t)llround(w_at(tt) * 1000.0));
      tt += gap_s * 1000;                         // nothing arrives
      fin = fin || d.feed(tt, 0);                 // the reconnect's 0 g
      tt += 100;
      for (int i = 0; i < 400 && !fin; i++, tt += 100) fin = d.feed(tt, (int32_t)llround(w_at(tt) * 1000.0));
      assert(fin);
      const ShotResult* rr = &d.result();
      if (rr->placement) printf("FLAGGED after a %u s gap\n", gap_s);
      assert(!rr->placement);
      assert(!rr->disturbed);   // 0a.7: the climb after the gap is not a jump
      assert(rr->valid);
    }
    printf("a pour across a scale-reconnect gap is not a placement OK\n");
  }

  // ---- 0a.7: something set on the scale must not become the yield ----
  // Replays a real stored curve on a fresh detector, arms it on 0 g first,
  // then lifts the cup off (scale far negative) and returns the record.
  auto replayThenLift = [](const CurvePoint* c, size_t n) -> ShotResult {
    static ShotDetector d;
    d = ShotDetector();
    uint32_t base = 500000, tt = base;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    assert(d.state() == ShotState::ARMED);
    uint32_t start = tt;
    bool fin = false;
    for (size_t i = 0; i < n && !fin; i++) {
      tt = start + c[i].t_ms;
      fin = d.feed(tt, c[i].weight_mg);
    }
    for (int i = 0; i < 40 && !fin; i++) { tt += 100; fin = d.feed(tt, -200000); }
    assert(fin);
    return d.result();
  };
  {
    // Shot 205 (2026-11-21): stopped at ~28.5 s on ~37.4 g, then +7.8 g and
    // +3.2 g at 33.9 s while settling, then lifted. 0a.6 recorded 49.41 g.
    // 0a.8: the lift during settling keeps the recognised stop (~28.5 s);
    // up to 0a.7 it overwrote stop_ms with the last sample before the lift.
    ShotResult rr = replayThenLift(SHOT_205, sizeof SHOT_205 / sizeof SHOT_205[0]);
    printf("shot 205 replay: valid=%d disturbed=%d stop_ms=%u at_stop=%d final=%d settle=%d\n",
           rr.valid, rr.disturbed, rr.stop_ms, rr.yield_at_stop_mg, rr.yield_final_mg,
           rr.settle_offset_mg);
    assert(rr.valid && rr.disturbed && !rr.placement);
    // The flat weight before the grip was 37.81 g.
    assert(rr.yield_final_mg >= 37700 && rr.yield_final_mg <= 37950);
    assert(rr.stop_ms > 26000 && rr.stop_ms < 30000);
    assert(rr.settle_offset_mg == rr.yield_final_mg - rr.yield_at_stop_mg);
    assert(rr.settle_offset_mg >= 0 && rr.settle_offset_mg <= 600);
    printf("a jump during settling is not the yield OK\n");
  }
  {
    // Shot 81 (2026-09-29): flat at 37.4 g, then +0.8 g and +5.3 g in the
    // last two samples, then lifted. 0a.6 recorded 43.5 g.
    ShotResult rr = replayThenLift(SHOT_81, sizeof SHOT_81 / sizeof SHOT_81[0]);
    printf("shot 81 replay: valid=%d disturbed=%d stop_ms=%u final=%d\n",
           rr.valid, rr.disturbed, rr.stop_ms, rr.yield_final_mg);
    assert(rr.valid && rr.disturbed);
    assert(rr.yield_final_mg >= 37000 && rr.yield_final_mg <= 38300);
    printf("shot 81's last-sample jump is not the yield OK\n");
  }
  {
    // A hand left resting on the scale through the end of settling: the
    // yield is the weight before it, not the weight with it.
    ShotDetector d;
    uint32_t tt = 900000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 300 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = s < 18.0 ? 2.0 * s : 36.0;      // 2 g/s to 36 g, then flat
      if (s >= 21.5) w += 12.0;                  // hand on the scale while settling
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("resting hand: disturbed=%d final=%d\n", rr.disturbed, rr.yield_final_mg);
    assert(rr.valid && rr.disturbed);
    assert(rr.yield_final_mg > 35500 && rr.yield_final_mg < 36500);
    printf("a hand resting through settling is not the yield OK\n");
  }
  {
    // An ordinary pour, lifted after the stop, is not disturbed.
    ShotDetector d;
    uint32_t tt = 1300000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    uint32_t start = tt;
    for (int i = 0; i < 300 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = s < 18.0 ? 2.0 * s : (s < 23.0 ? 36.0 : -180.0);   // lifted at 23 s
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("clean lift after stop: disturbed=%d stop_ms=%u final=%d settle=%d\n",
           rr.disturbed, rr.stop_ms, rr.yield_final_mg, rr.settle_offset_mg);
    assert(rr.valid && !rr.disturbed);
    assert(rr.yield_final_mg > 35500 && rr.yield_final_mg < 36500);
    // 0a.8: the plateau began at 18 s; the lift at 23 s does not move it.
    assert(rr.stop_ms >= 17500 && rr.stop_ms <= 18300);
    (void)start;
    printf("a clean lift after the stop is not disturbed OK\n");
  }

  {
    // An object set on the scale MID-POUR and taken off again. 0a.7 only
    // flags it; the yield logic is 0a.6's. (Reviews 2026-11-21: two drafts
    // that tried to take the object out under the moving weight truncated
    // the shot at 60 s, or ended it early when the object came off in steps.)
    for (int lift_at : {0, 25}) {
      ShotDetector d;
      uint32_t tt = 2000000;
      bool fin = false;
      for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
      for (int i = 0; i < 700 && !fin; i++, tt += 100) {
        double s = i / 10.0;
        double w = s < 18.0 ? 2.0 * s : 36.0;    // 2 g/s to 36 g
        if (s >= 10.0 && s < 14.0) w += 8.0;     // 8 g on the scale, 10-14 s
        if (lift_at && s >= lift_at) w = -180.0; // cup lifted
        fin = d.feed(tt, (int32_t)llround(w * 1000.0));
      }
      assert(fin);
      const ShotResult& rr = d.result();
      printf("object mid-pour (lift %d): disturbed=%d trunc=%d stop_ms=%u final=%d\n",
             lift_at, rr.disturbed, rr.truncated, rr.stop_ms, rr.yield_final_mg);
      assert(rr.valid && rr.disturbed && !rr.truncated);
      assert(rr.yield_final_mg > 35000 && rr.yield_final_mg < 36800);
      if (!lift_at) assert(rr.stop_ms > 16000 && rr.stop_ms < 20000);
    }
    printf("an object on and off the scale mid-pour is flagged, the shot unharmed OK\n");
  }
  {
    // Review 2026-11-21: a FAST pour across a short sample gap climbs past
    // "peak + 5 g" in one sample (7 g/s over 800 ms = 5.6 g). Judged as a
    // rate it is a pour, not an object.
    struct Case { double gps; uint32_t gap_ms; };
    for (const Case& c : {Case{7.0, 800}, Case{6.0, 900}, Case{9.0, 700}}) {
      ShotDetector d;
      uint32_t tt = 3000000;
      bool fin = false;
      for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
      uint32_t pour0 = tt;
      auto w_at = [&](uint32_t now) {
        double s = (now - pour0) / 1000.0;
        double v = c.gps * s;
        return v < 38.0 ? v : 38.0;
      };
      for (; tt < pour0 + 3000; tt += 100) d.feed(tt, (int32_t)llround(w_at(tt) * 1000.0));
      tt += c.gap_ms - 100;                      // samples stop briefly
      for (int i = 0; i < 400 && !fin; i++, tt += 100)
        fin = d.feed(tt, (int32_t)llround(w_at(tt) * 1000.0));
      assert(fin);
      const ShotResult& rr = d.result();
      printf("fast pour %.0f g/s across %u ms: disturbed=%d final=%d trunc=%d\n",
             c.gps, c.gap_ms, rr.disturbed, rr.yield_final_mg, rr.truncated);
      assert(!rr.disturbed && !rr.truncated);
      assert(rr.yield_final_mg > 37000);
    }
    printf("a fast pour across a short gap is not a disturbance OK\n");
  }
  {
    // A spoon resting 0.5 s mid-pour. Flagged only: it still becomes the
    // first sample at 80 % of final and moves mean flow, exactly as on 0a.6
    // (known limit) -- the flag is how a reader knows to distrust it.
    ShotDetector d;
    uint32_t tt = 4000000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 400 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = s < 18.0 ? 2.0 * s : 36.0;
      if (s >= 12.0 && s < 12.5) w += 8.0;       // spoon
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("spoon mid-pour: disturbed=%d mean=%d mgps win=%u..%u\n",
           rr.disturbed, rr.mean_flow_mgps, rr.flow_win_start_ms, rr.flow_win_end_ms);
    assert(rr.disturbed && rr.valid);
    assert(rr.yield_final_mg > 35500 && rr.yield_final_mg < 36500);
    printf("a spoon mid-pour is flagged OK\n");
  }

  // Review 2026-11-21, round 2: the cases that broke the offset draft, now
  // in SETTLING where 0a.7 repairs the yield. Pour 2 g/s to 36 g, flat from
  // 18 s (stop confirmed ~20 s, settling to ~25 s); `obj` adds weight.
  auto settleCase = [](const char* name, double (*obj)(double), bool burst,
                       int lift_at) -> ShotResult {
    static ShotDetector d;
    d = ShotDetector();
    uint32_t tt = 6000000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 400 && !fin; i++) {
      double s = i / 10.0;
      double w = (s < 18.0 ? 2.0 * s : 36.0) + obj(s);
      if (lift_at && s >= lift_at) w = -180.0;
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
      tt += (burst && i % 3 == 0) ? 0 : 100;   // every third sample: same ms
    }
    assert(fin);
    ShotResult r = d.result();
    printf("settling, %s: disturbed=%d final=%d stop_ms=%u\n", name, r.disturbed,
           r.yield_final_mg, r.stop_ms);
    assert(r.valid && r.disturbed && !r.placement);
    assert(r.yield_final_mg > 35500 && r.yield_final_mg < 36800);
    // Never ended early. (With bursts the clock runs a third slower than the
    // sample count, so the pour itself ends earlier in ms.) From 0a.8 a lift
    // during settling keeps the stop, so it is checked there too.
    if (!burst) assert(r.stop_ms > 16000 && r.stop_ms < 20500);
    return r;
  };
  settleCase("wobbling 10 g hand", [](double s) {
    return s >= 21.0 ? 10.0 + ((int)(s * 10) % 2 ? 0.6 : -0.6) : 0.0; }, false, 0);
  settleCase("12 g off in 5/5/2 g steps", [](double s) {
    return s < 21.0 ? 0.0 : s < 22.5 ? 12.0 : s < 22.6 ? 7.0 : s < 22.7 ? 2.0 : 0.0; }, false, 0);
  settleCase("12 g, 7 g taken off", [](double s) {
    return s < 21.0 ? 0.0 : s < 22.5 ? 12.0 : 5.0; }, false, 0);
  settleCase("8 g with same-ms bursts", [](double s) {
    return s >= 21.0 ? 8.0 : 0.0; }, true, 0);
  settleCase("10 g then lifted", [](double s) {
    return s >= 21.0 ? 10.0 : 0.0; }, false, 23);
  // Review 2026-11-21, round 3: a hand pressing and releasing. A draft that
  // unfroze the peak when the weight came back near it let each release
  // raise the peak until a 7 g hand passed for drips (43 g of 36).
  settleCase("7 g hand pressing and releasing", [](double s) {
    if (s < 21.0) return 0.0;
    double ph = s - 21.0;                         // 1 s cycles: 0.5 s on, 0.5 s off
    double off = 1.4 * (int)(ph);                 // each release leaves a little more
    return (ph - (int)ph) < 0.5 ? 7.0 : (off < 4.0 ? off : 4.0); }, false, 0);
  settleCase("8 g, then 1.4 g left behind", [](double s) {
    return s < 21.0 ? 0.0 : s < 22.5 ? 8.0 : 1.4; }, false, 0);
  // Review 2026-11-21, round 4: an object landing over several samples
  // (4 g + 4 g passed the one-sample rule: 44 g of 36, unflagged).
  settleCase("8 g landing as 4 + 4 g", [](double s) {
    return s < 21.0 ? 0.0 : s < 21.1 ? 4.0 : 8.0; }, false, 0);
  settleCase("12 g landing as 4 + 4 + 4 g", [](double s) {
    return s < 21.0 ? 0.0 : s < 21.1 ? 4.0 : s < 21.2 ? 8.0 : 12.0; }, false, 0);
  // Round 5: still samples between the steps. A step rule let the first
  // flat sample after each 4 g take it into the peak (44 g, unflagged).
  settleCase("4 g, still, then 4 g more", [](double s) {
    return s < 21.0 ? 0.0 : s < 21.3 ? 4.0 : 8.0; }, false, 0);
  printf("objects during settling never reach the yield or end the shot early OK\n");
  {
    // Round 5: under 5 g, so not a disturbance -- but a lift must still read
    // the peak without it, even after still samples. A 3 g object, and the
    // 2 g grip of a hand picking the cup up, each resting two samples.
    struct Small { const char* name; double g; double from; };
    for (const Small& c : {Small{"3 g object", 3.0, 21.0}, Small{"2 g grip", 2.0, 22.7}}) {
      ShotDetector d;
      uint32_t tt = 9000000;
      bool fin = false;
      for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
      for (int i = 0; i < 400 && !fin; i++, tt += 100) {
        double s = i / 10.0;
        double w = (s < 18.0 ? 2.0 * s : 36.0) + (s >= c.from ? c.g : 0.0);
        if (s >= 23.0) w = -180.0;                 // lifted
        fin = d.feed(tt, (int32_t)llround(w * 1000.0));
      }
      assert(fin);
      const ShotResult& rr = d.result();
      printf("%s then lift: disturbed=%d final=%d\n", c.name, rr.disturbed, rr.yield_final_mg);
      assert(rr.valid && !rr.disturbed);
      assert(rr.yield_final_mg > 35500 && rr.yield_final_mg < 36500);
    }
    printf("a small object or a grip before the lift is not the yield OK\n");
  }
  {
    // Round 4: a portafilter (300 g) set down while settling. The yield was
    // already protected, but the placement scan read the settling samples
    // and excluded the whole shot. It now stops where the object landed.
    ShotDetector d;
    uint32_t tt = 7000000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 400 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = (s < 18.0 ? 2.0 * s : 36.0) + (s >= 21.0 ? 300.0 : 0.0);
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("portafilter while settling: placement=%d disturbed=%d final=%d\n",
           rr.placement, rr.disturbed, rr.yield_final_mg);
    assert(rr.valid && rr.disturbed && !rr.placement);
    assert(rr.yield_final_mg > 35500 && rr.yield_final_mg < 36500);
    printf("an object over 20 g while settling is not a placement OK\n");
  }
  {
    // Round 5: the same portafilter, landing in a same-millisecond BLE burst.
    // appendSample() merges it into the previous slot, which a cut taken
    // before appending left inside the scan -- the shot was excluded.
    ShotDetector d;
    uint32_t tt = 7500000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 400 && !fin; i++) {
      double s = i / 10.0;
      bool land = (i == 210);
      double w = (s < 18.0 ? 2.0 * s : 36.0) + (s >= 21.0 ? 300.0 : 0.0);
      if (!land) tt += 100;                        // the landing shares the previous ms
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("portafilter in a same-ms burst: placement=%d disturbed=%d final=%d\n",
           rr.placement, rr.disturbed, rr.yield_final_mg);
    assert(rr.valid && rr.disturbed && !rr.placement);
    printf("a burst landing while settling is not a placement OK\n");
  }
  {
    // Round 4: a forced stop at 60 s with the pour still running. The peak
    // is not the true weight there, so it is not frozen: flagged only.
    ShotDetector d;
    uint32_t tt = 8000000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 800 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = 0.6 * s + (s >= 61.0 ? 8.0 : 0.0);   // slow pour past 60 s
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("truncated, object at 61 s: trunc=%d disturbed=%d final=%d\n",
           rr.truncated, rr.disturbed, rr.yield_final_mg);
    assert(rr.truncated && rr.disturbed);
    assert(rr.yield_final_mg > 44000);               // pour kept counting (0a.6 behaviour)
    printf("a truncated shot is flagged, not frozen OK\n");
  }
  // The tail test migration 016 (v_shot_end) applies: a settle that ran its
  // course ends >= STOP_WINDOW_MS + SETTLE_MS after stop_ms (SETTLE_MS after
  // a forced stop); a lift ends sooner.
  auto tailMs = [](const ShotResult& rr) -> uint32_t {
    return rr.sample_count ? rr.samples[rr.sample_count - 1].t_ms - rr.stop_ms : 0;
  };
  {
    // 0a.8: force-stopped at 60 s, then lifted while settling. The forced
    // stop's stop_ms and yield_at_stop are kept; nothing settled, so the
    // offset is 0 rather than the pour that kept running.
    ShotDetector d;
    uint32_t tt = 8500000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 800 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = s < 62.0 ? 0.6 * s : -180.0;          // slow pour, lifted at 62 s
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("truncated then lifted: trunc=%d stop_ms=%u at_stop=%d final=%d settle=%d tail=%u\n",
           rr.truncated, rr.stop_ms, rr.yield_at_stop_mg, rr.yield_final_mg,
           rr.settle_offset_mg, tailMs(rr));
    assert(rr.valid && rr.truncated);
    assert(rr.stop_ms >= ShotDetector::MAX_SHOT_MS && rr.stop_ms < ShotDetector::MAX_SHOT_MS + 500);
    assert(rr.yield_at_stop_mg > 35000 && rr.yield_at_stop_mg < 37000);   // 0.6 g/s x ~60 s
    assert(rr.yield_final_mg >= rr.yield_at_stop_mg);
    assert(rr.settle_offset_mg == 0);
    assert(tailMs(rr) < ShotDetector::SETTLE_MS);
    printf("a lift after a forced stop keeps the forced stop OK\n");
  }
  {
    // Review 2026-11-23: one 0 g sample (a cup-scale reconnect) while
    // settling is a dip, not a lift. Up to 0a.7 it ended the shot there.
    for (bool dip : {false, true}) {
      ShotDetector d;
      uint32_t tt = 9500000;
      bool fin = false;
      for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
      for (int i = 0; i < 400 && !fin; i++, tt += 100) {
        double s = i / 10.0;
        double w = s < 18.0 ? 2.0 * s : 36.0 + (s - 18.0 < 6.0 ? 0.05 * (s - 18.0) : 0.3);
        if (dip && i == 215) { tt += 1500; w = 0.0; }   // a gap, then the injected 0 g
        fin = d.feed(tt, (int32_t)llround(w * 1000.0));
      }
      assert(fin);
      const ShotResult& rr = d.result();
      printf("settling %s: stop_ms=%u final=%d settle=%d tail=%u\n",
             dip ? "with a 0 g dip" : "undisturbed", rr.stop_ms, rr.yield_final_mg,
             rr.settle_offset_mg, tailMs(rr));
      assert(rr.valid && !rr.disturbed && !rr.placement);
      assert(rr.stop_ms >= 17500 && rr.stop_ms <= 18300);
      assert(rr.yield_final_mg > 36000 && rr.yield_final_mg < 36500);
      assert(rr.settle_offset_mg > 0 && rr.settle_offset_mg < 1000);
      assert(tailMs(rr) >= ShotDetector::STOP_WINDOW_MS + ShotDetector::SETTLE_MS);
    }
    printf("a single dropped sample while settling is not a lift OK\n");
  }
  {
    // The lift side of the tail test, and the lift's offset: drips before
    // the lift only. Lifted 3 s into settling.
    ShotDetector d;
    uint32_t tt = 9700000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 400 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = s < 18.0 ? 2.0 * s : 36.0 + (s - 18.0 < 6.0 ? 0.05 * (s - 18.0) : 0.3);
      if (s >= 23.0) w = -180.0;
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("lifted 3 s into settling: stop_ms=%u settle=%d tail=%u last=%d\n",
           rr.stop_ms, rr.settle_offset_mg, tailMs(rr),
           rr.samples[rr.sample_count - 1].weight_mg);
    assert(rr.stop_ms >= 17500 && rr.stop_ms <= 18300);
    assert(rr.settle_offset_mg == rr.yield_final_mg - rr.yield_at_stop_mg);
    assert(rr.settle_offset_mg > 0 && rr.settle_offset_mg < 1000);
    assert(tailMs(rr) < ShotDetector::STOP_WINDOW_MS + ShotDetector::SETTLE_MS);
    assert(rr.samples[rr.sample_count - 1].weight_mg > 30000);   // the dip is trimmed off
    printf("a lift while settling is told apart by its tail OK\n");
  }
  // Migration 016's threshold, mirrored: 7 s after a flat stop, 5 s after
  // a forced one, and never past the last second before t_ms tops out at
  // 65535 (appendSample() drops later samples).
  auto needMs = [](uint32_t stop_ms) -> uint32_t {
    uint32_t base = stop_ms >= ShotDetector::MAX_SHOT_MS ? 5000 : 7000;
    uint32_t cap = stop_ms < 64535 ? 64535 - stop_ms : 0;
    return base < cap ? base : cap;
  };
  {
    // Review 2026-11-23 of the fix: a lift under way when the forced stop
    // was due stored yield_at_stop = -180 g and kept the dip in the curve.
    // The forced stop now waits for the hold: a lift there is a pour lift.
    ShotDetector d;
    uint32_t tt = 10000000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 800 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = s < 59.5 ? 0.6 * s : -180.0;
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("lift across the forced stop: trunc=%d stop_ms=%u at_stop=%d final=%d last=%d tail=%d\n",
           rr.truncated, rr.stop_ms, rr.yield_at_stop_mg, rr.yield_final_mg,
           rr.samples[rr.sample_count - 1].weight_mg,
           (int)(rr.samples[rr.sample_count - 1].t_ms - rr.stop_ms));
    assert(rr.valid && !rr.truncated);
    assert(rr.yield_at_stop_mg > 35000 && rr.yield_at_stop_mg == rr.yield_final_mg);
    assert(rr.samples[rr.sample_count - 1].weight_mg > 35000);   // no dip in the curve
    assert(rr.samples[rr.sample_count - 1].t_ms == rr.stop_ms);  // tail 0: a lift
    printf("a lift across the forced stop is a pour lift OK\n");
  }
  {
    // ...and a dip there that recovers within the hold only delays it.
    ShotDetector d;
    uint32_t tt = 10500000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    for (int i = 0; i < 800 && !fin; i++, tt += 100) {
      double s = i / 10.0;
      double w = (i >= 590 && i < 605) ? 0.0 : 0.6 * (s < 70 ? s : 70);   // a 1.5 s dip across 60 s
      fin = d.feed(tt, (int32_t)llround(w * 1000.0));
    }
    assert(fin);
    const ShotResult& rr = d.result();
    printf("bump at the forced stop: trunc=%d stop_ms=%u at_stop=%d\n",
           rr.truncated, rr.stop_ms, rr.yield_at_stop_mg);
    assert(rr.truncated && rr.stop_ms > 60000 && rr.stop_ms <= 61500);
    assert(rr.yield_at_stop_mg > 35000);
    printf("a bump at the forced stop only delays it OK\n");
  }
  {
    // Review: a forced stop after a sample gap lands past 60 s, the settle
    // window runs past t_ms = 65535, and the tail is cut. Not a lift.
    ShotDetector d;
    uint32_t tt = 11000000;
    bool fin = false;
    for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
    uint32_t start = tt;
    for (int i = 0; i < 900 && !fin; i++) {
      double s = (tt - start) / 1000.0;
      fin = d.feed(tt, (int32_t)llround(0.6 * s * 1000.0));
      tt += (s > 59.3 && s < 59.5) ? 1200 : 100;   // one 1.2 s gap just before 60 s
    }
    assert(fin);
    const ShotResult& rr = d.result();
    uint32_t tail = rr.samples[rr.sample_count - 1].t_ms - rr.stop_ms;
    printf("forced stop after a gap: stop_ms=%u tail=%u need=%u\n", rr.stop_ms, tail, needMs(rr.stop_ms));
    assert(rr.truncated && rr.stop_ms > 60000);
    assert(tail >= needMs(rr.stop_ms));
    printf("a settle cut at the t_ms ceiling is not a lift OK\n");
  }
  {
    // Review: the 2 s hold needs samples. A scale switched off with the cup
    // left the shot open; tick() ends it on the clock.
    for (bool lifted : {true, false}) {
      ShotDetector d;
      uint32_t tt = 12000000;
      for (int i = 0; i < 30; i++, tt += 100) d.feed(tt, 0);
      bool fin = false;
      for (int i = 0; i < 400 && !fin; i++, tt += 100) {
        double s = i / 10.0;
        if (s >= 23.0) break;                         // the scale goes quiet at 23 s
        fin = d.feed(tt, (int32_t)llround((s < 18.0 ? 2.0 * s : 36.0) * 1000.0));
        assert(!d.tick(tt));                          // never early
      }
      if (lifted) { d.feed(tt, -180000); assert(!d.tick(tt + 1900)); }
      assert(d.state() == ShotState::SETTLING);
      uint32_t now = tt + (lifted ? 2000 : 0);
      bool done = false;
      for (int k = 0; k < 100 && !done; k++, now += 100) done = d.tick(now);
      assert(done);
      const ShotResult& rr = d.result();
      uint32_t tail = rr.samples[rr.sample_count - 1].t_ms - rr.stop_ms;
      printf("scale quiet (%s): stop_ms=%u final=%d tail=%u\n",
             lifted ? "after a lift" : "no lift", rr.stop_ms, rr.yield_final_mg, tail);
      assert(rr.valid && rr.stop_ms >= 17500 && rr.stop_ms <= 18300);
      assert(rr.yield_final_mg > 35500 && rr.yield_final_mg < 36500);
      assert(tail < needMs(rr.stop_ms));             // reads as cut short, which it was
      assert(d.state() == ShotState::IDLE);
    }
    printf("a quiet scale cannot hold a shot open OK\n");
  }

  printf("all scenarios passed\n");
  return 0;
}
