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

  // 4. Three digits that stop moving are not a timer. This is how a
  //    temperature of 100 C or more stays readable instead of being
  //    reported as a 10.0 s shot that never ends.
  {
    DisplayBus d;
    assert(decode(d, FRAME_100, 1000));
    assert(d.reading().mode == DisplayBus::Mode::TIMER);
    assert(decode(d, FRAME_100, 1000 + 2 * 1000000));   // same value, 2 s later
    assert(d.reading().mode == DisplayBus::Mode::TEMPERATURE);
    assert(d.reading().temp_c == 100);
    printf("stalled 3-digit reading reclassified as temperature OK\n");
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

  printf("\nall display bus tests passed\n");
  return 0;
}
