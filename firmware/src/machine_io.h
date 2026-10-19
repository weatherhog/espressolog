#pragma once
#include <stdint.h>
#include <stddef.h>

// Milestones 0c and 0d: the two taps that read the machine's own wiring
// rather than its display. Pure logic, no Arduino, no clock of its own —
// main.cpp owns the ISR and the polling, this owns every decision.
//
// That split is not stylistic. machine_context.h carries the note that the
// first version of ITS logic lived inline in main.cpp, had no tests, and
// shipped two bugs a green suite could not see. Same shape, same split.
//
// Both taps are read-only dividers into a GPIO (hard invariant 1). The
// conditioning and the measured node impedances are in CLAUDE.md; what
// matters here is only what the levels mean:
//
//   flowmeter `#`  Digmesa FHKSC, NPN open collector. Idles HIGH on the
//                  board's own 10.04k pull-up (measured 4.69 V) and pulses
//                  LOW. Count falling edges.
//   switches       +5 V -> R45 (100R) -> switch common -> direction line.
//                  Idle LOW, pressed HIGH — measured 2026-10-04, not
//                  inferred from the schematic.

// ---------------------------------------------------------------------------
// 0d — flowmeter pulse counting.
//
// Raw pulses only, never millilitres (hard invariant 3). The ml/pulse factor
// is calibrated against water and lives in `flowmeter_calibration`.
//
// THE DATASHEET IS IN HAND (2026-10-09): #932-9525-B, 1.00 mm, 0 deg,
// fact:digmesa-k-factor 2382 pulses/L
// 2382 pulses/L, 0.41 g/pulse, rated 0.033-0.40 L/min. The old "~2400 from
// search summaries, spread 2386-2494" is retracted -- that spread was four
// different PART NUMBERS (-B/-B90/-B180/-B270), not a tolerance. The rated
// range is where K is SPECIFIED, not a hydraulic limit; this machine has
// already run the sensor past it.
//
// WHAT THE IN-CIRCUIT COUNTS ARE IS UNRESOLVED -- see CLAUDE.md's 0d entry.
// Two shots logged ~5x the datasheet; a published conclusion that they were
// pump noise is WITHDRAWN (a dry pump run logged 11 counts where coupling
// predicts 370). Derive nothing from in-circuit counts until it is settled.
class PulseCounter {
public:
  // Glitch floor. Espresso flow is ~2 ml/s; at the datasheet's 2382 pulses/L
  // that is ~4.8 Hz, and the rate at the sensor's rated max flow (0.40 L/min)
  // is 15.9 Hz, a 63 ms period. 2 ms rejects nothing real until 500 Hz and
  // kills contact bounce and any triac hash that gets in.
  //
  // THE BENCH TEST THIS COMMENT ASKED FOR HAS RUN (2026-10-09). It said:
  // if 100 Hz mains coupling shows up it must be COUNTED rather than quietly
  // filtered, so the bench can see it. What a shot actually produced was
  // ~22 Hz counted against ~128 Hz REJECTED, plus 629 ring overflows -- i.e.
  // the hash is bursty on a sub-2 ms scale, not a clean 100 Hz train. So this
  // floor is doing more rejecting than the comment anticipated, and it is the
  // constant most likely to need revisiting once the open question in the
  // header is settled and any filtering is fitted at the pin.
  //
  // Still do not raise it to swallow mains hum: that would also swallow a
  // real 90 Hz signal and hide a bad tap instead of reporting it.
  static constexpr uint32_t MIN_PERIOD_US = 2000;   // the default

  // RUNTIME-SETTABLE, for one specific experiment.
  //
  // CAUTION, 2026-10-09: everything below was written when the in-circuit
  // figure (5900-6500 pulses/L) was believed to be a K-factor. It is not --
  // see the header: what the counts measure is UNRESOLVED, and the two
  // stories this sweep was built to separate are no longer the live ones.
  // The sweep still works as a filter-width experiment; do not read its
  // framing as current.
  //
  // SWEEP SEVERAL WIDTHS, DO NOT COMPARE TWO. The first version of this
  // specified 2 ms against 20 ms and was unusable, because both outcomes
  // produce the same signature:
  //
  //   a 20 ms window starts rejecting real pulses at 50 Hz = 7.7-8.5 ml/s
  //   (across the withdrawn 5900-6500 p/L); the sensor's rated ceiling
  //   (0.40 L/min) is 23-25 ms, only 15-27 % clear, and a hot-water draw at
  //   10 ml/s is 15-17 ms — BELOW the window, where a 20 ms filter counts
  //   every second pulse, i.e. a 50 % drop.
  //   The "it was ringing" verdict is a drop toward 2382/K.
  //   Indistinguishable. And the two calibration flushes' durations were
  //   never recorded, so which regime they ran in is unknown. The 140 ms
  //   figure the first version quoted is ESPRESSO flow (1.2 ml/s); the
  //   experiment runs on a flush, which is unrestricted.
  //
  // THE WIDTH SWEEP IS THE SECONDARY TEST. On its own it cannot tell water
  // from a periodic interference source, because a plateau is produced by
  // anything whose period exceeds every width swept — and two such sources
  // are live here: 100 Hz mains/triac coupling (10 ms, which machine_io.h
  // deliberately does not reject) and one count per vibratory-pump stroke
  // (50 Hz, 20 ms) -- a reading now DEAD: the shots measured ~22 Hz.
  //
  // Sweep 2 / 5 / 10 ms, one flush each, WEIGHT AND DURATION recorded.
  // 20 ms is reported but NOT load-bearing: it starts rejecting real pulses
  // at 8.5 ml/s, which is inside the band a free flush runs at, so a missing
  // plateau there means nothing. 5 ms is marginal for the opposite reason —
  // at 2.47 counts per real pulse some ringing must already span ~3 ms — so
  // the comparison that carries weight is 2 vs 10.
  //
  //   2 and 10 agree      -> narrow-window ringing is not being counted
  //   2 >> 10             -> it is, and the 2 ms figure is inflated
  //
  // The alternative was a logic analyser on `#` — which is the tap that
  // produced the E01 fault on 2026-10-03, and whose corrected conditioning
  // has never been re-tested. This reuses the ESP tap, which has been.
  //
  // Deliberately NOT persisted in NVS: a filter width left over from a bench
  // run must not silently shape months of shot data. A reboot restores 2 ms.
  void setMinPeriodUs(uint32_t us) { min_period_us = us; }
  uint32_t minPeriodUs() const { return min_period_us; }

  // One timestamped falling edge, in ISR arrival order. Returns true if it
  // was counted, false if the glitch filter ate it.
  //
  // THE FILTER BOUNDS THE RATE OF FALSE COUNTS, NOT THEIR TOTAL. An earlier
  // comment here claimed a noise burst "costs at most one spurious count";
  // it does not. Sustained hash is counted once per MIN_PERIOD_US, so 50 ms
  // of 10 kHz noise yields 25 phantom pulses — about 10 ml of phantom water
  // at the datasheet's 2382 pulses/L. Measured against this class.
  // `glitches()` is therefore a diagnostic, not a reassurance: a large
  // rejected count means the counted one is suspect too.
  bool feedEdge(uint32_t t_us);

  // Free-running since boot. Never reset — a reset would make two readings
  // incomparable, and the windowing below is a subtraction, not a clear.
  uint32_t total() const { return counted; }
  uint32_t glitches() const { return rejected; }

  // Forget the diagnostics, keep the running total. main.cpp calls this when
  // the tap is enabled: `flow on` against an unwired GPIO6 lets a floating
  // input oscillate, and the counts it racks up are lifetime figures that then
  // describe a tap that no longer exists. `counted` is deliberately NOT reset —
  // the detector takes a difference across it, and a discontinuity there would
  // corrupt a shot rather than merely mislead a reader.
  void resetDiagnostics() { rejected = 0; }

  // Microseconds since the last counted pulse; UINT32_MAX if none yet.
  //
  // ONLY MEANINGFUL UNDER ~71 MINUTES — micros() wraps there, so a longer
  // idle reports elapsed-modulo-71-min and a dead tap can look freshly
  // active. Do not use this to decide "parked" vs "dead" over long spans;
  // main.cpp tracks that on millis() instead. Kept for the short-interval
  // case, where it is exact.
  uint32_t sinceLastUs(uint32_t now_us) const {
    return have_last ? (uint32_t)(now_us - last_us) : UINT32_MAX;
  }

private:
  uint32_t min_period_us = MIN_PERIOD_US;
  uint32_t counted = 0;
  uint32_t rejected = 0;
  uint32_t last_us = 0;
  bool     have_last = false;
};

// ---------------------------------------------------------------------------
// 0c — switch sensing, all four direction lines.
//
// THE SWITCHES ARE MOMENTARY, AND THAT CHANGES WHAT AN EVENT IS. Both levers
// are SCI R13-29 ON-OFF-ON with spring return to centre, and the control
// board latches: a short pulse starts the stored timer (or stops a running
// cycle), and the lever is back at centre long before the shot ends. So the
// line level NEVER means "a shot is in progress" — it means "a press is
// happening right now". Duration is the press, not the brew.
//
// A hold past ~2 s REPROGRAMS that direction's stored timer. That is the
// machine changing its own configuration, which is worth logging loudly:
// this machine's 1Cup is already ~10.5 s rather than the 15 s default
// because it was reprogrammed by hand, and nothing recorded when.
class SwitchBank {
public:
  // Order is the GPIO order in main.cpp, not J2's connector order — J2 runs
  // +5V, Steam, 2Cup, 1Cup, Water (note 2Cup BEFORE 1Cup, measured).
  enum Line : uint8_t { ONE_CUP = 0, TWO_CUP, STEAM, HOT_WATER, N_LINES };

  // Mechanical bounce on a lever switch is sub-millisecond to a few ms.
  // 20 ms is far past it and far short of the ~50 ms floor on a human
  // press. Also swallows the ~60 us low pulse GPIO15-18 emit at power-up,
  // which could not be a false press anyway (these lines read a press as
  // HIGH) but which is the kind of thing worth being immune to twice.
  static constexpr uint32_t DEBOUNCE_MS  = 20;
  // CLAUDE.md records the reprogramming threshold as "> ~2 s", from the
  // machine's behaviour rather than a datasheet. Flagged, never acted on.
  static constexpr uint32_t REPROGRAM_MS = 2000;

  struct Event {
    uint8_t  line;            // Line
    uint32_t pressed_at_ms;   // backdated to the first edge, not to confirmation
    uint32_t duration_ms;
    bool     reprogram;       // held long enough to have rewritten the timer
    // The press edge was NEVER SEEN: the line already read high on the first
    // poll, so pressed_at_ms is when this firmware started looking, not when
    // anyone pressed anything, and duration_ms is a lower bound on nothing in
    // particular. `reprogram` is forced false on these — it is the loudest
    // message this firmware prints, it asserts the machine rewrote its own
    // stored timer, and an unwitnessed press is not evidence of that.
    // Without this, enabling `switches` against an unwired (floating) line
    // and letting it settle minutes later printed a REPROGRAM alarm for a
    // press nobody made. Found in review, reproduced, then fixed.
    bool     synthetic;
  };

  // Raw levels for all four lines, true = pressed (line high). Timestamps
  // come from the caller so this stays host-testable and so a stretched
  // loop() distorts nothing it does not have to.
  void feed(uint32_t t_ms, const bool level[N_LINES]);

  // Forget every line and drop any queued event. main.cpp calls this when the
  // tap is enabled, because the taps go in one at a time: without it, state
  // adopted from a floating line during an earlier `switches on` survives the
  // `switches off`, and the first poll after rewiring emits a release whose
  // duration is the hours in between.
  void reset();

  // Drain completed events. More than one can complete in a single feed()
  // when both levers are released in the same tick, which a single
  // `event()` accessor would silently drop.
  bool popEvent(Event& out);

  bool pressed(uint8_t line) const {
    return line < N_LINES && st[line].stable;
  }
  // True when the current press was never witnessed starting — the line
  // already read high on the first poll. Callers must not present such a
  // press as evidence of anything: `status` used to print "steam=PRESSED(10s!)"
  // for a floating line, which is the status-pane form of the REPROGRAM alarm
  // the event path refuses to raise, from the same evidence (none).
  bool pressedSynthetic(uint8_t line) const {
    return line < N_LINES && st[line].stable && st[line].adopted;
  }
  // How long the current press has been held, 0 if not pressed. Lets status
  // show a stuck line; nothing here manufactures an event for one, because
  // a synthetic release would be a reading nobody took.
  uint32_t heldMs(uint8_t line, uint32_t now_ms) const {
    if (line >= N_LINES || !st[line].stable) return 0;
    return now_ms - st[line].since_ms;
  }
  uint32_t dropped() const { return lost; }

  static const char* lineName(uint8_t line);

private:
  struct LineState {
    bool     stable = false;      // debounced level
    bool     cand = false;        // candidate awaiting confirmation
    uint32_t cand_since = 0;      // when the candidate first appeared
    uint32_t since_ms = 0;        // when the current stable level began
    bool     init = false;
    bool     adopted = false;     // stable==true was assumed at init, not witnessed
  };
  static constexpr size_t EV_N = 8;

  LineState st[N_LINES];
  Event     q[EV_N];
  size_t    q_head = 0, q_count = 0;
  uint32_t  lost = 0;

  void push(const Event& e);
};
