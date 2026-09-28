#include "display.h"
#include <string.h>

// Seven-segment patterns as they appear on the wire: MSB = segment a,
// then b c d e f g. Read straight off the recorded frames — the timer
// walking 0..9 every 100 ms gave all ten digits in a single flush.
struct SegEntry { const char* pattern; char c; };
static const SegEntry SEGMENTS[] = {
  {"1111110", '0'}, {"0110000", '1'}, {"1101101", '2'}, {"1111001", '3'},
  {"0110011", '4'}, {"1011011", '5'}, {"1011111", '6'}, {"1110000", '7'},
  {"1111111", '8'}, {"1111011", '9'}, {"0000000", ' '}, {"0000001", '-'},
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

bool DisplayBus::decodeFrame(const uint8_t* bits, size_t n, uint32_t t_us) {
  if (n < DIGIT_BITS) return false;          // short frame: carries no digits

  char text[4] = {0};
  for (int d = 0; d < 3; d++) {
    size_t off = DIGIT_OFFSET[d];
    if (off + 7 > n) return false;
    char g = glyph(bits + off);
    if (!g) return false;                    // garbled frame, drop it whole
    text[d] = g;
  }

  // A blank leading position means two digits, which is always a
  // temperature. Three digits is a timer *while it is running*; if the
  // same three glyphs sit still for longer than a timer step could
  // explain, it is a temperature that needed the third digit.
  if (strcmp(text, prev_text) != 0) {
    memcpy(prev_text, text, sizeof(prev_text));
    prev_change_us = t_us;
  }
  bool moving = (t_us - prev_change_us) < TIMER_STALE_US;

  int value = 0;
  bool numeric = true;
  for (int d = 0; d < 3; d++) {
    if (text[d] == ' ') continue;
    if (text[d] < '0' || text[d] > '9') { numeric = false; break; }
    value = value * 10 + (text[d] - '0');
  }
  if (!numeric) return false;

  memcpy(last.text, text, sizeof(last.text));
  last.at_us = t_us;
  if (text[0] == ' ' || !moving) {
    last.mode = Mode::TEMPERATURE;
    last.temp_c = (int16_t)value;
    last.timer_dl = 0;
  } else {
    last.mode = Mode::TIMER;
    last.timer_dl = (uint16_t)value;
    last.temp_c = INT16_MIN;
  }
  return true;
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
