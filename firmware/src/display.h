#pragma once
#include <stdint.h>
#include <stddef.h>

// Ascaso Dream PID front-display bus. Decoded over 0b (2026-09-28) and
// 0e (2026-10-03). See CLAUDE.md for the probe point and the measured
// protocol; the captures everything here was derived from, with their
// channel mapping and sample rates, are in analysis/captures/.
//
// Plain synchronous serial, NOT I²C whatever sigrok's i2c decoder claims:
// a ~9.9 kHz clock, data stable across a whole clock period and sampled on
// the RISING edge.
//
// FRAME STRUCTURE. Every ~41 ms the bus sends a 67-bit frame carrying the
// BUTTON state, then a 66-bit frame carrying the DIGITS. The gap between
// the pair usually falls under FRAME_GAP_US, so they arrive glued together
// as one 133-bit frame — 67 + 66 = 133. When the gap runs long they arrive
// separately, which happens 1–8 % of the time depending on how hard the
// heater is working. An earlier version of this decoder only understood
// the merged form and silently dropped every split digit frame, including
// the timer's own `000`.
//
// DIGITS. Three 7-segment fields, MSB = segment a, order abcdefg. Idle the
// display shows boiler temperature in °C; during a brew the machine takes
// it over for a shot timer in tenths of a second. That timer is the
// machine's own pump-on to pump-off measurement — a truer shot boundary
// than a scale can infer. It does NOT say which switch direction ran:
// 1Cup and 2Cup emit byte-identical button frames, so J2 sensing (0c)
// still has a job.
//
// THE MODE BIT. Bit 47 of the digit frame is set while the display is
// showing a timer and clear while it is showing a temperature — verified
// across every capture, including a setpoint ramp through 96…120 °C where
// it stays clear. So "100" is unambiguous. This decoder used to guess,
// by assuming three digits that stopped changing for 1.5 s must be a
// temperature; the machine had been saying so outright all along.
//
// LETTERS. The display is not digits-only: the SET UP menu renders Ud, Pr,
// Cr, OFF and U, and a short press of the left button shows PrG while the
// brew temperature is being changed — which is exactly the event that
// silently invalidates an 0f noise-floor run. Note `S` is the same seven
// segments as `5` and `O` the same as `0`, so `PrS` reads `Pr5` and `OFF`
// reads `0FF`. That is the display's limitation, not ours.
//
// Pure logic, exactly like ShotDetector: no I/O, no clock of its own. It
// consumes edges timestamped by the caller at arrival, so ISR latency can
// never distort a frame and the whole thing is host-testable against real
// recorded frames (firmware/test/host/display_test.cpp).
class DisplayBus {
public:
  // Frame lengths are matched EXACTLY. Anything else is rejected, which is
  // what keeps the ~564-bit initialisation burst the machine emits for a
  // second after power-on from being truncated to MAX_BITS and decoded as
  // a temperature of 0.
  static constexpr size_t BUTTON_FRAME_BITS = 67;
  static constexpr size_t DIGIT_FRAME_BITS  = 66;
  static constexpr size_t MERGED_FRAME_BITS = BUTTON_FRAME_BITS + DIGIT_FRAME_BITS;  // 133
  static constexpr size_t MAX_BITS          = 192;   // 133 + slack; longer bursts clamp here and are rejected

  static constexpr uint32_t FRAME_GAP_US = 1000;     // clock idle this long ends a frame

  // BLANK is its own mode on purpose. An all-blank display is NOT zero
  // degrees, and folding it into TEMPERATURE made the decoder report
  // 0 °C every time the display blinked — which it does, constantly,
  // whenever the setpoint is being adjusted.
  //
  // SETPOINT is a number shown while the display is blinking: the value
  // being dialled in, not the boiler. Mode::TEMPERATURE used to cover
  // both, so a ramp to 120 looked like a boiler at 120.
  enum class Mode : uint8_t { NONE, TEMPERATURE, TIMER, TEXT, BLANK, SETPOINT };

  // While the setpoint is being adjusted the whole display blinks: runs of
  // ~5 blank frames 41 ms apart, a value, then blank again ~250 ms later.
  // Two blank frames this close together mean a blink; a single one does
  // not, which is what separates adjust mode from the lone blank a cold
  // boot emits before the first reading.
  static constexpr uint32_t BLINK_WINDOW_US = 600000;

  struct Reading {
    Mode     mode = Mode::NONE;
    char     text[4] = {0};      // as shown, e.g. " 95", "169", "PrG"
    int16_t  temp_c = INT16_MIN; // Mode::TEMPERATURE only
    uint16_t timer_dl = 0;       // Mode::TIMER only — deciseconds
    uint32_t at_us = 0;          // timestamp of the frame that produced it
  };

  // Front-panel buttons, from the 67-bit frame. Active low on the wire;
  // exposed here as pressed/not. Left enters temperature programming and
  // decreases; right increases. Both together for ~3 s opens the SET UP
  // menu — the only gesture that asserts both.
  struct Buttons {
    bool     left  = false;
    bool     right = false;
    uint32_t at_us = 0;
  };

  // Call on every RISING clock edge with the data line's level.
  // Returns true when a frame completed and yielded a fresh DISPLAY
  // reading. Button frames update buttons() but return false — they carry
  // no digits.
  bool feedEdge(uint32_t t_us, uint8_t data_bit);

  // Call from the main loop so a final frame still completes when the bus
  // goes quiet (machine switched off mid-frame). Returns as feedEdge().
  bool poll(uint32_t t_us);

  const Reading& reading() const { return last; }
  const Buttons& buttons() const { return btn; }

  // Showing the `PrG` banner right now. This is a TRANSIENT: it appears
  // for a frame or two on entering temperature programming and then the
  // display goes back to blinking numbers. Do not use it to mean "the
  // setpoint is being changed" — in one capture only 2 of 372 digit
  // frames read PrG while the adjustment ran for seven seconds.
  bool programming() const {
    return last.mode == Mode::TEXT && last.text[0] == 'P' && last.text[2] == 'G';
  }

  // The display is blinking, i.e. a value is being edited. This is the
  // sustained signal `programming()` is not.
  bool adjusting() const { return adjust; }

  // Latched: someone has entered temperature programming or edited a value
  // since this was last cleared. THIS is what belongs on a shot record —
  // a setpoint change is the event that silently invalidates a noise-floor
  // run, and it is over long before anyone reads a level.
  bool setpointTouched() const { return touched; }
  void clearSetpointTouched() { touched = false; }

  bool stale(uint32_t t_us, uint32_t max_age_us) const {
    return last.mode == Mode::NONE || (t_us - last.at_us) > max_age_us;
  }

  // Call when the reader is attached to a live bus — `display on`, and the
  // boot path that restores it from NVS. The ISR starts wherever the bus
  // happens to be, so the first frame assembled is a FRAGMENT of the one
  // already in progress: arbitrary length, arbitrary alignment. It is
  // garbage and should not be decoded.
  //
  // In practice the fragment is usually rejected by LENGTH first — only
  // ~2.4 % of attach points land on 66/67/133 — so the guard normally
  // spends itself on the first *good* frame instead. That costs one frame,
  // ~41 ms, to prevent a bogus decode 2.4 % of the time. Cheap end of the
  // trade. On the boot path the whole init burst is non-decodable, so the
  // guard survives it and is spent on the first 66-bit reading at +4.1 s.
  //
  // What it actually decodes as, measured over 60000 simulated attach
  // points against three real captures: **`-88`-class readings**, 37–51
  // times per capture. Not blanks — ZERO blank-decoding fragments on a
  // non-blinking display. Blast radius is small (`-` fails the numeric
  // test, so it lands in Mode::TEXT, which MachineContextTracker ignores)
  // but a reading invented from the wrong bit offsets has no business
  // reaching a consumer.
  //
  // There is a structural reason the obvious fear does not materialise:
  // the digit field offsets 106/115/124 are exactly 67 — the button-frame
  // length — above 39/48/57, so **the digit frame is the last 66 bits of
  // the merged 133**. A tail fragment of exactly 66 bits IS the correctly
  // aligned digit frame. The one length that reaches readDigits() is
  // self-correcting.
  //
  // DO NOT cite this as the cause of the 2026-10-07 SETPOINT-TOUCHED. It
  // was first attributed here and that was wrong twice over: a clean
  // attach yields one fragment, one blank cannot latch, and fragments do
  // not decode as blanks anyway. The cause was reproduced on the bench
  // instead — see docs/phase-0-status.md.
  //
  // It also drops a half-frame left behind by a previous detach, which
  // used to be flushed on the next attach stamped with the pre-detach
  // timestamp. Nobody had hit that; it is fixed here regardless.
  void beginCapture();

  // Exposed for the host test: decode one complete frame directly.
  bool decodeFrame(const uint8_t* bits, size_t n, uint32_t t_us);

private:
  // Offsets WITHIN the 66-bit digit frame. In a merged frame add 67.
  static constexpr size_t DIGIT_OFFSET[3]  = {39, 48, 57};
  static constexpr size_t TIMER_FLAG_BIT   = 47;
  // Offsets within the 67-bit button frame. Active low.
  static constexpr size_t BTN_LEFT_BIT     = 49;
  static constexpr size_t BTN_RIGHT_BIT    = 58;

  uint8_t  buf[MAX_BITS] = {0};
  size_t   count = 0;
  uint32_t last_edge_us = 0;
  bool     have_edge = false;
  bool     skip_first = false;   // armed by beginCapture(); see there

  Reading  last;
  Buttons  btn;

  uint32_t last_blank_us = 0;
  uint32_t prev_blank_us = 0;
  uint8_t  blanks = 0;          // saturates at 2
  bool     adjust = false;
  bool     touched = false;

  static char glyph(const uint8_t* bits);          // 7 bits -> character, 0 if unknown
  void readButtons(const uint8_t* bits, uint32_t t_us);
  bool readDigits(const uint8_t* bits, uint32_t t_us);
  bool finishFrame(uint32_t t_us);
};
