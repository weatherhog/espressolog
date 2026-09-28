#pragma once
#include <stdint.h>
#include <stddef.h>

// Ascaso Dream PID front-display bus, decoded 2026-09-28 (milestone 0b).
// See CLAUDE.md for the probe point and the measured protocol; the raw
// captures this was derived from are in analysis/captures/.
//
// The machine drives its display over a plain synchronous serial link —
// NOT I²C, whatever sigrok's i2c decoder claims. A ~9.9 kHz clock, data
// stable across a whole clock period and sampled on the RISING edge.
// Frames repeat every ~41 ms separated by >1 ms of clock idle, in two
// lengths: 133 bits (carries the digits) and 67 bits (does not).
//
// Three 7-segment fields, MSB = segment a, order abcdefg. Idle the display
// shows boiler temperature in °C; during a brew the machine takes it over
// for a shot timer in tenths of a second. That timer is the machine's own
// pump-on to pump-off measurement — the honest shot boundary that a scale
// can only approximate.
//
// Pure logic, exactly like ShotDetector: no I/O, no clock of its own. It
// consumes edges timestamped by the caller at arrival, so ISR latency can
// never distort the frame and the whole thing is host-testable against
// real recorded frames (firmware/test/host/display_test.cpp).
class DisplayBus {
public:
  static constexpr size_t   MAX_BITS        = 192;   // 133 + slack
  static constexpr size_t   DIGIT_BITS      = 120;   // below this: a short frame, no digits
  static constexpr uint32_t FRAME_GAP_US    = 1000;  // clock idle this long ends a frame
  // A running timer steps every 100 ms. If a three-glyph reading holds
  // still longer than this it is not a timer, so it must be a temperature
  // that happens to need three digits (≥100 °C).
  static constexpr uint32_t TIMER_STALE_US  = 1500000;

  enum class Mode : uint8_t { NONE, TEMPERATURE, TIMER };

  struct Reading {
    Mode     mode = Mode::NONE;
    char     text[4] = {0};      // as shown, e.g. " 95" or "169"
    int16_t  temp_c = INT16_MIN; // Mode::TEMPERATURE only
    uint16_t timer_dl = 0;       // Mode::TIMER only — deciseconds
    uint32_t at_us = 0;          // timestamp of the frame that produced it
  };

  // Call on every RISING clock edge with the data line's level.
  // Returns true when a frame completed and yielded a fresh reading.
  bool feedEdge(uint32_t t_us, uint8_t data_bit);

  // Call from the main loop so a final frame still completes when the bus
  // goes quiet (machine switched off mid-frame). Returns as feedEdge().
  bool poll(uint32_t t_us);

  const Reading& reading() const { return last; }
  bool stale(uint32_t t_us, uint32_t max_age_us) const {
    return last.mode == Mode::NONE || (t_us - last.at_us) > max_age_us;
  }

  // Exposed for the host test: decode one complete frame directly.
  bool decodeFrame(const uint8_t* bits, size_t n, uint32_t t_us);

private:
  static constexpr size_t DIGIT_OFFSET[3] = {106, 115, 124};

  uint8_t  buf[MAX_BITS] = {0};
  size_t   count = 0;
  uint32_t last_edge_us = 0;
  bool     have_edge = false;

  Reading  last;
  char     prev_text[4] = {0};
  uint32_t prev_change_us = 0;

  static char glyph(const uint8_t* bits);   // 7 bits -> character, 0 if unknown
  bool finishFrame(uint32_t t_us);
};
