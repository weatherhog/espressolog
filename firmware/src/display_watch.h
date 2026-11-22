#pragma once
#include <stdint.h>
#include "machine_context.h"

// v6 (firmware 0.15.1): the display-ring bookkeeping that judges the FIRST
// bus activity after the tap is switched on -- a power-up is frame-silent
// until +4.1 s, a running machine is not (MachineContextTracker). Pure, so
// the host tests can simulate the ring (test/host/machine_context_test.cpp);
// main.cpp feeds it once per drain (pollDisplay).
//
// "Whole capture": the ISR ring keeps the OLDEST edges and drops the newest
// when full, so after a drop the bus was watched whole again only from the
// drain that emptied the ring. Silence before the first activity counts
// from the latest such point (capture_ok_ms) -- never across a drop.
struct DisplayWatch {
  uint32_t capture_ok_ms  = 0;   // whole capture since here
  uint32_t dropped_seen   = 0;   // the ISR's drop counter at that point
  bool     first_noted    = false;
  uint32_t first_frame_ms = 0;   // the first activity's own time
  uint32_t first_silence  = 0;   // whole-capture silence before it

  // Every switch-on of the tap, including `display on` while on: the ring is
  // cleared then, which is a capture loss the drop counter never sees.
  void enable(uint32_t now_ms) {
    capture_ok_ms = now_ms;
    dropped_seen = 0;
    first_noted = false;
  }

  // After one drain. drain_start_ms: when it began; now_ms: when it ended;
  // dropped: the ISR's counter now; have_first / frame_ms: the decoder's
  // first activity since the capture began (DisplayBus::firstFrameUs), in
  // millis.
  void afterDrain(uint32_t drain_start_ms, uint32_t now_ms, uint32_t dropped,
                  bool have_first, uint32_t frame_ms) {
    const bool drop_new = dropped != dropped_seen;
    if (!first_noted && have_first) {
      first_noted = true;
      first_frame_ms = frame_ms;
      // Edges already in the ring when this drain began predate any drop it
      // is about to notice (the ring keeps the oldest), so a frame from them
      // is judged against the old capture-ok point. A frame from edges that
      // arrived DURING this drain, with a drop pending, came after the drop:
      // whole capture only from when the drain began emptying the ring.
      uint32_t base = capture_ok_ms;
      if (drop_new && (int32_t)(frame_ms - drain_start_ms) >= 0) base = drain_start_ms;
      first_silence = MachineContextTracker::firstFrameSilence(frame_ms, base);
    }
    if (drop_new) {
      dropped_seen = dropped;
      capture_ok_ms = now_ms;
    }
  }
};
