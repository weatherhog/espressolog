#pragma once
#include <stdint.h>
#include "display.h"

// What the machine itself contributed to a shot record (spool format v2).
//
// Kept out of ShotResult on purpose: the detector is pure scale logic and
// must stay host-testable without a display bus. Kept out of main.cpp for
// the same reason — the first version of this lived there, had no tests,
// and shipped two bugs that a green suite could not see.
struct MachineContext {
  int16_t  boiler_temp_start_dc = INT16_MIN;   // INT16_MIN = machine did not say
  uint16_t machine_timer_dl     = 0;           // 0 = no timer seen
  bool     setpoint_touched     = false;
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
    if (r.mode == DisplayBus::Mode::TEMPERATURE) {
      // Only Mode::TEMPERATURE is the boiler. SETPOINT is the number being
      // dialled in, BLANK and TEXT are not temperatures at all.
      last_boiler_dc = (int16_t)(r.temp_c * 10);
      last_boiler_us = r.at_us;
      have_boiler = true;
    }
    if (r.mode == DisplayBus::Mode::TIMER) {
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

  const MachineContext& context() const { return mc; }

private:
  MachineContext mc;
  int16_t  last_boiler_dc = INT16_MIN;
  uint32_t last_boiler_us = 0;
  bool     have_boiler = false;
};
