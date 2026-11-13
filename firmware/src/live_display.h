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
//     so a loop() stall cannot let a single glitch frame through.
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
//     a dropped frame does not leave the PWA blank until the heartbeat.
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
    if (!link_up) resend_now = false;
    was_up = link_up;
    if (r.mode == DisplayBus::Mode::NONE || stale) return false;

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
    if (r.mode != cand_mode) return false;
    const bool confirmed = r.mode == DisplayBus::Mode::BLANK
        ? (uint32_t)(r.at_us - cand_since_us) >= BLANK_HOLD_US
        : cand_frames >= HOLD_FRAMES;
    if (!confirmed) return false;
    if (!link_up) return false;

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

  void sent(uint32_t now_ms) {
    sent_mode = pend_mode;
    memcpy(sent_text, pend_text, sizeof(sent_text));
    sent_ms = now_ms;
    have_sent = true;
    resend_now = false;
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
