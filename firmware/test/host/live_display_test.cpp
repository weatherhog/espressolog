// Host test for LiveDisplayGate — when the machine display goes out on the
// live stream. Every case is a behaviour seen or reviewed on 2026-11-13.
#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include "../../src/live_display.h"
#include <cstdio>
#include <cstring>
#include <cassert>

static DisplayBus::Reading rd(DisplayBus::Mode m, const char* text, uint32_t at_us,
                              int16_t c = 0, uint16_t dl = 0) {
  DisplayBus::Reading r;
  r.mode = m;
  memcpy(r.text, text, 3);
  r.text[3] = 0;
  r.temp_c = c;
  r.timer_dl = dl;
  r.at_us = at_us;
  return r;
}
static DisplayBus::Reading temp(int16_t c, uint32_t at_us) {
  char t[4];
  snprintf(t, sizeof(t), "%3d", c);
  return rd(DisplayBus::Mode::TEMPERATURE, t, at_us, c);
}

// Feeds one frame, as main.cpp does: several loop() polls per decoded frame.
// Returns how many sends happened; the last frame sent lands in `last`.
struct Rig {
  LiveDisplayGate g;
  uint32_t now_ms = 0;
  bool up = true, send_ok = true, stale = false;
  int sends = 0, attempts = 0;
  char last[128] = {0};
  void frame(const DisplayBus::Reading& r, int polls = 4, uint32_t step_ms = 10) {
    for (int i = 0; i < polls; i++) {
      char out[128];
      if (g.poll(r, stale, up, now_ms, out, sizeof(out))) {
        attempts++;
        if (send_ok) { g.sent(now_ms); sends++; strcpy(last, out); }
        else g.failed(now_ms);
      }
      now_ms += step_ms;
    }
  }
};

int main() {
  // 1. A steady temperature goes out once, then only as a heartbeat.
  {
    Rig k;
    uint32_t us = 1000000;
    for (int i = 0; i < 10; i++) { k.frame(temp(95, us)); us += 41000; }
    assert(k.sends == 1);
    assert(strstr(k.last, "\"mode\":\"temp\"") && strstr(k.last, "\"c\":95"));
    for (int i = 0; i < 130; i++) { k.frame(temp(95, us)); us += 41000; }   // ~5.6 s
    assert(k.sends == 2);
    printf("1. steady reading: once, then the heartbeat OK\n");
  }

  // 2. The 2026-11-13 glitch: one TIMER frame between temperature frames,
  //    re-polled by a stalled loop for 300 ms, must never be sent.
  {
    Rig k;
    uint32_t us = 1000000;
    for (int i = 0; i < 5; i++) { k.frame(temp(95, us)); us += 41000; }
    int before = k.sends;
    k.frame(rd(DisplayBus::Mode::TIMER, " 95", us, 0, 95), 30, 10);  // one frame, 300 ms of polls
    us += 41000;
    for (int i = 0; i < 5; i++) { k.frame(temp(95, us)); us += 41000; }
    assert(k.sends == before);
    assert(!strstr(k.last, "timer"));
    printf("2. a lone TIMER frame is not sent, however long the loop stalls OK\n");
  }

  // 3. A real timer is sent within a few frames, and its value follows.
  {
    Rig k;
    uint32_t us = 1000000;
    k.frame(temp(95, us)); us += 41000;
    for (int d = 1; d <= 30; d++) {
      char t[4]; snprintf(t, sizeof(t), "%03d", d);
      k.frame(rd(DisplayBus::Mode::TIMER, t, us, 0, d)); us += 41000;
      k.frame(rd(DisplayBus::Mode::TIMER, t, us, 0, d)); us += 59000;
    }
    assert(strstr(k.last, "\"mode\":\"timer\""));
    assert(k.sends >= 8);   // capped at 4 Hz over ~3 s, not one per digit
    printf("3. a real timer goes out and keeps updating, rate-capped OK\n");
  }

  // 4. Blink blanks are not sent; a display that stays blank is.
  {
    Rig k;
    uint32_t us = 1000000;
    k.frame(temp(95, us)); us += 41000;
    k.frame(temp(95, us)); us += 41000;
    k.frame(temp(95, us)); us += 41000;
    for (int cycle = 0; cycle < 5; cycle++) {
      for (int i = 0; i < 5; i++) { k.frame(rd(DisplayBus::Mode::BLANK, "   ", us)); us += 41000; }
      for (int i = 0; i < 5; i++) {
        k.frame(rd(DisplayBus::Mode::SETPOINT, " 96", us, 96)); us += 41000;
      }
    }
    assert(!strstr(k.last, "blank"));
    assert(strstr(k.last, "setpoint"));
    for (int i = 0; i < 40; i++) { k.frame(rd(DisplayBus::Mode::BLANK, "   ", us)); us += 41000; }
    assert(strstr(k.last, "\"mode\":\"blank\""));
    printf("4. blink blanks stay off the stream, a lasting blank goes out OK\n");
  }

  // 5. A failed send is not marked sent, and a reconnect gets the reading
  //    at once instead of waiting for the heartbeat.
  {
    Rig k;
    uint32_t us = 1000000;
    k.up = false;
    for (int i = 0; i < 10; i++) { k.frame(temp(95, us)); us += 41000; }
    assert(k.sends == 0);
    k.up = true;
    k.frame(temp(95, us)); us += 41000;
    assert(k.sends == 1);
    k.send_ok = false;
    k.frame(temp(94, us)); us += 41000;        // change, but the send fails
    k.send_ok = true;
    for (int i = 0; i < 8; i++) { k.frame(temp(94, us)); us += 41000; }
    assert(k.sends == 2 && strstr(k.last, "\"c\":94"));
    printf("5. failed sends are retried, reconnects are sent at once OK\n");
  }

  // 5b. A reconnect resends an UNCHANGED reading at once — the latch, not the
  //     first-send path.
  {
    Rig k;
    uint32_t us = 1000000;
    for (int i = 0; i < 5; i++) { k.frame(temp(95, us)); us += 41000; }
    assert(k.sends == 1);
    k.up = false;
    k.frame(temp(95, us)); us += 41000;
    k.up = true;
    k.frame(temp(95, us), 1); us += 41000;      // one poll, well inside the 5 s heartbeat
    assert(k.sends == 2);
    printf("5b. a reconnect resends the unchanged reading at once OK\n");
  }

  // 5c. A failed send with the link still up is retried after MIN_GAP_MS,
  //     not on every loop().
  {
    Rig k;
    uint32_t us = 1000000;
    k.send_ok = false;
    for (int i = 0; i < 10; i++) { k.frame(temp(95, us), 10, 5); us += 41000; }  // 500 ms of polls
    assert(k.attempts >= 2 && k.attempts <= 3);
    printf("5c. failed sends back off instead of retrying every loop OK\n");
  }

  // 5d. After the reader goes stale, the hold applies again: a lone glitch
  //     frame on return is not sent, however many frames preceded the gap.
  {
    Rig k;
    uint32_t us = 1000000;
    for (int i = 0; i < 10; i++) { k.frame(temp(95, us)); us += 41000; }
    int before = k.sends;
    k.stale = true;
    k.frame(temp(95, us)); us += 60000000;      // machine off for a minute
    k.stale = false;
    k.frame(temp(20, us), 30, 10); us += 41000;  // same MODE as before the gap, one frame
    assert(k.sends == before);                   // without the reset: sent on this frame
    k.frame(temp(20, us)); us += 41000;
    k.frame(temp(20, us)); us += 41000;
    assert(k.sends == before + 1 && strstr(k.last, "\"c\":20"));
    printf("5d. returning from stale re-earns the hold OK\n");
  }

  // 5e. A failed send, then a link drop and reconnect inside the backoff:
  //     the reconnect is still sent at once.
  {
    Rig k;
    uint32_t us = 1000000;
    for (int i = 0; i < 5; i++) { k.frame(temp(95, us)); us += 41000; }
    k.now_ms += 1000;                           // clear of MIN_GAP_MS since the last send
    k.send_ok = false;
    k.frame(temp(94, us), 1, 1); us += 41000;   // change; this send fails
    assert(k.attempts == 2 && k.sends == 1);
    k.send_ok = true;
    k.up = false; k.frame(temp(94, us), 1, 1); us += 41000;
    k.up = true;  k.frame(temp(94, us), 1, 1); us += 41000;   // 2 ms after the failure
    assert(k.sends == 2 && strstr(k.last, "\"c\":94"));
    printf("5e. a reconnect inside the failed-send backoff is sent at once OK\n");
  }

  // 5f. A failed send, then ~25 days of silence: sending resumes as soon as
  //     the machine is back, not ~25 days later.
  {
    Rig k;
    uint32_t us = 1000000;
    for (int i = 0; i < 5; i++) { k.frame(temp(95, us)); us += 41000; }
    k.now_ms += 1000;
    k.send_ok = false;
    k.frame(temp(94, us), 1, 1); us += 41000;
    assert(k.attempts == 2 && k.sends == 1);
    k.send_ok = true;
    k.stale = true; k.frame(temp(94, us), 1, 1); k.stale = false;
    k.now_ms += 2147483648u + 1000;             // past 2^31 ms, beyond the backoff
    for (int i = 0; i < 5; i++) { k.frame(temp(94, us)); us += 41000; }
    assert(k.sends == 2);
    printf("5f. a failed send does not lock sends out across a long quiet spell OK\n");
  }

  // 5g. A blank that has NOT yet earned its 1.5 s, the reader goes stale,
  //     one BLANK frame on return: not sent. Without the reset the old start
  //     time carries over the gap and confirms it at once.
  {
    Rig k;
    uint32_t us = 1000000;
    for (int i = 0; i < 5; i++) { k.frame(temp(95, us)); us += 41000; }
    for (int i = 0; i < 30; i++) { k.frame(rd(DisplayBus::Mode::BLANK, "   ", us)); us += 41000; }  // 1.23 s
    int before = k.sends;
    k.stale = true; k.frame(rd(DisplayBus::Mode::BLANK, "   ", us)); k.stale = false;
    us += 60000000;                             // a minute later
    k.frame(rd(DisplayBus::Mode::BLANK, "   ", us), 30, 10); us += 41000;
    assert(k.sends == before);
    printf("5g. a blank across a stale gap re-earns its hold OK\n");
  }

  // 6. Nothing from a stale reader.
  {
    LiveDisplayGate g;
    char out[128];
    for (uint32_t i = 0; i < 20; i++)
      assert(!g.poll(temp(95, 1000000 + i * 41000), true, true, i * 50, out, sizeof(out)));
    printf("6. stale readings are never sent OK\n");
  }

  printf("\nall live display tests passed\n");
  return 0;
}
