#include "detector.h"
#include <string.h>
#include <limits.h>

// ---- WeighingDetector -------------------------------------------------------

int32_t WeighingDetector::computeFlow(uint32_t t, int32_t w) const {
  if (ring_count < 2) return 0;
  uint32_t target = t - FLOW_WINDOW_MS;
  const RingEntry* best = nullptr;
  for (size_t i = 0; i < ring_count; i++) {
    const RingEntry& e = ring[(ring_head + RING_N - 1 - i) % RING_N];
    best = &e;
    if ((int32_t)(e.t - target) <= 0) break;
  }
  uint32_t dt = t - best->t;
  if (dt == 0) return 0;
  return (int32_t)(((int64_t)(w - best->w) * 1000) / (int64_t)dt);
}

bool WeighingDetector::emit(uint32_t now) {
  if (emitted || n == 0 || now - stable_since < STABLE_HOLD_MS) return false;
  ev.started_at_ms = stable_since;
  ev.grams_mg = (int32_t)(sum_mg / n);
  ev.stable_ms = now - stable_since;
  emitted = true;
  return true;
}

bool WeighingDetector::feed(uint32_t t, int32_t w, bool shot_active) {
  int32_t flow = computeFlow(t, w);
  ring[ring_head] = { t, w };
  ring_head = (ring_head + 1) % RING_N;
  if (ring_count < RING_N) ring_count++;

  bool stable = !shot_active && w > MIN_WEIGHT_MG
                && flow < STABLE_FLOW_MGPS && flow > -STABLE_FLOW_MGPS;

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

void ShotDetector::appendSample(uint32_t t, int32_t w) {
  uint32_t rel = t - t0;
  if (rel > UINT16_MAX || buf_count >= MAX_SAMPLES) return;  // final weight still tracked via last_w
  // BLE can deliver a burst of notifications stamped in the same
  // millisecond; t_ms is the record's key, so the later reading wins.
  if (buf_count > 0 && buf[buf_count - 1].t_ms == (uint16_t)rel) {
    buf[buf_count - 1].weight_mg = w;
    return;
  }
  buf[buf_count++] = { (uint16_t)rel, w, 0, INT16_MIN };
}

void ShotDetector::beginPour(uint32_t crossing_t) {
  st = ShotState::POURING;
  t0 = crossing_t;
  buf_count = 0;
  running_max = 0;
  stop_cand_at = 0;
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
}

bool ShotDetector::finishShot() {
  res.yield_final_mg = last_w - baseline;
  res.settle_offset_mg = res.yield_final_mg - res.yield_at_stop_mg;
  res.fault = false;
  res.valid = res.yield_final_mg >= REJECT_MIN_MG;
  res.sample_count = buf_count;
  res.samples = buf;

  // Mean flow over the stable window: 5 g out .. 80 % of final, which skips
  // the first-drop lag and the tail-off.
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
  resetToIdle();
  return true;
}

void ShotDetector::finishFault() {
  res.fault = true;
  res.valid = false;
  // The last samples are the cup leaving the scale; the pre-lift maximum is
  // the honest yield (learned from real shot #2, 2026-08-27).
  res.yield_final_mg = running_max;
  res.sample_count = buf_count;
  res.samples = buf;
  resetToIdle();
}

void ShotDetector::resetToIdle() {
  st = ShotState::IDLE;
  ring_count = 0;   // a stale pre-pour history must not arm the next shot
  pour_cand_since = 0;
  parked_since = 0;
  stop_cand_at = 0;
}

bool ShotDetector::feed(uint32_t t, int32_t w) {
  flow_now = computeFlow(t, w);
  ringPush(t, w);
  int32_t prev_w = last_w;
  last_w = w;

  switch (st) {
    case ShotState::IDLE: {
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
      if (flow_now > POUR_FLOW_MGPS) {
        parked_since = 0;
        if (pour_cand_since == 0) pour_cand_since = t;
        if (t - pour_cand_since >= POUR_HOLD_MS) {
          // The current sample is already in the ring, so the replay in
          // beginPour picks it up too.
          beginPour(pour_cand_since);
        }
      } else {
        pour_cand_since = 0;
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
      }
      break;
    }

    case ShotState::POURING: {
      int32_t rel = w - baseline;
      appendSample(t, rel);
      if (rel > running_max) running_max = rel;
      if (flow_now > res.peak_flow_mgps) res.peak_flow_mgps = flow_now;

      if (running_max - rel > FAULT_DROP_MG) {  // cup removed
        finishFault();
        return true;
      }
      if (flow_now < STOP_FLOW_MGPS) {
        if (stop_cand_at == 0) { stop_cand_at = t; stop_cand_w = rel; }
        if (t - stop_cand_at >= STOP_HOLD_MS) {
          enterSettling(t, stop_cand_w, stop_cand_at, false);
        }
      } else {
        stop_cand_at = 0;
      }
      if (st == ShotState::POURING && t - t0 >= MAX_SHOT_MS) {
        // Not an espresso anymore; stop before uint16 t_ms can wrap.
        enterSettling(t, rel, t, true);
      }
      break;
    }

    case ShotState::SETTLING: {
      appendSample(t, w - baseline);
      if ((int32_t)(t - settle_until) >= 0) return finishShot();
      break;
    }
  }
  return false;
}
