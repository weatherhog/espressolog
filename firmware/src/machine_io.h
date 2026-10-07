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
// is calibrated against water on a Bookoo and lives in
// `flowmeter_calibration`; the datasheet figure (~2400 pulses/L) comes from
// search summaries of a datasheet nobody could retrieve, with sources
// spread 2386-2494, so it is a sanity check and not a constant.
class PulseCounter {
public:
  // Glitch floor. Espresso flow is ~2 ml/s, i.e. ~5 Hz; the fastest thing
  // this sensor sees is a hot-water draw, call it 10 ml/s or ~24 Hz, which
  // is a 42 ms period. 2 ms rejects nothing real until 500 Hz (200 ml/s,
  // absurd) and kills contact bounce and any triac hash that gets in.
  //
  // The machine's pump triac is random-phase and fires at mains zero cross,
  // so 100 Hz coupling is the specific thing being guarded against here —
  // 10 ms period, comfortably above this floor, so a burst of it would be
  // COUNTED, not rejected. That is deliberate: if 100 Hz shows up in the
  // count, the bench test must see it rather than have it quietly filtered.
  // Raising this floor to 11 ms to swallow mains hum would also swallow a
  // real 90 Hz signal, and would hide a bad tap instead of reporting it.
  static constexpr uint32_t MIN_PERIOD_US = 2000;

  // One timestamped falling edge, in ISR arrival order. Returns true if it
  // was counted, false if the glitch filter ate it.
  bool feedEdge(uint32_t t_us);

  // Free-running since boot. Never reset — a reset would make two readings
  // incomparable, and the windowing below is a subtraction, not a clear.
  uint32_t total() const { return counted; }
  uint32_t glitches() const { return rejected; }

  // Microseconds since the last counted pulse; UINT32_MAX if none yet.
  // Lets status tell "parked" apart from "tap is dead".
  uint32_t sinceLastUs(uint32_t now_us) const {
    return have_last ? (uint32_t)(now_us - last_us) : UINT32_MAX;
  }

private:
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
  };

  // Raw levels for all four lines, true = pressed (line high). Timestamps
  // come from the caller so this stays host-testable and so a stretched
  // loop() distorts nothing it does not have to.
  void feed(uint32_t t_ms, const bool level[N_LINES]);

  // Drain completed events. More than one can complete in a single feed()
  // when both levers are released in the same tick, which a single
  // `event()` accessor would silently drop.
  bool popEvent(Event& out);

  bool pressed(uint8_t line) const {
    return line < N_LINES && st[line].stable;
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
  };
  static constexpr size_t EV_N = 8;

  LineState st[N_LINES];
  Event     q[EV_N];
  size_t    q_head = 0, q_count = 0;
  uint32_t  lost = 0;

  void push(const Event& e);
};
