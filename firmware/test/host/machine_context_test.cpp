// Host test for MachineContextTracker.
//
// This exists because the first version of this logic lived inline in
// main.cpp, which cannot be host-tested, and shipped two bugs that a fully
// green suite could not see: a previous shot's timer leaking into the next
// one, and setpoint_touched staying true for the rest of the boot. Both
// would have silently poisoned the first real field data in these columns.
#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include "../../src/machine_context.h"
#include "../../src/display_watch.h"
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

// A timer value as the display shows it: on more than one frame -- frames
// come every ~41 ms, the timer ticks every 100 ms. v != d is a deliberate
// misread, shown on its one frame only.
static void feed(MachineContextTracker& m, uint16_t v, uint16_t d, uint32_t at_us) {
  m.onReading(timer(v, at_us), false);
  if (v == d) m.onReading(timer(v, at_us + 41000), false);
}

int main() {
  // 1. The ordinary path: temperature before the pour, timer during it.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    t += 4000000;                                  // pump starts; 4 s of first-drip lag
    for (uint16_t d = 1; d <= 210; d++) feed(m, d, d, t + d * 100000);
    m.onPourStart(t);                              // detector reaches POURING
    for (uint16_t d = 1; d <= 210; d++) feed(m, d, d, t + d * 100000);

    assert(m.context().boiler_temp_start_dc == 950);
    assert(m.context().machine_timer_dl == 210);
    assert(!m.context().setpoint_touched);
    printf("ordinary shot: 95 C before the pour, 21.0 s timer OK\n");
  }

  // 2. THE LEAK. A long shot, then a short one. Without telling the runs apart
  //    and a reset after writing, the short shot inherits the long one's
  //    timer — because the tracker keeps a running maximum and a new
  //    brew's early values never exceed a stale one.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
    assert(m.context().machine_timer_dl == 280);
    // Record written. Nothing is reset here deliberately — the next pour
    // clears it, and the second run's own date tells it apart.

    t += 60000000;
    m.onReading(temp(93, t), false);
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 220; d++) feed(m, d, d, t + d * 100000);

    assert(m.context().machine_timer_dl == 220);   // 22.0 s, NOT 28.0
    assert(m.context().boiler_temp_start_dc == 930);
    printf("second shot does not inherit the first one's timer OK\n");
  }

  // 3. A flush between shots runs the machine's timer but writes no
  //    record, so onShotWritten() never fires. Telling runs apart by date has
  //    to carry it alone.
  {
    MachineContextTracker m;
    uint32_t t = 1000000;
    for (uint16_t d = 1; d <= 150; d++) feed(m, d, d, t + d * 100000);  // 15 s flush
    t += 30000000;
    m.onReading(temp(94, t), false);
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 90; d++) feed(m, d, d, t + d * 100000);   // 9 s shot

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
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
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
    m.onFirstFrame(60000, 60000, 60000);
    for (uint32_t t = 60041; t <= 370000; t += 41) m.onBusFrame(t, t - 5);
    m.onPumpPress(PUMP_2CUP, 360000, 25000);
    m.resolvePump(368000);
    const MachineContext& c = m.context();
    assert(c.machine_on_ms == 300000);   // raw: from the first counted frame
    assert(c.machine_on_flags == 0);
    assert(c.pump_idle_ms == 25000);
    printf("on-time from a cold start: %u ms, exact OK\n", (unsigned)c.machine_on_ms);
  }
  {
    // Board booted into a machine already on (frames 0.2 s after watching
    // started -- a running bus sends one every 41 ms):
    // the time counts from that first frame and is only a lower bound.
    MachineContextTracker m;
    m.watchBusAgain(0);
    m.onFirstFrame(200, 200, 200);
    for (uint32_t t = 241; t <= 130000; t += 41) m.onBusFrame(t, t - 5);
    m.onPumpPress(PUMP_2CUP, 120000);
    m.resolvePump(128000);
    assert(m.context().machine_on_ms == 119800);
    assert(m.context().machine_on_flags == MACHINE_ON_LOWER_BOUND);
    assert(m.context().pump_idle_ms == PUMP_IDLE_NONE);   // the default: not known
    printf("on-time into a running machine is a lower bound OK\n");
  }
  {
    // Switched off and on again within one boot: the second power-on counts,
    // and it is exact even though the first session was a lower bound.
    MachineContextTracker m;
    m.watchBusAgain(0);
    m.onFirstFrame(500, 500, 500);
    for (uint32_t t = 541; t <= 600000; t += 41) m.onBusFrame(t, t - 5);          // first session
    for (uint32_t t = 900000; t <= 1100000; t += 41) m.onBusFrame(t, t - 5);      // 5 min off, back on
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
    m.onFirstFrame(30000, 30000, 30000);
    for (uint32_t t = 30041; t <= 100000; t += 41) m.onBusFrame(t, t - 5);
    for (uint32_t t = 105000; t <= 200000; t += 41) m.onBusFrame(t, t - 5);
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
    m.onFirstFrame(30000, 30000, 30000);
    for (uint32_t t = 30041; t <= 100000; t += 41) m.onBusFrame(t, t - 5);
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
    m.onFirstFrame(60000, 60000, 60000);
    for (uint32_t t = 60041; t <= 200000; t += 41) m.onBusFrame(t, t - 5);
    // loop() blocked from 200 s to 220 s: the first poll after it (220 s)
    // decodes the frames buffered meanwhile; the poll before it was 200 s.
    m.onBusFrame(220000, 199990);
    for (uint32_t t = 220041; t <= 400000; t += 41) m.onBusFrame(t, t - 5);
    m.onPumpPress(PUMP_2CUP, 390000);
    m.resolvePump(398000);
    assert(m.context().machine_on_ms == 330000);     // still from 60 s, not 220 s
    assert(m.context().machine_on_flags == 0);
    printf("a blocked loop is not a power-on OK\n");
  }
  {
    // Review 2026-11-22: the loop blocked while the machine was OFF, then a
    // real power-on hours later -- it must count (a lingering "blind" flag
    // hid it), exact.
    MachineContextTracker m;
    m.watchBusAgain(0);
    m.onFirstFrame(60000, 60000, 60000);
    for (uint32_t t = 60041; t <= 200000; t += 41) m.onBusFrame(t, t - 5);
    // machine off at 200 s; polls keep running (and one stall happens, which
    // does not matter: polls before and after it still see no frame).
    m.onBusFrame(7200000, 7199995);                 // switched on at 7200 s
    for (uint32_t t = 7200041; t <= 7510000; t += 41) m.onBusFrame(t, t - 5);
    m.onPumpPress(PUMP_2CUP, 7500000);
    m.resolvePump(7508000);
    assert(m.context().machine_on_ms == 300000);
    assert(m.context().machine_on_flags == 0);
    printf("a stall while off does not hide the next power-on OK\n");
  }
  {
    // A frame gap over 10 s across a blocked loop: the last poll before the
    // block came 0 ms after the previous frame, so the WITNESSED gap is tiny
    // and it is not a power cycle, whatever the block's length.
    MachineContextTracker m;
    m.watchBusAgain(0);
    m.onFirstFrame(30000, 30000, 30000);
    for (uint32_t t = 30041; t <= 100000; t += 41) m.onBusFrame(t, t - 5);
    m.onBusFrame(110030, 100030);                   // block from 100.03 s to 110.03 s
    for (uint32_t t = 110071; t <= 200000; t += 41) m.onBusFrame(t, t - 5);
    m.onPumpPress(PUMP_2CUP, 190000);
    m.resolvePump(198000);
    assert(m.context().machine_on_ms == 160000);
    printf("a frame gap over 10 s across a blocked loop is not a power-on OK\n");
  }
  {
    // The poll-time fallback (onBusFrame for a first frame, which the
    // firmware never takes): always a lower bound, whatever the silence.
    MachineContextTracker m;
    m.watchBusAgain(2000);
    m.onBusFrame(9000, 8995);
    for (uint32_t t = 9041; t <= 130000; t += 41) m.onBusFrame(t, t - 5);
    m.onPumpPress(PUMP_2CUP, 120000);
    m.resolvePump(128000);
    assert(m.context().machine_on_flags == MACHINE_ON_LOWER_BOUND);
    // The operator's strip (2026-11-22): board and machine powered by one
    // switch. The board starts watching at ~2 s, after the init burst; the
    // bus is frame-silent until +4.1 s. Watched silence >= 0.5 s: a power-up,
    // not flagged; raw, from the first counted frame (analysis adds ~4.1 s).
    MachineContextTracker a, b;
    a.watchBusAgain(2000);
    a.onFirstFrame(4100, 2100, 4100);           // first frame: 2.1 s after watching
    for (uint32_t t = 4141; t <= 21000; t += 41) a.onBusFrame(t, t - 5);
    a.onPumpPress(PUMP_2CUP, 20000); a.resolvePump(20500);
    assert(a.context().machine_on_flags == 0 && a.context().machine_on_ms == 15900);
    // Frames within 0.2 s of starting to watch: already running.
    b.watchBusAgain(2000);
    b.onFirstFrame(2200, 200, 2200);
    for (uint32_t t = 2241; t <= 21000; t += 41) b.onBusFrame(t, t - 5);
    b.onPumpPress(PUMP_2CUP, 20000); b.resolvePump(20500);
    assert(b.context().machine_on_flags == MACHINE_ON_LOWER_BOUND && b.context().machine_on_ms == 17800);
    // The firmware path: onFirstFrame with the frame's own time. A slow
    // setup() polls late, but the frame stamped at +4.1 s still says
    // "silent for 3.8 s since watching started at 0.3 s".
    MachineContextTracker f;
    f.watchBusAgain(300);
    f.onFirstFrame(4100, 3800, 4105);
    for (uint32_t t = 4141; t <= 21000; t += 41) f.onBusFrame(t, t - 5);
    f.onPumpPress(PUMP_2CUP, 20000); f.resolvePump(20500);
    assert(f.context().machine_on_flags == 0 && f.context().machine_on_ms == 15900);
    MachineContextTracker g;                     // the same, with frames 40 ms in
    g.watchBusAgain(300);
    g.onFirstFrame(340, 40, 345);
    for (uint32_t t = 381; t <= 21000; t += 41) g.onBusFrame(t, t - 5);
    g.onPumpPress(PUMP_2CUP, 20000); g.resolvePump(20500);
    assert(g.context().machine_on_flags == MACHINE_ON_LOWER_BOUND);
    // Review 2026-11-22: the first frame polled late (setup() slow): the
    // blind time between the frame and its poll is not witnessed silence,
    // so the next frames do not read as a power cycle.
    MachineContextTracker h;
    h.watchBusAgain(300);
    h.onFirstFrame(340, 40, 15000);              // polled 14.7 s after the frame
    for (uint32_t t = 15041; t <= 30000; t += 41) h.onBusFrame(t, t - 5);
    h.onPumpPress(PUMP_2CUP, 29000); h.resolvePump(29500);
    assert(h.context().machine_on_flags == MACHINE_ON_LOWER_BOUND && h.context().machine_on_ms == 28660);
    // Frames lost before the first one (silence passed as 0): not a power-up.
    MachineContextTracker k;
    k.watchBusAgain(300);
    k.onFirstFrame(5000, 0, 5005);
    assert(k.busSeen());
    for (uint32_t t = 5041; t <= 21000; t += 41) k.onBusFrame(t, t - 5);
    k.onPumpPress(PUMP_2CUP, 20000); k.resolvePump(20500);
    assert(k.context().machine_on_flags == MACHINE_ON_LOWER_BOUND);
    // The firmware's judgement (firstFrameSilence): silence counts from when
    // capture was whole (the enable, or the drain after the latest drop).
    using MCT = MachineContextTracker;
    assert(MCT::firstFrameSilence(1499, 1000) == 499);
    assert(MCT::firstFrameSilence(1500, 1000) == MCT::POWERUP_SILENCE_MS);
    // Review 2026-11-22: a drop during the init burst (+1.08 s), drained at
    // +1.2 s; the first frame at +4.1 s still has 2.9 s of whole capture.
    assert(MCT::firstFrameSilence(4100, 1200) == 2900);
    // A frame drained from a full ring, from before the latest drop's drain:
    // nothing whole before it -- 0.
    assert(MCT::firstFrameSilence(1000, 1200) == 0);
    assert(MCT::firstFrameSilence(100, 0xFFFFFF00u) == 356);     // millis wrapped
    // DisplayWatch (display_watch.h): the ring bookkeeping main.cpp feeds per
    // drain. Times in ms; drain(start, end, dropped, have_first, first_ms).
    {
      // Power-up seen whole: enabled at 300, first activity at 4100.
      DisplayWatch w; w.enable(300);
      w.afterDrain(4000, 4001, 0, false, 0);
      w.afterDrain(4110, 4111, 0, true, 4100);
      assert(w.first_noted && w.first_frame_ms == 4100 && w.first_silence == 3800);
      // Review 2026-11-22: a drop during the init burst (~1.08 s), drained at
      // 1.2 s, frames at 4.1 s: 2.9 s of whole capture -- still a power-up.
      DisplayWatch b; b.enable(300);
      b.afterDrain(1200, 1201, 37, false, 0);           // the drain that saw the drop
      assert(b.capture_ok_ms == 1201);
      b.afterDrain(4110, 4111, 37, true, 4100);
      assert(b.first_silence == 4100 - 1201);
      // A running machine, ring full at the first drain: the frames drained
      // are the oldest (near the enable) -- judged against the enable, not
      // the drain: a lower bound.
      DisplayWatch r; r.enable(300);
      r.afterDrain(2500, 2501, 900, true, 340);
      assert(r.first_silence == 40 && r.capture_ok_ms == 2501);
      // Review 2026-11-22: a frame from edges that arrived DURING the drain
      // that noticed a drop came after it -- whole only from the drain start.
      DisplayWatch d; d.enable(300);
      d.afterDrain(5000, 5010, 12, true, 5004);
      assert(d.first_silence == 4);
      // `display on` again: the ring was cleared, whole capture restarts.
      DisplayWatch e; e.enable(300);
      e.afterDrain(1000, 1001, 0, false, 0);
      e.enable(4050);
      e.afterDrain(4110, 4111, 0, true, 4100);
      assert(e.first_silence == 50);
      // Judged once: later drains do not re-judge.
      e.afterDrain(9000, 9001, 0, true, 8000);
      assert(e.first_frame_ms == 4100);
    }
    printf("boot ordering, and power-up vs already running OK\n");
  }
  {
    // millis() wrapping between frames: unsigned arithmetic still sees a
    // witnessed 20 s silence as a power cycle.
    MachineContextTracker m;
    m.watchBusAgain(0xFFFF0000u);
    m.onBusFrame(0xFFFF0000u + 20000, 0xFFFF0000u + 19995);         // first frame, exact
    m.onBusFrame(0xFFFF0000u + 20041, 0xFFFF0000u + 20036);
    const uint32_t back = 0xFFFF0000u + 20041 + 60000;              // wraps past 0
    m.onBusFrame(back, back - 5);
    for (uint32_t t = back + 41; t != back + 40000; t += 41) {   // machine stays on
      m.onBusFrame(t, t - 5);
      if ((uint32_t)(t - back) > 39900) break;
    }
    m.onPumpPress(PUMP_2CUP, back + 30000);
    m.resolvePump(back + 38000);
    assert(m.context().machine_on_ms == 30000);
    printf("millis wrap OK\n");
  }

  // 16-20. A SHOT'S TIMER IS ITS OWN PUMP RUN OR 0, NEVER ANOTHER RUN'S
  //    (0.16.1). Until then onPourStart() carried the timer unconditionally
  //    and the header read it at completion, after the 5 s settle, so the
  //    PWA's shot-time headline could show the previous brew's timer (a
  //    hot-water draw) or a flush's (pressed while SETTLING). Frames here are
  //    100 ms apart for TIMER and 41 ms for temperature; a pump run of d
  //    tenths ends at its last frame.
  {
    // 16. A hot-water draw after a shot. The display does not time it, so
    //     no TIMER frame arrives after its pour starts.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + 100000 + d * 100000);
    t += 100000 + 280 * 100000;
    for (int i = 1; i <= 50; i++) m.onReading(temp(95, t + i * 41000), false);  // pump off
    m.onPourStop(t + 50 * 41000, 2000);
    assert(m.context().machine_timer_dl == 280);
    t += 60000000;                                  // a minute later: lever B up
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (int i = 2; i <= 200; i++) m.onReading(temp(95, t + i * 41000), false);  // no timer
    m.onPourStop(t + 200 * 41000, 2000);
    assert(m.context().machine_timer_dl == 0);      // NOT the shot's 28.0 s
    printf("a hot-water draw after a shot stores no timer, not the shot's OK\n");
  }
  {
    // 17. A flush pressed while SETTLING: the shot keeps its own timer.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + 100000 + d * 100000);
    t += 100000 + 280 * 100000;                     // pump off at t
    for (int i = 1; i <= 100; i++) m.onReading(temp(95, t + i * 41000), false);
    // Flat from ~t+2.1 s, confirmed 2 s later at ~t+4.1 s.
    m.onPourStop(t + 100 * 41000, 2000);
    t += 100 * 41000;
    for (uint16_t d = 1; d <= 150; d++) feed(m, d, d, t + d * 100000);  // flush
    assert(m.context().machine_timer_dl == 280);    // NOT 15.0 s
    printf("a flush while settling does not replace the shot's timer OK\n");
  }
  {
    // 18. A flush pressed in the 2 s between the (backdated) stop and its
    //     confirmation: it has already restarted the timer when onPourStop()
    //     runs, which puts the shot's run back.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + 100000 + d * 100000);
    t += 100000 + 280 * 100000;                     // pump off at t
    for (int i = 1; i <= 25; i++) m.onReading(temp(95, t + i * 41000), false);
    const uint32_t stop_us = t + 25 * 41000;        // flat from here
    for (uint16_t d = 1; d <= 15; d++) feed(m, d, d, stop_us + 400000 + d * 100000);
    m.onPourStop(stop_us + 2000000, 2000);
    assert(m.context().machine_timer_dl == 280);
    for (uint16_t d = 16; d <= 150; d++) feed(m, d, d, stop_us + 400000 + d * 100000);
    assert(m.context().machine_timer_dl == 280);
    printf("a flush between the stop and its confirmation is undone OK\n");
  }
  {
    // 19. The carry that IS kept: the timer running when the pour starts
    //     (pre-infusion) and ending right at it with no frame after -- and
    //     a pump that outlasts the flat scale goes on counting past the
    //     stop, unbroken, then a later, longer flush cannot replace it.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    for (uint16_t d = 1; d <= 40; d++) feed(m, d, d, t + d * 100000);
    m.onPourStart(t + 40 * 100000 + 41000);
    assert(m.context().machine_timer_dl == 40);
    printf("a timer running at pour start is kept OK\n");

    MachineContextTracker k;
    t = 1000000;
    k.onReading(temp(95, t), false);
    k.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 250; d++) feed(k, d, d, t + 100000 + d * 100000);
    k.onPourStop(t + 100000 + 250 * 100000 + 10000, 2000);   // still timing
    for (uint16_t d = 251; d <= 300; d++) feed(k, d, d, t + 100000 + d * 100000);
    assert(k.context().machine_timer_dl == 300);
    t += 100000 + 300 * 100000;
    for (int i = 1; i <= 30; i++) k.onReading(temp(95, t + i * 41000), false);
    t += 30 * 41000;
    for (uint16_t d = 1; d <= 400; d++) feed(k, d, d, t + d * 100000);
    assert(k.context().machine_timer_dl == 300);   // not the 40 s flush
    printf("a pump outlasting the stop counts on, a later flush does not OK\n");
  }
  {
    // 20. The display not decoding at pour start: no timer is carried.
    MachineContextTracker m;
    uint32_t t = 1000000;
    for (uint16_t d = 1; d <= 40; d++) feed(m, d, d, t + d * 100000);
    m.onPourStart(t + 40 * 100000 + 41000, false);
    assert(m.context().machine_timer_dl == 0);
    // A run is its date: the same run picked up after a 3 s dropout (its
    // readings still date its start) goes on counting past the stop...
    MachineContextTracker k;
    k.onPourStart(t);
    for (uint16_t d = 1; d <= 100; d++) feed(k, d, d, t + d * 100000);
    k.onPourStop(t + 100 * 100000 + 10000, 2000);           // still timing
    const uint32_t back = t + 130 * 100000;                  // 3 s of no frames
    for (uint16_t d = 130; d <= 200; d++) feed(k, d, d, back + (d - 130) * 100000);
    assert(k.context().machine_timer_dl == 200);
    // ...while a flush seen after a dropout -- a later date -- does not.
    MachineContextTracker f;
    f.onPourStart(t);
    for (uint16_t d = 1; d <= 100; d++) feed(f, d, d, t + d * 100000);
    f.onPourStop(t + 100 * 100000 + 10000, 2000);
    const uint32_t fl = t + 100 * 100000 + 3000000;          // flush started ~2 s after
    for (uint16_t d = 10; d <= 200; d++) feed(f, d, d, fl + (d - 10) * 100000);
    assert(f.context().machine_timer_dl == 100);
    printf("no timer carried from a stale display; a run is its date across a gap OK\n");
  }

  {
    // 21. Review of 3f34199: a shot frozen by onPourStop() must not hand its
    //     value to the next shot. Shot 1 runs 28.0 s; shot 2's pump runs
    //     4.0 s and ends right at its pour start, with no frame after it.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + 100000 + d * 100000);
    t += 100000 + 280 * 100000;
    for (int i = 1; i <= 100; i++) m.onReading(temp(95, t + i * 41000), false);
    m.onPourStop(t + 100 * 41000, 2000);
    assert(m.context().machine_timer_dl == 280);
    t += 60000000;
    m.onReading(temp(95, t), false);
    for (uint16_t d = 1; d <= 40; d++) feed(m, d, d, t + d * 100000);
    m.onPourStart(t + 40 * 100000 + 41000);
    assert(m.context().machine_timer_dl == 40);     // NOT shot 1's 280
    for (int i = 2; i <= 100; i++) m.onReading(temp(95, t + 40 * 100000 + i * 41000), false);
    m.onPourStop(t + 40 * 100000 + 100 * 41000, 2000);
    assert(m.context().machine_timer_dl == 40);
    printf("a frozen shot's timer does not reach the next shot OK\n");
  }
  {
    // 22. A shot that finishes straight from POURING (a lift during the
    //     pour): the completion-time onPourStop() undoes a flush pressed
    //     after the stop, and a second call is a no-op.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + 100000 + d * 100000);
    t += 100000 + 280 * 100000;                     // pump off; stop = last kept sample
    for (int i = 1; i <= 10; i++) m.onReading(temp(95, t + i * 41000), false);
    const uint32_t stop_us = t + 10 * 41000;
    for (uint16_t d = 1; d <= 15; d++) feed(m, d, d, stop_us + 300000 + d * 100000);
    assert(m.context().machine_timer_dl == 15);     // the flush, until the stop is known
    m.onPourStop(stop_us + 2000000, 2000);          // lift hold over: completed
    assert(m.context().machine_timer_dl == 280);
    m.onPourStop(stop_us + 2100000, 2100);
    assert(m.context().machine_timer_dl == 280);
    printf("a shot completed from POURING drops a flush after its stop OK\n");
  }

  {
    // 23. Review of 7b27abe: one misread HIGH TIMER frame (750 in a 25 s
    //     run) is ignored, after the stop while the pump runs on and during
    //     the pour alike.
    MachineContextTracker k;
    uint32_t t = 1000000;
    k.onReading(temp(95, t), false);
    k.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 250; d++) feed(k, d, d, t + 100000 + d * 100000);
    k.onPourStop(t + 100000 + 250 * 100000 + 10000, 2000);   // forced stop, pump on
    k.onReading(timer(750, t + 100000 + 251 * 100000), false);
    for (uint16_t d = 252; d <= 300; d++) feed(k, d, d, t + 100000 + d * 100000);
    assert(k.context().machine_timer_dl == 300);   // not 75.0 s
    MachineContextTracker m;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 250; d++)
      feed(m, d == 200 ? 750 : d, d, t + 100000 + d * 100000);
    assert(m.context().machine_timer_dl == 250);
    printf("a misread high TIMER frame is ignored OK\n");
  }
  {
    // 24. Review of 7b27abe: two flushes between the stop and a late
    //     onPourStop() (a scale lost mid-pour is completed up to 65 s on):
    //     the shot goes back to its own run, not the first flush.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + 100000 + d * 100000);
    t += 100000 + 280 * 100000;
    for (int i = 1; i <= 10; i++) m.onReading(temp(95, t + i * 41000), false);
    const uint32_t stop = t + 10 * 41000;
    uint32_t u = stop + 200000;
    for (uint16_t d = 1; d <= 30; d++) feed(m, d, d, u + d * 100000);
    u += 30 * 100000;
    for (int i = 1; i <= 10; i++) m.onReading(temp(95, u + i * 41000), false);
    u += 10 * 41000;
    for (uint16_t d = 1; d <= 30; d++) feed(m, d, d, u + d * 100000);
    u += 30 * 100000;
    m.onPourStop(u, (u - stop) / 1000);
    assert(m.context().machine_timer_dl == 280);   // not a flush's 3.0 s
    printf("several flushes before a late stop do not replace the shot's run OK\n");
  }
  {
    // 25. Review of 7b27abe: a misread LOW frame just before the stop is
    //     confirmed must not make the run look as if it began after the
    //     stop; it is the run in force at the stop and goes on counting.
    MachineContextTracker k;
    uint32_t t = 1000000;
    k.onReading(temp(95, t), false);
    k.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 250; d++)
      feed(k, d == 245 ? 1 : d, d, t + 100000 + d * 100000);
    const uint32_t stop = t + 100000 + 240 * 100000;
    const uint32_t now = t + 100000 + 250 * 100000 + 10000;
    k.onPourStop(now, (now - stop) / 1000);
    for (uint16_t d = 251; d <= 300; d++) feed(k, d, d, t + 100000 + d * 100000);
    assert(k.context().machine_timer_dl == 300);
    printf("a run split by a misread low frame keeps its start OK\n");
  }

  {
    // 26. Review of 520f3ff: ONE low frame is a misread, not a restart --
    //     as the last frame before the stop, and as the last before pour
    //     start. A real restart (two agreeing frames) still counts.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onReading(temp(95, t), false);
    m.onPourStart(t + 41000);
    for (uint16_t d = 1; d <= 280; d++)
      feed(m, d == 280 ? 3 : d, d, t + 100000 + d * 100000);
    t += 100000 + 280 * 100000;
    for (int i = 1; i <= 100; i++) m.onReading(temp(95, t + i * 41000), false);
    m.onPourStop(t + 100 * 41000, 2000);
    assert(m.context().machine_timer_dl == 279);   // not 0.3 s
    MachineContextTracker k;
    t = 1000000;
    for (uint16_t d = 1; d <= 40; d++) feed(k, d == 40 ? 2 : d, d, t + d * 100000);
    k.onPourStart(t + 40 * 100000 + 41000);
    assert(k.context().machine_timer_dl == 39);
    for (uint16_t d = 1; d <= 30; d++) feed(k, d, d, t + 4200000 + d * 100000);
    assert(k.context().machine_timer_dl == 30);    // a real restart takes over
    printf("a single low TIMER frame is not a restart OK\n");
  }
  {
    // 27. Review of 520f3ff: the misread bound restarts with the run. A
    //     28.0 s run restarts into 0.1, 0.2, ...; its last frame misread as
    //     25.0 must not pass on the old run's peak.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
    const uint32_t u = t + 280 * 100000;
    for (uint16_t d = 1; d <= 30; d++)
      feed(m, d == 30 ? 250 : d, d, u + d * 100000);
    assert(m.context().machine_timer_dl == 29);
    printf("the misread bound restarts with the run OK\n");
  }
  {
    // 28. Four flushes between the stop and a late freeze keep the shot's run.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
    const uint32_t stop = t + 280 * 100000 + 500000;
    uint32_t u = stop;
    for (int f = 0; f < 4; f++) {
      for (int i = 1; i <= 10; i++) m.onReading(temp(95, u + i * 41000), false);
      u += 10 * 41000;
      for (uint16_t d = 1; d <= 30; d++) feed(m, d, d, u + d * 100000);
      u += 30 * 100000;
    }
    m.onPourStop(u, (u - stop) / 1000);
    assert(m.context().machine_timer_dl == 280);
    printf("four flushes before a late freeze keep the shot's run OK\n");
  }

  {
    // 29. Review of 2c58c24: one unchecked frame after a break could set the
    //     run. A misdecoded non-TIMER frame, then a frame misread LOW (12.2
    //     for 20.2), then the real run: the run keeps counting to 28.0.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 200; d++) feed(m, d, d, t + d * 100000);
    m.onReading(temp(95, t + 200 * 100000 + 50000), false);   // misdecoded frame
    m.onReading(timer(201, t + 201 * 100000), false);
    m.onReading(timer(122, t + 202 * 100000), false);         // misread low
    for (uint16_t d = 203; d <= 280; d++) feed(m, d, d, t + d * 100000);
    m.onPourStop(t + 290 * 100000, 500);
    assert(m.context().machine_timer_dl == 280);
    printf("a misread after a break does not take over the run OK\n");
  }
  {
    // 30. Review of 2c58c24: a flush whose first counted-looking frame is
    //     misread HIGH (8.2 for 0.2) must not date it before the stop.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
    t += 280 * 100000;
    for (int i = 1; i <= 10; i++) m.onReading(temp(95, t + i * 41000), false);
    const uint32_t stop = t + 10 * 41000;
    const uint32_t fl = stop + 400000;
    m.onReading(timer(1, fl + 100000), false);
    m.onReading(timer(82, fl + 200000), false);               // misread high
    for (uint16_t d = 3; d <= 15; d++) feed(m, d, d, fl + d * 100000);
    m.onPourStop(stop + 2000000, 2000);
    assert(m.context().machine_timer_dl == 280);
    printf("a flush's misread first frame does not date it before the stop OK\n");
  }
  {
    // 31. A run split by a misdecoded non-TIMER frame just before the stop
    //     is still the run in force at the stop: its date, not its frames'
    //     continuity, says so.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 240; d++) feed(m, d, d, t + d * 100000);
    m.onReading(temp(95, t + 240 * 100000 + 50000), false);   // misdecoded
    for (uint16_t d = 241; d <= 250; d++) feed(m, d, d, t + d * 100000);
    m.onPourStop(t + 250 * 100000 + 10000, 1000);             // stop at ~24.0 s
    for (uint16_t d = 251; d <= 300; d++) feed(m, d, d, t + d * 100000);
    assert(m.context().machine_timer_dl == 300);
    printf("a run split by a misdecoded frame is still the stop's run OK\n");
  }

  {
    // 32. Review of d24b655: micros() wraps every ~71.6 min, so a brew that
    //     starts exactly 2^32 us after the last counted run has the same
    //     date. It must be a new run (20.0 s), not the old one (28.0 s).
    MachineContextTracker m;
    const uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
    m.onPourStop(t + 290 * 100000, 500);
    // an hour of idle temperature frames, every ~10 s here
    for (uint32_t i = 1; i <= 426; i++) m.onReading(temp(95, t + 290 * 100000 + i * 10000000u), false);
    for (uint16_t d = 1; d <= 3; d++) feed(m, d, d, t + d * 100000);   // t + 2^32
    m.onPourStart(t + 3 * 100000 + 41000);
    for (uint16_t d = 4; d <= 200; d++) feed(m, d, d, t + d * 100000);
    m.onPourStop(t + 210 * 100000, 500);
    assert(m.context().machine_timer_dl == 200);
    printf("a brew one micros() wrap later is a new run OK\n");
  }
  {
    // 33. Review of d24b655: the same misread twice in a row (12.2 for
    //     25.1, 25.2) after a forced stop must not end the following.
    MachineContextTracker m;
    const uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 250; d++) feed(m, d, d, t + d * 100000);
    m.onPourStop(t + 250 * 100000 + 10000, 0);              // forced stop, pump on
    m.onReading(timer(122, t + 251 * 100000), false);
    m.onReading(timer(122, t + 251 * 100000 + 41000), false);
    for (uint16_t d = 253; d <= 300; d++) feed(m, d, d, t + d * 100000);
    assert(m.context().machine_timer_dl == 300);
    printf("a repeated misread after the stop does not end the run OK\n");
  }
  {
    // 34. Review of d24b655: two idle frames decoded as TIMER " 95" just
    //     after the stop (the 2026-11-13 glitch, twice) are not a run.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
    t += 280 * 100000;
    for (int i = 1; i <= 12; i++) m.onReading(temp(95, t + i * 41000), false);
    const uint32_t stop = t + 12 * 41000;
    m.onReading(timer(95, stop + 500000), false);
    m.onReading(timer(95, stop + 541000), false);
    for (int i = 1; i <= 30; i++) m.onReading(temp(95, stop + 541000 + i * 41000), false);
    m.onPourStop(stop + 2000000, 2000);
    assert(m.context().machine_timer_dl == 280);
    printf("two idle 9.5 glitch frames near the stop are not a run OK\n");
  }

  {
    // 35. Review of 059ef1e: THE DISPLAY HOLDS THE FINAL VALUE ~5 s after
    //     pump-off (2026-10-03 captures). A hot-water draw pressed 1.5 s into
    //     a flush's hold has no timer of its own and stores 0, not the
    //     flush's 10.5 s. And a shot whose pour starts while its own timer
    //     still counts keeps it.
    MachineContextTracker m;
    uint32_t t = 1000000;
    for (uint16_t d = 1; d <= 105; d++) feed(m, d, d, t + d * 100000);  // flush
    t += 105 * 100000;                               // pump off; 105 held 5 s
    for (int i = 1; i <= 36; i++) m.onReading(timer(105, t + i * 41000), false);
    m.onPourStart(t + 36 * 41000 + 10000);           // the draw, 1.5 s into the hold
    for (int i = 37; i <= 122; i++) m.onReading(timer(105, t + i * 41000), false);
    for (int i = 1; i <= 200; i++) m.onReading(temp(95, t + 5000000 + i * 41000), false);
    m.onPourStop(t + 5000000 + 200 * 41000, 2000);
    assert(m.context().machine_timer_dl == 0);       // not 105
    printf("a draw inside a flush's held final value stores no timer OK\n");
  }
  {
    // 36. Review of 059ef1e: after a 3 s dropout the same misread twice
    //     (2.1, dated after the run's last frame) must not lock the real run
    //     out: it goes on to 28.0.
    MachineContextTracker m;
    const uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 230; d++) feed(m, d, d, t + d * 100000);
    m.onReading(timer(21, t + 260 * 100000), false);           // dropout, then misread
    m.onReading(timer(21, t + 260 * 100000 + 41000), false);
    for (uint16_t d = 261; d <= 280; d++) feed(m, d, d, t + d * 100000);
    m.onPourStop(t + 290 * 100000, 500);
    assert(m.context().machine_timer_dl == 280);
    printf("a repeated misread after a dropout does not lock the run out OK\n");
  }
  {
    // 37. A flush pressed while the shot's final value is still held, after
    //     the stop: it counts, so it is another run, and the shot keeps its
    //     own 28.0.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 280; d++) feed(m, d, d, t + d * 100000);
    t += 280 * 100000;
    for (int i = 1; i <= 60; i++) m.onReading(timer(280, t + i * 41000), false);   // held
    m.onPourStop(t + 60 * 41000, 1000);
    for (int i = 61; i <= 80; i++) m.onReading(timer(280, t + i * 41000), false);
    const uint32_t fl = t + 80 * 41000;
    for (uint16_t d = 0; d <= 150; d++) feed(m, d, d, fl + 50000 + d * 100000);
    assert(m.context().machine_timer_dl == 280);
    printf("a flush during the held value after the stop is another run OK\n");
  }

  {
    // 38. Review of 9734611 (injected into the 2026-10-03 captures): a tenths
    //     digit misread by one or two passes the pair rule. One held frame
    //     reading 107 for 105 must not raise the shot's timer, nor make a
    //     draw confirmed 0.6 s after the last real count carry the run; and
    //     the last rising frame misread +2 must not either.
    MachineContextTracker m;
    uint32_t t = 1000000;
    m.onPourStart(t);
    for (uint16_t d = 1; d <= 105; d++) feed(m, d, d, t + d * 100000);
    uint32_t h = t + 105 * 100000 + 82000;                     // the hold
    m.onReading(timer(107, h), false);                         // one-segment misread
    for (int i = 1; i <= 20; i++) m.onReading(timer(105, h + i * 41000), false);
    m.onPourStop(h + 20 * 41000, 1000);
    assert(m.context().machine_timer_dl == 105);               // not 107
    for (int i = 21; i <= 100; i++) m.onReading(timer(105, h + i * 41000), false);

    MachineContextTracker k;                                   // the draw
    t = 1000000;
    for (uint16_t d = 1; d <= 105; d++) feed(k, d, d, t + d * 100000);
    h = t + 105 * 100000 + 82000;
    k.onReading(timer(106, h), false);                         // misread, at the hold's start
    for (int i = 1; i <= 12; i++) k.onReading(timer(105, h + i * 41000), false);
    k.onPourStart(h + 450000);                                 // 0.53 s after the last count
    assert(k.context().machine_timer_dl == 0);

    MachineContextTracker e;                                   // the last rise misread
    t = 1000000;
    e.onPourStart(t);
    for (uint16_t d = 1; d <= 104; d++) feed(e, d, d, t + d * 100000);
    e.onReading(timer(107, t + 105 * 100000), false);          // 105 shown as 107
    for (int i = 1; i <= 20; i++) e.onReading(timer(105, t + 105 * 100000 + i * 41000), false);
    e.onPourStop(t + 105 * 100000 + 20 * 41000, 500);
    assert(e.context().machine_timer_dl == 105);
    printf("a tenths digit misread by one or two does not raise the timer OK\n");
  }
  {
    // 39. Review of 9734611: a pour exactly one micros() wrap after the last
    //     count-up must not carry that run.
    MachineContextTracker m;
    const uint32_t t = 1000000;
    for (uint16_t d = 1; d <= 105; d++) feed(m, d, d, t + d * 100000);
    for (uint32_t i = 1; i <= 426; i++) m.onReading(temp(95, t + 110 * 100000 + i * 10000000u), false);
    m.onPourStart(t + 105 * 100000 + 41000 + 100000);         // + 2^32 us, a draw
    assert(m.context().machine_timer_dl == 0);
    printf("a pour one micros() wrap after a run does not carry it OK\n");
  }

  {
    // 40. Review of 6aa4443 (real captures): during a flush's held final
    //     value, a misread pair whose date matches the run (105 read as 125,
    //     ~2 s into the hold) must not push that run into a hot-water draw
    //     that started 0.4 s earlier.
    MachineContextTracker m;
    const uint32_t t = 1000000;
    for (uint16_t d = 1; d <= 105; d++) feed(m, d, d, t + d * 100000);
    const uint32_t h = t + 105 * 100000 + 82000;
    for (int i = 0; i < 46; i++) m.onReading(timer(105, h + i * 41000), false);
    m.onPourStart(h + 46 * 41000 - 400000);                    // the draw
    m.onReading(timer(125, h + 46 * 41000), false);
    m.onReading(timer(125, h + 47 * 41000), false);
    for (int i = 48; i <= 120; i++) m.onReading(timer(105, h + i * 41000), false);
    m.onPourStop(h + 120 * 41000, 1000);
    assert(m.context().machine_timer_dl == 0);
    printf("a misread in a flush's hold does not reach a draw OK\n");
  }

  printf("\nall machine context tests passed\n");
  return 0;
}
