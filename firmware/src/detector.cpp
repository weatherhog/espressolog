#include "detector.h"
#include <string.h>
#include <limits.h>

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
  buf[buf_count++] = { (uint16_t)rel, w, 0, INT16_MIN };
}

void ShotDetector::beginPour(uint32_t crossing_t) {
  st = ShotState::POURING;
  t0 = crossing_t;
  buf_count = 0;
  running_max = 0;
  stop_cand_at = 0;
  // Backdate: replay the pre-history ring from the flow crossing onward.
  for (size_t i = 0; i < ring_count; i++) {
    const RingEntry& e = ring[(ring_head + RING_N - ring_count + i) % RING_N];
    if ((int32_t)(e.t - t0) < 0) continue;
    appendSample(e.t, e.w);
    if (e.w > running_max) running_max = e.w;
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
  res.yield_final_mg = last_w;
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
  stop_cand_at = 0;
}

bool ShotDetector::feed(uint32_t t, int32_t w) {
  flow_now = computeFlow(t, w);
  ringPush(t, w);
  last_w = w;

  switch (st) {
    case ShotState::IDLE: {
      // ARMED: every sample in the last 1 s within ±0.5 g of zero (tared
      // with the cup on), and the ring actually spans that second.
      if (w < -ARM_BAND_MG || w > ARM_BAND_MG) break;
      bool spans = false, all_in_band = true;
      for (size_t i = 0; i < ring_count; i++) {
        const RingEntry& e = ring[(ring_head + RING_N - 1 - i) % RING_N];
        if (t - e.t >= ARM_HOLD_MS) { spans = true; break; }
        if (e.w < -ARM_BAND_MG || e.w > ARM_BAND_MG) { all_in_band = false; break; }
      }
      if (spans && all_in_band) st = ShotState::ARMED;
      break;
    }

    case ShotState::ARMED: {
      if (flow_now > POUR_FLOW_MGPS) {
        if (pour_cand_since == 0) pour_cand_since = t;
        if (t - pour_cand_since >= POUR_HOLD_MS) {
          // The current sample is already in the ring, so the replay in
          // beginPour picks it up too.
          beginPour(pour_cand_since);
        }
      } else {
        pour_cand_since = 0;
        // Disarm if weight leaves the zero band without a pour developing
        // (cup lifted, drift) — otherwise ARMED would wedge.
        if (w < -ARM_BAND_MG || w > ARM_BAND_MG) st = ShotState::IDLE;
      }
      break;
    }

    case ShotState::POURING: {
      appendSample(t, w);
      if (w > running_max) running_max = w;
      if (flow_now > res.peak_flow_mgps) res.peak_flow_mgps = flow_now;

      if (running_max - w > FAULT_DROP_MG) {  // cup removed
        finishFault();
        return true;
      }
      if (flow_now < STOP_FLOW_MGPS) {
        if (stop_cand_at == 0) { stop_cand_at = t; stop_cand_w = w; }
        if (t - stop_cand_at >= STOP_HOLD_MS) {
          enterSettling(t, stop_cand_w, stop_cand_at, false);
        }
      } else {
        stop_cand_at = 0;
      }
      if (st == ShotState::POURING && t - t0 >= MAX_SHOT_MS) {
        // Not an espresso anymore; stop before uint16 t_ms can wrap.
        enterSettling(t, w, t, true);
      }
      break;
    }

    case ShotState::SETTLING: {
      appendSample(t, w);
      if ((int32_t)(t - settle_until) >= 0) return finishShot();
      break;
    }
  }
  return false;
}
