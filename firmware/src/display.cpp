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

  // An all-blank display is not a reading of zero. Track it, because the
  // interval between blank ONSETS is what says which blink this is — see
  // BLINK_WINDOW_US in the header.
  if (text[0] == ' ' && text[1] == ' ' && text[2] == ' ') {
    // An onset: the first blank after a value shown long enough to be real.
    // Shown for less is a glitch inside one blank — a continuation.
    if (!prev_blank && (!have_onset || t_us - first_shown_us >= MIN_SHOWN_US)) {
      if (have_onset) {
        uint32_t period = t_us - last_onset_us;
        if (period < BLINK_WINDOW_US) {
          adjust  = true;
          touched = true;
          target  = false;                   // an edit, whatever came before
        } else if (period < TARGET_BLINK_MAX_US) {
          // Set even while an edit is live (adjust wins the decode while it
          // lasts). When in doubt, a value is NOT the boiler: if an edit's
          // one shown frame was dropped this costs ~1 s of TARGET after the
          // edit, and refusing it instead let steam values count as the
          // boiler ~1 s early after an edit made during steam (review,
          // 2026-11-14).
          target = true;
        }
      }
      last_onset_us = t_us;
      have_onset    = true;
    }
    prev_blank    = true;
    last_blank_us = t_us;
    last.mode = Mode::BLANK;
    return true;
  }
  if (prev_blank) first_shown_us = t_us;
  prev_blank = false;
  expireBlinks(t_us);

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
  } else if (target) {
    // Slow blink: steam's 165, or the 95 it is cooling back to. A target,
    // not the boiler — and not an edit, so nothing latches.
    last.mode   = Mode::TARGET;
    last.temp_c = (int16_t)value;
  } else {
    last.mode   = Mode::TEMPERATURE;
    last.temp_c = (int16_t)value;
  }
  return true;
}

// A blink ends when no blank has come for long enough. Called on every shown
// frame and from poll(), so a machine switched off mid-blink — no frames at
// all — does not leave an onset armed to pair with the cold-boot blank. Once
// neither blink is live the old onset is forgotten too, so an onset hours
// later cannot pair with it (and micros() wrap cannot alias it). The compare
// is signed so that a caller passing a time older than the last blank does
// nothing rather than reading it as ~71 minutes; in firmware that cannot
// happen — pollDisplay() drains the ISR ring before it reads micros() — so
// this is a guard for the host tests and future callers, not a fix.
void DisplayBus::expireBlinks(uint32_t t_us) {
  int32_t since_blank = (int32_t)(t_us - last_blank_us);
  if (adjust && since_blank > (int32_t)BLINK_WINDOW_US) adjust = false;
  if (since_blank > (int32_t)TARGET_END_US) { target = false; have_onset = false; }
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

// See the header for why the first frame after attaching is discarded.
void DisplayBus::beginCapture() {
  count         = 0;      // also drops any half-frame left by a detach
  have_edge     = false;  // and the stale timestamp that would have stamped it
  skip_first    = true;
  last_blank_us = 0;
  last_onset_us = 0;
  have_onset    = false;
  prev_blank    = false;
  first_shown_us = 0;
  adjust        = false;
  target        = false;
  touched       = false;  // a fresh attach starts with a clean latch; we
                          // were not watching while it was detached.
  last          = Reading{};    // not just mode: printDisplay() shows
  btn           = Buttons{};    // last.text, so a partial reset leaves a
                                // stale reading on screen next to "none".
}

bool DisplayBus::finishFrame(uint32_t t_us) {
  size_t n = count;
  count = 0;
  if (n == 0) return false;
  if (skip_first) {
    // Spend the guard only on a frame that WOULD have been decoded. The
    // power-on activity is full of one-bit blips (32-33 of them: 32 clock
    // pulses before the init sequence, plus the rail rise on some boots; see
    // the power-on table in docs/machine-display-bus.md) and decodeFrame()
    // rejects those anyway; letting one
    // consume the guard would hand the real misaligned fragment straight
    // through, which is the opposite of the intent.
    if (n == MERGED_FRAME_BITS || n == BUTTON_FRAME_BITS || n == DIGIT_FRAME_BITS)
      skip_first = false;
    return false;
  }
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
  // Finish the pending frame FIRST, at its own timestamp, then expire — the
  // pending frame belongs to the past, the expiry to now. Rarely matters in
  // firmware: the ISR ring fills ~160 ms into a loop() stall and the frames
  // it caught are finished by feedEdge() during the drain, so what is left
  // pending is normally a fragment. It matters when the bus goes quiet
  // straight after a complete frame (power-off mid-blink) and poll() is
  // late.
  bool fresh = false;
  if (have_edge && count != 0 && (uint32_t)(t_us - last_edge_us) > FRAME_GAP_US)
    fresh = finishFrame(last_edge_us);
  if (have_onset || adjust || target) expireBlinks(t_us);
  return fresh;
}
