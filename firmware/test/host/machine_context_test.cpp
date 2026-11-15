// Host test for MachineContextTracker.
//
// This exists because the first version of this logic lived inline in
// main.cpp, which cannot be host-tested, and shipped two bugs that a fully
// green suite could not see: a previous shot's timer leaking into the next
// one, and setpoint_touched staying true for the rest of the boot. Both
// would have silently poisoned the first real field data in these columns.
#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include "../../src/machine_context.h"
#include <cstdio>
#include <cassert>

static DisplayBus::Reading temp(int16_t c, uint32_t at_us) {
  DisplayBus::Reading r;
  r.mode = DisplayBus::Mode::TEMPERATURE;
  r.temp_c = c;
  r.at_us = at_us;
  return r;
}
static DisplayBus::Reading timer(uint16_t dl, uint32_t at_us) {
  DisplayBus::Reading r;
  r.mode = DisplayBus::Mode::TIMER;
  r.timer_dl = dl;
  r.at_us = at_us;
  return r;
}
static DisplayBus::Reading setpoint(int16_t c, uint32_t at_us) {
  DisplayBus::Reading r;
  r.mode = DisplayBus::Mode::SETPOINT;
  r.temp_c = c;
  r.at_us = at_us;
  return r;
}

int main() {
  // 1. The ordinary path: temperature before the pour, timer during it.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    t += 4000000;                                  // pump starts; 4 s of first-drip lag
    for (uint16_t d = 1; d <= 210; d++) m.onReading(timer(d, t + d * 100000), false);
    m.onPourStart(t);                              // detector reaches POURING
    for (uint16_t d = 1; d <= 210; d++) m.onReading(timer(d, t + d * 100000), false);

    assert(m.context().boiler_temp_start_dc == 950);
    assert(m.context().machine_timer_dl == 210);
    assert(!m.context().setpoint_touched);
    printf("ordinary shot: 95 C before the pour, 21.0 s timer OK\n");
  }

  // 2. THE LEAK. A long shot, then a short one. Without restart detection
  //    and a reset after writing, the short shot inherits the long one's
  //    timer — because the tracker keeps a running maximum and a new
  //    brew's early values never exceed a stale one.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) m.onReading(timer(d, t + d * 100000), false);
    assert(m.context().machine_timer_dl == 280);
    // Record written. Nothing is reset here deliberately — the next pour
    // clears it, and the timer's restart detection handles the gap.

    t += 60000000;
    m.onReading(temp(93, t), false);
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 220; d++) m.onReading(timer(d, t + d * 100000), false);

    assert(m.context().machine_timer_dl == 220);   // 22.0 s, NOT 28.0
    assert(m.context().boiler_temp_start_dc == 930);
    printf("second shot does not inherit the first one's timer OK\n");
  }

  // 3. A flush between shots runs the machine's timer but writes no
  //    record, so onShotWritten() never fires. The restart detection has
  //    to carry it alone.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    for (uint16_t d = 1; d <= 150; d++) m.onReading(timer(d, t + d * 100000), false);  // 15 s flush
    t += 30000000;
    m.onReading(temp(94, t), false);
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 90; d++) m.onReading(timer(d, t + d * 100000), false);   // 9 s shot

    assert(m.context().machine_timer_dl == 90);
    printf("flush before a shot does not inflate its timer OK\n");
  }

  // 4. setpoint_touched must not be sticky across shots. The bus latch is
  //    per-boot and the caller clears it at pour start; the tracker must
  //    not re-latch from a flag that is no longer true.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), true);                // someone adjusted the setpoint
    m.onPourStart(t);
    m.onReading(timer(10, t + 100000), true);      // still latched during this shot
    assert(m.context().setpoint_touched);

    t += 60000000;
    m.onReading(temp(95, t), false);               // caller cleared the bus latch
    m.onPourStart(t);
    m.onReading(timer(10, t + 100000), false);
    assert(!m.context().setpoint_touched);
    printf("setpoint-touched does not persist into the next shot OK\n");
  }

  // 5. Only Mode::TEMPERATURE is the boiler. A number shown while the
  //    display is blinking is the setpoint being dialled in — recording
  //    that as the boiler would log a ramp to 120 as a 120 C brew.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onReading(setpoint(120, t + 1000000), false);
    m.onPourStart(t + 2000000);
    // Not 120. And, since 2026-11-13, not the 95 before it either: an
    // edit's first blink cycle decodes as TEMPERATURE, so the reading before
    // a SETPOINT frame may itself be the value being dialled (case 9). A
    // pour straight after an edit records "the machine did not say".
    assert(m.context().boiler_temp_start_dc == INT16_MIN);
    printf("setpoint being dialled is not recorded as boiler temperature OK\n");
  }

  // 6. A temperature older than the ceiling is not this shot's.
  {
    MachineContextTracker m;
    m.onReading(temp(95, 1000000), false);
    m.onPourStart(1000000 + MachineContextTracker::BOILER_MAX_AGE_US + 1);
    assert(m.context().boiler_temp_start_dc == INT16_MIN);
    printf("stale temperature is not claimed OK\n");
  }

  // 8. A lone TIMER frame is not a timer. 2026-11-13: an idle frame decoded
  //    as TIMER " 95" between two temperature frames. Arriving while
  //    SETTLING, it reset the 28 s maximum and recorded 9.5 s. main.cpp
  //    calls onReading every loop, so the same frame is fed repeatedly —
  //    that must not count as a second frame.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) m.onReading(timer(d, t + d * 100000), false);
    t += 280 * 100000;
    m.onReading(temp(95, t + 41000), false);       // pump stopped, back to temp
    for (int i = 0; i < 5; i++) m.onReading(timer(95, t + 82000), false);  // glitch, re-fed
    m.onReading(temp(95, t + 123000), false);
    assert(m.context().machine_timer_dl == 280);
    printf("a lone glitch TIMER frame does not overwrite the shot's timer OK\n");
  }

  // 9. A SETPOINT or TARGET reading invalidates the last boiler reading.
  //    The first blink cycle of an edit or a steam cycle decodes as
  //    TEMPERATURE before the blink is recognised, so steam's 165 (or the 120
  //    being dialled) would otherwise become the next shot's starting boiler
  //    temperature. Recorded as "the machine did not say" instead, until a
  //    real reading returns.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(165, t), false);              // steam, first cycle
    DisplayBus::Reading tg = temp(95, t + 1000000);
    tg.mode = DisplayBus::Mode::TARGET;            // cooling back, recognised
    m.onReading(tg, false);
    m.onPourStart(t + 2000000);
    assert(m.context().boiler_temp_start_dc == INT16_MIN);

    m.onReading(setpoint(120, t + 3000000), true);
    m.onReading(temp(95, t + 9000000), false);     // settled: real again
    m.onPourStart(t + 10000000);
    assert(m.context().boiler_temp_start_dc == 950);

    MachineContextTracker e;
    e.onReading(temp(120, t), false);              // edit, first cycle
    e.onReading(setpoint(120, t + 410000), true);
    e.onPourStart(t + 1000000);
    assert(e.context().boiler_temp_start_dc == INT16_MIN);
    printf("SETPOINT and TARGET invalidate the last boiler reading OK\n");
  }

  // 10. Pump start (0c, spool v3): which lever started the pump for this
  //     shot, and how long before first drip.
  {
    // The ordinary shot: 2Cup pressed, first drip 8.2 s later, stopped by a
    // second press 25 s after that. The stop press must not be chosen.
    MachineContextTracker m;
    m.onPumpPress(PUMP_2CUP, 100000);
    m.onPourStart(1);
    m.onPumpPress(PUMP_2CUP, 133200);              // the stop press, after t=0
    m.resolvePump(108200);                         // t=0 = first drip
    assert(m.context().pump_line == PUMP_2CUP);
    assert(m.context().pump_lead_ms == 8200);

    // A flush before the shot: 1Cup started and stopped, then 2Cup for the
    // shot. The shot's own start press is the latest before t=0.
    MachineContextTracker f;
    f.onPumpPress(PUMP_1CUP, 10000);
    f.onPumpPress(PUMP_1CUP, 15000);
    f.onPumpPress(PUMP_2CUP, 40000);
    f.resolvePump(47000);
    assert(f.context().pump_line == PUMP_2CUP && f.context().pump_lead_ms == 7000);

    // A press more than PUMP_LEAD_MAX_MS before t=0 is not this shot's.
    MachineContextTracker o;
    o.onPumpPress(PUMP_1CUP, 1000);
    o.resolvePump(1000 + MachineContextTracker::PUMP_LEAD_MAX_MS + 1);
    assert(o.context().pump_line == PUMP_NONE);
    assert(o.context().pump_lead_ms == PUMP_LEAD_NONE);

    // No switch tap, no presses: "not known", not zero.
    MachineContextTracker n;
    n.resolvePump(50000);
    assert(n.context().pump_line == PUMP_NONE && n.context().pump_lead_ms == PUMP_LEAD_NONE);

    // Across a millis() wrap (49.7 days): press just before, drip just after.
    MachineContextTracker w;
    w.onPumpPress(PUMP_1CUP, 0xFFFFF000u);         // 4096 ms before the wrap
    w.resolvePump(0x00000800u);                    // 2048 ms after it
    assert(w.context().pump_line == PUMP_1CUP && w.context().pump_lead_ms == 6144);

    // More presses than the ring holds: the oldest are forgotten, and the
    // newest before t=0 still wins.
    MachineContextTracker r;
    for (uint32_t i = 0; i < 10; i++) r.onPumpPress(PUMP_1CUP, 1000 + i * 1000);
    r.onPumpPress(PUMP_2CUP, 20000);
    r.resolvePump(26000);
    assert(r.context().pump_line == PUMP_2CUP && r.context().pump_lead_ms == 6000);
    printf("pump start: latest press before first drip, within the window OK\n");
  }

  // 11. Resolved AT POUR START: presses during the shot — the stop press, a
  //     restart, a flush begun while it settles — cannot crowd the start
  //     press out of the 4-entry ring, because the choice is already made.
  {
    MachineContextTracker m;
    m.onPumpPress(PUMP_2CUP, 100000);              // start
    m.onPourStart(1);
    m.resolvePump(108000);                         // pour start: t=0 known
    for (uint32_t i = 0; i < 6; i++) m.onPumpPress(PUMP_1CUP, 130000 + i * 3000);
    assert(m.context().pump_line == PUMP_2CUP && m.context().pump_lead_ms == 8000);
    printf("pump start fixed at pour start survives later presses OK\n");
  }

  // 14. A press confirmed only AFTER the POURING transition (first drip
  //     within ~300 ms of it, or a slow loop): pour start finds nothing, and
  //     the completion-time fallback picks it up. A press already found at
  //     pour start is not replaced by the fallback.
  {
    MachineContextTracker m;
    m.onPourStart(1);
    m.resolvePump(100200);                         // pour start: no press yet
    assert(m.context().pump_line == PUMP_NONE);
    m.onPumpPress(PUMP_1CUP, 100000);              // confirmed late, backdated
    m.onPumpPress(PUMP_1CUP, 125000);              // and the stop press
    m.resolvePumpIfUnknown(100200);                // at completion
    assert(m.context().pump_line == PUMP_1CUP && m.context().pump_lead_ms == 200);

    MachineContextTracker k;
    k.onPumpPress(PUMP_2CUP, 100000);
    k.onPourStart(1);
    k.resolvePump(108000);                         // found at pour start
    k.onPumpPress(PUMP_1CUP, 107000);              // a late-confirmed other press
    k.resolvePumpIfUnknown(108000);
    assert(k.context().pump_line == PUMP_2CUP && k.context().pump_lead_ms == 8000);
    printf("completion fallback fills a late press, keeps a found one OK\n");
  }

  // 15. Pre-drip pulses (v4): captured for the press's window only, and
  //     "not captured" — not zero — when there was no press.
  {
    PulseLog log;
    for (uint32_t t = 95000; t < 99000; t += 50) log.push(t);   // an earlier flush
    for (uint32_t t = 100200; t <= 102000; t += 100) log.push(t); // pre-infusion
    for (uint32_t t = 105000; t <= 108000; t += 50) log.push(t);  // after the pause
    MachineContextTracker m;
    m.onPumpPress(PUMP_2CUP, 100000);
    m.onPourStart(1);
    m.resolvePump(108000);
    m.capturePrePulses(log, 108000);
    const MachineContext& c = m.context();
    assert(m.prePulsesCaptured());
    assert(c.pre_pulse_total == 19 + 61 && c.pre_pulse_count == c.pre_pulse_total);
    assert(c.pre_pulse_flags & PRE_PULSES_COMPLETE);
    assert(c.pre_pulse_ms[0] == 200 && c.pre_pulse_ms[18] == 2000 && c.pre_pulse_ms[19] == 5000);

    MachineContextTracker n;                       // no press: not captured
    n.onPourStart(1);
    n.resolvePump(108000);
    n.capturePrePulses(log, 108000);
    assert(!n.prePulsesCaptured() && n.context().pre_pulse_count == 0);

    m.onPourStart(1);                              // the next shot starts clean
    assert(!m.prePulsesCaptured() && m.context().pre_pulse_count == 0);
    printf("pre-drip pulses: the press's window, or 'not captured' OK\n");
  }

  // 12. The line mapping: lever A's two directions and hot water; steam and
  //     anything unknown are PUMP_NONE and are not remembered at all.
  {
    assert(pumpLineForSwitch(SwitchBank::ONE_CUP) == PUMP_1CUP);
    assert(pumpLineForSwitch(SwitchBank::TWO_CUP) == PUMP_2CUP);
    assert(pumpLineForSwitch(SwitchBank::HOT_WATER) == PUMP_WATER);
    assert(pumpLineForSwitch(SwitchBank::STEAM) == PUMP_NONE);
    assert(pumpLineForSwitch(SwitchBank::N_LINES) == PUMP_NONE);
    MachineContextTracker m;
    m.onPumpPress(pumpLineForSwitch(SwitchBank::STEAM), 100000);
    m.resolvePump(105000);
    assert(m.context().pump_line == PUMP_NONE);
    printf("switch line mapping, steam and unknown ignored OK\n");
  }

  // 13. Switch sensing toggled: remembered presses are forgotten, so a
  //     press from before `switches off` cannot be attributed after it.
  {
    MachineContextTracker m;
    m.onPumpPress(PUMP_1CUP, 100000);
    m.clearPresses();
    m.resolvePump(105000);
    assert(m.context().pump_line == PUMP_NONE);
    assert(m.context().pump_lead_ms == PUMP_LEAD_NONE);
    printf("clearPresses forgets earlier presses OK\n");
  }

  // 7. Display bus never on: everything stays "the machine did not say".
  {
    MachineContextTracker m;
    m.onPourStart(5000000);
    assert(m.context().boiler_temp_start_dc == INT16_MIN);
    assert(m.context().machine_timer_dl == 0);
    assert(!m.context().setpoint_touched);
    printf("no display bus yields no claims OK\n");
  }

  printf("\nall machine context tests passed\n");
  return 0;
}
