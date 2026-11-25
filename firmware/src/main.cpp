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
#include "live_display.h"
#include "machine_io.h"
#include "brew_guard.h"
#include "display_watch.h"
#include "drive_bench.h"
#include <soc/gpio_reg.h>
#include <esp_timer.h>
#include <driver/gpio.h>

// 0.15.2 (main): the bench `drive` command reads the 1Cup photoMOS 1 Q on
// GPIO11 (GPIO1 up to 0.15.1). 30beb28 moved the pin while still reporting
// 0.15.1, so a 0.15.1 built from that commit already reads GPIO11. 0.16.0
// is the same story: built before main was merged into this branch (what
// the board was flashed with on 2026-11-23) it reads GPIO1, built after it
// GPIO11 -- and 0.16.1 (the timer fix), built from a branch that left
// before that merge, reads GPIO1 until it is rebased onto it. The version
// number does not say which: `drive status` prints the pin. Not bumped for
// it: the PWA trusts timers from 0.16.1 up (the timer fix,
// web/src/components.jsx), so a number at or above that must carry it.
const char* FIRMWARE_VERSION = "0.16.0";
const char* DETECTOR_VERSION = "0a.8";

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
//                         +--[ 15K ]-- GND   <- AS BUILT; use 16.4K (2x8.2K)
//                                               on the perfboard, see below
//
// Safe whichever way the line is driven AT NOMINAL -- the corner is a
// separate question and 15K fails it, see below. WITH 15K: 3.23 V at the pin if push-pull,
// 2.69 V if open-drain through the board's 4.7K pull-ups (VIH is 2.48 V), and
// the machine's own high never falls below 4.16 V against a display needing
// 3.5 V. WITH THE SPECIFIED 16.4K: 3.33 V push-pull (above VDD, but the
// clamp is off -- see docs/phase-0-status.md) and 2.80 V open-drain.
// Do NOT enable an internal pull-down — 45K in parallel with the 15K
// leg drops the open-drain case to 2.33 V and the pin stops reading high
// (2.41 V at 16.4K; the warning holds either way).
//
// 2026-10-19: the 15K shunt FAILS its tolerance corner at -70 to -87 mV (the
// spread is the unmetered 4.7K pull-up's tolerance; quote the pessimistic
// fact:divider-j5-shunt@1 16.4 K
// figure). FIT 16.4K -- two 8.2K in series, +18 mV, and the clamp is not
// conducting at the push-pull corner. There, the internal-pull-down case is
// 2.41 V rather than 2.33 V -- still under VIH, so the warning above holds,
// but 2.33 V is 15K arithmetic and does not carry across.
//
// The 4.16 V above is CALCULATED, never metered, and whether this line is
// open-drain or push-pull has never been tested. Worth settling (open-drain
// would free 18-22K) but not blocking. Meter it UNPOWERED in ohms: J5 pin 2
// or 3 against J2 POSITION 1, which is where the +5 V is -- J5 itself has
// none, and ohming against "5 V" without that reference returns a false null
// that looks like push-pull. A DC reading on the live line is no good
// either; it switches. See docs/phase-0-status.md.
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
static constexpr uint16_t DBUS_RING = 512;      // 511 usable slots: ~50 ms of a burst at 9.9 kHz, but the bus
                                                 // averages 3244 edges/s (133 per 41 ms), so a loop() stall must
                                                 // exceed ~158 ms to drop edges (docs/phase-0-status.md)
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

// What the machine contributes to a shot record (format v2). The logic
// lives in machine_context.h so it can be host-tested — the first version
// of this sat inline here, had no tests, and shipped two bugs a green
// suite could not see: a previous shot's timer leaking into the next, and
// setpoint_touched staying true for the whole boot.
static MachineContextTracker machine_ctx;

// Idempotent, like flowSetEnabled and switchesSetEnabled. `display on` while
// already on zeroes the frame and drop counters, which is the only way to get
// a per-event figure out of them: both are free-running since boot and nothing
// else resets them. 0.9.0 returned early here, so the written experiment for
// the dropped-frame question — "run a flush and see whether dropped stays near
// zero" — could not be performed at all without a reboot. That defect was
// fixed in flowSetEnabled in the same commit and left standing in the one path
// the open question depends on.
//
// Interrupt detached before the ring is zeroed, for the same race reason.
static uint32_t frames_seen = 0;      // dbus_frames as machineContextPoll last saw it
static uint32_t dbus_watch_ms = 0;    // millis() when the tap was last switched on (not micros:
                                      // a 71-minute wrap broke the silence judgement)
static DisplayWatch dwatch;            // v6: the first-activity judgement (display_watch.h)
static uint32_t ctx_last_poll_ms = 0; // when machineContextPoll last ran

static void displaySetEnabled(bool on) {
  const bool was_on = display_on;
  if (display_on) detachInterrupt(digitalPinToInterrupt(DISPLAY_CLK_PIN));
  if (on) {
    pinMode(DISPLAY_CLK_PIN, INPUT);
    pinMode(DISPLAY_DATA_PIN, INPUT);
    dbus_head = dbus_tail = 0;
    dbus_frames = dbus_dropped = 0;
    frames_seen = 0;   // else the zeroing above reads as a new frame (review 2026-11-22)
    dwatch.enable(millis());   // every enable: the ring was just cleared
    display_bus.beginCapture();   // drop the in-progress frame; see display.h
    attachInterrupt(digitalPinToInterrupt(DISPLAY_CLK_PIN), dbusIsr, RISING);
  }
  display_on = on;
  // Off, on or re-attached: the machine may have been edited while nothing
  // watched, so a remembered setpoint is no longer the machine's.
  machine_ctx.forgetSetpoint();
  // And whether the machine was switched on while nothing watched (v6) --
  // only when the tap actually changed: `display on` while on (the counter
  // reset) must not turn an exact on-time into a lower bound.
  // The watch start is moved only on a real change, for the same reason.
  if (on != was_on) {
    dbus_watch_ms = millis();
    machine_ctx.watchBusAgain(dbus_watch_ms);
  }
}

static void pollDisplay() {
  if (!display_on) return;
  const uint32_t drain_start = millis();
  while (dbus_tail != dbus_head) {
    DbusEdge e = { dbus_ring[dbus_tail].t_us, dbus_ring[dbus_tail].bit };
    dbus_tail = (uint16_t)((dbus_tail + 1) % DBUS_RING);
    if (display_bus.feedEdge(e.t_us, e.bit)) dbus_frames++;
  }
  if (display_bus.poll(micros())) dbus_frames++;   // bus went quiet mid-frame
  // v6: feed the first-activity judgement once per drain (display_watch.h).
  uint32_t first_us = 0;
  const bool have_first = display_bus.firstFrameUs(&first_us);
  const uint32_t now = millis();
  dwatch.afterDrain(drain_start, now, dbus_dropped, have_first,
                    have_first ? now - (micros() - first_us) / 1000 : 0);
}


static void machineContextPoll() {
  if (!display_on) return;
  // v6: any decoded frame means the machine is on (onBusFrame), which also
  // needs the previous poll's time: only silence a poll witnessed counts.
  const uint32_t now = millis();
  const uint32_t prev = ctx_last_poll_ms;
  ctx_last_poll_ms = now;
  if (dbus_frames != frames_seen) {
    frames_seen = dbus_frames;
    if (!machine_ctx.busSeen() && dwatch.first_noted) {
      // The first frame since watching started, judged on its OWN time
      // (0.15.1): a power-up is frame-silent until +4.1 s, a running
      // machine is not. Logged so one boot on the strip shows the margin.
      // Judged in pollDisplay(), where the drops are known (display_watch.h).
      machine_ctx.onFirstFrame(dwatch.first_frame_ms, dwatch.first_silence, now);
      Serial.printf("# machine: first display activity %lu ms after watching started, %lu ms of whole capture before it -> %s\n",
                    (unsigned long)(dwatch.first_frame_ms - dbus_watch_ms), (unsigned long)dwatch.first_silence,
                    dwatch.first_silence >= MachineContextTracker::POWERUP_SILENCE_MS
                    ? "switched on (on-time exact)" : "already running, or not enough seen (on-time a lower bound)");
    } else {
      machine_ctx.onBusFrame(now, prev);
    }
  }
  machine_ctx.onReading(display_bus.reading(), display_bus.setpointTouched());
}

// How fresh a display reading must be to be believed about the CURRENT state.
// The bus frames every ~41 ms, so 2 s is ~50 frames of slack.
//
// NOT MachineContextTracker::BOILER_MAX_AGE_US, which a draft borrowed because
// it was the constant to hand. That is a DATA-ATTRIBUTION bound — "how old a
// pre-shot temperature may be and still be called this shot's", deliberately
// 60 s — and reusing it coupled two unrelated decisions.
//
// The micros() aliasing in DisplayBus::stale() is REDUCED, NOT REMOVED: a
// last-seen TIMER frame still reads fresh for 2 s out of every 71.58 min.
// Nothing below may take an irreversible action on that, which is why the
// press guards are no longer cleared here — see machineBusy().
static constexpr uint32_t DISPLAY_FRESH_US = 2000000;

// The machine display on the live stream, for the PWA. Radio only, no flash,
// so it is safe during a shot (invariant 2). The rules live in
// live_display.h, where they are host-tested.
static LiveDisplayGate live_display;

static void liveDisplay(uint32_t now_ms) {
  if (!display_on) return;
  char frame[128];
  if (live_display.poll(display_bus.reading(), display_bus.stale(micros(), DISPLAY_FRESH_US),
                        net.liveUp(), now_ms, frame, sizeof(frame))) {
    if (net.sendLive(frame)) live_display.sent(now_ms);
    else                     live_display.failed(now_ms);
  }
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
                   : r.mode == DisplayBus::Mode::TARGET      ? "target"
                   : r.mode == DisplayBus::Mode::BLANK       ? "blank"
                   : r.mode == DisplayBus::Mode::TEXT        ? "text" : "none";
  out.printf("# display=[%s] mode=%s", r.text, mode);
  if (r.mode == DisplayBus::Mode::TEMPERATURE) out.printf(" %d C", (int)r.temp_c);
  else if (r.mode == DisplayBus::Mode::SETPOINT) out.printf(" %d C (being dialled)", (int)r.temp_c);
  else if (r.mode == DisplayBus::Mode::TARGET)   out.printf(" %d C (target, slow blink)", (int)r.temp_c);
  else if (r.mode == DisplayBus::Mode::TIMER)  out.printf(" %u.%u s", r.timer_dl / 10, r.timer_dl % 10);
  if (display_bus.adjusting())       out.print(" ADJUSTING");
  if (display_bus.programming())     out.print(" PrG");
  if (display_bus.setpointTouched()) out.print(" SETPOINT-TOUCHED");
  if (display_bus.stale(micros(), DISPLAY_FRESH_US)) out.print(" (STALE)");

  const DisplayBus::Buttons& b = display_bus.buttons();
  out.printf(" | buttons=%s%s", b.left ? "L" : "-", b.right ? "R" : "-");
  // UNITS DIFFER AND THEY ARE PRINTED SIDE BY SIDE: frames are completed
  // frames (~66-133 bits each), dropped are individual ISR edges. Comparing
  // them directly reads a ~1 % loss as a ~100 % one, which is exactly the
  // mistake made on 2026-10-08. Labelled rather than left to the reader.
  // Snapshot once so the printed counts and the percentage are consistent.
  // dbus_dropped is volatile (the ISR writes it); dbus_frames is not, but
  // reading both once is what keeps the three numbers from coming out of
  // different ISR states, which is widest during the storm that makes anyone
  // read this line.
  uint32_t fr = dbus_frames, dr = dbus_dropped;
  // ~128 bits/frame. The merged pair is 67+66 = 133, and the pair splits into
  // two separately-counted frames some of the time — CLAUDE.md measures 1.18 %
  // idle but 3.62-6.56 % during a flush and settling, which is exactly when
  // this is read, so 133/(1+p) lands near 128. The old 100 overstated the loss
  // by ~31 %. 64-bit because fr*128 overflows uint32 past ~33 M frames.
  // GUARD ON EITHER COUNTER. Guarding on frames alone printed "~0 %" when
  // frames==0 and dropped was climbing — which is precisely the leading
  // hypothesis for the open display-bus question (a noisy GPIO4 firing so
  // fast no frame ever completes). That under-reads total loss as zero, the
  // same direction as the units error this line was written to prevent.
  uint64_t edges = (uint64_t)fr * 128ULL + dr;
  // Upper bound, not exact: under the leading hypothesis for the open
  // dropped-edge question (a GPIO4 so noisy no frame completes) most dropped
  // edges belong to no frame at all, so this denominator understates the true
  // edge count and the printed percentage reads high.
  out.printf(" | frames=%lu dropped_edges=%lu (~%lu%% of bus)\n",
             (unsigned long)fr, (unsigned long)dr,
             edges ? (unsigned long)(((uint64_t)dr * 100ULL) / edges) : 0UL);
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
// firmware). The silkscreen check is DONE (2026-10-12) and the pin map is
// confirmed label-to-die for all seven pins -- see CLAUDE.md's GPIO budget,
// which is normative. This comment told the reader to confirm it for four
// days after it was closed; do not re-add an imperative here, point at
// CLAUDE.md instead.
//
// NEVER enable an internal pull-up or pull-down on any of these. The taps are
// resistor dividers in the hundreds of kilohms, and the ESP32's internal
// resistors are ~45 kR — they would sit across the shunt leg and collapse the
// level. The display bus carries the same warning for the same arithmetic.
static constexpr uint8_t FLOW_PIN = 6;                      // J4 `#`, via 100K + 220K
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
// `flow off` mid-pour discards real counts.
//
// The command handler REFUSES while POURING or SETTLING, so the tap cannot in
// fact change mid-shot; the clear there is belt-and-braces for a future caller
// that does not go through the CLI. An earlier version of this comment said
// the clear was what made mid-shot changes safe, crediting a safety property
// to the wrong mechanism.
static bool flow_tap_shot = false;

// Staleness on millis(), not micros(). PulseCounter::sinceLastUs is exact but
// wraps at ~71 min, so an overnight idle on a dead tap reported a few minutes
// and read as healthy. millis() wraps at ~49.7 days, which covers every span
// this diagnostic is asked about.
static uint32_t flow_last_ms = 0;
// Every counted pulse's millis() time, for the press-to-confirmation pulse train each
// shot carries (spool v4, machine_context.h capturePrePulses()). ~2 KB.
static PulseLog pulse_log;
// The last flow_dropped value pollFlow() saw, to notice the ISR losing
// pulses. File scope so flowSetEnabled() can zero it with flow_dropped.
static uint32_t flow_dropped_seen = 0;
static bool     flow_ever = false;

// Same producer/consumer split as the display bus: the ISR stamps and
// enqueues, loop() is the only consumer, and the glitch filter runs in the
// pure class so the logic that ships is the logic the host tests exercise.
// 64 entries is ~1.1 s at the fastest plausible real rate (~59 Hz, a
// hot-water draw at the WITHDRAWN 5900-6500 pulses/L; at the datasheet's 2382 it is ~16 Hz — it was ~24 Hz against the
// old ~2400 figure) and the loop drains every ~5 ms, so an overflow here
// does not mean
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

// IDEMPOTENT ON PURPOSE: `flow on` while already on still zeroes the
// diagnostics, because that is how an operator gets a clean slate between
// bench runs. Up to 0.9.0 this opened with `if (on == flow_on) return;`, so
// the reset never ran in the one case anyone would type it for — the documented clean
// slate silently did nothing and a stale glitch count read as current.
// Found at the bench 2026-10-08 while calibrating, after `flow on` left
// glitches=1384 standing and the next reading was interpreted against it.
//
// The interrupt is detached before the ring is zeroed, in both paths. Zeroing
// flow_head/flow_tail while the ISR can still fire is a race that would drop
// or duplicate an edge, and re-running this on a live tap is now a reachable
// case rather than a hypothetical one.
static void flowSetEnabled(bool on) {
  if (flow_on) detachInterrupt(digitalPinToInterrupt(FLOW_PIN));
  if (on) {
    pinMode(FLOW_PIN, INPUT);
    flow_head = flow_tail = 0;
    // Same rationale as switches.reset(): the taps go in one at a time, and
    // counts racked up by an earlier `flow on` against a floating pin describe
    // a tap that is no longer there. Without this the glitch warning below
    // latches on for ~16 shots, and `flow_ever` never lets status print
    // "NEVER" again — the one message that identifies a dead, freshly-refitted
    // J4. flow_dropped is cleared for the same reason.
    flow.resetDiagnostics();
    pulse_log.clear(millis());   // pulses before a re-enable describe another tap state
    flow_dropped = 0;
    flow_dropped_seen = 0;   // or a later drop count matching the old one goes unseen
    flow_ever = false;
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowIsr, FALLING);
  }
  flow_on = on;
}

static uint32_t sw_last_poll = 0, sw_max_gap = 0;
// A command that deliberately blocks loop() for seconds (selftest, drive)
// calls this when it ends, so the block is not recorded as a `worst poll
// gap` -- the number that diagnoses dropped presses and J5 frame drops.
static void noteLoopBlocked() { sw_last_poll = 0; }

// Every character an ASCII digit (true for ""). The cast keeps a pasted
// non-ASCII byte out of isdigit()'s undefined range.
static bool isAllDigits(const String& v) {
  for (unsigned i = 0; i < v.length(); i++)
    if (!isdigit((unsigned char)v[i])) return false;
  return true;
}

// Idempotent for the same reason as flowSetEnabled above: re-running
// `switches on` is how state adopted from a floating line gets dropped, and
// returning early when already on made that a no-op.
static void switchesSetEnabled(bool on) {
  if (on) {
    for (uint8_t i = 0; i < SwitchBank::N_LINES; i++) pinMode(SW_PINS[i], INPUT);
    // The taps go in one at a time, so an earlier `switches on` against an
    // unwired line may have adopted a floating high. Carrying that across the
    // rewire made the first poll afterwards emit a release whose duration was
    // the hours in between — see SwitchBank::reset().
    switches.reset();
    machine_ctx.clearPresses();
    // Both, not just the first: sw_max_gap is only ever raised, so leaving it
    // made `switches on` reset the debounce state but not the diagnostic the
    // reset exists to give a clean slate for. 0.9.0 cleared only sw_last_poll
    // and the write-up still called the path idempotent.
    sw_last_poll = sw_max_gap = 0;
  }
  switches_on = on;
}

// Called at the TOP of loop(), before the scale-sample drain that tags each
// sample with flow.total(). Running it at the bottom (where 0.9.0 first put
// it) left every sample carrying the PREVIOUS iteration's count; now it
// carries this one's.
//
// Note what this does NOT fix: a backlog drained in a single iteration still
// shares one flow.total() snapshot, so a stall still yields a plateau and
// then a jump. The move relocates that discontinuity from after the backlog
// to before it; it does not shrink it. An earlier version of this comment
// claimed the staircase was the thing being fixed, which overstated it.
static void pollFlow() {
  if (!flow_on) return;
  // The ISR's ring overflowed since the last drain: pulses were lost before
  // they could reach the log, so a pre-drip window open now is incomplete.
  uint32_t dropped_now = flow_dropped;
  if (dropped_now != flow_dropped_seen) {
    flow_dropped_seen = dropped_now;
    pulse_log.markGap(millis());
  }
  while (flow_tail != flow_head) {
    uint32_t t_us = flow_ring[flow_tail];
    flow_tail = (uint16_t)((flow_tail + 1) % FLOW_RING);
    if (flow.feedEdge(t_us)) {
      flow_last_ms = millis();
      flow_ever = true;
      // The edge's own time on the millis() base the press and t=0 use:
      // millis() now, less how long ago the ISR stamped it. Not t_us / 1000 —
      // micros() wraps every 71.6 min and millis() does not wrap with it.
      pulse_log.push(flow_last_ms - (uint32_t)(micros() - t_us) / 1000);
    }
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

// ------------------------------------------------------- is the pump running?
// The detector cannot answer this. It is scale-driven, so it says IDLE from
// pump-on until first drip plus POUR_HOLD_MS, which is exactly the window
// invariant 2 cares about. The machine itself answers it two ways.
//
// PREFERRED: the display shows its brew timer. CLAUDE.md establishes that
// Mode::TIMER IS the machine's own pump-on to pump-off measurement — it
// starts when the pump starts and stops when it stops, including when a
// cycle is cut short by hand. That is exactly the window we must not write
// flash or block on HTTP in, with no guard time needed at either end.
//
// FALLBACK: a 1Cup/2Cup/Water press, for when the J5 tap is not fitted or
// the bus has gone stale. Then a press is the only start-of-cycle signal
// there is, and the stored duration is user-modifiable state we do not know
// — 1Cup is ~10.5 s on this machine and 2Cup is 45 s (operator, set
// deliberately, 2026-10-19; the factory default is 60 s). This comment said
// 2Cup was UNKNOWN until then.
//
// NEITHER IS THE BINDING CASE. Lever B latches the same way and a hot-water
// draw runs for "a couple of minutes" before the machine self-terminates, so
// fact:brew-guard-ms 75 s
// BREW_GUARD_MS (75 s) CAN EXPIRE WITH THE PUMP STILL RUNNING. See
// brew_guard.h.
//
// So the guard is generous for 1Cup/2Cup (10.5 s and 45 s stored) and
// INSUFFICIENT for a lever-B draw. It is still deliberately long: the cost of
// expiring early is a flash write during a pour, the cost of being long is
// only that an upload waits a minute. Fixing the lever-B case needs an
// edge-driven guard WITH an upper bound -- see the latched-busy-forever
// comment in brew_guard.cpp for why the bound is not optional -- and the
// self-terminate period measured first.
static BrewGuard brew_guard;

// Gather what the guard needs and ask it. main.cpp owns the pin reads, the
// clock and the NVS tap flags; brew_guard.h owns the decision — see the header
// for why that split exists and what it cost to learn.
static bool machineBusy() {
  BrewGuard::Inputs in;
  in.switches_on = switches_on;
  in.display_usable =
      display_on && !display_bus.stale(micros(), DISPLAY_FRESH_US);
  in.display_timer = display_bus.reading().mode == DisplayBus::Mode::TIMER;

  uint32_t now = millis();
  if (switches_on) {
    for (uint8_t i = 0; i < SwitchBank::N_LINES; i++) {
      in.pressed[i]   = switches.pressed(i);
      in.synthetic[i] = switches.pressedSynthetic(i);
      in.held_ms[i]   = switches.heldMs(i, now);
    }
  }
  return brew_guard.busy(now, in);
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
  while (switches.popEvent(e)) {
    // 1Cup, 2Cup and Water all run the pump. Remember when, because the
    // detector will not know for several seconds — see machineBusy().
    // Backdate to the press, not the release: the pump started then.
    // Ignore synthetic events — a line already high at the first poll was
    // never witnessed going high, and this file refuses to treat that as
    // evidence everywhere else (it forces `reprogram` false and prints
    // `high?` rather than `PRESSED`). A floating line settling must not arm
    // a 75 s upload block.
    if (!e.synthetic && e.line < SwitchBank::N_LINES && e.line != SwitchBank::STEAM) {
      brew_guard.onRelease(e.line, e.pressed_at_ms);
      // And remember it for the shot record (spool v3): which lever started
      // the pump, and when. Same filter, same reason.
      // v6: and how long the pump had stood idle before it, while the flow
      // tap is on to say (the pulse log is empty otherwise).
      uint32_t idle_ms = PUMP_IDLE_NONE;
      if (!flow_on || !pulse_log.idleBefore(e.pressed_at_ms, &idle_ms)) idle_ms = PUMP_IDLE_NONE;
      machine_ctx.onPumpPress(pumpLineForSwitch(e.line), e.pressed_at_ms, idle_ms);
    }
    reportSwitch(e);
  }
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
  // REFUSE UNLESS IDLE. An earlier version allowed ARMED, which is the worst
  // state to allow: ARMED is cup-on-scale, tared, about to pull. This blocks
  // loop() for seconds, the 64-deep sample queue holds ~3.2 s at 2x10 Hz and
  // xQueueSend drops silently, so a pour started during the run arrives as a
  // stale burst across a multi-second hole — the detector either misses the
  // onset or confirms it from a backlog with a fabricated flow rate. That is
  // invariant 6 reached by the same non-flash route the shot guard was added
  // for, and checking only at entry did not cover it.
  if (detector.state() != ShotState::IDLE) {
    out.println("# selftest: REFUSED — the scale is armed or a shot is running."
                " Take the cup off the scale first.");
    return;
  }
  if (pulses < 1 || pulses > 5000) pulses = 500;
  // Cap the blocking time rather than silently substituting a rate. An
  // earlier version floored hz at 20 FIRST, which quietly rewrote the very
  // example this comment cites — `selftest 500 1`, an easy argument-order
  // slip — into a 5 s run at 100 Hz, and also turned `selftest 100 5` (5 Hz,
  // the realistic espresso rate and the most useful bench case) into
  // something else. Refusing and saying why is strictly better than running
  // a different test than the one asked for.
  if (hz < 1 || hz > 400) hz = 100;
  if (pulses / hz > 30) {
    out.printf("# selftest: REFUSED — %lu pulses at %lu Hz blocks loop() for ~%lu s (max 30)."
               " Try fewer pulses or a higher rate.\n",
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
  // NO mid-run abort here, deliberately. An attempt at one re-checked
  // detector.state() inside this loop — it could never have fired:
  // detector.feed() is reached only from the sample drain in loop(), which is
  // exactly what this busy-wait is blocking, so the state is frozen for the
  // duration. scales.tick() only enqueues. A guard that cannot reach its own
  // condition is worse than no guard, because it reads as coverage.
  //
  // What actually bounds the risk is the IDLE-only entry check plus the 30 s
  // cap: a shot cannot begin without weight on the scale, and weight on the
  // scale means not IDLE. The residual case — cup placed AND pour started
  // entirely within the run — is left open and is the reason this stays a
  // deliberate bench command rather than anything automatic.
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
  noteLoopBlocked();                    // this block was not a loop() stall
  // Its synthetic pulses are not a pump run: a shot's idle time (v6) must
  // not be measured from them (review 2026-11-22).
  pulse_log.clear(millis());
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

// Every machine-facing sense pin, once: `pins` prints them, makeTapPinsReadable
// configures them, and the `drive` refusal watches them. A new tap pin goes
// here and nowhere else, so it cannot be printed but missed by the refusal.
// `group` is the connector, which is where `pins` puts its " | " separators.
struct TapPin { const char* name; uint8_t pin; bool display; char group; };
static const TapPin TAP_PINS[] = {
  { "flow",  FLOW_PIN,         false, '4' },
  { "1cup",  SW_PINS[0],       false, '2' },
  { "2cup",  SW_PINS[1],       false, '2' },
  { "steam", SW_PINS[2],       false, '2' },
  { "water", SW_PINS[3],       false, '2' },
  { "clk",   DISPLAY_CLK_PIN,  true,  '5' },
  { "data",  DISPLAY_DATA_PIN, true,  '5' },
};

// Every sense pin INPUT, so gpio_get_level() reads the pad: `pins` and the
// `drive` refusals both need it.
static void makeTapPinsReadable() {
  // The display pins too, but ONLY when the bus is off — they are configured
  // by displaySetEnabled otherwise, and there is no reason to touch a pad with
  // a live interrupt on it. Without this, `pins` printed clk=0 data=0 whenever
  // the display tap was off: an ESP32-S3 pad comes out of reset with its input
  // buffer disabled, so gpio_get_level() reads 0 regardless of pin voltage.
  // CLAUDE.md records both J5 lines as idling HIGH on 4.7 K pull-ups, so that
  // output read as a dead tap and would have sent the operator re-metering a
  // working harness — from the command whose own comment calls it the first
  // thing to run at the machine.
  for (const TapPin& t : TAP_PINS)
    if (!t.display || !display_on) pinMode(t.pin, INPUT);
  delayMicroseconds(100);   // a pad just switched to INPUT settles on its divider
}

static void printPinLevels(Stream& out) {
  out.print("# pins:");
  for (size_t i = 0; i < sizeof(TAP_PINS) / sizeof(TAP_PINS[0]); i++) {
    const TapPin& t = TAP_PINS[i];
    bool new_group = i > 0 && t.group != TAP_PINS[i - 1].group;
    out.printf("%s%s/GPIO%u=%d", new_group ? " | " : " ", t.name, t.pin,
               gpio_get_level((gpio_num_t)t.pin));
  }
  out.print("\n");   // LF, as `pins` always ended this line
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
  makeTapPinsReadable();
  printPinLevels(out);
  // `#` does not reliably idle high: the open collector parks on or off with
  // the turbine (CLAUDE.md, 0d). This said "flow=1" up to 0b76f40; 27ff1e6
  // changed it while still reporting 0.13.1, so 0.13.1 is ambiguous here and
  // 0.13.2 is the first version that always prints "flow=0 or 1".
  out.println("# expected at rest, on the machine: flow=0 or 1 (it parks either way),"
              " all four switches=0 (idle low, a press reads 1), clk=1 data=1"
              " (either can read 0 mid-frame while the bus runs)");
}

static void printMachine(Stream& out) {
  if (!flow_on) {
    out.println("# flow: off — 'flow on' once the J4 tap is wired."
                " READ docs/phase-0-status.md FIRST: the 2026-10-03 tap faulted the machine (E01).");
  } else {
    out.printf("# flow: pulses=%lu glitches=%lu dropped=%lu filter=%luus last=",
               (unsigned long)flow.total(), (unsigned long)flow.glitches(),
               (unsigned long)flow_dropped, (unsigned long)flow.minPeriodUs());
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
      bool syn = switches.pressedSynthetic(i);
      out.printf(" %s=%s", SwitchBank::lineName(i),
                 switches.pressed(i) ? (syn ? "high?" : "PRESSED") : "idle");
      uint32_t held = switches.heldMs(i, now);
      // >=, matching the event path in machine_io.cpp — 0.9.0 had > here and
      // >= there, so a line held at exactly 2000 ms disagreed with itself.
      // Suppressed on an unwitnessed press for the same reason the event path
      // suppresses the REPROGRAM alarm: "(3600s!)" on a line that merely read
      // high at boot sends the operator hunting a stuck lever. `high?` says
      // what was actually observed.
      if (!syn && held >= SwitchBank::REPROGRAM_MS)
        out.printf("(%lus!)", (unsigned long)(held / 1000));
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
  if (r.placement)
    Serial.println("# NOT A SHOT: rose >20 g above its earlier peak within 1 s — a cup set down. Recorded, flagged, excluded.");
  if (r.disturbed)
    Serial.println("# DISTURBED: something landed on the scale (>5 g above the peak). Kept out of the yield if it landed while settling; mid-pour or after a forced stop it is only flagged. Recorded.");
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

// A FINISHED SHOT IS HELD, THEN WRITTEN BY serviceHeldShots() -- IN THE SAME
// loop() ITERATION WHEN THE MACHINE IS QUIET (0a.8, reviews 2026-11-23).
// Every completion -- the end of settling, a lift, the forced stop at 60 s,
// a quiet scale -- used to write LittleFS at once, so a flush started inside
// the 5 s settle window, or a lever-B draw running past a forced stop, got a
// flash write with the pump on (invariant 2). Completions happen inside the
// sample drain, BEFORE this iteration's machine polls, so deciding there
// read last iteration's switches (review of ba93901); serviceHeldShots()
// runs after the polls, with `quiet` -- the test weighings use: no shot in
// progress and the brew guard idle. Quiet: written in that same iteration,
// so a shot is in RAM only for the rest of one loop() pass. Busy: held
// until the first quiet pass, oldest first.
//
// A held shot is copied out -- samples into PSRAM, since the detector's
// buffer is the next shot's -- with the machine context and tap state as
// they stood at completion. HOLD_SLOTS of them, so a second shot finishing
// while the pump still runs is held too. Escape hatches, all choosing
// invariant 6 over invariant 2: past HOLD_MAX_MS a held shot is written
// whatever the guard says with the detector IDLE, and past
// HOLD_MAX_ARMED_MS with it ARMED too -- ARMED with a busy guard may be a
// running pump (pre-infusion, a flush), but a cup left on the scale keeps
// the detector ARMED most of the day, so IDLE alone let a guard stuck busy
// hold a shot until the power went (review of 5652cd2); no pump run lasts
// 30 min. With every slot full the oldest is written at once. A slot is
// freed only when its write succeeded; a failed one is retried every
// HOLD_RETRY_MS, logged once, not every loop() pass. A held shot is lost if
// the board loses power before it is written; `reboot` writes them first
// and refuses while any is still held, unless forced.
static constexpr uint32_t HOLD_MAX_MS       = 5UL * 60 * 1000;
static constexpr uint32_t HOLD_MAX_ARMED_MS = 30UL * 60 * 1000;
static constexpr uint32_t HOLD_RETRY_MS     = 30UL * 1000;
static constexpr int HOLD_SLOTS = 2;
struct HeldShot {
  sample_t*      samples = nullptr;   // MAX_SAMPLES, PSRAM (setup())
  bool           used = false;
  uint32_t       at = 0;
  ShotResult     r;
  MachineContext mc;
  bool           flow_tap = false;
  String         mac;
  uint32_t       retry_at = 0;        // after a failed write: not before this
  bool           failing = false;     // a write failed; logged once
};
static HeldShot held_shots[HOLD_SLOTS];

// True only when the record reached the spool.
static bool writeShotRecord(const ShotResult& r, const MachineContext& mc,
                            bool flow_tap, const String& mac) {
  if (!spool_ok) {
    Serial.println("# spool unavailable — record NOT persisted");
    return false;
  }
  String path = spool.writeShot(r, mac, mc, flow_tap);
  if (!path.length()) {
    Serial.println("# spool write FAILED — record NOT persisted");
    return false;
  }
  Serial.printf("# spooled %s%s\n", path.c_str(), r.valid ? "" : " (reject)");
  return true;
}

// Frees the slot only on success: a failed write keeps the shot in RAM.
// Frees the slot only on success. After a failure the slot is not tried
// again for HOLD_RETRY_MS (force: try now, e.g. `reboot`).
static bool writeHeld(HeldShot& h, const char* why, bool force = false) {
  if (!h.used) return true;
  uint32_t now = millis();
  if (h.failing && !force && (int32_t)(now - h.retry_at) < 0) return false;
  if (!h.failing)
    Serial.printf("# writing a held shot (%s, held %lu ms)\n", why,
                  (unsigned long)(now - h.at));
  if (!spool_ok || !writeShotRecord(h.r, h.mc, h.flow_tap, h.mac)) {
    if (!h.failing)
      Serial.printf("# held shot NOT written (%s) -- kept in RAM, retrying every %lu s\n",
                    spool_ok ? "spool write failed" : "spool unavailable",
                    (unsigned long)(HOLD_RETRY_MS / 1000));
    h.failing = true;
    h.retry_at = now + HOLD_RETRY_MS;
    return false;
  }
  if (h.failing) Serial.println("# held shot written after earlier failures");
  h.used = false;
  h.failing = false;
  return true;
}

// Oldest first, so the spool keeps the order the shots were pulled in.
static HeldShot* oldestHeld() {
  HeldShot* o = nullptr;
  for (HeldShot& h : held_shots)
    if (h.used && (!o || (int32_t)(h.at - o->at) < 0)) o = &h;
  return o;
}

// Returns how many are still held (a failure stops it: order is kept).
static int writeAllHeld(const char* why) {
  while (HeldShot* h = oldestHeld())
    if (!writeHeld(*h, why, true)) break;
  int n = 0;
  for (HeldShot& h : held_shots) if (h.used) n++;
  return n;
}

// From loop(), after the machine polls, once `quiet` is known.
static void serviceHeldShots(bool quiet) {
  ShotState st = detector.state();
  while (HeldShot* h = oldestHeld()) {
    uint32_t age = millis() - h->at;
    bool ok;
    if (quiet) ok = writeHeld(*h, "machine quiet");
    else if ((st == ShotState::IDLE && age >= HOLD_MAX_MS)
             || (st == ShotState::ARMED && age >= HOLD_MAX_ARMED_MS))
      ok = writeHeld(*h, "held too long, writing regardless");
    else break;
    if (!ok) break;
  }
}

static void handleShotComplete(const ShotResult& r) {
  printShotSummary(r);
  // Everything the detector finishes is spooled — including rejects (<5 g),
  // flagged so the server excludes them. A missed second shot on 2026-09-04
  // was undiagnosable because a possible reject left no trace; a few bytes
  // of flash per false trigger buys that visibility.
  //
  // Pour start already picked the press; this only fills it in if the
  // press was confirmed after the POURING transition. Done now, not at the
  // write: the next pour resets the context.
  machine_ctx.resolvePumpIfUnknown(r.started_at_ms);
  if (flow_tap_shot && !machine_ctx.prePulsesCaptured())
    machine_ctx.capturePrePulses(pulse_log, r.started_at_ms);
  if (machine_ctx.context().setpoint_dc != INT16_MIN)
    Serial.printf("# setpoint %d C (last dialled on the display)\n",
                  machine_ctx.context().setpoint_dc / 10);
  else if (machine_ctx.context().setpoint_pending)
    Serial.println("# setpoint edit not confirmed before this pour — recorded as unknown; no brew temperature carried onto or across this shot");
  else
    Serial.println("# setpoint not known (no confirmed edit seen, or the display was not decoding) — the server may carry an earlier one");

  // No PSRAM: nowhere to hold it, so write now, as 0.15.1 did.
  if (!held_shots[0].samples) {
    writeShotRecord(r, machine_ctx.context(), flow_tap_shot, scales.slotMac(ScaleRole::YIELD));
    Serial.println("# ---- end shot ----");
    return;
  }
  HeldShot* slot = nullptr;
  for (HeldShot& h : held_shots) if (!h.used) { slot = &h; break; }
  if (!slot) {                        // every slot full: the oldest goes now
    HeldShot* o = oldestHeld();
    if (writeHeld(*o, "every hold slot full", true)) {
      slot = o;
    } else {
      // The spool is down and RAM is full: this shot cannot be kept either.
      Serial.println("# every hold slot full and the spool will not take one — this shot is NOT persisted");
      Serial.println("# ---- end shot ----");
      return;
    }
  }
  uint16_t n = r.sample_count <= ShotDetector::MAX_SAMPLES ? r.sample_count
                                                          : (uint16_t)ShotDetector::MAX_SAMPLES;
  memcpy(slot->samples, r.samples, (size_t)n * sizeof(sample_t));
  slot->r = r;
  slot->r.samples = slot->samples;
  slot->r.sample_count = n;
  slot->mc = machine_ctx.context();
  slot->flow_tap = flow_tap_shot;
  slot->mac = scales.slotMac(ScaleRole::YIELD);
  slot->at = millis();
  slot->failing = false;
  slot->retry_at = 0;
  slot->used = true;
  Serial.println("# shot held -- written once the machine is quiet (this pass, if it is)");
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

// ---- drive side, BENCH ONLY (docs/drive-bench.md) --------------------------
// The 74HC221 drive block's pins, fixed by the drive layout
// (docs/board-and-perfboard.md, "Drive-side layout"; CLAUDE.md DRIVE table;
// on the PCB hardware/pcb/gen_schematic.py, the devkit maps A and K).
// Channel n's trigger fires U1/U2 and its Q comes back through 1 K.
struct DriveChan { const char* name; uint8_t trig; uint8_t q; };
static constexpr DriveChan DRIVE_CH[4] = {
  // Chip and pins as on the PCB, where the '221s are crossed (U1 drives
  // photoMOS 1 of each chain, U2 photoMOS 2). On the perfboard U1 is both
  // 1Cup channels and U2 both 2Cup ones; the GPIOs are the same either way.
  { "1Cup photoMOS 1", 42, 11 },    // U1 ch1: pin 1 A̅, pin 13 Q. GPIO1 until the
                                     // PCB (2026-11-24): GPIO1 is ADC1, now the J3 NTC
  { "1Cup photoMOS 2", 41, 2  },    // U2 ch1: pin 1 A̅, pin 13 Q
  { "2Cup photoMOS 1", 40, 47 },    // U1 ch2: pin 9 A̅, pin 5 Q
  { "2Cup photoMOS 2", 39, 21 },    // U2 ch2: pin 9 A̅, pin 5 Q
};
static constexpr uint8_t GATE_EN_PIN   = 38;   // U3 R̅, 10 K pull-down
static constexpr uint8_t GATE_TRIG_PIN = 10;   // U3 A̅, 10 K pull-up
static constexpr uint8_t GATE_Q_PIN    = 9;    // U3 Q (the tied R̅ net), via 1 K
// The run's hard cap: past this GPIO38 drops whatever Q is doing. Above
// the gate's 500 ms worst-corner cap, so a held gate shows as HELD rather
// than being cut at its own nominal end; far under the ~2 s reprogram
// threshold — which only matters with a drive-leg jumper fitted, which
// this tool is never run with.
static constexpr int64_t DRIVE_CAP_US = 1000000;

// Every drive pin an INPUT, GPIO38's latch written LOW first so a later
// pinMode(OUTPUT) cannot drive it HIGH from a latch a CPU reset left at 1
// (docs/phase-0-plan.md, "What the firmware still does"). The external
// pulls hold the levels: A̅ high, R̅ low. Called at boot and after every run.
//
// The five Q readbacks get the internal pull-down (~45 K, against a '221's
// push-pull output through 1 K, so it never shifts a level): a readback
// whose chip or 1 K is out -- U3 on the Phase-0 board, any chip not yet in
// its socket -- reads LOW instead of floating, so a missing chip shows as
// NO PULSE rather than as noise, and no build needs a special case.
//
// `no_pd_pin` (the `nopd` option) is the one readback left without it, so a
// sweep's every release keeps the same condition. That pin floats when its
// chip or 1 K is out, and then can trip the idle check at random instead of
// showing NO PULSE.
static void driveRelease(int no_pd_pin = -1) {
  digitalWrite(GATE_EN_PIN, LOW);
  pinMode(GATE_EN_PIN, INPUT);
  pinMode(GATE_TRIG_PIN, INPUT);
  pinMode(GATE_Q_PIN, INPUT_PULLDOWN);
  for (const DriveChan& c : DRIVE_CH) {
    pinMode(c.trig, INPUT);
    pinMode(c.q, c.q == no_pd_pin ? INPUT : INPUT_PULLDOWN);
  }
}

// One falling edge on a trigger: latch HIGH before OUTPUT, so the pin never
// drives A̅ low by accident (pinMode sets the mode, not the level), then a
// few µs low, then back to INPUT on its pull-up.
static void drivePulseLow(uint8_t pin) {
  digitalWrite(pin, HIGH);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delayMicroseconds(5);
  digitalWrite(pin, HIGH);
  pinMode(pin, INPUT);
}

static inline bool fastRead(uint8_t pin) {
  return pin < 32 ? (REG_READ(GPIO_IN_REG) >> pin) & 1
                  : (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
}

// Sample the five Q readbacks continuously from t0 until the watched trace
// falls, or the cap. `stop_on` is the trace whose fall ends the run, plus
// RETRIGGER_TAIL_US to catch a second pulse; -1 = the gate.
//
// The loop is a busy-wait in loopTask, which blocks loop(), so no spool
// write or upload runs during it. What can still stall it: on this core,
// the flow and display ISRs (which is why a sweep refuses with either tap
// on), the USB-serial interrupt, the FreeRTOS tick, and the higher-priority
// arduino_events task (a Wi-Fi reconnect runs it); from the other core, a
// flash write (NimBLE or Wi-Fi writing NVS disables the cache on both). An
// edge seen late reads as a pulse a little long or short; an outlier run is
// repeated, not believed, and a sweep that ran across a Wi-Fi reconnect is
// run again.
// A stall that hits every run alike is not an outlier and this cannot see
// it -- hardware edge capture would; that is `drivetest`'s problem
// (docs/drive-bench.md).
static constexpr int64_t RETRIGGER_TAIL_US = 20000;
static bool driveSample(int64_t t0, QTrace q[4], QTrace& gate, int stop_on) {
  int64_t ended_at = -1;
  for (;;) {
    int64_t t = esp_timer_get_time() - t0;
    for (int i = 0; i < 4; i++) q[i].feed(fastRead(DRIVE_CH[i].q), t);
    gate.feed(fastRead(GATE_Q_PIN), t);   // pulled down: LOW with U3 out
    const QTrace& s = stop_on < 0 ? gate : q[stop_on];
    if (ended_at < 0 && s.fell()) ended_at = t;
    if (ended_at >= 0 && t - ended_at > RETRIGGER_TAIL_US) return false;
    if (t > DRIVE_CAP_US) return s.level;      // still high at the cap: held
    if (!s.rose() && t > 5000) return false;   // never started
  }
}

static void printTrace(Stream& out, const char* name, const QTrace& q) {
  if (!q.rose()) { out.printf("#   %-16s no pulse\n", name); return; }
  if (!q.fell()) { out.printf("#   %-16s rose +%.3f ms, still HIGH\n", name, q.rise_us / 1000.0); return; }
  out.printf("#   %-16s %.3f ms (rise +%.3f ms, %u edges)\n", name,
             q.width_us() / 1000.0, q.rise_us / 1000.0, (unsigned)q.edges);
}

// `drive status` | `drive gate [n]` | `drive fire <1-4> [n] [nopd]` | `drive direct <1-4> [n] [nopd]`
static void runDrive(Stream& out, const String& arg) {
  String a = arg; a.trim();
  int sp = a.indexOf(' ');
  String sub = sp < 0 ? a : a.substring(0, sp);
  String rest = sp < 0 ? "" : a.substring(sp + 1);
  rest.trim();

  if (sub == "" || sub == "status") {
    out.printf("# drive pins (nothing driven): GPIO38 en=%d  gate A̅=%d Q=%d\n",
               digitalRead(GATE_EN_PIN), digitalRead(GATE_TRIG_PIN), digitalRead(GATE_Q_PIN));
    for (int i = 0; i < 4; i++)
      out.printf("#   ch%d %-16s A̅(GPIO%u)=%d  Q(GPIO%u)=%d\n", i + 1, DRIVE_CH[i].name,
                 DRIVE_CH[i].trig, digitalRead(DRIVE_CH[i].trig), DRIVE_CH[i].q, digitalRead(DRIVE_CH[i].q));
    out.println("# idle is: en 0, every A̅ 1, every Q 0 (Q pins pulled down: a missing chip reads 0)");
    return;
  }

  bool gate_only = sub == "gate", fire = sub == "fire", direct = sub == "direct";
  if (!gate_only && !fire && !direct) {
    out.println("# drive status | drive gate [n] | drive fire <1-4> [n] [nopd] | drive direct <1-4> [n] [nopd]");
    return;
  }
  // Arguments as whole words, in any order after the channel: a number is
  // the run count, `nopd` (fire/direct only) drops the internal pull-down on
  // the fired channel's readback for docs/drive-bench.md step 4 -- against a
  // 10 K readback the ~45 K pull-down leaves a HIGH at ~2.7 V, 0.2 V over
  // VIH. Anything else is refused by name, so `3nopd` cannot run silently
  // with the pull-down still on.
  int ch = -1, n = 1;
  bool nopd = false, have_ch = gate_only, have_n = false;
  while (rest.length()) {
    int sp2 = rest.indexOf(' ');
    String tok = sp2 < 0 ? rest : rest.substring(0, sp2);
    rest = sp2 < 0 ? "" : rest.substring(sp2 + 1);
    rest.trim();
    bool numeric = tok.length() > 0 && isAllDigits(tok);
    if (tok == "nopd") {
      if (gate_only) { out.println("# drive: `nopd` is for fire/direct"); return; }
      nopd = true;
    } else if (numeric && !have_ch) {
      ch = tok.toInt() - 1; have_ch = true;
    } else if (numeric && !have_n) {
      n = tok.toInt(); have_n = true;
    } else {
      out.printf("# drive: REFUSED — what is `%s`? (drive %s%s)\n", tok.c_str(),
                 sub.c_str(), gate_only ? " [n]" : " <1-4> [n] [nopd]");
      return;
    }
  }
  if (!gate_only && (ch < 0 || ch > 3)) { out.println("# drive: channel 1-4"); return; }
  // Refused, not clamped: 50 runs already block loop() for ~75 s, and the
  // detector is checked once, before the first run.
  if (n < 1 || n > 50) { out.println("# drive: REFUSED — run count 1-50"); return; }

  // BENCH ONLY. Invariant 1 is held by the open drive-leg jumpers, which
  // firmware cannot see; these refusals keep the tool off a live machine
  // anyway, so it is never the reason a jumper mistake becomes a press.
  if (detector.state() != ShotState::IDLE) {
    out.println("# drive: REFUSED — the scale is armed or a shot is running");
    return;
  }
  // Both taps off: their ISRs would stall the sampler, and with the display
  // ISR off the J5 pads can be read here as `pins` reads them.
  if (display_on || flow_on) {
    out.println("# drive: REFUSED — `display off` and `flow off` first (docs/drive-bench.md)");
    return;
  }
  // On the machine? J5's clock and data idle HIGH on the machine's 4.7 K
  // pull-ups whenever J5 is plugged and the machine is on, and read LOW
  // through the board's own 16.4 K shunts unplugged; J4's `#` reads HIGH
  // plugged with the turbine parked open, LOW through its 220 K shunt
  // unplugged. A live bus pulls both J5 lines LOW for part of every 41 ms
  // frame, so one read is not enough: watch for 50 ms, refuse on any HIGH.
  // J2 is in the set for a held lever only -- it carries no ground, so with
  // J4 and J5 unplugged its lines read LOW whether or not it is plugged.
  // None of this sees a harness plugged into a machine that is OFF.
  makeTapPinsReadable();
  {
    int64_t w0 = esp_timer_get_time();
    while (esp_timer_get_time() - w0 < 50000)
      for (const TapPin& t : TAP_PINS)
        if (gpio_get_level((gpio_num_t)t.pin)) {
          out.printf("# drive: REFUSED — %s/GPIO%u read HIGH, so a harness is on a powered"
                     " machine. Bench only: J2, J4 and J5 unplugged (docs/drive-bench.md).\n",
                     t.name, t.pin);
          out.println("# off the machine every one of these reads 0; a J5 line can read 0"
                      " mid-frame, so a 0 below does not clear it:");
          printPinLevels(out);
          noteLoopBlocked();
          return;
        }
  }

  out.println("# ***********************************************************");
  out.println("# * DRIVE BENCH: fires 74HC221 channels one at a time.");
  out.println("# * Off the machine only, drive-leg jumpers (a1)(a2) OPEN.");
  out.println("# * loop() blocks up to ~1.5 s per run; scales and uploads stall.");
  out.println("# ***********************************************************");

  const int no_pd_pin = nopd ? DRIVE_CH[ch].q : -1;   // the same in every release of the sweep
  WidthStats ws;
  uint16_t bad = 0;
  int done = 0;
  bool aborted = false;
  for (int run = 0; run < n; run++) {
    driveRelease(no_pd_pin);
    delayMicroseconds(100);   // let the pulls settle the just-released pins
    // Every A̅ an armed R̅ rise could fire must read HIGH, and every Q LOW,
    // first (docs/phase-0-plan.md: never let R̅ rise without reading them).
    bool idle = digitalRead(GATE_TRIG_PIN) == HIGH && digitalRead(GATE_EN_PIN) == LOW
             && digitalRead(GATE_Q_PIN) == LOW;
    for (const DriveChan& c : DRIVE_CH)
      idle = idle && digitalRead(c.trig) == HIGH && digitalRead(c.q) == LOW;
    if (!idle) {
      out.println("# drive: ABORTED — not idle (an A̅ low, a Q high, or the arm net high). See `drive status`.");
      aborted = true;
      break;
    }
    QTrace q[4], gate;
    bool held;
    int64_t t0;
    // Arm: GPIO38 high. With the gate fitted that only enables it (its A̅ is
    // high, so the R̅ rise is inert); in the no-gate rework (`direct`) it
    // drives the tied R̅ net and arms the four drive channels.
    digitalWrite(GATE_EN_PIN, HIGH);
    pinMode(GATE_EN_PIN, OUTPUT);
    delayMicroseconds(50);
    if (gate_only) {
      t0 = esp_timer_get_time();
      drivePulseLow(GATE_TRIG_PIN);
      held = driveSample(t0, q, gate, -1);
    } else {
      if (fire) {
        drivePulseLow(GATE_TRIG_PIN);
        int64_t g0 = esp_timer_get_time();
        while (!digitalRead(GATE_Q_PIN) && esp_timer_get_time() - g0 < 1000) {}
        if (!digitalRead(GATE_Q_PIN)) {
          driveRelease(no_pd_pin);
          out.println("# drive: ABORTED — the gate did not fire (U3 or its 1 K out? `direct` is the no-gate build)");
          aborted = true;
          break;
        }
        // The gate is already high when the drive fires; its trace starts at
        // the drive trigger, so its "width" here is not the gate's width.
        gate.feed(true, 0);
      }
      t0 = esp_timer_get_time();
      drivePulseLow(DRIVE_CH[ch].trig);
      held = driveSample(t0, q, gate, ch);
    }
    driveRelease(no_pd_pin);   // disarm unconditionally: ends any pulse still running

    const QTrace& fired = gate_only ? gate : q[ch];
    const QTrace* others[4];
    uint8_t no = 0;
    for (int i = 0; i < 4; i++) if (i != ch) others[no++] = &q[i];
    BenchVerdict v = judge(fired, (fire ? &gate : nullptr), others, no, held);
    out.printf("# run %d/%d: %s\n", run + 1, n, verdictName(v));
    if (gate_only) printTrace(out, "gate", gate);
    else {
      printTrace(out, DRIVE_CH[ch].name, q[ch]);
      if (fire) printTrace(out, "gate, from trig", gate);
    }
    for (int i = 0; i < 4; i++)
      if ((gate_only || i != ch) && q[i].edges) printTrace(out, DRIVE_CH[i].name, q[i]);
    if (v == BenchVerdict::OK) { ws.add(fired.width_us()); }
    else bad++;
    done++;
    delay(500);   // a gap between runs; the '221s are non-retriggerable anyway
  }
  driveRelease();
  // Printed on an abort too: the runs already finished are still data, but
  // the line says the sweep did not complete.
  const char* ab = aborted ? "ABORTED " : "";
  if (ws.n)
    out.printf("# %s%s: %d of %d runs, n=%u ok  min %.3f  mean %.3f  max %.3f ms  (%u bad)\n",
               ab, gate_only ? "gate" : DRIVE_CH[ch].name, done, n, (unsigned)ws.n,
               ws.min_us / 1000.0, ws.mean_us() / 1000.0, ws.max_us / 1000.0, (unsigned)bad);
  else
    out.printf("# %s%d of %d runs, no good runs (%u bad)\n", ab, done, n, (unsigned)bad);
  // The sweep needed `display off` and `flow off`, and both are SAVED. A board
  // back on the machine with them off logs no flow, boiler temperature or
  // setpoint -- and with the display off the brew guard loses its TIMER
  // branch, so a 1Cup/2Cup shot whose press the poller missed is unguarded
  // and flash can be written mid-shot (invariant 2).
  out.println("# REMINDER: `display on` and `flow on` before this board goes back on the"
              " machine. Display off also weakens the brew guard: a missed 1Cup/2Cup press"
              " goes unguarded (invariant 2).");
  noteLoopBlocked();
}

static void handleCommand(String line, Stream& out) {
  line.trim();
  if (line.isEmpty()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);

  if (cmd == "help" || cmd == "h") {
    out.println("# status|s  tare|t  raw|r  verbose|v  display [on|off]  flow [on|off|filter <us>]  switches [on|off]  pins  selftest [n] [hz]  drive [status|gate|fire|direct]  ls  dump <file>  rm <file>  format  fake  net  scales  assign <yield|dose> <mac|last>  set <ssid|pass|endpoint> <val>  reboot [force]");
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
      // Same guard as flow/switches, and NEWLY NEEDED. Until 0.9.1 the early
      // return in displaySetEnabled made a mid-shot `display on` a harmless
      // no-op; now it detaches the ISR, zeroes the ring, discards the in-flight
      // frame via beginCapture() and zeroes the counters — on top of the NVS
      // write. And the documented procedure for the dropped-edge experiment is
      // "`display on` to zero the counters, then one flush", which is exactly
      // what an operator types around a brew.
      ShotState st_now = detector.state();
      if (st_now == ShotState::POURING || st_now == ShotState::SETTLING) {
        out.println("# refused — a shot is in progress"
                    " (this would drop the in-flight frame and stall on flash)");
        printDisplay(out);
        return;
      }
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
    bool is_filter = is_flow && arg.startsWith("filter");
    // A bare `flow filter` is a QUERY and must not be refused: it writes
    // nothing and changes no counting basis, so the refusal message named two
    // effects it does not have — and with the tap off, printMachine does not
    // show the width, so the query was the only way to read it.
    bool is_filter_set = is_filter && arg.substring(6).length() > 0;
    bool mutating = is_filter_set || arg == "on" || arg == "off";

    // ONE GUARD FOR EVERY MUTATING FORM, including `filter`. 0.9.1 put the
    // filter branch above the guard with its own `return`, so it slipped past:
    // inlet_pulses is a delta on flow.total(), so changing the window mid-pour
    // changes the counting basis inside one record, and nothing in the record
    // says which width produced it (SPOOL_FLAG_FLOW_TAP only says the tap was
    // live; firmware_version is the same either way). That is the second of
    // the two reasons this guard exists, and it applies verbatim.
    //
    // The rule is general but the enforcement is not: `set`, `rm`, `fake` and
    // `dump` all touch flash with no such check. Pre-existing and not fixed
    // here — this comment should not read as if it were enforced everywhere.
    if (mutating) {
      ShotState st_now = detector.state();
      if (st_now == ShotState::POURING || st_now == ShotState::SETTLING) {
        // Two reasons, as the flow/switches guard has always had: an NVS
        // write is tens of ms of flash, which invariant 2 forbids during a
        // shot; and changing the tap or the filter width mid-pour changes the
        // counting basis inside a single record, which nothing in that record
        // would say.
        out.println("# refused — a shot is in progress"
                    " (flash write, and a mid-shot change to the counting basis)");
        printMachine(out);
        return;
      }
    }

    if (is_filter) {
      String v = arg.substring(6); v.trim();
      if (!v.length()) {
        out.printf("# flow filter = %lu us (usage: flow filter <microseconds>)\n",
                   (unsigned long)flow.minPeriodUs());
      } else {
        // Digits only. String::toInt() is atol-shaped and yields 0 for
        // non-numeric text, so `flow filter abc` used to pass the range check
        // and set 0 — the one value that disables filtering entirely and makes
        // the next flush read as raw edge counts. One typo would have looked
        // like a dramatic K-factor confirmation.
        bool numeric = isAllDigits(v);
        uint32_t us = numeric ? (uint32_t)v.toInt() : 0;
        if (!numeric) {
          out.printf("# refused — '%s' is not a number\n", v.c_str());
        } else if (us > 100000) {
          out.println("# refused — a filter above 100 ms would reject real flow");
        } else {
          flow.setMinPeriodUs(us);
          out.printf("# flow filter = %lu us%s\n", (unsigned long)us,
                     us == 0 ? "  *** OFF — EVERY EDGE COUNTS, including hash ***" : "");
          out.println("# NOT persisted; a reboot restores 2000 us.");
          // `flow on` resets only `rejected`/`dropped`. `counted` is never
          // reset by design (the detector takes a difference across it), and
          // `counted` is what prints as pulses= and is the ONLY number the
          // counts-per-gram comparison uses. 0.9.1 told the operator to run
          // `flow on` to zero the counters, which would have had them read a
          // cumulative figure as a per-run one — the same error this release
          // exists to document.
          out.println("# To compare runs: note pulses= BEFORE and AFTER each"
                      " run and subtract. 'flow on' does NOT zero pulses=.");
        }
      }
      printMachine(out);
      return;
    }

    if (arg == "on" || arg == "off") {
      bool on = (arg == "on");
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
  } else if (cmd == "drive") {
    runDrive(out, arg);
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
  } else if (cmd == "format") {
    // The deliberate act that LittleFS.begin() used to perform by itself on
    // any mount failure, destroying every unsent shot. Needed once on a
    // blank partition; never otherwise.
    if (detector.state() != ShotState::IDLE) {
      // ARMED is the ordinary resting state with a cup on the scale (it is
      // "stable at ANY value", see armed_test), so say which it is rather
      // than claiming a shot is running when one usually is not.
      out.printf("# format: REFUSED — detector is %s. Take the cup off the"
                 " scale (or wait for the shot to finish) and retry.\n",
                 stateName(detector.state()));
    } else if (arg != "yes") {
      // Count the quarantined records too. pendingCount() deliberately skips
      // /spool/bad, so a draft of this prompt — the one thing between the
      // operator and an irreversible erase — undercounted exactly the records
      // that are already in trouble and that format also destroys.
      out.printf("# format: ERASES EVERYTHING on the spool: %u pending record(s)"
                 " and %u quarantined. There is no undo. Type 'format yes'.\n",
                 (unsigned)(spool_ok ? spool.pendingCount() : 0),
                 (unsigned)(spool_ok ? spool.quarantinedCount() : 0));
    } else {
      out.println("# format: erasing…");
      bool ok = LittleFS.format();
      // Re-probe UNCONDITIONALLY. A draft short-circuited on `ok`, so a
      // format that failed before unmounting — wrong partition label, busy —
      // left a perfectly healthy filesystem with spool_ok stuck false, and
      // the device silently stopped persisting every shot until reboot. That
      // is invariant 6 broken by the error path of the command that exists
      // to protect it.
      spool_ok = LittleFS.begin(false, "/littlefs", 10, "littlefs") && spool.begin();
      out.printf("# format: %s, spool %s\n", ok ? "done" : "FAILED",
                 spool_ok ? "ready" : "UNAVAILABLE");
      // spool.begin() redraws boot_id and resets seq, so the server's
      // (device, boot_id, seq) idempotency key changes here. Harmless after
      // an erase, worth knowing.

    }
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
    int still = writeAllHeld("reboot");
    if (still > 0 && arg != "force") {
      out.printf("# %d shot(s) held in RAM could not be written -- a reboot loses them."
                 " Fix the spool, or 'reboot force' to lose them.\n", still);
      return;
    }
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
  // First thing in setup(), before the 2 s USB wait: drive pins INPUT and
  // GPIO38's latch LOW. The bootloader and core init still run before this;
  // through them GPIO38 is an unconfigured pad and the external pulls hold
  // the line, not this call: in the gate build GPIO38's 10 K holds the
  // GATE's R̅ low, and the drive channels' tied R̅ (arm) net is held low by
  // its own 10 K (and by U3's Q, when U3 is fitted); only in the no-gate rework is GPIO38's 10 K
  // the arm net's.
  driveRelease();
  Serial.begin(115200);
  Serial0.begin(115200);   // UART bridge (COM port): debug log + CLI
  // The display tap goes on FIRST (0.15.1), before the 2 s USB wait and the
  // storage/BLE/WiFi bring-up: the machine's power-up is recognised by its
  // frame silence until +4.1 s after rails up, and on a shared power strip
  // the board must be watching before then (machine_context.h onFirstFrame).
  bool display_pref;
  {
    Preferences p;
    p.begin("espl", true);
    display_pref = p.getBool("display", false);
    p.end();
  }
  if (display_pref) displaySetEnabled(true);
  // native USB CDC needs a moment before the first lines land. Drain the
  // display ring meanwhile (0.15.1): the machine's power-on burst would fill
  // it and the frames from +4.1 s would be dropped until loop() starts.
  for (uint32_t t0 = millis(); millis() - t0 < 2000; delay(5)) pollDisplay();
  Serial.printf("# espressolog %s (detector %s)\n", FIRMWARE_VERSION, DETECTOR_VERSION);

  // The held-shot buffers (handleShotComplete()). PSRAM, which this workload
  // otherwise leaves unused; without them a finished shot is written at once.
  bool all_held = true;
  for (HeldShot& h : held_shots) {
    h.samples = (sample_t*)ps_malloc(ShotDetector::MAX_SAMPLES * sizeof(sample_t));
    if (!h.samples) all_held = false;
  }
  if (!all_held) {
    for (HeldShot& h : held_shots) { free(h.samples); h.samples = nullptr; }
    Serial.println("# no PSRAM for the held-shot buffers -- shots are written as they finish, pump or not");
  }

  // 4th arg: our partition is labeled "littlefs", not the wrapper's default "spiffs"
  //
  // MOUNT WITHOUT FORMATTING FIRST. This passed format-on-fail = true until
  // 2026-10-12, so any mount failure — corruption, a partition-table change,
  // a half-written block after a brown-out — silently reformatted the
  // partition and destroyed every unsent shot. That is invariant 6 being
  // broken by the recovery path. A failed mount is now reported and left
  // alone, so the flash can be dumped and the records recovered; formatting
  // is a deliberate act, and `format` on the CLI is where it belongs.
  //
  // The cost is that a genuinely blank partition no longer self-formats on
  // first boot. That happens once per board, is visible in this log line,
  // and is worth paying to never auto-erase a backlog.
  if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
    Serial.println("# LittleFS: MOUNT FAILED — shots will NOT be persisted.");
    Serial.println("# If this partition has never been used, run 'format' once."
                   " If it HAS held shots, do NOT format: dump the flash first,"
                   " because formatting destroys every record still waiting.");
  } else {
    spool_ok = spool.begin();
    Serial.printf("# LittleFS: %u/%u KB used, spool %s, %u pending\n",
                  (unsigned)(LittleFS.usedBytes() / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024),
                  spool_ok ? "ready" : "INIT FAILED",
                  (unsigned)spool.pendingCount());
  }

  sample_queue = xQueueCreate(64, sizeof(ScaleSample));
  pollDisplay();
  scales.begin(sample_queue);
  pollDisplay();
  net.begin(&spool);
  pollDisplay();

  {
    Preferences p;
    p.begin("espl", true);
    bool on = p.getBool("display", false);
    bool fl = p.getBool("flow", false);
    bool sw = p.getBool("switches", false);
    p.end();
    // `on` (display) was acted on first thing in setup(); here it is only reported.
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

// A shot the detector finished, from feed() or tick(): spool it and tell
// the live stream.
static void shotCompleted() {
  handleShotComplete(detector.result());
  char frame[96];
  snprintf(frame, sizeof(frame),
           "{\"ev\":\"shot\",\"valid\":%d,\"fault\":%d,\"placement\":%d,\"disturbed\":%d}",
           detector.result().valid, detector.result().fault, detector.result().placement,
           detector.result().disturbed);
  net.sendLive(frame);
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
        machine_ctx.onPourStart(micros(),
                                display_on && !display_bus.stale(micros(), DISPLAY_FRESH_US));
        // Which press started the pump: decided now, while t=0 is fresh and
        // before the stop press or a later flush can crowd it out.
        machine_ctx.resolvePump(detector.result().started_at_ms);
        // Latch the tap state for THIS shot. See flow_tap_shot.
        flow_tap_shot = flow_on;
        // And the pulses between the press and first drip, while the log
        // surely still holds them (spool v4).
        machine_ctx.notePourConfirmed(millis());
        if (flow_tap_shot)
          machine_ctx.capturePrePulses(pulse_log, detector.result().started_at_ms);
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
      if (complete) shotCompleted();
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
  // POLL THE MACHINE BEFORE DECIDING WHETHER IT IS QUIET. These sat BELOW the
  // decision until 2026-10-12, so `quiet` was computed from the PREVIOUS
  // iteration's display reading — and with a ~41 ms frame period that leaves
  // machineBusy() blind for roughly 50-150 ms after pump-on. One iteration in
  // that window is all it takes, because tryUploadOldest() is synchronous
  // with 3 s connect and 5 s response timeouts. A multi-second stall at pump
  // start is precisely the dropped-edge signature this guard exists to
  // prevent, so computing the guard from stale state defeated it at exactly
  // the moment it mattered.
  pollSwitches();
  pollDisplay();
  machineContextPoll();
  liveDisplay(millis());

  // Detector 0a.8: a shot ends on time when the yield scale goes quiet for
  // SCALE_QUIET_MS (3 s) -- settling, or a pour 65 s on (detector.cpp,
  // tick()). Without this a scale switched off with the cup, or one whose
  // battery died mid-pour, left the shot open and every spool write
  // blocked. NOT gated on the machine: the shot closes at once, so a flush
  // or the next shot cannot feed into it, and the spool write is what
  // waits (handleShotComplete(), invariant 2) -- gating tick() itself was
  // tried and kept the shot open (review of c85dcf8). After the drain, so
  // nothing queued before it is missed; a sample stamped after it is fed
  // next pass, and 3 s of silence cannot hinge on it.
  if (detector.tick(millis())) {
    Serial.printf("# shot finished on time: the yield scale went quiet%s\n",
                  detector.result().fault
                    ? " mid-pour -- recorded as a fault, excluded on the server; un-exclude it if the shot was good"
                    : "");
    shotCompleted();
  }

  ShotState st = detector.state();
  // INVARIANT 2: never write flash or block on the network during a shot.
  //
  // This was `IDLE || ARMED` until 2026-10-12, and ARMED is not quiet. The
  // detector only reaches POURING at first drip plus POUR_HOLD_MS, so ARMED
  // spans pump-on, the whole of pre-infusion and the first drops — with the
  // pump running, the flow ISR counting and the display bus decoding. In
  // that window this loop was free to write a weighing to LittleFS and to
  // call net.tick(), whose HTTP path is synchronous with a 3 s connect and
  // 5 s response timeout. With the server unreachable and a record pending,
  // loop() stalls seconds at a time; the 64-entry sample queue holds ~3-6 s,
  // the 512-entry display ring ~158 ms of average traffic (~50 ms of a
  // continuous burst), and a switch press (80-295 ms,
  // 2026-10-19) is missed outright. (Cited "345-480 ms" until 2026-10-27:
  // the superseded 2026-10-08 range. A SHORTER press makes the stall worse,
  // not better, so the point stands a fortiori.) It is a candidate explanation for the one burst of
  // ~6086 dropped display edges at pump start that docs/phase-0-status.md
  // records as unexplained — not proof, but it predicts that signature.
  //
  // IT IS EASY TO HIT, and a draft of this comment WITHDREW that on a false
  // premise — then this review put it back. The withdrawal said "ARMED means
  // |w| <= ARM_BAND_MG (0.5 g) so no 5 g weighing can complete there". Wrong:
  // ARM_BAND_MG is a FLATNESS band, not a band around zero. `detector.cpp`
  // compares each ring entry against the CURRENT reading (`e.w > w + BAND`)
  // and says so in its own words — "ARMED (0a.3): stable at ANY value for
  // 1 s" — because a tare is just a baseline the scale applied for you, and
  // the un-tared cup-swap workflow depends on it.
  //
  // So: an un-tared cup of any mass sits still for a second -> the
  // WeighingDetector episode is stable AND the detector is ARMED; the pump
  // starts and the first drips break the episode (RANGE_MG = 0.3 g) while
  // tryPourOnset still needs >300 mg/s for 300 ms, which pre-infusion drips
  // need not meet. emit() fires, queueWeighing() queues, and the flush writes
  // LittleFS mid-pre-infusion. That is the breach, and it is reachable.
  //
  // ROOT CAUSE OF THE FALSE PREMISE: CLAUDE.md's "Shot detection" section
  // still describes ARMED as "weight within +/-0.5 g of zero... tared with
  // the cup on", which is stale against detector 0a.6. Corrected there too.
  //
  // NOTE `ARMED` IS DELIBERATELY STILL IN `quiet` BELOW. Removing it would
  // block every upload for as long as a cup sits on the scale, which is most
  // of the day. machineBusy() is what separates "armed and idle" from "armed
  // and the pump is running" — so with NO tap enabled (display_on and
  // switches_on both default false and are NVS-persisted) this guard returns
  // false and the pre-fix behaviour is restored. That is not a bug being
  // hidden: with no machine signal there is nothing to distinguish them with.
  // It does mean the invariant-2 fix is conditional on a tap being live.
  bool quiet = (st == ShotState::IDLE || st == ShotState::ARMED) && !machineBusy();

  // Held shots go to flash first, once quiet -- or, past HOLD_MAX_MS idle
  // (HOLD_MAX_ARMED_MS armed), regardless (see handleShotComplete()).
  serviceHeldShots(quiet);

  if (quiet && spool_ok) {
    while (pending_w_n > 0) {
      pending_w_n--;
      String path = spool.writeWeighing(pending_w[pending_w_n].role,
                                        pending_w[pending_w_n].ev,
                                        scales.slotMac((ScaleRole)pending_w[pending_w_n].role));
      if (path.length()) Serial.printf("# spooled %s\n", path.c_str());
    }
  }

  scales.tick(millis());
  // Network (and its flash reads) only while nothing is brewing.
  net.tick(millis(), quiet);
  updateLed();
  pollSerial(Serial);
  pollSerial(Serial0);
  delay(5);   // ~10 Hz data; nothing here needs a tighter spin
}
