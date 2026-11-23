#include "detector.h"
#include <string.h>
#include <limits.h>

// ---- WeighingDetector -------------------------------------------------------

bool WeighingDetector::emit(uint32_t now) {
  if (emitted || n == 0 || now - stable_since < STABLE_HOLD_MS) return false;
  ev.started_at_ms = stable_since;
  ev.grams_mg = (int32_t)(sum_mg / n);
  ev.stable_ms = now - stable_since;
  emitted = true;
  return true;
}

bool WeighingDetector::feed(uint32_t t, int32_t w, bool shot_active) {
  int32_t d = have_prev ? w - prev_w : 0;
  int32_t nmin = stable_since ? (w < span_min ? w : span_min) : w;
  int32_t nmax = stable_since ? (w > span_max ? w : span_max) : w;
  bool stable = have_prev && !shot_active && w > MIN_WEIGHT_MG
                && d <= STEP_MG && d >= -STEP_MG
                && nmax - nmin <= RANGE_MG;
  prev_w = w;
  have_prev = true;

  if (!stable) {
    // Episode over. If it lasted long enough and hasn't emitted (the usual
    // case: the dose was lifted off), this is the moment the event exists.
    bool fired = emit(t);
    stable_since = 0;
    sum_mg = 0;
    n = 0;
    emitted = false;
    return fired;
  }

  if (stable_since == 0) {
    stable_since = t;
    sum_mg = 0;
    n = 0;
    emitted = false;
  }
  span_min = nmin;
  span_max = nmax;
  sum_mg += w;
  n++;
  // Parked longer than the cap: emit now so the record exists even if the
  // scale sleeps mid-episode; the emitted flag stops a second event.
  if (!emitted && t - stable_since >= STABLE_CAP_MS) return emit(t);
  return false;
}

// ---- ShotDetector -----------------------------------------------------------

void ShotDetector::ringPush(uint32_t t, int32_t w) {
  ring[ring_head] = { t, w };
  ring_head = (ring_head + 1) % RING_N;
  if (ring_count < RING_N) ring_count++;
}

// Causal dw/dt: slope from the stored sample nearest (t − 700 ms) to now,
// over their *measured* Δt, so BLE notification jitter cannot bias it.
int32_t ShotDetector::computeFlow(uint32_t t, int32_t w) const {
  if (ring_count < 2) return 0;
  uint32_t target = t - FLOW_WINDOW_MS;
  const RingEntry* best = nullptr;
  for (size_t i = 0; i < ring_count; i++) {
    const RingEntry& e = ring[(ring_head + RING_N - 1 - i) % RING_N];
    best = &e;
    if ((int32_t)(e.t - target) <= 0) break;  // first entry at/before target, scanning backwards
  }
  uint32_t dt = t - best->t;
  if (dt == 0) return 0;
  return (int32_t)(((int64_t)(w - best->w) * 1000) / (int64_t)dt);
}

bool ShotDetector::ringAt(uint32_t target_t, uint32_t& t_out, int32_t& w_out) const {
  if (ring_count == 0) return false;
  const RingEntry* best = nullptr;
  for (size_t i = 0; i < ring_count; i++) {
    const RingEntry& e = ring[(ring_head + RING_N - 1 - i) % RING_N];
    best = &e;
    if ((int32_t)(e.t - target_t) <= 0) break;
  }
  t_out = best->t;
  w_out = best->w;
  return true;
}

// Pour onset, shared by IDLE and ARMED: flow > 0.3 g/s sustained 300 ms,
// with a step guard (a >5 g single-sample jump is a cup being moved, not a
// pour). From ARMED the baseline is the armed one; from IDLE — the cup was
// set down and the shot started before a second of stillness passed — the
// baseline is the weight ~1 s before the flow crossing (0a.5: arming is a
// nicety, never a precondition — a missed shot costs more than a rough zero).
bool ShotDetector::tryPourOnset(uint32_t t, int32_t w, int32_t prev_w, bool armed) {
  (void)w; (void)prev_w;
  // A cup being set down is a huge step that stays inside the 700 ms flow
  // window for 700 ms after the sample it landed on — reading as a "pour" of
  // hundreds of g/s. No onset until the step has aged out of the window.
  if (last_step_at != 0 && t - last_step_at < FLOW_WINDOW_MS + 200) {
    pour_cand_since = 0;
    return false;
  }
  if (flow_now > POUR_FLOW_MGPS) {
    if (pour_cand_since == 0) pour_cand_since = t;
    if (t - pour_cand_since >= POUR_HOLD_MS) {
      if (!armed) {
        // Baseline: the weight ~1 s before the crossing — but never from
        // before the cup landed, or the cup's own mass becomes "yield".
        uint32_t target = pour_cand_since - 1000;
        if (last_step_at != 0 && (int32_t)(target - last_step_at) < 0) target = last_step_at;
        uint32_t bt; int32_t bw;
        if (ringAt(target, bt, bw)) baseline = bw;
      }
      beginPour(pour_cand_since);   // current sample is already in the ring
      return true;
    }
  } else {
    pour_cand_since = 0;
  }
  return false;
}

void ShotDetector::appendSample(uint32_t t, int32_t w) {
  uint32_t rel = t - t0;
  if (rel > UINT16_MAX || buf_count >= MAX_SAMPLES) return;  // final weight still tracked via last_w
  // BLE can deliver a burst of notifications stamped in the same
  // millisecond; t_ms is the record's key, so the later reading wins.
  if (buf_count > 0 && buf[buf_count - 1].t_ms == (uint16_t)rel) {
    buf[buf_count - 1].weight_mg = w;
    return;
  }
  // Pulses since t=0, saturating rather than wrapping: uint16 holds ~27 L at
  // the datasheet's 2382 pulses/L against a few hundred per shot, so this
  // only ever trips if the tap is counting something that is not flow — and
  // a pegged ceiling says that out loud where a wrap would read plausible.
  uint32_t d = pulse_now - pulse_base;
  if (d > UINT16_MAX) d = UINT16_MAX;
  buf[buf_count++] = { (uint16_t)rel, w, (uint16_t)d, INT16_MIN };
}

void ShotDetector::beginPour(uint32_t crossing_t) {
  st = ShotState::POURING;
  t0 = crossing_t;
  // Set BEFORE the backdated replay below, so those samples store 0. Their
  // true counts are not recoverable — the ring keeps weights, not pulses —
  // and 0 is the one answer that cannot over-attribute flow to this shot.
  //
  // Note this runs at pour CONFIRMATION while t0 is the crossing POUR_HOLD_MS
  // earlier, so the window opens ~300 ms after t=0 rather than at it. Fixing
  // that properly means carrying pulse counts in the ring alongside weights;
  // it is deliberately not done, because the first-order gap is much larger:
  // the window opens at FIRST DRIP, not at pump-on, so every pre-infusion
  // pulse is outside it until 0c supplies the pump-start edge. Closing the
  // 300 ms while seconds of pre-infusion go uncounted would be polish on the
  // wrong axis. Both gaps close together when 0c lands.
  pulse_base = pulse_now;
  buf_count = 0;
  running_max = 0;
  lift_since = 0;
  disturbing = false;
  scan_cut = false;
  landed_now = false;
  placement_scan_end = 0;
  // Backdate: replay the pre-history ring from the flow crossing onward,
  // relative to the armed baseline.
  for (size_t i = 0; i < ring_count; i++) {
    const RingEntry& e = ring[(ring_head + RING_N - ring_count + i) % RING_N];
    if ((int32_t)(e.t - t0) < 0) continue;
    int32_t rel = e.w - baseline;
    appendSample(e.t, rel);
    if (rel > running_max) running_max = rel;
  }
  res = {};
  res.started_at_ms = t0;
  res.peak_flow_mgps = flow_now;
}

void ShotDetector::enterSettling(uint32_t t, int32_t w_at_stop, uint32_t stop_t, bool truncated) {
  st = ShotState::SETTLING;
  res.stop_ms = stop_t - t0;
  res.yield_at_stop_mg = w_at_stop;
  res.truncated = truncated;
  settle_until = t + SETTLE_MS;
  // Both callers enter with no drop pending: the flat stop is in the
  // not-dropped branch, and the forced stop waits for the hold to resolve.
  lift_since = 0;
}

// Called from loop() with millis() after the sample drain (0a.8). The
// detector otherwise moves only on a sample, so a scale that goes quiet --
// switched off with the cup, a BLE drop, a dead battery -- left the shot
// open, and main writes no spool while one is (invariant 6, reviews
// 2026-11-23). "Quiet" is SCALE_QUIET_MS without a sample: the scale
// manager's own STALE_MS, so a routine BLE gap (~240 ms) or a gap of a
// second or two is never taken for one. When it is quiet:
//  - SETTLING with a lift pending past its hold: a lift, as if one more
//    dropped sample had arrived. Pending but quiet for less than that
//    (a reconnect's injected 0 g, then the real reading 2 s later): wait.
//  - SETTLING past the settle window: finish on the last weight.
//  - POURING, once MAX_SHOT_MS + SETTLE_MS has passed since t0 -- when the
//    detector would have force-stopped and settled by now, so a reconnect
//    mid-pour cannot split one shot in two. Under 5 g it is a reject, as
//    any sub-5 g record. Anything else is a FAULT, "yield scale lost
//    mid-pour": spooled (invariant 6), excluded at ingest, and it keeps its
//    dose -- the coffee was used. NOT A GUESS AT WHAT HAPPENED: the last
//    second or two before the scale vanished cannot tell jitter, drips, a
//    lift and a reconnect's 0 g apart, and four review rounds of rules that
//    tried (2026-11-23: a pending drop as a lift, a flat tail as a stop) each
//    misfiled the case next to the one they fixed. A good shot among these
//    is un-excluded by hand. A short pending drop is trimmed off the curve
//    first (feed() takes any drop that lasts LIFT_HOLD_MS, so a pending one
//    is shorter). fault was otherwise unused from 0a.4.
// tick() is never gated: it decides at once, and main defers only the
// spool write while the pump runs (invariant 2). Gating tick() kept the
// shot open, so a flush or the next shot fed into it (review of c85dcf8).
// Each of these ends the curve before a settle window could close, so
// v_shot_end reads the shot as cut short ("ended_by_lift" means exactly
// that) -- which it was, by the scale rather than a hand.
bool ShotDetector::tick(uint32_t now) {
  if (st != ShotState::SETTLING && st != ShotState::POURING) return false;
  if ((int32_t)(now - last_t) < (int32_t)SCALE_QUIET_MS) return false;
  if (st == ShotState::POURING) {
    if ((int32_t)(now - t0) < (int32_t)(MAX_SHOT_MS + SETTLE_MS)) return false;
    if (lift_since != 0) buf_count = lift_buf;     // a short drop: trimmed off the curve
    if (running_max < REJECT_MIN_MG) return finalizeLifted();   // a reject
    finalizeLifted();
    res.fault = true;    // yield scale lost mid-pour: kept, never a shot
    res.valid = false;
    return true;
  }
  if (lift_since != 0) {
    if ((int32_t)(now - lift_since) < (int32_t)LIFT_HOLD_MS) return false;
    buf_count = lift_buf;
    return finishLiftedSettling();
  }
  if ((int32_t)(now - settle_until) >= 0) return finishShot();
  return false;
}

// Mean flow over the stable window: 5 g out .. 80 % of final, which skips
// the first-drop lag and the tail-off. Reads res.yield_final_mg and buf.
void ShotDetector::computeFlowWindow() {
  int32_t w80 = (int32_t)(((int64_t)res.yield_final_mg * 8) / 10);
  const sample_t *s1 = nullptr, *s2 = nullptr;
  for (uint16_t i = 0; i < buf_count; i++) {
    if (!s1 && buf[i].weight_mg >= REJECT_MIN_MG) s1 = &buf[i];
    if (!s2 && buf[i].weight_mg >= w80) { s2 = &buf[i]; break; }
  }
  if (s1 && s2 && s2->t_ms > s1->t_ms) {
    res.flow_win_start_ms = s1->t_ms;
    res.flow_win_end_ms = s2->t_ms;
    res.mean_flow_mgps = (int32_t)(((int64_t)(s2->weight_mg - s1->weight_mg) * 1000)
                                   / (s2->t_ms - s1->t_ms));
  }
}

// Normal completion: the cup stayed on through settling, so last_w is the
// settled weight (drips included) — the most accurate yield. Unless
// something landed on the scale while settling (0a.7): then the peak frozen
// at that moment is the yield, and the drips after it are lost.
bool ShotDetector::finishShot() {
  res.yield_final_mg = disturbing ? running_max : last_w - baseline;
  res.settle_offset_mg = res.yield_final_mg - res.yield_at_stop_mg;
  res.fault = false;
  res.valid = res.yield_final_mg >= REJECT_MIN_MG;
  res.sample_count = buf_count;
  res.samples = buf;
  res.placement = risesLikeAPlacement();
  computeFlowWindow();
  resetToIdle();
  return true;
}

// Did the curve ever climb PLACEMENT_RISE_MG ABOVE ITS EARLIER PEAK within
// PLACEMENT_RISE_MS? For each sample j, the reference is the highest weight
// seen before j's 1 s window opened — not the lowest weight inside it. A cup
// landing climbs far above anything before it; a real shot recovering from
// a dip (a lift, a bump, the single 0 g sample a cup-scale reconnect
// injects) only climbs back to where it was. The first version compared
// against the window's minimum and excluded good shots for exactly that
// (review, 2026-11-15). One pass: `i` trails `j` to the window's start, and
// `peak_before` is the maximum of every sample before `i`.
//
// A GAP IN THE SAMPLES (a scale reconnect: seconds of nothing, then often
// the injected 0 g) hides how fast the curve rose across it, so a real pour
// measured against its pre-gap peak would read as a placement once the gap
// is long enough (8 s at 2.5 g/s — review 2026-11-15). After a gap longer
// than PLACEMENT_RISE_MS, the first PLACEMENT_RISE_MS of samples only feed
// the peak and are never judged: a cup landing during or just after a scale
// outage goes unflagged, which keeps a record rather than excluding one.
//
// 0a.7: an object that lands WHILE SETTLING is not a placement -- the shot
// before it is real and its yield is protected -- so the scan stops at the
// sample where it landed (placement_scan_end). A cup put back on the scale
// (shot 180) rises during the pour and is unaffected.
bool ShotDetector::risesLikeAPlacement() const {
  uint16_t n = scan_cut ? placement_scan_end : buf_count;
  if (n == 0) return false;
  uint16_t i = 0;
  int32_t peak_before = buf[0].weight_mg;   // no sample before the first window
  bool seeding = false;
  uint32_t seed_from = 0;
  for (uint16_t j = 0; j < n; j++) {
    if (j > 0 && (uint32_t)(buf[j].t_ms - buf[j - 1].t_ms) > PLACEMENT_RISE_MS) {
      seeding = true;
      seed_from = buf[j].t_ms;
    }
    if (seeding) {
      if ((uint32_t)(buf[j].t_ms - seed_from) <= PLACEMENT_RISE_MS) {
        if (buf[j].weight_mg > peak_before) peak_before = buf[j].weight_mg;
        continue;
      }
      seeding = false;
    }
    while (i < j && (uint32_t)(buf[j].t_ms - buf[i].t_ms) > PLACEMENT_RISE_MS) {
      if (buf[i].weight_mg > peak_before) peak_before = buf[i].weight_mg;
      i++;
    }
    if (buf[j].weight_mg - peak_before > PLACEMENT_RISE_MG) return true;
  }
  return false;
}

// The cup was lifted DURING THE POUR (sustained drop below the peak).
// Lifting after a pour is normal — not a fault — so this finalizes a real
// shot at the peak weight. Only a lift with almost nothing in the cup (an
// aborted/channelled pour) is a reject. A lift WHILE SETTLING is
// finishLiftedSettling() (0a.8). buf_count is expected to be trimmed to the pre-lift
// samples by the caller.
bool ShotDetector::finalizeLifted() {
  res.yield_final_mg = running_max;
  res.yield_at_stop_mg = running_max;
  res.settle_offset_mg = 0;   // no settle measured — cup left early
  res.stop_ms = buf_count > 0 ? buf[buf_count - 1].t_ms : 0;
  res.fault = false;
  res.valid = running_max >= REJECT_MIN_MG;
  res.sample_count = buf_count;
  res.samples = buf;
  res.placement = risesLikeAPlacement();
  computeFlowWindow();
  resetToIdle();
  return true;
}

// The cup was lifted WHILE SETTLING (0a.8): the drop held LIFT_HOLD_MS, as
// during the pour, and buf is trimmed to the samples before it. The stop was
// already recognised by enterSettling() -- backdated on the flat path, the
// forced stop at 60 s on the truncated one -- so keep it, and its
// yield_at_stop. The lift only ends settling early: the yield is the peak
// (frozen if something landed, 0a.7), and settle_offset is the drips the
// peak followed before the lift. That is NOT a lower bound on a full settle:
// the peak follows every reading under DRIP_STEP_MG above it, so upward
// jitter (+-0.1 g) and the first sub-0.5 g of a grip ratchet it up, where
// finishShot() reads the last weight -- a lift's offset can read a few
// tenths high, and it covers only the time settled before the lift. After a
// forced stop (truncated) the pour may still be running and every reading
// joins the peak, objects included, so there is no settle to measure:
// settle_offset stays 0 and final - at_stop does not hold on that path.
// Nothing in the record flags a lift; the samples identify it exactly
// (server/migrations/016_settle_end.sql). Up to 0a.7 this went through
// finalizeLifted() on the first dropped sample, with no hold, and overwrote
// stop_ms with the last sample before the lift -- up to STOP_WINDOW_MS +
// SETTLE_MS late (shot 212, 2026-11-23: 30.5 s stored for a stop at
// ~25.3 s) -- and zeroed settle_offset.
bool ShotDetector::finishLiftedSettling() {
  res.yield_final_mg = running_max;
  res.settle_offset_mg = res.truncated ? 0 : res.yield_final_mg - res.yield_at_stop_mg;
  res.fault = false;
  res.valid = running_max >= REJECT_MIN_MG;
  res.sample_count = buf_count;
  res.samples = buf;
  res.placement = risesLikeAPlacement();
  computeFlowWindow();
  resetToIdle();
  return true;
}

// Something set on the scale once the shot has substance (0a.7). ONSET needs
// all three, so a real pour cannot trip it:
//  - the sample lands more than PLACEMENT_STEP_MG ABOVE THE PEAK so far: a
//    weight recovering from a dip (a bump, the 0 g sample a cup-scale
//    reconnect injects) only climbs back to about where it was;
//  - it rose FASTER than MAX_POUR_MGPS since the previous sample: a fast
//    pour across a short gap (7 g/s over 800 ms = 5.6 g) is a pour;
//  - not within PLACEMENT_RISE_MS after a gap longer than that (a
//    reconnect: the injected 0 g lands right after it and the real climb a
//    sample later -- the exception risesLikeAPlacement() makes).
// Before substance the POURING tare rule owns big steps.
bool ShotDetector::jumpsOntoScale(uint32_t t, int32_t w, int32_t prev_w, int32_t rel) const {
  uint32_t dt = (uint32_t)(t - prev_t);
  int32_t pour_allowance = (int32_t)(((int64_t)MAX_POUR_MGPS * dt) / 1000);
  return running_max >= REJECT_MIN_MG
      && rel - running_max > PLACEMENT_STEP_MG
      && w - prev_w > pour_allowance
      && (gap_at == 0 || (uint32_t)(t - gap_at) > PLACEMENT_RISE_MS);
}

void ShotDetector::resetToIdle() {
  st = ShotState::IDLE;
  ring_count = 0;   // a stale pre-pour history must not arm the next shot
  pour_cand_since = 0;
  parked_since = 0;
  lift_since = 0;
  disturbing = false;
  scan_cut = false;
  landed_now = false;
  placement_scan_end = 0;
}

bool ShotDetector::feed(uint32_t t, int32_t w, uint32_t inlet_pulses_total) {
  pulse_now = inlet_pulses_total;
  if (last_t != 0 && (uint32_t)(t - last_t) > PLACEMENT_RISE_MS) gap_at = t;
  prev_t = last_t;
  last_t = t;
  flow_now = computeFlow(t, w);
  ringPush(t, w);
  int32_t prev_w = last_w;
  last_w = w;
  if (w - prev_w > PLACEMENT_STEP_MG || prev_w - w > PLACEMENT_STEP_MG) last_step_at = t;

  switch (st) {
    case ShotState::IDLE: {
      // A pour can begin straight from IDLE (0a.5): if the cup went down
      // and the switch was hit before a second of stillness, the shot is
      // still detected — with the pre-rise weight as its baseline.
      if (tryPourOnset(t, w, prev_w, false)) break;

      // ARMED (0a.3): stable at ANY value for 1 s. The mean over that
      // second becomes the baseline the curve is measured against — a tare
      // is just a baseline the scale applied for you.
      bool spans = false, flat = true;
      int64_t sum = 0;
      uint32_t n = 0;
      for (size_t i = 0; i < ring_count; i++) {
        const RingEntry& e = ring[(ring_head + RING_N - 1 - i) % RING_N];
        if (t - e.t >= ARM_HOLD_MS) { spans = true; break; }
        if (e.w > w + ARM_BAND_MG || e.w < w - ARM_BAND_MG) { flat = false; break; }
        sum += e.w;
        n++;
      }
      if (spans && flat && n > 0) {
        baseline = (int32_t)(sum / n);
        st = ShotState::ARMED;
      }
      break;
    }

    case ShotState::ARMED: {
      // A step in either direction within one sample is a cup being moved,
      // not espresso — go IDLE and re-arm on the new stable value ~1 s
      // later (that re-arm IS the un-tared cup-swap workflow working).
      if (w - prev_w > PLACEMENT_STEP_MG || prev_w - w > PLACEMENT_STEP_MG) {
        st = ShotState::IDLE;
        pour_cand_since = 0;
        parked_since = 0;
        break;
      }
      if (tryPourOnset(t, w, prev_w, true)) { parked_since = 0; break; }
      if (flow_now > POUR_FLOW_MGPS) { parked_since = 0; break; }  // candidate building

      // A slow positive creep is first drips — stay armed on the same
      // baseline. A mass sitting flat AWAY from the baseline is the new
      // reality (drift, gentle re-place): adopt it as the baseline.
      int32_t dev = w - baseline;
      if ((dev > ARM_BAND_MG || dev < -ARM_BAND_MG)
          && flow_now < PARKED_FLOW_MGPS && flow_now > -PARKED_FLOW_MGPS) {
        if (parked_since == 0) parked_since = t;
        if (t - parked_since >= PARKED_HOLD_MS) {
          baseline = w;
          parked_since = 0;
        }
      } else {
        parked_since = 0;
      }
      break;
    }

    case ShotState::POURING: {
      // A single-sample step while almost nothing is out yet is the user
      // taring (or nudging) the cup during pre-infusion — the real workflow:
      // cup preheated with water, emptied and set down as the machine is
      // already running, tared as the first drips fall. Not a lift: shift
      // the baseline by the step so the curve stays continuous. (A genuine
      // early abort still ends via the flat-abort rule.)
      if (running_max < REJECT_MIN_MG
          && (w - prev_w > PLACEMENT_STEP_MG || prev_w - w > PLACEMENT_STEP_MG)) {
        baseline += (w - prev_w);
      }
      int32_t rel = w - baseline;
      appendSample(t, rel);
      // 0a.7: during the pour a jump onto the scale is only FLAGGED. The
      // true weight is moving, so taking the object out reliably needs more
      // than the curve gives (review 2026-11-21: an offset carried under a
      // pour grew with a wobbling hand and ended shots early); the yield
      // logic here is exactly 0a.6's, and the flag tells the reader.
      if (jumpsOntoScale(t, w, prev_w, rel)) res.disturbed = true;
      if (rel > running_max) running_max = rel;
      if (flow_now > res.peak_flow_mgps) res.peak_flow_mgps = flow_now;

      if (running_max - rel > LIFT_DROP_MG) {
        // Dropped well below the peak. A brief dip is a bump; a sustained
        // one for LIFT_HOLD_MS is the cup being taken — finalize the shot at
        // the peak (lifting after a pour is normal, not a fault).
        if (lift_since == 0) {
          lift_since = t;
          lift_buf = buf_count > 0 ? buf_count - 1 : 0;  // exclude the dropping sample
        }
        if (t - lift_since >= LIFT_HOLD_MS) {
          buf_count = lift_buf;   // trim the descending lift transition
          return finalizeLifted();
        }
      } else {
        lift_since = 0;
        uint32_t t_then; int32_t w_then;
        if (running_max >= REJECT_MIN_MG) {
          // Stop: weight flat (|Δ| < 0.3 g) over the last 2 s, once the shot
          // has substance. Jitter-proof where "flow < 0.1 g/s" was not.
          if (ringAt(t - STOP_WINDOW_MS, t_then, w_then) && t - t_then >= STOP_WINDOW_MS) {
            int32_t delta = w - w_then;
            if (delta < STOP_DELTA_MG && delta > -STOP_DELTA_MG) {
              // The plateau began at t_then; that's the stop.
              enterSettling(t, w_then - baseline, t_then, false);
            }
          }
        } else if (ringAt(t - ABORT_FLAT_MS, t_then, w_then) && t - t_then >= ABORT_FLAT_MS) {
          // Never reached 5 g and flat for a long while: a false trigger.
          int32_t delta = w - w_then;
          if (delta < STOP_DELTA_MG && delta > -STOP_DELTA_MG) {
            res.yield_final_mg = running_max;
            res.fault = false;
            res.valid = false;
            res.sample_count = buf_count;
            res.samples = buf;
            resetToIdle();
            return true;
          }
        }
      }
      if (st == ShotState::POURING && t - t0 >= MAX_SHOT_MS && lift_since == 0) {
        // Not an espresso anymore; stop before uint16 t_ms can wrap. NOT
        // while a drop is pending (0a.8, review 2026-11-23): the forced
        // stop would take its weight mid-dip (a stored yield_at_stop of
        // -180 g) and leave the dip in the curve. The hold resolves within
        // LIFT_HOLD_MS -- a lift finalizes as one, a bump lets the forced
        // stop through on the next sample -- well before t_ms can wrap.
        enterSettling(t, rel, t, true);
      }
      break;
    }

    case ShotState::SETTLING: {
      int32_t rel = w - baseline;
      if (res.truncated) {
        // Forced stop at MAX_SHOT_MS: the pour may still be running, so the
        // peak is not the true weight. Flag only, as during the pour (0a.7).
        if (jumpsOntoScale(t, w, prev_w, rel)) res.disturbed = true;
        if (rel > running_max) running_max = rel;   // late drips
      } else {
        // 0a.7: the stop is confirmed (2 s flat), so the true weight is the
        // peak plus drips, and drips creep up in tenths of a gram. The peak
        // follows a reading only when it is LESS than DRIP_STEP_MG ABOVE THE
        // PEAK -- judged against the peak, not the previous reading, so
        // something whose first reading lands 0.5 g or more above the peak
        // never joins it however still it sits (review 2026-11-21: a step
        // rule let any flat sample after an object or a grip take it in).
        // Something creeping on in steps under 0.5 g IS followed, like drips
        // (detector.h, known limits). Anything that ends up more than
        // PLACEMENT_STEP_MG above the peak is an object, however many samples
        // it took to land (4 g + 4 g passed a one-sample rule). That FREEZES THE PEAK FOR THE REST OF
        // SETTLING -- nothing after it, wobble, press and release or a
        // remainder, can raise it -- and a lift or the end of settling reads
        // it. Never cleared on purpose (a draft that unfroze it near the peak
        // let a pressing hand pass for drips). The cost is the drips after
        // the landing, a few tenths of a gram. Shot 205: +0.6, +7.8, +3.2 g,
        // then the lift. No rate test here: nothing is pouring.
        if (!disturbing && rel - running_max > PLACEMENT_STEP_MG) {
          disturbing = true;
          res.disturbed = true;
          landed_now = true;
        }
        if (!disturbing && rel > running_max && rel - running_max < DRIP_STEP_MG)
          running_max = rel;
      }
      // The pour already ended (stop confirmed). A drop well below the peak
      // that holds LIFT_HOLD_MS is the cup being taken: finish at the peak,
      // keeping the stop (0a.8). A shorter dip -- a bump, the single 0 g
      // sample a cup-scale reconnect injects -- is not a lift, and settling
      // never ends on a dropped reading: it waits for the weight to come
      // back (then finishShot() reads it) or for the hold. Up to 0a.7 the
      // first dropped sample ended the shot.
      if (running_max - rel > LIFT_DROP_MG) {
        if (lift_since == 0) {
          lift_since = t;
          // This sample is not appended yet -- unless it shares the previous
          // slot's millisecond, when appendSample() will merge it into that
          // slot, which must then go too.
          lift_buf = buf_count;
          if (buf_count > 0 && buf[buf_count - 1].t_ms == (uint16_t)(t - t0)) lift_buf--;
        }
        if (t - lift_since >= LIFT_HOLD_MS) {
          buf_count = lift_buf;            // trim the dip off the record
          return finishLiftedSettling();
        }
        appendSample(t, rel);
        break;
      }
      lift_since = 0;
      appendSample(t, rel);
      if (landed_now) {
        // The placement scan stops before the sample where it landed. Taken
        // AFTER appendSample(), which merges a same-millisecond sample into
        // the previous slot -- set before it, a burst put the landing inside
        // the scan and excluded the shot (review 2026-11-21).
        landed_now = false;
        scan_cut = true;
        placement_scan_end = buf_count > 0 ? buf_count - 1 : 0;
      }
      if ((int32_t)(t - settle_until) >= 0) return finishShot();
      break;
    }
  }
  return false;
}
