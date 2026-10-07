// Espresso shot logger — Phase 0a, stages 0–4.
// BLE scale in, shot detection, records spooled to LittleFS, uploaded over
// HTTP with delete-on-confirm. No machine contact (ever, in phase 0).

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "scale.h"
#include "detector.h"
#include "spool.h"
#include "net.h"
#include "display.h"
#include "machine_io.h"
#include <driver/gpio.h>

const char* FIRMWARE_VERSION = "0.9.0";
const char* DETECTOR_VERSION = "0a.6";

static ScaleManager scales;
static ShotDetector detector;
static WeighingDetector weighing[2];   // per role: yield, dose
static Spool spool;
static Net net;
static QueueHandle_t sample_queue;
static bool raw_stream = true;
static bool spool_ok = false;

// Weighings can complete on the dose scale while the yield scale is
// mid-pour; finished events wait here and reach flash only when no shot is
// in progress — invariant 2 holds globally, not per-scale.
struct PendingWeighing { uint8_t role; WeighingDetector::Event ev; };
static PendingWeighing pending_w[8];
static size_t pending_w_n = 0;

// ---------------------------------------------------------------- display bus
// Milestone 0b: the machine's own front display, tapped read-only at J5.
// Protocol and bit offsets are in display.h; the probe point is in CLAUDE.md.
// Idle it carries boiler temperature; during a brew it carries the machine's
// own shot timer, which is a truer pump-on/pump-off boundary than a scale can
// infer.
//
// OFF BY DEFAULT, enabled with `display on` (persisted in NVS). An
// unconnected input floats and would fire this ISR on mains hum at 100 Hz
// forever, so the interrupt is only attached once the tap is actually wired.
//
// Level conditioning — the bus is 5 V and these pins are NOT 5 V tolerant:
//
//     J5 line --[ 8.2K ]--+-- GPIO
//                         +--[ 15K ]-- GND
//
// Safe whichever way the line is driven: 3.23 V at the pin if push-pull,
// 2.69 V if open-drain through the board's 4.7K pull-ups (VIH is 2.48 V), and
// the machine's own high never falls below 4.16 V against a display needing
// 3.5 V. Do NOT enable an internal pull-down — 45K in parallel with the 15K
// leg drops the open-drain case to 2.33 V and the pin stops reading high.
static constexpr uint8_t DISPLAY_CLK_PIN  = 4;
static constexpr uint8_t DISPLAY_DATA_PIN = 5;

static DisplayBus display_bus;
static bool display_on = false;

// Same producer/consumer split as the BLE path: the ISR does nothing but
// stamp and enqueue, loop() is the single consumer. A plain ring rather than
// a FreeRTOS queue only because these edges arrive at ~10 kHz rather than the
// scales' 10 Hz, and per-edge queue overhead at that rate is not worth it.
// Framing is deliberately NOT done here — it lives in DisplayBus::feedEdge so
// the logic that ships is the logic the host tests exercise, and so no flash
// read can ever happen inside the ISR.
struct DbusEdge { uint32_t t_us; uint8_t bit; };
static constexpr uint16_t DBUS_RING = 512;      // ~50 ms of edges at 9.9 kHz
static volatile DbusEdge dbus_ring[DBUS_RING];
static volatile uint16_t dbus_head = 0, dbus_tail = 0;
static volatile uint32_t dbus_dropped = 0;
static uint32_t dbus_frames = 0;

static void IRAM_ATTR dbusIsr() {
  uint16_t h = dbus_head;
  uint16_t nxt = (uint16_t)((h + 1) % DBUS_RING);
  if (nxt == dbus_tail) { dbus_dropped++; return; }   // consumer stalled
  dbus_ring[h].t_us = micros();
  dbus_ring[h].bit  = gpio_get_level((gpio_num_t)DISPLAY_DATA_PIN) ? 1 : 0;
  dbus_head = nxt;
}

static void displaySetEnabled(bool on) {
  if (on == display_on) return;
  if (on) {
    pinMode(DISPLAY_CLK_PIN, INPUT);
    pinMode(DISPLAY_DATA_PIN, INPUT);
    dbus_head = dbus_tail = 0;
    display_bus.beginCapture();   // drop the in-progress frame; see display.h
    attachInterrupt(digitalPinToInterrupt(DISPLAY_CLK_PIN), dbusIsr, RISING);
  } else {
    detachInterrupt(digitalPinToInterrupt(DISPLAY_CLK_PIN));
  }
  display_on = on;
}

static void pollDisplay() {
  if (!display_on) return;
  while (dbus_tail != dbus_head) {
    DbusEdge e = { dbus_ring[dbus_tail].t_us, dbus_ring[dbus_tail].bit };
    dbus_tail = (uint16_t)((dbus_tail + 1) % DBUS_RING);
    if (display_bus.feedEdge(e.t_us, e.bit)) dbus_frames++;
  }
  if (display_bus.poll(micros())) dbus_frames++;   // bus went quiet mid-frame
}

// What the machine contributes to a shot record (format v2). The logic
// lives in machine_context.h so it can be host-tested — the first version
// of this sat inline here, had no tests, and shipped two bugs a green
// suite could not see: a previous shot's timer leaking into the next, and
// setpoint_touched staying true for the whole boot.
static MachineContextTracker machine_ctx;

static void machineContextPoll() {
  if (!display_on) return;
  machine_ctx.onReading(display_bus.reading(), display_bus.setpointTouched());
}

static void printDisplay(Stream& out) {
  if (!display_on) {
    out.println("# display bus off — 'display on' once the J5 tap is wired");
    return;
  }
  const DisplayBus::Reading& r = display_bus.reading();
  const char* mode = r.mode == DisplayBus::Mode::TEMPERATURE ? "temp"
                   : r.mode == DisplayBus::Mode::TIMER       ? "timer"
                   : r.mode == DisplayBus::Mode::SETPOINT    ? "setpoint"
                   : r.mode == DisplayBus::Mode::BLANK       ? "blank"
                   : r.mode == DisplayBus::Mode::TEXT        ? "text" : "none";
  out.printf("# display=[%s] mode=%s", r.text, mode);
  if (r.mode == DisplayBus::Mode::TEMPERATURE) out.printf(" %d C", (int)r.temp_c);
  else if (r.mode == DisplayBus::Mode::SETPOINT) out.printf(" %d C (being dialled)", (int)r.temp_c);
  else if (r.mode == DisplayBus::Mode::TIMER)  out.printf(" %u.%u s", r.timer_dl / 10, r.timer_dl % 10);
  if (display_bus.adjusting())       out.print(" ADJUSTING");
  if (display_bus.programming())     out.print(" PrG");
  if (display_bus.setpointTouched()) out.print(" SETPOINT-TOUCHED");
  if (display_bus.stale(micros(), 2000000)) out.print(" (STALE)");

  const DisplayBus::Buttons& b = display_bus.buttons();
  out.printf(" | buttons=%s%s", b.left ? "L" : "-", b.right ? "R" : "-");
  out.printf(" | frames=%lu dropped=%lu\n",
             (unsigned long)dbus_frames, (unsigned long)dbus_dropped);
}

// --------------------------------------------------- machine wiring (0c/0d)
// The two taps that read the machine's own harness rather than its display.
// Every decision lives in machine_io.h so it is host-tested; what is here is
// an ISR, a poll, and the pin numbers.
//
// BOTH DEFAULT OFF, like the display bus and for the same reason: an
// unconnected input floats, and a floating line reads as a pulse train or a
// phantom press forever. Enable each only once its tap is physically wired.
//
// Pin choices come from CLAUDE.md's GPIO budget. They avoid 0/3/45/46
// (strapping), 19/20 (native USB), 26-32 (flash) and 33-37 (octal PSRAM on
// this N16R8 module — configuring one of those typically crashes the
// firmware). CONFIRM AGAINST THE SILKSCREEN before the perfboard is drilled:
// this is a clone DevKitC-1 and these came from the constraint list rather
// than a pin-by-pin check of the board in hand.
//
// NEVER enable an internal pull-up or pull-down on any of these. The taps are
// resistor dividers in the hundreds of kilohms, and the ESP32's internal
// resistors are ~45 kR — they would sit across the shunt leg and collapse the
// level. The display bus carries the same warning for the same arithmetic.
static constexpr uint8_t FLOW_PIN = 6;                      // J4 `#`, via 120K + 220K
// Order matches SwitchBank::Line, NOT J2's connector order (J2 runs
// +5V, Steam, 2Cup, 1Cup, Water — note 2Cup sits BEFORE 1Cup).
static constexpr uint8_t SW_PINS[SwitchBank::N_LINES] = { 7, 15, 16, 17 };

static PulseCounter flow;
static SwitchBank   switches;
static bool flow_on = false, switches_on = false;

// Whether the flow tap was live FOR THIS SHOT, latched when the pour starts.
// Sampling flow_on at shot close instead (which is what 0.9.0 first did) gets
// it wrong in both directions: `flow on` typed during SETTLING stamps a whole
// record of placeholder zeros as a measurement meaning "no water moved", and
// `flow off` mid-pour discards real counts. Any change to the tap while a shot
// is in progress clears this, because a count that covers part of a shot is
// not a measurement of that shot.
static bool flow_tap_shot = false;

// Staleness on millis(), not micros(). PulseCounter::sinceLastUs is exact but
// wraps at ~71 min, so an overnight idle on a dead tap reported a few minutes
// and read as healthy. millis() wraps at ~49.7 days, which covers every span
// this diagnostic is asked about.
static uint32_t flow_last_ms = 0;
static bool     flow_ever = false;

// Same producer/consumer split as the display bus: the ISR stamps and
// enqueues, loop() is the only consumer, and the glitch filter runs in the
// pure class so the logic that ships is the logic the host tests exercise.
// 64 entries is ~2.6 s at the fastest plausible real rate (~24 Hz, a
// hot-water draw) against a 5 ms loop, so an overflow here does not mean
// "busy" — it means the line is carrying something that is not flow. That is
// a bench-test result, which is why it is counted rather than smoothed.
static constexpr uint16_t FLOW_RING = 64;
static volatile uint32_t flow_ring[FLOW_RING];
static volatile uint16_t flow_head = 0, flow_tail = 0;
static volatile uint32_t flow_dropped = 0;

// The Digmesa is an NPN open collector: it idles high on the board's own
// 10.04K pull-up (4.69 V measured) and pulses low, so the falling edge is
// the pulse. Counting FALLING rather than CHANGE also halves the ISR rate.
static void IRAM_ATTR flowIsr() {
  uint16_t h = flow_head;
  uint16_t nxt = (uint16_t)((h + 1) % FLOW_RING);
  if (nxt == flow_tail) { flow_dropped++; return; }
  flow_ring[h] = micros();
  flow_head = nxt;
}

static void flowSetEnabled(bool on) {
  if (on == flow_on) return;
  if (on) {
    pinMode(FLOW_PIN, INPUT);
    flow_head = flow_tail = 0;
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowIsr, FALLING);
  } else {
    detachInterrupt(digitalPinToInterrupt(FLOW_PIN));
  }
  flow_on = on;
}

static uint32_t sw_last_poll = 0, sw_max_gap = 0;

static void switchesSetEnabled(bool on) {
  if (on == switches_on) return;
  if (on) {
    for (uint8_t i = 0; i < SwitchBank::N_LINES; i++) pinMode(SW_PINS[i], INPUT);
    // The taps go in one at a time, so an earlier `switches on` against an
    // unwired line may have adopted a floating high. Carrying that across the
    // rewire made the first poll afterwards emit a release whose duration was
    // the hours in between — see SwitchBank::reset().
    switches.reset();
    sw_last_poll = 0;   // the off period is not a loop() stall; see sw_max_gap
  }
  switches_on = on;
}

// Called at the TOP of loop(), before the scale-sample drain that tags each
// sample with flow.total(). Running it at the bottom (which is where 0.9.0
// first put it) left every sample carrying the previous iteration's count —
// invisible at a 5 ms loop, but after a stall the whole backlog of samples
// gets one stale count and the next one jumps by the lot, which is a
// staircase in the inlet curve made of loop artefacts rather than flow.
static void pollFlow() {
  if (!flow_on) return;
  while (flow_tail != flow_head) {
    uint32_t t_us = flow_ring[flow_tail];
    flow_tail = (uint16_t)((flow_tail + 1) % FLOW_RING);
    if (flow.feedEdge(t_us)) { flow_last_ms = millis(); flow_ever = true; }
  }
}

// The switch lines are POLLED, not interrupt-driven. A press is tens to
// hundreds of milliseconds against a ~5 ms loop, so polling has ample
// resolution, and it cannot be storm-triggered by a floating line the way
// four more edge interrupts could. The risk it does carry is a press missed
// entirely inside a long loop() stall, so the worst gap is measured and
// reported rather than assumed — if the bench shows gaps near a press
// length, this becomes interrupt-driven and the measurement will say so.
//
// A missed press is also not silent: the display bus runs the machine's own
// shot timer, so a brew with no switch event still leaves a trace.
static void reportSwitch(const SwitchBank::Event& e) {
  Serial.printf("# switch %s %lu ms%s%s\n", SwitchBank::lineName(e.line),
                (unsigned long)e.duration_ms,
                e.synthetic ? "  (press edge never seen — line was already high)" : "",
                e.reprogram ? "   *** HELD PAST 2 s: STORED TIMER REPROGRAMMED ***" : "");
  char frame[160];
  snprintf(frame, sizeof(frame),
           "{\"ev\":\"switch\",\"line\":\"%s\",\"t\":%lu,\"ms\":%lu,\"reprogram\":%d,\"synthetic\":%d}",
           SwitchBank::lineName(e.line), (unsigned long)e.pressed_at_ms,
           (unsigned long)e.duration_ms, e.reprogram ? 1 : 0, e.synthetic ? 1 : 0);
  net.sendLive(frame);
}

static void pollSwitches() {
  if (!switches_on) return;
  uint32_t now = millis();
  if (sw_last_poll && (now - sw_last_poll) > sw_max_gap) sw_max_gap = now - sw_last_poll;
  sw_last_poll = now;

  bool lv[SwitchBank::N_LINES];
  for (uint8_t i = 0; i < SwitchBank::N_LINES; i++)
    lv[i] = gpio_get_level((gpio_num_t)SW_PINS[i]) != 0;   // idle LOW, pressed HIGH
  switches.feed(now, lv);

  SwitchBank::Event e;
  while (switches.popEvent(e)) reportSwitch(e);
}

// ----------------------------------------------------------- bench self-test
// The loopback jig. Host tests prove the glitch filter and the debouncer; what
// they cannot prove is that the ISR, the ring and a real 5 ms loop keep up
// with a pulse train — there is no independent count to check against during
// a real shot, so a quiet undercount would look exactly like a slow pour.
// Here the expected count is known exactly.
//
// GPIO8 is deliberately NOT one of the seven tap pins, and is the only pin
// this firmware ever drives ON A MACHINE-FACING NET. (It is not the only
// output full stop — updateLed() drives the onboard RGB LED on GPIO48, which
// reaches nothing outside the DevKit. An earlier version of this comment said
// "the only pin in this firmware that is ever an output", which was simply
// false, and it was carrying the invariant-1 argument for this whole
// command.) GPIO8 is returned to INPUT the moment the run ends.
static constexpr uint8_t SELFTEST_PIN = 8;

static void runSelfTest(Stream& out, uint32_t pulses, uint32_t hz) {
  if (!flow_on) { out.println("# selftest: run 'flow on' first"); return; }
  // Refuse during a shot. This injects pulses straight into the count the
  // detector is folding into live samples, AND busy-waits for seconds, which
  // overflows the 64-deep scale queue (xQueueSend drops silently) and puts a
  // hole in the curve. That is invariant 2's harm reached without a flash
  // write, so the same quiet test the spool and net.tick use applies here.
  ShotState st_now = detector.state();
  if (st_now != ShotState::IDLE && st_now != ShotState::ARMED) {
    out.println("# selftest: REFUSED — a shot is in progress."
                " It would add phantom pulses to the record and stall the scale queue.");
    return;
  }
  if (hz < 20 || hz > 400) hz = 100;   // floor at 20: see the duration cap below
  if (pulses < 1 || pulses > 5000) pulses = 500;
  // Cap the blocking time. `selftest 500 1` — an easy argument-order slip —
  // used to busy-wait 500 s with no watchdog to rescue it (the loop task runs
  // on core 1 and idle-task WDT is not enabled there), leaving the board deaf
  // to scales, spool and serial for eight minutes.
  if (pulses / hz > 30) {
    out.printf("# selftest: REFUSED — %lu pulses at %lu Hz would block loop() for %lu s (max 30).\n",
               (unsigned long)pulses, (unsigned long)hz, (unsigned long)(pulses / hz));
    return;
  }

  out.println("# ***********************************************************");
  out.printf("# * SELFTEST DRIVES GPIO%u AS AN OUTPUT.\n", SELFTEST_PIN);
  out.println("# * UNPLUG THE J4 LEAD FIRST. Hard invariant 1 says phase 0");
  out.println("# * never actuates anything, and this is the one command that");
  out.println("# * can break it. Bench only: jumper GPIO8 to GPIO6, nothing");
  out.println("# * else connected. BLE stalls for the duration.");
  out.println("# ***********************************************************");
  out.printf("# driving %lu pulses at %lu Hz (%lu ms)…\n",
             (unsigned long)pulses, (unsigned long)hz,
             (unsigned long)(pulses * 1000UL / hz));

  uint32_t c0 = flow.total(), g0 = flow.glitches(), d0 = flow_dropped;
  uint32_t half = 500000UL / hz;
  pinMode(SELFTEST_PIN, OUTPUT);
  digitalWrite(SELFTEST_PIN, HIGH);     // the line idles high, like the sensor
  delay(5);
  for (uint32_t i = 0; i < pulses; i++) {
    digitalWrite(SELFTEST_PIN, LOW);
    delayMicroseconds(half);
    digitalWrite(SELFTEST_PIN, HIGH);
    delayMicroseconds(half);
    pollFlow();                         // drain as loop() would, not in a lump
  }
  // Settle and drain while GPIO8 still HOLDS the line high. Releasing it first
  // leaves GPIO6 floating against the jumper, and a floating CMOS input can
  // oscillate — adding counts to the very delta this test is about to judge,
  // and failing a run that passed.
  delay(20);
  pollFlow();
  uint32_t counted = flow.total() - c0;
  uint32_t glitch  = flow.glitches() - g0;
  uint32_t dropped = flow_dropped - d0;
  pinMode(SELFTEST_PIN, INPUT);         // high-impedance again, measurement taken
  sw_last_poll = 0;                     // this block was not a loop() stall
  out.printf("# selftest: expected %lu, counted %lu (%+ld), glitches %lu, ring drops %lu\n",
             (unsigned long)pulses, (unsigned long)counted,
             (long)counted - (long)pulses, (unsigned long)glitch, (unsigned long)dropped);
  if (counted == pulses && glitch == 0 && dropped == 0)
    out.println("# selftest: PASS — every edge counted exactly once");
  else if (counted == 0)
    out.println("# selftest: FAIL — nothing counted. Is GPIO8 jumpered to GPIO6?");
  else
    out.println("# selftest: FAIL — see the deltas above; do NOT trust a shot's pulse count yet");
}

// Raw levels of every machine input. The bench tool for a divider: touch the
// line, watch the number. Also the first thing to run at the machine, before
// anything is counted, because it answers "is this tap on the net I think it
// is" without needing the machine to do anything.
static void printPins(Stream& out) {
  // Make sure every line is readable even when its tap is still off — `pins`
  // is most useful BEFORE `flow on`/`switches on`, while a divider is being
  // checked with a jumper. INPUT is high-impedance, so this stays inside
  // invariant 1; it is also the state these pins should be in regardless.
  pinMode(FLOW_PIN, INPUT);
  for (uint8_t i = 0; i < SwitchBank::N_LINES; i++) pinMode(SW_PINS[i], INPUT);
  // The display pins too, but ONLY when the bus is off — they are configured
  // by displaySetEnabled otherwise, and there is no reason to touch a pad with
  // a live interrupt on it. Without this, `pins` printed clk=0 data=0 whenever
  // the display tap was off: an ESP32-S3 pad comes out of reset with its input
  // buffer disabled, so gpio_get_level() reads 0 regardless of pin voltage.
  // CLAUDE.md records both J5 lines as idling HIGH on 4.7 K pull-ups, so that
  // output read as a dead tap and would have sent the operator re-metering a
  // working harness — from the command whose own comment calls it the first
  // thing to run at the machine.
  if (!display_on) {
    pinMode(DISPLAY_CLK_PIN, INPUT);
    pinMode(DISPLAY_DATA_PIN, INPUT);
  }
  out.printf("# pins: flow/GPIO%u=%d | 1cup/GPIO%u=%d 2cup/GPIO%u=%d steam/GPIO%u=%d water/GPIO%u=%d",
             FLOW_PIN,  gpio_get_level((gpio_num_t)FLOW_PIN),
             SW_PINS[0], gpio_get_level((gpio_num_t)SW_PINS[0]),
             SW_PINS[1], gpio_get_level((gpio_num_t)SW_PINS[1]),
             SW_PINS[2], gpio_get_level((gpio_num_t)SW_PINS[2]),
             SW_PINS[3], gpio_get_level((gpio_num_t)SW_PINS[3]));
  out.printf(" | clk/GPIO%u=%d data/GPIO%u=%d\n",
             DISPLAY_CLK_PIN,  gpio_get_level((gpio_num_t)DISPLAY_CLK_PIN),
             DISPLAY_DATA_PIN, gpio_get_level((gpio_num_t)DISPLAY_DATA_PIN));
  out.println("# expected at rest: flow=1 (idles high on the board pull-up),"
              " all four switches=0 (idle low, a press reads 1)");
}

static void printMachine(Stream& out) {
  if (!flow_on) {
    out.println("# flow: off — 'flow on' once the J4 tap is wired."
                " READ docs/phase-0-status.md FIRST: the 2026-10-03 tap faulted the machine (E01).");
  } else {
    out.printf("# flow: pulses=%lu glitches=%lu dropped=%lu last=",
               (unsigned long)flow.total(), (unsigned long)flow.glitches(),
               (unsigned long)flow_dropped);
    if (!flow_ever) out.println("NEVER (nothing has pulsed since boot)");
    else out.printf("%lu ms ago\n", (unsigned long)(millis() - flow_last_ms));
    if (flow.glitches() > flow.total() / 4 && flow.glitches() > 20)
      out.println("# flow: WARNING — glitches are a large fraction of counts."
                  " The filter rate-limits noise, it does not reject it:"
                  " the counted figure is suspect too.");
  }
  if (!switches_on) {
    out.println("# switches: off — 'switches on' once the J2 tap is wired");
  } else {
    uint32_t now = millis();
    out.print("# switches:");
    for (uint8_t i = 0; i < SwitchBank::N_LINES; i++) {
      out.printf(" %s=%s", SwitchBank::lineName(i), switches.pressed(i) ? "PRESSED" : "idle");
      uint32_t held = switches.heldMs(i, now);
      // >=, matching the event path in machine_io.cpp — 0.9.0 had > here and
      // >= there, so a line held at exactly 2000 ms disagreed with itself.
      if (held >= SwitchBank::REPROGRAM_MS) out.printf("(%lus!)", (unsigned long)(held / 1000));
    }
    out.printf(" | worst poll gap=%lu ms, events dropped=%lu\n",
               (unsigned long)sw_max_gap, (unsigned long)switches.dropped());
  }
}

static const char* stateName(ShotState s) {
  switch (s) {
    case ShotState::IDLE:     return "IDLE";
    case ShotState::ARMED:    return "ARMED";
    case ShotState::POURING:  return "POURING";
    case ShotState::SETTLING: return "SETTLING";
  }
  return "?";
}

static void printShotSummary(const ShotResult& r) {
  Serial.println("# ---- shot complete ----");
  Serial.printf("# valid=%d fault=%d truncated=%d detector=%s fw=%s\n",
                r.valid, r.fault, r.truncated, DETECTOR_VERSION, FIRMWARE_VERSION);
  Serial.printf("# stop_ms=%lu yield_at_stop_mg=%ld yield_final_mg=%ld settle_offset_mg=%ld\n",
                (unsigned long)r.stop_ms, (long)r.yield_at_stop_mg,
                (long)r.yield_final_mg, (long)r.settle_offset_mg);
  Serial.printf("# peak_flow_mgps=%ld mean_flow_mgps=%ld flow_win=%lu..%lu ms samples=%u\n",
                (long)r.peak_flow_mgps, (long)r.mean_flow_mgps,
                (unsigned long)r.flow_win_start_ms, (unsigned long)r.flow_win_end_ms,
                r.sample_count);
}

static void handleShotComplete(const ShotResult& r) {
  printShotSummary(r);
  // Everything the detector finishes is spooled — including rejects (<5 g),
  // flagged so the server excludes them. A missed second shot on 2026-09-04
  // was undiagnosable because a possible reject left no trace; a few bytes
  // of flash per false trigger buys that visibility.
  if (!spool_ok) {
    Serial.println("# spool unavailable — record NOT persisted");
  } else {
    String path = spool.writeShot(r, scales.slotMac(ScaleRole::YIELD),
                                  machine_ctx.context(), flow_tap_shot);
    if (path.length()) Serial.printf("# spooled %s%s\n", path.c_str(), r.valid ? "" : " (reject)");
  }
  Serial.println("# ---- end shot ----");
}

// Synthetic record straight into the spool — lets the spool/CLI/upload path
// be exercised without brewing. Clearly marked via detector fields being
// plausible-but-fake: dump it, then rm it.
static void spoolFakeShot(Stream& out) {
  static sample_t fake[220];
  for (int i = 0; i < 220; i++) {
    uint16_t t = i * 100;
    int32_t w = (i < 40) ? i * 25 : 1000 + (i - 40) * 200;   // slow start, then 2 g/s
    fake[i] = { t, w, 0, INT16_MIN };
  }
  ShotResult r = {};
  r.valid = true;
  r.started_at_ms = millis();
  r.stop_ms = 20000;
  r.yield_at_stop_mg = fake[219].weight_mg;
  r.yield_final_mg = fake[219].weight_mg;
  r.peak_flow_mgps = 2000;
  r.mean_flow_mgps = 1900;
  r.flow_win_start_ms = 3000;
  r.flow_win_end_ms = 18000;
  r.sample_count = 220;
  r.samples = fake;
  String path = spool.writeShot(r, scales.slotMac(ScaleRole::YIELD));
  out.printf("# fake shot: %s\n", path.length() ? path.c_str() : "WRITE FAILED");
}

static void handleCommand(String line, Stream& out) {
  line.trim();
  if (line.isEmpty()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);

  if (cmd == "help" || cmd == "h") {
    out.println("# status|s  tare|t  raw|r  verbose|v  display [on|off]  flow [on|off]  switches [on|off]  pins  selftest [n] [hz]  ls  dump <file>  rm <file>  fake  net  scales  assign <yield|dose> <mac|last>  set <ssid|pass|endpoint> <val>  reboot");
  } else if (cmd == "status" || cmd == "s") {
    out.printf("# yield: state=%d mac=%s | dose: state=%d mac=%s\n",
               (int)scales.slotState(ScaleRole::YIELD), scales.slotMac(ScaleRole::YIELD).c_str(),
               (int)scales.slotState(ScaleRole::DOSE),
               scales.slotMac(ScaleRole::DOSE).isEmpty() ? "(unbound)" : scales.slotMac(ScaleRole::DOSE).c_str());
    out.printf("# detector=%s flow=%ld mg/s | spool=%u pending | heap=%lu\n",
               stateName(detector.state()), (long)detector.flowNowMgps(),
               (unsigned)spool.pendingCount(), (unsigned long)ESP.getFreeHeap());
    if (display_on) printDisplay(out);
    printMachine(out);
  } else if (cmd == "display") {
    if (arg == "on" || arg == "off") {
      bool on = (arg == "on");
      Preferences p;
      p.begin("espl", false);
      p.putBool("display", on);
      p.end();
      displaySetEnabled(on);
    }
    printDisplay(out);
  } else if (cmd == "flow" || cmd == "switches") {
    // Persisted in NVS exactly like `display`: the taps go in one at a time,
    // and a reboot must not silently re-enable an input whose wire is out.
    bool is_flow = (cmd == "flow");
    if (arg == "on" || arg == "off") {
      bool on = (arg == "on");
      // Writing NVS is a flash write of tens of ms, which invariant 2 forbids
      // during a shot, and toggling the flow tap mid-pour would make the
      // record cover only part of the shot. Both reasons, one guard.
      ShotState st_now = detector.state();
      if (st_now == ShotState::POURING || st_now == ShotState::SETTLING) {
        out.println("# refused — a shot is in progress (NVS write + partial count)");
        printMachine(out);
        return;
      }
      Preferences p;
      p.begin("espl", false);
      p.putBool(is_flow ? "flow" : "switches", on);
      p.end();
      if (is_flow) { flowSetEnabled(on); flow_tap_shot = false; }
      else switchesSetEnabled(on);
    }
    printMachine(out);
  } else if (cmd == "pins") {
    printPins(out);
  } else if (cmd == "selftest") {
    int sp2 = arg.indexOf(' ');
    uint32_t n  = arg.isEmpty() ? 500 : (uint32_t)(sp2 < 0 ? arg : arg.substring(0, sp2)).toInt();
    uint32_t hz = sp2 < 0 ? 100 : (uint32_t)arg.substring(sp2 + 1).toInt();
    runSelfTest(out, n, hz);
  } else if (cmd == "tare" || cmd == "t") {
    out.printf("# tare: %s\n", scales.tare(ScaleRole::YIELD) ? "sent" : "not connected");
  } else if (cmd == "raw" || cmd == "r") {
    raw_stream = !raw_stream;
    out.printf("# raw stream %s\n", raw_stream ? "on" : "off");
  } else if (cmd == "verbose" || cmd == "v") {
    static bool verbose = false;
    verbose = !verbose;
    scales.setVerbose(verbose);
    out.printf("# verbose %s\n", verbose ? "on" : "off");
  } else if (cmd == "ls") {
    spool.list(out);
  } else if (cmd == "dump") {
    spool.dump(arg, out);
  } else if (cmd == "rm") {
    out.printf("# rm %s: %s\n", arg.c_str(), spool.remove(arg) ? "ok" : "failed");
  } else if (cmd == "fake") {
    spoolFakeShot(out);
  } else if (cmd == "net") {
    net.status(out);
  } else if (cmd == "scales") {
    scales.listDiscovered(out);
  } else if (cmd == "assign") {
    int sp2 = arg.indexOf(' ');
    String which = sp2 < 0 ? arg : arg.substring(0, sp2);
    String mac = sp2 < 0 ? "" : arg.substring(sp2 + 1);
    if ((which != "yield" && which != "dose") || mac.isEmpty()) {
      out.println("# usage: assign <yield|dose> <mac|last>");
    } else {
      scales.assign(which == "yield" ? ScaleRole::YIELD : ScaleRole::DOSE, mac, out);
    }
  } else if (cmd == "set") {
    int sp2 = arg.indexOf(' ');
    String key = sp2 < 0 ? arg : arg.substring(0, sp2);
    String val = sp2 < 0 ? "" : arg.substring(sp2 + 1);
    const char* nvs_key = key == "ssid" ? "wifi_ssid"
                        : key == "pass" ? "wifi_pass"
                        : key == "endpoint" ? "endpoint" : nullptr;
    if (!nvs_key || val.isEmpty()) {
      out.println("# usage: set <ssid|pass|endpoint> <value>");
    } else {
      Preferences p;
      p.begin("espl", false);
      p.putString(nvs_key, val);
      p.end();
      out.printf("# %s saved to NVS — 'reboot' to apply\n", nvs_key);
    }
  } else if (cmd == "reboot") {
    out.println("# rebooting");
    out.flush();
    ESP.restart();
  } else {
    out.printf("# unknown command: %s ('help' lists them)\n", cmd.c_str());
  }
}

// The CLI answers on both ports: USB CDC (Serial, also the data stream) and
// the UART bridge (Serial0, also the debug log) — whichever the cable is in.
static void pollSerial(Stream& in) {
  static String lines[2];
  String& line = lines[&in == (Stream*)&Serial ? 0 : 1];
  while (in.available()) {
    char c = in.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) handleCommand(line, in);
      line = "";
    } else if (line.length() < 96) {
      line += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);   // UART bridge (COM port): debug log + CLI
  delay(2000);   // native USB CDC needs a moment before the first lines land
  Serial.printf("# espressolog %s (detector %s)\n", FIRMWARE_VERSION, DETECTOR_VERSION);

  // 4th arg: our partition is labeled "littlefs", not the wrapper's default "spiffs"
  if (!LittleFS.begin(true /* format on first use */, "/littlefs", 10, "littlefs")) {
    Serial.println("# LittleFS: MOUNT FAILED — shots will NOT be persisted");
  } else {
    spool_ok = spool.begin();
    Serial.printf("# LittleFS: %u/%u KB used, spool %s, %u pending\n",
                  (unsigned)(LittleFS.usedBytes() / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024),
                  spool_ok ? "ready" : "INIT FAILED",
                  (unsigned)spool.pendingCount());
  }

  sample_queue = xQueueCreate(64, sizeof(ScaleSample));
  scales.begin(sample_queue);
  net.begin(&spool);

  {
    Preferences p;
    p.begin("espl", true);
    bool on = p.getBool("display", false);
    bool fl = p.getBool("flow", false);
    bool sw = p.getBool("switches", false);
    p.end();
    if (on) displaySetEnabled(true);
    if (fl) flowSetEnabled(true);
    if (sw) switchesSetEnabled(true);
    Serial.printf("# display bus %s (clk=GPIO%u data=GPIO%u)\n",
                  on ? "on" : "off (see 'display on')",
                  DISPLAY_CLK_PIN, DISPLAY_DATA_PIN);
    Serial.printf("# flow %s (GPIO%u) | switches %s (1cup=GPIO%u 2cup=GPIO%u steam=GPIO%u water=GPIO%u)\n",
                  fl ? "on" : "off", FLOW_PIN, sw ? "on" : "off",
                  SW_PINS[0], SW_PINS[1], SW_PINS[2], SW_PINS[3]);
  }

  Serial.println("# scanning for Bookoo… ('help' for commands)");
}

// Headless status glance: the DevKit's onboard RGB LED shows whether a shot
// would be captured right now. Off = cup scale not connected; amber = idle
// (settling toward a baseline); green = armed; blue = shot in progress.
// Dim on purpose — it lives in a kitchen. Boards with the LED on a
// different pin than the variant defines simply stay dark; nothing depends
// on it.
static void updateLed() {
#ifdef RGB_BUILTIN
  static uint8_t last = 255;
  uint8_t mode;
  if (!scales.connected(ScaleRole::YIELD)) mode = 0;
  else switch (detector.state()) {
    case ShotState::ARMED:   mode = 2; break;
    case ShotState::POURING:
    case ShotState::SETTLING: mode = 3; break;
    default:                 mode = 1; break;
  }
  if (mode == last) return;
  last = mode;
  switch (mode) {
    case 0: neopixelWrite(RGB_BUILTIN, 0, 0, 0); break;
    case 1: neopixelWrite(RGB_BUILTIN, 12, 6, 0); break;   // amber: idle
    case 2: neopixelWrite(RGB_BUILTIN, 0, 14, 0); break;   // green: armed
    case 3: neopixelWrite(RGB_BUILTIN, 0, 4, 16); break;   // blue: pouring
  }
#endif
}

static void queueWeighing(uint8_t role, const WeighingDetector::Event& ev) {
  Serial.printf("# weighing[%s]: %ld mg stable %lu ms\n",
                role == 0 ? "yield" : "dose", (long)ev.grams_mg, (unsigned long)ev.stable_ms);
  if (pending_w_n >= sizeof(pending_w) / sizeof(pending_w[0])) {
    Serial.println("# weighing queue full — event dropped");
    return;
  }
  pending_w[pending_w_n++] = { role, ev };
}

void loop() {
  // Before the drain below: every sample is tagged with flow.total(), so the
  // count must be current as of this iteration, not the previous one.
  pollFlow();

  ScaleSample smp;
  while (xQueueReceive(sample_queue, &smp, 0) == pdTRUE) {
    bool shot_active = detector.state() == ShotState::POURING
                    || detector.state() == ShotState::SETTLING;

    char frame[96];
    if (smp.role == (uint8_t)ScaleRole::YIELD) {
      ShotState before = detector.state();
      bool complete = detector.feed(smp.t_ms, smp.weight_mg, flow.total());
      if (before != ShotState::POURING && detector.state() == ShotState::POURING) {
        machine_ctx.onPourStart(micros());
        // Latch the tap state for THIS shot. See flow_tap_shot.
        flow_tap_shot = flow_on;
        // The bus latch is per-boot; a new shot starts with a clean slate,
        // otherwise one tweak at the start of a session condemns every
        // shot after it.
        display_bus.clearSetpointTouched();
      }
      if (raw_stream) {
        Serial.printf("%lu,%ld,%s\n", (unsigned long)smp.t_ms,
                      (long)smp.weight_mg, stateName(detector.state()));
      }
      if (detector.state() != before && !complete) {
        Serial.printf("# state %s -> %s\n", stateName(before), stateName(detector.state()));
      }
      snprintf(frame, sizeof(frame), "{\"r\":0,\"t\":%lu,\"w\":%ld,\"s\":\"%s\"}",
               (unsigned long)smp.t_ms, (long)smp.weight_mg, stateName(detector.state()));
      net.sendLive(frame);
      if (complete) {
        handleShotComplete(detector.result());
        snprintf(frame, sizeof(frame), "{\"ev\":\"shot\",\"valid\":%d,\"fault\":%d}",
                 detector.result().valid, detector.result().fault);
        net.sendLive(frame);
      }
      shot_active = detector.state() == ShotState::POURING
                 || detector.state() == ShotState::SETTLING;
    } else {
      if (raw_stream) {
        Serial.printf("%lu,%ld,DOSE\n", (unsigned long)smp.t_ms, (long)smp.weight_mg);
      }
      snprintf(frame, sizeof(frame), "{\"r\":1,\"t\":%lu,\"w\":%ld}",
               (unsigned long)smp.t_ms, (long)smp.weight_mg);
      net.sendLive(frame);
    }

    // Every stable reading on any scale becomes an anonymous weighing —
    // attribution is the server's job when a shot closes.
    if (weighing[smp.role].feed(smp.t_ms, smp.weight_mg, shot_active)) {
      queueWeighing(smp.role, weighing[smp.role].event());
    }
  }

  ShotState st = detector.state();
  bool quiet = st == ShotState::IDLE || st == ShotState::ARMED;

  if (quiet && spool_ok) {
    while (pending_w_n > 0) {
      pending_w_n--;
      String path = spool.writeWeighing(pending_w[pending_w_n].role,
                                        pending_w[pending_w_n].ev,
                                        scales.slotMac((ScaleRole)pending_w[pending_w_n].role));
      if (path.length()) Serial.printf("# spooled %s\n", path.c_str());
    }
  }

  pollSwitches();
  pollDisplay();
  machineContextPoll();
  scales.tick(millis());
  // Network (and its flash reads) only while nothing is brewing.
  net.tick(millis(), quiet);
  updateLed();
  pollSerial(Serial);
  pollSerial(Serial0);
  delay(5);   // ~10 Hz data; nothing here needs a tighter spin
}
