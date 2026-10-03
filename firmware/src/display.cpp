#include "display.h"
#include <string.h>

// Seven-segment patterns as they appear on the wire: MSB = segment a,
// then b c d e f g. Read straight off recorded frames — the timer walking
// 0..9 every 100 ms gave all ten digits in a single flush, and the SET UP
// menu gave the letters.
//
// S and O are absent on purpose: on seven segments S is identical to 5 and
// O identical to 0, so the machine cannot distinguish them and neither can
// we. `PrS` arrives as `Pr5`, `OFF` as `0FF`. Only context would tell.
struct SegEntry { const char* pattern; char c; };
static const SegEntry SEGMENTS[] = {
  {"1111110", '0'}, {"0110000", '1'}, {"1101101", '2'}, {"1111001", '3'},
  {"0110011", '4'}, {"1011011", '5'}, {"1011111", '6'}, {"1110000", '7'},
  {"1111111", '8'}, {"1111011", '9'}, {"0000000", ' '}, {"0000001", '-'},
  {"1100111", 'P'}, {"0000101", 'r'}, {"1011110", 'G'}, {"1001111", 'E'},
  {"0001111", 't'}, {"0111110", 'U'}, {"0111101", 'd'}, {"1001110", 'C'},
  {"1000111", 'F'},
};

char DisplayBus::glyph(const uint8_t* bits) {
  for (const auto& e : SEGMENTS) {
    bool hit = true;
    for (int i = 0; i < 7; i++) {
      if (bits[i] != (uint8_t)(e.pattern[i] - '0')) { hit = false; break; }
    }
    if (hit) return e.c;
  }
  return 0;   // not a glyph we know — caller discards the frame
}

// `bits` points at a 67-bit button frame. Active low on the wire.
void DisplayBus::readButtons(const uint8_t* bits, uint32_t t_us) {
  btn.left  = (bits[BTN_LEFT_BIT]  == 0);
  btn.right = (bits[BTN_RIGHT_BIT] == 0);
  btn.at_us = t_us;
}

// `bits` points at a 66-bit digit frame.
bool DisplayBus::readDigits(const uint8_t* bits, uint32_t t_us) {
  char text[4] = {0};
  for (int d = 0; d < 3; d++) {
    char g = glyph(bits + DIGIT_OFFSET[d]);
    if (!g) return false;                    // garbled frame, drop it whole
    text[d] = g;
  }

  memcpy(last.text, text, sizeof(last.text));
  last.at_us    = t_us;
  last.temp_c   = INT16_MIN;
  last.timer_dl = 0;

  // An all-blank display is not a reading of zero. Track it, because a
  // REPEATED blank is the blink that means a value is being edited.
  if (text[0] == ' ' && text[1] == ' ' && text[2] == ' ') {
    prev_blank_us = last_blank_us;
    last_blank_us = t_us;
    if (blanks < 2) blanks++;
    if (blanks == 2 && (uint32_t)(last_blank_us - prev_blank_us) < BLINK_WINDOW_US) {
      adjust  = true;
      touched = true;
    }
    last.mode = Mode::BLANK;
    return true;
  }

  // A non-blank reading long after the last blank means the blink stopped.
  if (adjust && (uint32_t)(t_us - last_blank_us) > BLINK_WINDOW_US) {
    adjust = false;
    blanks = 0;
  }

  int value = 0;
  bool numeric = true;
  for (int d = 0; d < 3; d++) {
    if (text[d] == ' ') continue;
    if (text[d] < '0' || text[d] > '9') { numeric = false; break; }
    value = value * 10 + (text[d] - '0');
  }

  if (!numeric) {
    // A word, not a number — PrG, Ud, Cr, 0FF and friends. Worth keeping
    // rather than discarding.
    last.mode = Mode::TEXT;
    if (text[0] == 'P' && text[2] == 'G') touched = true;
    return true;
  }

  // The machine says whether it is a timer. No heuristic, no ambiguity
  // at 100 — a timer frame and a temperature frame can read identically.
  if (bits[TIMER_FLAG_BIT]) {
    last.mode     = Mode::TIMER;
    last.timer_dl = (uint16_t)value;
  } else if (adjust) {
    // Blinking: this is the value being dialled in, not the boiler.
    last.mode   = Mode::SETPOINT;
    last.temp_c = (int16_t)value;
  } else {
    last.mode   = Mode::TEMPERATURE;
    last.temp_c = (int16_t)value;
  }
  return true;
}

bool DisplayBus::decodeFrame(const uint8_t* bits, size_t n, uint32_t t_us) {
  // Exact lengths only. A frame of any other size is noise, a fragment, or
  // the power-on initialisation burst, and decoding it would invent a
  // reading rather than miss one.
  if (n == MERGED_FRAME_BITS) {
    readButtons(bits, t_us);
    return readDigits(bits + BUTTON_FRAME_BITS, t_us);
  }
  if (n == BUTTON_FRAME_BITS) {
    readButtons(bits, t_us);
    return false;                            // no digits in this frame
  }
  if (n == DIGIT_FRAME_BITS) {
    return readDigits(bits, t_us);
  }
  return false;
}

bool DisplayBus::finishFrame(uint32_t t_us) {
  size_t n = count;
  count = 0;
  if (n == 0) return false;
  return decodeFrame(buf, n, t_us);
}

bool DisplayBus::feedEdge(uint32_t t_us, uint8_t data_bit) {
  bool produced = false;
  if (have_edge && (uint32_t)(t_us - last_edge_us) > FRAME_GAP_US) {
    produced = finishFrame(last_edge_us);   // stamp the frame, not the gap
  }
  last_edge_us = t_us;
  have_edge = true;
  if (count < MAX_BITS) buf[count++] = data_bit ? 1 : 0;
  return produced;
}

bool DisplayBus::poll(uint32_t t_us) {
  if (!have_edge || count == 0) return false;
  if ((uint32_t)(t_us - last_edge_us) <= FRAME_GAP_US) return false;
  return finishFrame(last_edge_us);
}
