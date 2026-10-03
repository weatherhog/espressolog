// Host-side test for DisplayBus, milestone 0b.
//
// Every frame below is REAL — lifted verbatim from the captures in
// analysis/captures/, recorded off the machine on 2026-09-28 while the
// display showed 95 degrees and then ran its shot timer during a flush.
// Synthetic frames would only prove the decoder agrees with itself.
#include "../../src/display.h"
#include <cstdio>
#include <cstring>
#include <cassert>

// 133 bits, recorded from the machine
static const char FRAME_95[] =
  "0000000000000000000000000000000011100010000000101111111101111111"
  "1100000000000000000000000000000000000001000000000001111011001011"
  "01100";

// 67 bits, recorded from the machine
static const char FRAME_SHORT[] =
  "0000000000000000000000000000000011100010000000101111111101111111"
  "110";

// 133 bits, recorded from the machine
static const char FRAME_001[] =
  "0000000000000000000000000000000011100010000000101111111101111111"
  "1100000000000000000000000000000000000001001111110011111110000110"
  "00000";

// 133 bits, recorded from the machine
static const char FRAME_010[] =
  "0000000000000000000000000000000011100010000000101111111101111111"
  "1100000000000000000000000000000000000001001111110010110000001111"
  "11000";

// 133 bits, recorded from the machine
static const char FRAME_099[] =
  "0000000000000000000000000000000011100010000000101111111101111111"
  "1100000000000000000000000000000000000001001111110011111011001111"
  "01100";

// 133 bits, recorded from the machine
static const char FRAME_100[] =
  "0000000000000000000000000000000011100010000000101111111101111111"
  "1100000000000000000000000000000000000001000110000011111110001111"
  "11000";

// 133 bits, recorded from the machine
static const char FRAME_169[] =
  "0000000000000000000000000000000011100010000000101111111101111111"
  "1100000000000000000000000000000000000001000110000011011111001111"
  "01100";

// --- 2026-10-03, through the J5 T-piece. See analysis/captures/. ---

// 66 bits: a STANDALONE digit frame, the half that used to be thrown away
// whenever the gap to its button frame ran over 1 ms. Reads " 95".
static const char FRAME_66_TEMP[] =
  "0000000000000000000000000000000000001000000000001111011001011011"
  "00";

// 66 bits, standalone, timer flag set. Reads "000" — the timer's own first
// value, which the old decoder never once saw.
static const char FRAME_66_TIMER[] =
  "0000000000000000000000000000000000001001111110011111110001111110"
  "00";

// 133 bits reading "100" with the timer flag CLEAR: a real temperature,
// captured while the setpoint was being ramped past 100 C. Same three
// glyphs as FRAME_100 below, opposite meaning. This pair is the whole
// reason the mode bit matters.
static const char FRAME_TEMP_100[] =
  "0000000000000000000000000000000011100010000000101111111101011111"
  "1100000000000000000000000000000000000001000110000001111110001111"
  "11000";

// 133 bits reading "PrG" — the display while the brew temperature is being
// changed. The old decoder dropped this as garbled.
static const char FRAME_PRG[] =
  "0000000000000000000000000000000011100010000000101011111101111111"
  "1100000000000000000000000000000000000001001100111000000101001011"
  "11000";

// 67-bit button frames: left held, right held, both held. "Both" occurs
// only during the SET UP entry gesture.
static const char FRAME_BTN_LEFT[] =
  "0000000000000000000000000000000011100010000000101011111101111111"
  "110";
static const char FRAME_BTN_RIGHT[] =
  "0000000000000000000000000000000011100010000000101111111101011111"
  "110";
static const char FRAME_BTN_BOTH[] =
  "0000000000000000000000000000000011100010000000101011111101011111"
  "110";

// 133 bits, all three digit fields blank. Real — this is the dark half of
// the blink the display does while a value is being edited. 99 of them in
// the capture it came from.
static const char FRAME_BLANK[] =
  "0000000000000000000000000000000011100010000000101111111101011111"
  "1100000000000000000000000000000000000001000000000000000000000000"
  "00000";

static uint8_t scratch[DisplayBus::MAX_BITS];

// Turn a "0110..." literal into the bit array the decoder consumes.
static size_t bits(const char* s) {
  size_t n = strlen(s);
  assert(n <= DisplayBus::MAX_BITS);
  for (size_t i = 0; i < n; i++) scratch[i] = (uint8_t)(s[i] - '0');
  return n;
}

static bool decode(DisplayBus& d, const char* frame, uint32_t t_us) {
  size_t n = bits(frame);
  return d.decodeFrame(scratch, n, t_us);
}

int main() {
  // 1. The static capture: 114 s of the display sitting at 95 C.
  {
    DisplayBus d;
    assert(decode(d, FRAME_95, 1000));
    assert(d.reading().mode == DisplayBus::Mode::TEMPERATURE);
    assert(strcmp(d.reading().text, " 95") == 0);
    assert(d.reading().temp_c == 95);
    printf("static 95 C OK\n");
  }

  // 2. Short (67-bit) frames carry no digits and must be ignored outright.
  {
    DisplayBus d;
    assert(!decode(d, FRAME_SHORT, 1000));
    assert(d.reading().mode == DisplayBus::Mode::NONE);
    printf("short frame rejected OK\n");
  }

  // 3. The shot timer, in tenths of a second. Values stepping 100 ms apart
  //    are a running timer, not a temperature.
  {
    DisplayBus d;
    struct { const char* f; uint16_t dl; } steps[] = {
      {FRAME_001, 1}, {FRAME_010, 10}, {FRAME_099, 99},
      {FRAME_100, 100}, {FRAME_169, 169},
    };
    uint32_t t = 1000;
    for (auto& s : steps) {
      t += 100000;                       // 100 ms apart
      assert(decode(d, s.f, t));
      assert(d.reading().mode == DisplayBus::Mode::TIMER);
      assert(d.reading().timer_dl == s.dl);
    }
    assert(strcmp(d.reading().text, "169") == 0);
    printf("shot timer 0.1 s .. 16.9 s OK\n");
  }

  // 4. "100" is a timer or a temperature depending on a bit the machine
  //    sets, not on how long it has sat still. Both frames below are real
  //    and both read "100": one was captured during a flush, the other
  //    while the setpoint was ramped past 100 C. The decoder used to guess
  //    between them with a 1.5 s staleness heuristic.
  {
    DisplayBus d;
    assert(decode(d, FRAME_100, 1000));
    assert(d.reading().mode == DisplayBus::Mode::TIMER);
    assert(d.reading().timer_dl == 100);
    assert(strcmp(d.reading().text, "100") == 0);

    assert(decode(d, FRAME_TEMP_100, 2000));
    assert(d.reading().mode == DisplayBus::Mode::TEMPERATURE);
    assert(d.reading().temp_c == 100);
    assert(strcmp(d.reading().text, "100") == 0);

    // And the timer stays a timer however long it holds still — a paused
    // reading is not evidence of anything.
    assert(decode(d, FRAME_100, 2000 + 10 * 1000000));
    assert(d.reading().mode == DisplayBus::Mode::TIMER);
    printf("timer vs temperature at 100 resolved by the mode bit OK\n");
  }

  // 5. Frame assembly: the decoder finds frame boundaries from clock idle,
  //    exactly as the ISR will feed them, and never from a bit count.
  {
    DisplayBus d;
    size_t n = bits(FRAME_95);
    uint8_t frame[DisplayBus::MAX_BITS];
    memcpy(frame, scratch, n);

    uint32_t t = 5000;
    for (size_t i = 0; i < n; i++) {
      assert(!d.feedEdge(t, frame[i]));     // mid-frame: nothing completes
      t += 101;                             // ~9.9 kHz clock
    }
    assert(d.reading().mode == DisplayBus::Mode::NONE);

    t += 26000;                             // the ~26 ms gap between frames
    bool got = d.feedEdge(t, frame[0]);     // first edge of the next frame
    assert(got);
    assert(d.reading().temp_c == 95);
    printf("frame assembly from clock idle OK\n");
  }

  // 6. poll() completes a frame when the bus simply stops — the machine
  //    being switched off must not strand the last reading.
  {
    DisplayBus d;
    size_t n = bits(FRAME_95);
    uint8_t frame[DisplayBus::MAX_BITS];
    memcpy(frame, scratch, n);
    uint32_t t = 5000;
    for (size_t i = 0; i < n; i++) { d.feedEdge(t, frame[i]); t += 101; }
    assert(!d.poll(t));                     // gap not long enough yet
    assert(d.poll(t + 50000));
    assert(d.reading().temp_c == 95);
    printf("poll() flushes a trailing frame OK\n");
  }

  // 7. A corrupted digit field must discard the whole frame rather than
  //    report a wrong number. Silence beats a plausible lie.
  {
    DisplayBus d;
    assert(decode(d, FRAME_95, 1000));
    int16_t good = d.reading().temp_c;

    char bad[256];
    strcpy(bad, FRAME_95);
    bad[124] = bad[124] == '1' ? '0' : '1';   // flip a segment of the units digit
    bad[125] = bad[125] == '1' ? '0' : '1';
    assert(!decode(d, bad, 2000));
    assert(d.reading().temp_c == good);       // previous reading untouched
    printf("garbled frame discarded OK\n");
  }

  // 8. Staleness: a consumer must be able to tell a live reading from an
  //    old one, because the bus going quiet looks exactly like a steady
  //    temperature otherwise.
  {
    DisplayBus d;
    assert(decode(d, FRAME_95, 1000000));
    assert(!d.stale(1000000 + 500000, 2000000));
    assert(d.stale(1000000 + 3000000, 2000000));
    printf("staleness OK\n");
  }

  // 9. Standalone 66-bit digit frames. These are the ones the old decoder
  //    dropped — 1 to 8 % of all digit frames, worst while the boiler is
  //    recovering after a brew.
  {
    DisplayBus d;
    assert(decode(d, FRAME_66_TEMP, 1000));
    assert(d.reading().mode == DisplayBus::Mode::TEMPERATURE);
    assert(d.reading().temp_c == 95);

    assert(decode(d, FRAME_66_TIMER, 2000));
    assert(d.reading().mode == DisplayBus::Mode::TIMER);
    assert(d.reading().timer_dl == 0);
    assert(strcmp(d.reading().text, "000") == 0);
    printf("standalone 66-bit digit frames OK\n");
  }

  // 10. Buttons, from the 67-bit frame. A button frame yields no display
  //     reading, so it must return false while still updating state.
  {
    DisplayBus d;
    assert(!decode(d, FRAME_SHORT, 1000));
    assert(!d.buttons().left && !d.buttons().right);

    assert(!decode(d, FRAME_BTN_LEFT, 2000));
    assert(d.buttons().left && !d.buttons().right);

    assert(!decode(d, FRAME_BTN_RIGHT, 3000));
    assert(!d.buttons().left && d.buttons().right);

    assert(!decode(d, FRAME_BTN_BOTH, 4000));
    assert(d.buttons().left && d.buttons().right);
    assert(d.buttons().at_us == 4000);
    printf("button state from 67-bit frames OK\n");
  }

  // 11. A merged frame carries both halves: buttons AND digits.
  {
    DisplayBus d;
    assert(decode(d, FRAME_TEMP_100, 1000));      // right button was held
    assert(d.reading().temp_c == 100);
    assert(d.buttons().right && !d.buttons().left);
    printf("merged frame yields buttons and digits OK\n");
  }

  // 12. PrG is a reading, not a garbled frame. It means someone is
  //     changing the brew temperature, which silently invalidates an 0f
  //     noise-floor run if it goes unrecorded.
  {
    DisplayBus d;
    assert(decode(d, FRAME_PRG, 1000));
    assert(d.reading().mode == DisplayBus::Mode::TEXT);
    assert(strcmp(d.reading().text, "PrG") == 0);
    assert(d.programming());
    assert(d.reading().temp_c == INT16_MIN);       // not a temperature

    assert(decode(d, FRAME_95, 2000));
    assert(!d.programming());
    printf("PrG decoded as text, not discarded OK\n");
  }

  // 13. The power-on initialisation burst. For about a second after the
  //     machine is switched on the bus emits a ~564-bit run; clamped to
  //     MAX_BITS its three digit fields read blank, and the old decoder
  //     turned that into a boiler temperature of 0 C.
  {
    DisplayBus d;
    assert(decode(d, FRAME_95, 1000));
    int16_t good = d.reading().temp_c;

    uint32_t t = 10000;
    for (int i = 0; i < 564; i++) { d.feedEdge(t, 0); t += 101; }
    assert(!d.poll(t + 50000));                    // burst produces nothing
    assert(d.reading().temp_c == good);            // and leaves 95 alone
    printf("power-on init burst rejected OK\n");
  }

  // 14. A blank display is NOT zero degrees. This is the bug that shipped
  //     in the first draft of 0.8.0: ' ' was skipped in the numeric
  //     accumulate, so "   " came out as a perfectly confident 0 C — and
  //     the display blanks constantly, 99 times in one 30 s capture.
  {
    DisplayBus d;
    assert(decode(d, FRAME_95, 1000));
    assert(d.reading().temp_c == 95);

    assert(decode(d, FRAME_BLANK, 2000));
    assert(d.reading().mode == DisplayBus::Mode::BLANK);
    assert(d.reading().temp_c == INT16_MIN);       // NOT 0
    assert(strcmp(d.reading().text, "   ") == 0);
    printf("blank display is not 0 C OK\n");
  }

  // 15. Two blanks close together are the blink, and a number shown while
  //     blinking is the setpoint being dialled in — not the boiler. The
  //     ramp that produced FRAME_TEMP_100 ran 95 -> 120; reporting that as
  //     boiler temperature would put 120 C into the log.
  {
    DisplayBus d;
    assert(decode(d, FRAME_BLANK, 1000));
    assert(!d.adjusting());                        // one blank proves nothing
    assert(decode(d, FRAME_BLANK, 1000 + 41000));  // 41 ms later: a blink
    assert(d.adjusting());

    assert(decode(d, FRAME_TEMP_100, 1000 + 250000));
    assert(d.reading().mode == DisplayBus::Mode::SETPOINT);
    assert(d.reading().temp_c == 100);
    printf("blink detected, value read as setpoint not boiler OK\n");
  }

  // 16. A cold boot emits exactly ONE blank before the first reading.
  //     That must not look like a blink, or every power-on would be
  //     logged as someone adjusting the setpoint.
  {
    DisplayBus d;
    assert(decode(d, FRAME_BLANK, 1000));
    assert(!d.adjusting());
    assert(!d.setpointTouched());
    assert(decode(d, FRAME_95, 1000 + 511000));    // measured boot gap
    assert(d.reading().mode == DisplayBus::Mode::TEMPERATURE);
    assert(d.reading().temp_c == 95);
    assert(!d.adjusting());
    printf("lone boot blank is not a blink OK\n");
  }

  // 17. The latch. PrG is transient and the blink stops the moment the
  //     user walks away, so a consumer that samples a level will miss the
  //     whole event. A shot record needs "did anyone touch this".
  {
    DisplayBus d;
    assert(!d.setpointTouched());
    assert(decode(d, FRAME_PRG, 1000));
    assert(d.setpointTouched());

    assert(decode(d, FRAME_95, 1000 + 5000000));   // 5 s later, back to normal
    assert(!d.adjusting());
    assert(d.setpointTouched());                   // still latched
    d.clearSetpointTouched();
    assert(!d.setpointTouched());
    printf("setpoint-touched latch OK\n");
  }

  // 18. The blink stops: a number well clear of the last blank is the
  //     boiler again, not a setpoint.
  {
    DisplayBus d;
    assert(decode(d, FRAME_BLANK, 1000));
    assert(decode(d, FRAME_BLANK, 1000 + 41000));    // 41 ms: a blink
    assert(d.adjusting());
    assert(decode(d, FRAME_95, 1000 + 41000 + 2000000));  // 2 s of no blinking
    assert(!d.adjusting());
    assert(d.reading().mode == DisplayBus::Mode::TEMPERATURE);
    printf("blink expiry returns to temperature OK\n");
  }

  printf("\nall display bus tests passed\n");
  return 0;
}
