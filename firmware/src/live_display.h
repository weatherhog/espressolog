#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "display.h"

// Decides when the machine display goes out on the live stream, for the PWA.
// A screen, never a record: the shot's machine facts come from
// MachineContextTracker and the spool header, not from this.
//
// Pure logic, host-tested (test/host/live_display_test.cpp). It sat inline in
// main.cpp for one commit, which is how machine_context.h's first version
// shipped two bugs a green suite could not see.
//
// The rules:
//   - A mode must hold for HOLD_FRAMES consecutive DECODED frames before it
//     is sent. Counted in frames (Reading::at_us changing), not wall-clock,
//     so re-polling one glitch frame through a loop() stall cannot pass it.
//     2026-11-13: one idle frame decoded as TIMER " 95" between two
//     temperature frames; forwarded, it flashed "9.5 s" on the PWA.
//     Cost: the PrG banner (a frame or two) is never forwarded — the PWA
//     shows the blinking setpoint that follows it instead.
//   - BLANK must hold for BLANK_HOLD_US of frame time. The display blinks
//     while a value is dialled (blank runs ~200 ms) and during steam
//     cooldown (~1 s period); only a display that STAYS blank is sent, so
//     a blank machine still reads as present rather than gone.
//   - Changes go out at most every MIN_GAP_MS; an unchanged reading is
//     repeated every HEARTBEAT_MS so a viewer can tell unchanged from gone.
//   - A link that has just come up gets the current reading at once, and a
//     frame is only marked sent when the send succeeded, so a reconnect or
//     a dropped frame does not leave the PWA blank until the heartbeat. A
//     failed send is retried after MIN_GAP_MS, not on every loop().
//   - Leaving stale resets the frame count, so the hold applies again after
//     the machine has been off.
//
// KNOWN LIMIT: frames are counted as this class OBSERVES them, one reading
// per poll. After a loop() stall, two observed frames of one mode can have
// unobserved frames of another between them and still count as consecutive.
// The cost is a cosmetic flash on the PWA; the real fix is counting frames
// inside DisplayBus, which MachineContextTracker needs too.
class LiveDisplayGate {
public:
  static constexpr uint8_t  HOLD_FRAMES   = 3;         // ~82 ms at 41 ms/frame
  static constexpr uint32_t BLANK_HOLD_US = 1500000;
  static constexpr uint32_t MIN_GAP_MS    = 250;
  static constexpr uint32_t HEARTBEAT_MS  = 5000;

  // Call every loop. `stale` is DisplayBus::stale(); `link_up` is whether a
  // send can succeed. Returns true and fills `out` when a frame should go;
  // the caller must then call sent(now_ms) if and only if the send worked.
  bool poll(const DisplayBus::Reading& r, bool stale, bool link_up,
            uint32_t now_ms, char* out, size_t out_len) {
    // Latched until a send succeeds: the link can come up while the reading
    // is still being confirmed, and the edge must not be lost to that.
    if (link_up && !was_up) resend_now = true;
    // A dropped link also ends any backoff: the reconnect is sent at once.
    if (!link_up) { resend_now = false; have_attempt = false; }
    was_up = link_up;
    if (r.mode == DisplayBus::Mode::NONE || stale) {
      // Forget the frame history: the first frames after the machine comes
      // back must earn the hold again, not inherit a count (or, for BLANK, a
      // start time) from before it went quiet.
      have_frame = false;
      cand_frames = 0;
      return false;
    }

    if (!have_frame || r.at_us != last_frame_us) {      // a new decoded frame
      if (have_frame && r.mode == cand_mode) {
        if (cand_frames < 255) cand_frames++;
      } else {
        cand_mode = r.mode;
        cand_frames = 1;
        cand_since_us = r.at_us;
      }
      last_frame_us = r.at_us;
      have_frame = true;
    }
    const bool confirmed = r.mode == DisplayBus::Mode::BLANK
        ? (uint32_t)(r.at_us - cand_since_us) >= BLANK_HOLD_US
        : cand_frames >= HOLD_FRAMES;
    if (!confirmed) return false;
    if (!link_up) return false;
    // Back off after a failed send. Unsigned elapsed time, like sent_ms, and
    // the flag is dropped once the wait is over: a signed compare against a
    // deadline that is never cleared locks sends out for ~24.8 days after a
    // 24.8-day quiet spell (third review, 2026-11-13).
    if (have_attempt) {
      if (now_ms - attempt_ms < MIN_GAP_MS) return false;
      have_attempt = false;
    }

    const bool changed = !have_sent || r.mode != sent_mode
                      || memcmp(r.text, sent_text, sizeof(sent_text)) != 0;
    const uint32_t since = now_ms - sent_ms;
    const bool due = resend_now || !have_sent
                  || (changed ? since >= MIN_GAP_MS : since >= HEARTBEAT_MS);
    if (!due) return false;

    format(r, out, out_len);
    pend_mode = r.mode;
    memcpy(pend_text, r.text, sizeof(pend_text));
    return true;
  }

  // The send was attempted and failed with the link still reported up.
  // Without this, poll() would say "due" on every loop() until it worked.
  void failed(uint32_t now_ms) {
    have_attempt = true;
    attempt_ms = now_ms;
  }

  void sent(uint32_t now_ms) {
    sent_mode = pend_mode;
    memcpy(sent_text, pend_text, sizeof(sent_text));
    sent_ms = now_ms;
    have_sent = true;
    resend_now = false;
    have_attempt = false;
  }

private:
  static void format(const DisplayBus::Reading& r, char* out, size_t n) {
    // The glyph table yields digits, space, '-' and letters; anything that
    // could break the JSON is masked.
    char txt[4];
    for (int i = 0; i < 3; i++) {
      char c = r.text[i];
      txt[i] = (c >= ' ' && c <= '~' && c != '"' && c != '\\') ? c : '?';
    }
    txt[3] = 0;
    switch (r.mode) {
      case DisplayBus::Mode::TEMPERATURE:
        snprintf(out, n, "{\"ev\":\"machine\",\"mode\":\"temp\",\"text\":\"%s\",\"c\":%d}", txt, (int)r.temp_c);
        break;
      case DisplayBus::Mode::SETPOINT:
        snprintf(out, n, "{\"ev\":\"machine\",\"mode\":\"setpoint\",\"text\":\"%s\",\"c\":%d}", txt, (int)r.temp_c);
        break;
      case DisplayBus::Mode::TIMER:
        snprintf(out, n, "{\"ev\":\"machine\",\"mode\":\"timer\",\"text\":\"%s\",\"dl\":%u}", txt, (unsigned)r.timer_dl);
        break;
      case DisplayBus::Mode::BLANK:
        snprintf(out, n, "{\"ev\":\"machine\",\"mode\":\"blank\"}");
        break;
      default:
        snprintf(out, n, "{\"ev\":\"machine\",\"mode\":\"text\",\"text\":\"%s\"}", txt);
        break;
    }
  }

  bool             was_up = false;
  bool             resend_now = false;
  bool             have_attempt = false;
  uint32_t         attempt_ms = 0;
  bool             have_frame = false;
  uint32_t         last_frame_us = 0;
  DisplayBus::Mode cand_mode = DisplayBus::Mode::NONE;
  uint8_t          cand_frames = 0;
  uint32_t         cand_since_us = 0;
  bool             have_sent = false;
  DisplayBus::Mode sent_mode = DisplayBus::Mode::NONE;
  char             sent_text[4] = {0};
  uint32_t         sent_ms = 0;
  DisplayBus::Mode pend_mode = DisplayBus::Mode::NONE;
  char             pend_text[4] = {0};
};
