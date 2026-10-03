// Host test for MachineContextTracker.
//
// This exists because the first version of this logic lived inline in
// main.cpp, which cannot be host-tested, and shipped two bugs that a fully
// green suite could not see: a previous shot's timer leaking into the next
// one, and setpoint_touched staying true for the rest of the boot. Both
// would have silently poisoned the first real field data in these columns.
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
    assert(m.context().boiler_temp_start_dc == 950);   // 95, not 120
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
