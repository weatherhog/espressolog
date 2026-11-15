#pragma once
#include <stdint.h>
#include "display.h"
#include "machine_io.h"
#include "pulse_log.h"

// What the machine itself contributed to a shot record (spool format v2).
//
// Kept out of ShotResult on purpose: the detector is pure scale logic and
// must stay host-testable without a display bus. Kept out of main.cpp for
// the same reason — the first version of this lived there, had no tests,
// and shipped two bugs that a green suite could not see.
// Which lever direction started the pump for a shot, in the spool's own
// encoding (format v3). Kept separate from SwitchBank::Line so the wire
// format does not move if the poller's enum is ever reordered.
enum PumpLine : uint8_t { PUMP_NONE = 0, PUMP_1CUP = 1, PUMP_2CUP = 2, PUMP_WATER = 3 };
static constexpr uint32_t PUMP_LEAD_NONE = 0xFFFFFFFF;

// v4: the flowmeter pulses between the lever press and first drip.
static constexpr uint16_t PRE_PULSES_MAX    = 256;
static constexpr uint16_t PRE_PULSES_NONE   = 0xFFFF;   // pre_pulse_total: not captured
static constexpr uint8_t  PRE_PULSES_COMPLETE = 0x01;   // pre_pulse_flags: no pulse lost

// The poller's line -> the spool's encoding. Steam never runs the pump, and
// an unknown line is not guessed at: both are PUMP_NONE, so a line added to
// SwitchBank later cannot be silently recorded as hot water.
inline uint8_t pumpLineForSwitch(uint8_t sw_line) {
  switch (sw_line) {
    case SwitchBank::ONE_CUP:   return PUMP_1CUP;
    case SwitchBank::TWO_CUP:   return PUMP_2CUP;
    case SwitchBank::HOT_WATER: return PUMP_WATER;
    default:                    return PUMP_NONE;
  }
}

struct MachineContext {
  int16_t  boiler_temp_start_dc = INT16_MIN;   // INT16_MIN = machine did not say
  uint16_t machine_timer_dl     = 0;           // 0 = no timer seen
  bool     setpoint_touched     = false;
  // v3 (0c): the lever press that started the pump for this shot, and how
  // long before the detector's t=0 (first drip) it was. Both from raw
  // millis() timestamps; PUMP_NONE / PUMP_LEAD_NONE = not known.
  uint8_t  pump_line            = PUMP_NONE;
  uint32_t pump_lead_ms         = PUMP_LEAD_NONE;
  // v4: every counted flowmeter pulse from that press to t=0, as ms after
  // the press — the pre-infusion phase the per-sample inlet_pulses window
  // (opened at first drip) never sees. Raw: analysis derives pump-on, pause
  // and resume from the gaps. pre_pulse_total is how many lay in the window
  // (PRE_PULSES_NONE = not captured: no press, flow tap off); at most
  // PRE_PULSES_MAX are kept in pre_pulse_ms, the first ones.
  uint16_t pre_pulse_total      = PRE_PULSES_NONE;
  uint8_t  pre_pulse_flags      = 0;
  uint16_t pre_pulse_count      = 0;
  uint16_t pre_pulse_ms[PRE_PULSES_MAX] = {};
};

// Gathers MachineContext across a shot. None of it is available at the
// moment the shot completes, so it has to be accumulated:
//
//   boiler temperature  readable only BEFORE the pump starts. The machine
//                       takes the display over for its timer the moment the
//                       pump runs, so by the time the shot ends the last
//                       temperature is twenty seconds old. Tracked
//                       continuously, snapshotted backwards at pour start.
//   machine timer       counts up during the brew and is gone afterwards.
//   setpoint touched    PrG is a transient banner — two frames in a capture
//                       where the adjustment ran seven seconds. Latched.
//
// Pure logic: no Arduino, no I/O, no clock of its own.
class MachineContextTracker {
public:
  // How old the pre-shot temperature may be and still be called this
  // shot's. Generous, because the gap between pump-on and the detector
  // reaching POURING is first-drip lag, which is puck-dependent.
  static constexpr uint32_t BOILER_MAX_AGE_US = 60000000;   // 60 s

  // Call on every fresh display reading. `touched_latch` is
  // DisplayBus::setpointTouched(); the caller clears it at pour start.
  void onReading(const DisplayBus::Reading& r, bool touched_latch) {
    // main.cpp calls this every loop with the CURRENT reading, so the same
    // frame arrives many times; `at_us` is what says a new frame decoded.
    if (!have_frame || r.at_us != last_frame_us) {
      prev_frame_mode = have_frame ? last_frame_mode : DisplayBus::Mode::NONE;
      last_frame_mode = r.mode;
      last_frame_us = r.at_us;
      have_frame = true;
    }
    const bool timer_confirmed = r.mode == DisplayBus::Mode::TIMER
                              && prev_frame_mode == DisplayBus::Mode::TIMER;
    if (r.mode == DisplayBus::Mode::TEMPERATURE) {
      // Only Mode::TEMPERATURE is the boiler. SETPOINT is the number being
      // dialled in, TARGET is steam's 165 or the 95 it is cooling back to,
      // BLANK and TEXT are not temperatures at all.
      last_boiler_dc = (int16_t)(r.temp_c * 10);
      last_boiler_us = r.at_us;
      have_boiler = true;
    } else if (r.mode == DisplayBus::Mode::SETPOINT || r.mode == DisplayBus::Mode::TARGET) {
      // AND THEY INVALIDATE the last one. Several non-boiler values decode
      // as TEMPERATURE: a blink's first cycle (it is recognised at its
      // second onset), steam's steady 165 for as long as steam holds
      // temperature (minutes, live), and
      // a held-button ramp. A later SETPOINT or TARGET frame marks them
      // untrustworthy, so a pour after it records "the machine did not say".
      // THIS SHORTENS THE EXPOSURE; IT DOES NOT CLOSE IT: a pour that starts
      // inside one of those windows, before the next blink frame, still
      // stores the value (CLAUDE.md, display-bus entry).
      have_boiler = false;
    }
    // A TIMER reading counts only when the frame before it was TIMER too.
    // 2026-11-13: one idle frame decoded as TIMER with text " 95" between two
    // temperature frames. Taken alone it reset the running maximum and set
    // 9.5 s — and after a shot, while SETTLING, that would overwrite the
    // real duration in the spool header. A real timer spans hundreds of
    // frames (one per ~41 ms, a digit per 100 ms), so requiring two in a row
    // costs at most the first frame of a brew.
    if (timer_confirmed) {
      // A reading LOWER than the running maximum means the machine's timer
      // restarted, i.e. a new brew or flush began. Without this the maximum
      // is sticky and a short shot inherits a long one's duration — which
      // is exactly what the first version of this did.
      if (r.timer_dl < mc.machine_timer_dl) mc.machine_timer_dl = 0;
      if (r.timer_dl > mc.machine_timer_dl) mc.machine_timer_dl = r.timer_dl;
    }
    if (touched_latch) mc.setpoint_touched = true;
  }

  // Call when the detector reaches POURING. Takes the temperature from
  // before the pump started, which is the only time it is readable, and
  // keeps whatever the timer has reached for this brew.
  // There is deliberately no onShotWritten(): a reset there was tried and
  // no test could be made to fail without it. onPourStart() clears
  // everything, and the timer restart detection above handles a flush
  // between shots. Unexercised defensive code is where the last two bugs
  // in this file hid.
  void onPourStart(uint32_t now_us) {
    uint16_t timer_carry = mc.machine_timer_dl;
    mc = MachineContext{};
    mc.machine_timer_dl = timer_carry;
    if (have_boiler && (uint32_t)(now_us - last_boiler_us) < BOILER_MAX_AGE_US) {
      mc.boiler_temp_start_dc = last_boiler_dc;
    }
  }

  // A lever press that runs the pump (1Cup, 2Cup, hot water — never steam),
  // backdated to the press edge, from a witnessed (non-synthetic) event.
  // Each press is remembered whether it STARTED the pump or stopped it: the
  // board latches, so the poller cannot tell the two apart. resolvePump()
  // sorts that out from the shot's own timing.
  void onPumpPress(uint8_t line, uint32_t pressed_at_ms) {
    if (line == PUMP_NONE) return;
    presses[press_head] = { line, pressed_at_ms };
    press_head = (uint8_t)((press_head + 1) % N_PRESSES);
    if (press_count < N_PRESSES) press_count++;
  }

  // Call AT POUR START, right after onPourStart(), with the detector's t=0
  // (millis(), the backdated first-drip crossing — known from the moment
  // the detector enters POURING). Attributes the LATEST remembered press at
  // or before t=0 and no more than PUMP_LEAD_MAX_MS before it. Resolving
  // here rather than at completion means presses made during the shot (the
  // stop press, a flush started while it settles) cannot push the start
  // press out of the ring (review, 2026-11-14).
  //
  // KNOWN LIMITS. (1) If this shot's start press was never confirmed (the
  // poller's two-poll debounce can miss a very fast press), an earlier
  // flush's stop press within the window is attributed instead — wrong
  // lever possible, lead too long. Nothing here can see that. (2) A
  // stop-and-restart before first drip (a manual pre-infusion) attributes
  // the restart, so the lead leaves out the first pump run. (3) t=0 is the
  // scale sample's ARRIVAL time, so the lead includes the BLE delay (~10 Hz
  // plus link latency, unmeasured, roughly 100-200 ms).
  static constexpr uint32_t PUMP_LEAD_MAX_MS = 60000;
  void resolvePump(uint32_t t0_ms) {
    mc.pump_line = PUMP_NONE;
    mc.pump_lead_ms = PUMP_LEAD_NONE;
    bool found = false;
    uint32_t best_lead = 0;
    for (uint8_t i = 0; i < press_count; i++) {
      const Press& p = presses[i];
      uint32_t lead = t0_ms - p.at_ms;
      if ((int32_t)lead < 0 || lead > PUMP_LEAD_MAX_MS) continue;  // after t=0, or too old
      if (!found || lead < best_lead) {
        found = true;
        best_lead = lead;
        mc.pump_line = p.line;
        mc.pump_lead_ms = lead;
      }
    }
  }

  // Call when the shot completes. If pour start found no press, look again:
  // the poller drains after the scale samples in loop(), so a press whose
  // first drip came within ~300 ms (or during a slow loop) is confirmed —
  // backdated to its edge, before t=0 — only after the POURING transition.
  // A press already found at pour start is kept; this only fills a gap.
  // (Resolving solely at pour start lost that case: review, 2026-11-15.)
  void resolvePumpIfUnknown(uint32_t t0_ms) {
    if (mc.pump_line == PUMP_NONE) resolvePump(t0_ms);
  }

  // Copy the pulses between this shot's press and t=0 out of the log. Call
  // at pour start after resolvePump() — the log holds ~22 s, so this is when
  // they are surely still there — and again at completion only if pour start
  // found no press (resolvePumpIfUnknown()), when they may already be gone:
  // then the COMPLETE flag says so. Does nothing without a press, so the
  // record says "not captured" rather than "no pulses".
  void capturePrePulses(const PulseLog& log, uint32_t t0_ms) {
    if (mc.pump_line == PUMP_NONE || mc.pump_lead_ms == PUMP_LEAD_NONE) return;
    bool complete = false;
    uint16_t copied = 0;
    uint16_t total = log.extract(t0_ms - mc.pump_lead_ms, t0_ms, mc.pre_pulse_ms,
                                 PRE_PULSES_MAX, &copied, &complete);
    mc.pre_pulse_total = total >= PRE_PULSES_NONE ? (uint16_t)(PRE_PULSES_NONE - 1) : total;
    mc.pre_pulse_count = copied;
    mc.pre_pulse_flags = complete ? PRE_PULSES_COMPLETE : 0;
  }
  bool prePulsesCaptured() const { return mc.pre_pulse_total != PRE_PULSES_NONE; }

  // Switch sensing turned off or back on: remembered presses describe a
  // tap state that may no longer hold, so forget them.
  void clearPresses() { press_head = press_count = 0; }

  const MachineContext& context() const { return mc; }

private:
  struct Press { uint8_t line; uint32_t at_ms; };
  static constexpr uint8_t N_PRESSES = 4;
  Press   presses[N_PRESSES] = {};
  uint8_t press_head = 0;
  uint8_t press_count = 0;

  MachineContext mc;
  int16_t  last_boiler_dc = INT16_MIN;
  uint32_t last_boiler_us = 0;
  bool     have_boiler = false;
  DisplayBus::Mode last_frame_mode = DisplayBus::Mode::NONE;
  DisplayBus::Mode prev_frame_mode = DisplayBus::Mode::NONE;
  uint32_t last_frame_us = 0;
  bool     have_frame = false;
};
