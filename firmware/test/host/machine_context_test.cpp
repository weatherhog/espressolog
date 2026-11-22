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

  // 15. Pre-drip pulses (v4) — numbered 15, placed here beside the other
  //     pump-start cases: captured for the press's window only, and
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
    m.notePourConfirmed(108000);
    m.capturePrePulses(log, 108000);
    const MachineContext& c = m.context();
    assert(m.prePulsesCaptured());
    assert(c.pre_pulse_total == 19 + 61 && c.pre_pulse_count == c.pre_pulse_total);
    assert(c.pre_pulse_flags & PRE_PULSES_COMPLETE);
    assert(c.pre_pulse_ms[0] == 200 && c.pre_pulse_ms[18] == 2000 && c.pre_pulse_ms[19] == 5000);

    MachineContextTracker n;                       // no press: not captured
    n.onPourStart(1);
    n.resolvePump(108000);
    n.notePourConfirmed(108000);
    n.capturePrePulses(log, 108000);
    assert(!n.prePulsesCaptured() && n.context().pre_pulse_count == 0);

    // The window ends where inlet_pulses starts (pour confirmation, ~300 ms
    // after the backdated t=0), so pulses in between are not in neither.
    log.push(108100);
    log.push(108250);
    MachineContextTracker e;
    e.onPumpPress(PUMP_2CUP, 100000);
    e.onPourStart(1);
    e.resolvePump(108000);
    e.notePourConfirmed(108300);                   // confirmed at 108300
    e.capturePrePulses(log, 108000);
    assert(e.context().pre_pulse_total == 19 + 61 + 2);

    // The completion fallback (press confirmed late) ends at the same
    // confirmation, not at t=0, so it meets inlet_pulses too.
    MachineContextTracker f;
    f.onPourStart(1);
    f.resolvePump(108000);                         // no press yet
    f.notePourConfirmed(108300);
    f.capturePrePulses(log, 108000);               // does nothing: no press
    assert(!f.prePulsesCaptured());
    f.onPumpPress(PUMP_2CUP, 100000);              // confirmed late
    f.resolvePumpIfUnknown(108000);
    f.capturePrePulses(log, 108000);
    assert(f.context().pre_pulse_total == 19 + 61 + 2);

    // A confirmation absurdly late (a stall) is clamped so offsets fit: a
    // pulse 70 s after the press would otherwise be stored as 0xFFFF.
    log.push(100000 + 70000);
    MachineContextTracker g;
    g.onPumpPress(PUMP_2CUP, 100000);
    g.onPourStart(1);
    g.resolvePump(108000);
    g.notePourConfirmed(100000 + 200000);
    g.capturePrePulses(log, 108000);
    for (uint16_t i = 0; i < g.context().pre_pulse_count; i++)
      assert(g.context().pre_pulse_ms[i] < 0xFFFF);

    m.onPourStart(1);                              // the next shot starts clean
    assert(!m.prePulsesCaptured() && m.context().pre_pulse_count == 0);
    assert(m.context().pump_line == PUMP_NONE && m.context().boiler_temp_start_dc == INT16_MIN);
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

  // Setpoint from the display (v5): a value counts once it has BLINKED —
  // shown as SETPOINT, then blanked — twice in a row with the same value.
  // It survives across shots, an edit during a shot applies to the next one,
  // and nothing else is taken: a ramp's passing values, the steady frames
  // after the blink stops, a single misread blink, steam's TARGET, SET UP
  // values, TEMPERATURE. Not stamped while the display is not live, and
  // forgotten when the tap is switched.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    auto blank = [&](uint32_t at) {
      DisplayBus::Reading r; r.mode = DisplayBus::Mode::BLANK; r.at_us = at; return r;
    };
    auto blink = [&](int16_t c) {               // one shown phase, then its blank
      m.onReading(setpoint(c, t += 204000), true);
      m.onReading(blank(t += 210000), true);
    };
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 100);
    assert(m.context().setpoint_dc == INT16_MIN);         // no edit seen yet
    // An edit: ramp 95 -> 106 (no blanks), the blink settles on 93 — with
    // one misread blink of 99 in it — then ~0.6 s of steady SETPOINT frames
    // showing something else (the boiler, after a large edit), then normal.
    for (int16_t c = 95; c <= 106; c++) m.onReading(setpoint(c, t += 41000), true);
    m.onPourStart(t);
    assert(m.context().setpoint_dc == INT16_MIN);         // a ramp is not a setpoint
    blink(93);
    m.onPourStart(t);
    assert(m.context().setpoint_dc == INT16_MIN);         // one blink is not enough
    blink(99);                                            // misread
    blink(93);
    m.onPourStart(t);
    assert(m.context().setpoint_dc == INT16_MIN);         // 93, 99, 93: no two agree
    blink(93);
    blink(93);
    m.onReading(setpoint(88, t += 204000), true);         // steady tail, never blanked
    m.onReading(setpoint(88, t += 41000), true);
    m.onReading(temp(88, t += 41000), false);
    m.onPourStart(t);
    assert(m.context().setpoint_dc == 930);
    // Survives the next shot with no edit in between.
    m.onPourStart(t += 60000000);
    assert(m.context().setpoint_dc == 930);
    // Not taken: steam's TARGET blinking, SET UP values, out of range.
    for (int i = 0; i < 3; i++) {
      DisplayBus::Reading tg = setpoint(165, t += 500000);
      tg.mode = DisplayBus::Mode::TARGET;
      m.onReading(tg, false);
      m.onReading(blank(t += 500000), false);
    }
    blink(5); blink(5);
    blink(165); blink(165);
    m.onPourStart(t);
    assert(m.context().setpoint_dc == 930);
    // An edit during the shot: this shot keeps 93, the next one gets 96.
    blink(96); blink(96);
    assert(m.context().setpoint_dc == 930);
    // An edit that has not confirmed yet is UNKNOWN, not the old value: two
    // different blinks that disagree, or the pump's timer cutting off a
    // different blink, make it pending — for ONE shot — and two equal
    // blinks confirm the new value. A lone different blink is ignored.
    {
      MachineContextTracker e;
      uint32_t u = 1000000;
      auto eb = [&](int16_t c) {
        e.onReading(setpoint(c, u += 204000), true);
        DisplayBus::Reading b; b.mode = DisplayBus::Mode::BLANK; b.at_us = (u += 210000);
        e.onReading(b, true);
      };
      eb(93); eb(93);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 930);
      eb(93);                                             // the same value: nothing changes
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 930 && !e.context().setpoint_pending);
      // One different blink alone is ignored: on an edit's last blink it is
      // a misread, and a real new value confirms on its next blink anyway.
      eb(98);
      e.onReading(temp(93, u += 41000), false);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 930 && !e.context().setpoint_pending);
      // Two different blinks that disagree: an edit passing through values.
      eb(94); eb(95);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == INT16_MIN);
      assert(e.context().setpoint_pending);               // not carried onto
      assert(!e.context().setpoint_touched);              // its own flag, not touched
      e.onPourStart(u);
      assert(!e.context().setpoint_pending);              // one shot only...
      assert(e.context().setpoint_dc == INT16_MIN);       // ...the setpoint stays unknown
      // The tap switched mid-edit: still pending.
      eb(97); eb(98);
      e.forgetSetpoint();
      e.onPourStart(u);
      assert(e.context().setpoint_pending);
      // A one-degree edit: a single SETPOINT blink of the new value, then
      // the lever — the pump's TIMER cuts the run. Pending, not the old value.
      eb(96); eb(96);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 960);
      eb(97);
      e.onReading(timer(1, u += 41000), false);
      e.onReading(timer(2, u += 41000), false);           // two in a row
      e.onPourStart(u);
      assert(e.context().setpoint_dc == INT16_MIN && e.context().setpoint_pending);
      // The lever pressed DURING the new value's first shown phase, before
      // any blank: nothing has blinked, so the shown value decides.
      eb(96); eb(96);
      e.onPourStart(u);
      e.onReading(temp(97, u += 41000), false);           // an edit's first cycles
      e.onReading(setpoint(97, u += 204000), true);
      e.onReading(timer(1, u += 41000), false);
      e.onReading(timer(2, u += 41000), false);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == INT16_MIN && e.context().setpoint_pending);
      // An ordinary shot straight after a confirmed edit: nothing — also
      // when the steady tail shows a different number when the lever goes.
      eb(96); eb(96);
      e.onPourStart(u);
      e.onReading(timer(1, u += 41000), false);
      e.onReading(timer(2, u += 41000), false);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 960 && !e.context().setpoint_pending);
      eb(96); eb(96);
      e.onReading(setpoint(93, u += 204000), true);       // the tail
      e.onReading(timer(1, u += 41000), false);
      e.onReading(timer(2, u += 41000), false);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 960 && !e.context().setpoint_pending);
      // A single stray TIMER frame after a misread blink: nothing.
      eb(96); eb(96);
      eb(99);
      e.onReading(timer(1, u += 41000), false);
      e.onReading(temp(96, u += 41000), false);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 960 && !e.context().setpoint_pending);
      // The same lone blink ended by TEMPERATURE instead (the normal end of
      // an edit) is a misread: ignored. And TIMER after a confirmed value
      // (an ordinary shot) is nothing.
      eb(96); eb(96);
      e.onPourStart(u);
      eb(91);
      e.onReading(temp(96, u += 41000), false);
      e.onReading(timer(1, u += 41000), false);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 960 && !e.context().setpoint_pending);
      eb(96); eb(96);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 960 && !e.context().setpoint_pending);
      eb(96);                                             // more blinks of it: unchanged
      e.onReading(setpoint(88, u += 204000), true);       // steady tail: does not clear
      e.onReading(temp(88, u += 41000), false);
      e.onPourStart(u);
      assert(e.context().setpoint_dc == 960);
    }
    m.onPourStart(t += 60000000);
    assert(m.context().setpoint_dc == 960);
    // The display not live at pour start: nothing stamped, still remembered.
    m.onPourStart(t += 1000, false);
    assert(m.context().setpoint_dc == INT16_MIN);
    m.onPourStart(t += 1000, true);
    assert(m.context().setpoint_dc == 960);
    // The tap switched: forgotten.
    m.forgetSetpoint();
    m.onPourStart(t += 1000);
    assert(m.context().setpoint_dc == INT16_MIN);
    printf("the dialled setpoint is the value that blinked twice; ramps, tails, misreads, steam and SET UP are not taken OK\n");
  }

  // ---- v6: machine on-time and pump idle time (2026-11-22) ----
  {
    // Board booted at 0 with the machine OFF; machine switched on at 60 s
    // (first frame after a silent bus, well past the 10 s gap). A press at
    // 360 s, first drip at 368 s: on for 300 s, exact.
    MachineContextTracker m;
    m.watchBusAgain(0);
    for (uint32_t t = 60000; t <= 370000; t += 41) m.onBusFrame(t);
    m.onPumpPress(PUMP_2CUP, 360000, 25000);
    m.resolvePump(368000);
    const MachineContext& c = m.context();
    assert(c.machine_on_ms == 300000);
    assert(c.machine_on_flags == 0);
    assert(c.pump_idle_ms == 25000);
    printf("on-time from a cold start: %u ms, exact OK\n", (unsigned)c.machine_on_ms);
  }
  {
    // Board booted into a machine already on (frames from 1 s after boot):
    // the time counts from that first frame and is only a lower bound.
    MachineContextTracker m;
    m.watchBusAgain(0);
    for (uint32_t t = 1000; t <= 130000; t += 41) m.onBusFrame(t);
    m.onPumpPress(PUMP_2CUP, 120000);
    m.resolvePump(128000);
    assert(m.context().machine_on_ms == 119000);
    assert(m.context().machine_on_flags == MACHINE_ON_LOWER_BOUND);
    assert(m.context().pump_idle_ms == PUMP_IDLE_NONE);   // the default: not known
    printf("on-time into a running machine is a lower bound OK\n");
  }
  {
    // Switched off and on again within one boot: the second power-on counts,
    // and it is exact even though the first session was a lower bound.
    MachineContextTracker m;
    m.watchBusAgain(0);
    for (uint32_t t = 500; t <= 600000; t += 41) m.onBusFrame(t);          // first session
    for (uint32_t t = 900000; t <= 1100000; t += 41) m.onBusFrame(t);      // 5 min off, back on
    m.onPumpPress(PUMP_1CUP, 1050000);
    m.resolvePump(1058000);
    assert(m.context().machine_on_ms == 150000);
    assert(m.context().machine_on_flags == 0);
    printf("a second power-on in one boot is exact OK\n");
  }
  {
    // A short dropout of frames (5 s, under the 10 s gap) is not a power-on.
    MachineContextTracker m;
    m.watchBusAgain(0);
    for (uint32_t t = 30000; t <= 100000; t += 41) m.onBusFrame(t);
    for (uint32_t t = 105000; t <= 200000; t += 41) m.onBusFrame(t);
    m.onPumpPress(PUMP_2CUP, 190000);
    m.resolvePump(198000);
    assert(m.context().machine_on_ms == 160000);
    printf("a 5 s frame dropout is not a power-on OK\n");
  }
  {
    // No frames for minutes before the shot (display tap off): the bus is
    // not live at t=0, so on-time is unknown rather than stale.
    MachineContextTracker m;
    m.watchBusAgain(0);
    for (uint32_t t = 30000; t <= 100000; t += 41) m.onBusFrame(t);
    m.onPumpPress(PUMP_2CUP, 400000);
    m.resolvePump(408000);
    assert(m.context().machine_on_ms == MACHINE_ON_NONE);
    printf("no live bus at the shot gives unknown on-time OK\n");
  }
  {
    // Review 2026-11-22: a loop() blocked for 20 s (selftest, an upload)
    // with the machine on must not read as off-and-on -- the board was blind.
    MachineContextTracker m;
    m.watchBusAgain(0);
    for (uint32_t t = 60000; t <= 200000; t += 41) m.onBusFrame(t);
    m.noteBlind();                                   // the poll after the stall
    for (uint32_t t = 220000; t <= 400000; t += 41) m.onBusFrame(t);
    m.onPumpPress(PUMP_2CUP, 390000);
    m.resolvePump(398000);
    assert(m.context().machine_on_ms == 330000);     // still from 60 s, not 220 s
    assert(m.context().machine_on_flags == 0);
    printf("a blind spell is not a power-on OK\n");
  }

  printf("\nall machine context tests passed\n");
  return 0;
}
