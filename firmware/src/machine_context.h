#pragma once
#include <stdint.h>
#include "display.h"
#include "machine_io.h"
#include "pulse_log.h"

// What the machine itself contributed to a shot record (spool format v2).
//
// Kept out of ShotResult on purpose: the detector is pure scale logic and
// must stay host-testable without a display bus. Kept out of main.cpp for
// the same reason — the first version of this lived there, had no tests,
// and shipped two bugs that a green suite could not see.
// Which lever direction started the pump for a shot, in the spool's own
// encoding (format v3). Kept separate from SwitchBank::Line so the wire
// format does not move if the poller's enum is ever reordered.
enum PumpLine : uint8_t { PUMP_NONE = 0, PUMP_1CUP = 1, PUMP_2CUP = 2, PUMP_WATER = 3 };
static constexpr uint32_t PUMP_LEAD_NONE = 0xFFFFFFFF;

// v6: how long the machine had been on, and how long the pump had been idle,
// when this shot's pump started. 0xFFFFFFFF = not known.
static constexpr uint32_t MACHINE_ON_NONE = 0xFFFFFFFF;
static constexpr uint32_t PUMP_IDLE_NONE  = 0xFFFFFFFF;
// machine_on_flags bit 0: machine_on_ms is only a LOWER BOUND -- the display
// bus was already live when this board started watching it (the board booted,
// or the display tap was switched on, into a running machine).
static constexpr uint8_t  MACHINE_ON_LOWER_BOUND = 0x01;

// v4: the flowmeter pulses between the lever press and first drip.
static constexpr uint16_t PRE_PULSES_MAX    = 256;
static constexpr uint16_t PRE_PULSES_NONE   = 0xFFFF;   // pre_pulse_total: not captured
static constexpr uint8_t  PRE_PULSES_COMPLETE = 0x01;   // pre_pulse_flags: no pulse lost

// The poller's line -> the spool's encoding. Steam never runs the pump, and
// an unknown line is not guessed at: both are PUMP_NONE, so a line added to
// SwitchBank later cannot be silently recorded as hot water.
inline uint8_t pumpLineForSwitch(uint8_t sw_line) {
  switch (sw_line) {
    case SwitchBank::ONE_CUP:   return PUMP_1CUP;
    case SwitchBank::TWO_CUP:   return PUMP_2CUP;
    case SwitchBank::HOT_WATER: return PUMP_WATER;
    default:                    return PUMP_NONE;
  }
}

struct MachineContext {
  int16_t  boiler_temp_start_dc = INT16_MIN;   // INT16_MIN = machine did not say
  uint16_t machine_timer_dl     = 0;           // 0 = no timer seen
  bool     setpoint_touched     = false;
  // v3 (0c): the lever press that started the pump for this shot, and how
  // long before the detector's t=0 (first drip) it was. Both from raw
  // millis() timestamps; PUMP_NONE / PUMP_LEAD_NONE = not known.
  uint8_t  pump_line            = PUMP_NONE;
  uint32_t pump_lead_ms         = PUMP_LEAD_NONE;
  // v4: every counted flowmeter pulse from that press to pour confirmation,
  // as ms after the press — the pre-infusion phase the per-sample
  // inlet_pulses window (opened at pour confirmation) never sees. Raw: analysis derives pump-on, pause
  // and resume from the gaps. pre_pulse_total is how many lay in the window
  // (PRE_PULSES_NONE = not captured: no press, flow tap off); at most
  // PRE_PULSES_MAX are kept in pre_pulse_ms, the first ones.
  uint16_t pre_pulse_total      = PRE_PULSES_NONE;
  uint8_t  pre_pulse_flags      = 0;
  uint16_t pre_pulse_count      = 0;
  uint16_t pre_pulse_ms[PRE_PULSES_MAX] = {};
  // v5: the brew-temperature SETPOINT, as last dialled in on the machine's
  // front panel while this board watched (INT16_MIN = no confirmed edit
  // seen since boot or since the display tap was switched, an edit not yet
  // confirmed — then setpoint_pending is set — or the display not
  // decoding at pour start). See MachineContextTracker::noteSetpoint.
  int16_t  setpoint_dc          = INT16_MIN;
  // v5: a brew-temperature edit had begun and not confirmed before this
  // pour — still blinking, or cut off by the pump's timer — so this shot's
  // setpoint is unknown, not "the last one".
  bool     setpoint_pending     = false;
  // v6: ms from the machine being switched on to this shot's pump press
  // (the display bus came alive after being silent; MACHINE_ON_NONE = not
  // known, e.g. the display tap off or no press), and machine_on_flags.
  uint32_t machine_on_ms        = MACHINE_ON_NONE;
  uint8_t  machine_on_flags     = 0;
  // v6: ms from the previous pump run's last flowmeter pulse to this shot's
  // pump press -- how long the pump had stood idle (a shot, a flush or a
  // hot-water draw all run it). PUMP_IDLE_NONE = not known: the flow tap off,
  // no pulse since it was enabled, or pulses lost in between.
  uint32_t pump_idle_ms         = PUMP_IDLE_NONE;
};

// Gathers MachineContext across a shot. None of it is available at the
// moment the shot completes, so it has to be accumulated:
//
//   boiler temperature  readable only BEFORE the pump starts. The machine
//                       takes the display over for its timer the moment the
//                       pump runs, so by the time the shot ends the last
//                       temperature is twenty seconds old. Tracked
//                       continuously, snapshotted backwards at pour start.
//   machine timer       counts up during the brew and is gone afterwards.
//   setpoint touched    PrG is a transient banner — two frames in a capture
//                       where the adjustment ran seven seconds. Latched.
//
// Pure logic: no Arduino, no I/O, no clock of its own.
class MachineContextTracker {
public:
  // How old the pre-shot temperature may be and still be called this
  // shot's. Generous, because the gap between pump-on and the detector
  // reaching POURING is first-drip lag, which is puck-dependent.
  static constexpr uint32_t BOILER_MAX_AGE_US = 60000000;   // 60 s
  // A dialled value outside this is not a brew setpoint (the manual's own
  // range is narrower; a capture held the button to 120).
  static constexpr int16_t SETPOINT_MIN_C = 80;
  static constexpr int16_t SETPOINT_MAX_C = 130;

  // Call on every fresh display reading. `touched_latch` is
  // DisplayBus::setpointTouched(); the caller clears it at pour start.
  void onReading(const DisplayBus::Reading& r, bool touched_latch) {
    // main.cpp calls this every loop with the CURRENT reading, so the same
    // frame arrives many times; `at_us` is what says a new frame decoded.
    const bool fresh = !have_frame || r.at_us != last_frame_us;
    if (fresh) {
      prev_frame_mode = have_frame ? last_frame_mode : DisplayBus::Mode::NONE;
      last_frame_mode = r.mode;
      last_frame_us = r.at_us;
      have_frame = true;
    }
    const bool timer_confirmed = r.mode == DisplayBus::Mode::TIMER
                              && prev_frame_mode == DisplayBus::Mode::TIMER;
    if (r.mode == DisplayBus::Mode::TEMPERATURE) {
      // Only Mode::TEMPERATURE is the boiler. SETPOINT is the number being
      // dialled in, TARGET is steam's 165 or the 95 it is cooling back to,
      // BLANK and TEXT are not temperatures at all.
      last_boiler_dc = (int16_t)(r.temp_c * 10);
      last_boiler_us = r.at_us;
      have_boiler = true;
    } else if (r.mode == DisplayBus::Mode::SETPOINT || r.mode == DisplayBus::Mode::TARGET) {
      // A SETPOINT frame is the number being dialled in; see noteSetpoint().
      if (r.mode == DisplayBus::Mode::SETPOINT) shown_setpoint_c = r.temp_c;
      // AND THEY INVALIDATE the last one. Several non-boiler values decode
      // as TEMPERATURE: a blink's first cycle (it is recognised at its
      // second onset), steam's steady 165 for as long as steam holds
      // temperature (minutes, live), and
      // a held-button ramp. A later SETPOINT or TARGET frame marks them
      // untrustworthy, so a pour after it records "the machine did not say".
      // THIS SHORTENS THE EXPOSURE; IT DOES NOT CLOSE IT: a pour that starts
      // inside one of those windows, before the next blink frame, still
      // stores the value (CLAUDE.md, display-bus entry).
      have_boiler = false;
    }
    // A TIMER reading counts only when the frame before it was TIMER too.
    // 2026-11-13: one idle frame decoded as TIMER with text " 95" between two
    // temperature frames. Taken alone it reset the running maximum and set
    // 9.5 s — and after a shot, while SETTLING, that would overwrite the
    // real duration in the spool header. A real timer spans hundreds of
    // frames (one per ~41 ms, a digit per 100 ms), so requiring two in a row
    // costs at most the first frame of a brew.
    if (timer_confirmed) {
      // A reading LOWER than the running maximum means the machine's timer
      // restarted, i.e. a new brew or flush began. Without this the maximum
      // is sticky and a short shot inherits a long one's duration — which
      // is exactly what the first version of this did.
      if (r.timer_dl < mc.machine_timer_dl) mc.machine_timer_dl = 0;
      if (r.timer_dl > mc.machine_timer_dl) mc.machine_timer_dl = r.timer_dl;
    }
    if (fresh) noteSetpoint(r);
    if (touched_latch) mc.setpoint_touched = true;
  }

  // The display tap was switched off or on, or re-attached: a setpoint seen
  // before it may no longer be the machine's — it could have been edited
  // while nothing watched (review, 2026-11-15).
  // An edit already pending STAYS pending: switching the tap mid-edit must
  // not turn "unconfirmed" back into "carry the old one" (review 2026-11-15).
  void forgetSetpoint() {
    last_setpoint_dc = INT16_MIN;
    shown_setpoint_c = blinked_setpoint_c = INT16_MIN;
  }

  // v6: MACHINE ON-TIME, from the display bus. The bus is silent while the
  // machine is switched off (no frames at all, 2026-11-22), so the first
  // frame after more than MACHINE_OFF_GAP_MS of silence is the machine being
  // switched on. Call with millis() whenever a frame was decoded.
  //   Lower bound: if this board had not been watching the bus for that
  // long -- the first frame since boot or since watchBusAgain() -- the
  // machine may have been on before; the time is then measured from that
  // first frame and flagged MACHINE_ON_LOWER_BOUND.
  static constexpr uint32_t MACHINE_OFF_GAP_MS = 10000;
  void onBusFrame(uint32_t now_ms) {
    const bool blind = blind_pending;
    blind_pending = false;
    if (!bus_seen) {
      bus_on_since_ms = now_ms;
      // Watching only since boot/re-enable: a machine already running then
      // looks the same as one switched on, unless the bus stayed silent for
      // a full gap first.
      bus_lower_bound = (uint32_t)(now_ms - watch_since_ms) <= MACHINE_OFF_GAP_MS;
      bus_seen = true;
    } else if ((uint32_t)(now_ms - last_bus_ms) > MACHINE_OFF_GAP_MS && !blind) {
      bus_on_since_ms = now_ms;          // switched off and on again
      bus_lower_bound = false;
    }
    // A gap the board was blind for (noteBlind) is not evidence of a power
    // cycle: keep the on-time. KNOWN LIMIT: a machine really switched off and
    // on within such a blind spell keeps the older, too-long on-time.
    last_bus_ms = now_ms;
  }
  // The display tap was switched or re-attached, or the board just booted:
  // what the bus did before is unknown from here on.
  void watchBusAgain(uint32_t now_ms) {
    bus_seen = false;
    watch_since_ms = now_ms;
  }
  // The board could not watch the bus for longer than the off gap (loop()
  // blocked): the next frame's gap says nothing about the machine.
  void noteBlind() { blind_pending = true; }

  // Call when the detector reaches POURING. Takes the temperature from
  // before the pump started, which is the only time it is readable, and
  // keeps whatever the timer has reached for this brew.
  // There is deliberately no onShotWritten(): a reset there was tried and
  // no test could be made to fail without it. onPourStart() clears
  // everything, and the timer restart detection above handles a flush
  // between shots. Unexercised defensive code is where the last two bugs
  // in this file hid.
  // display_live: the display bus is on and decoding now. Without it the
  // remembered setpoint is not stamped — an edit could be going unseen.
  void onPourStart(uint32_t now_us, bool display_live = true) {
    uint16_t timer_carry = mc.machine_timer_dl;
    // Field by field, not `mc = MachineContext{}`: that builds a ~530-byte
    // temporary on the loop task's 8 KB stack in the middle of the sample
    // drain (review, 2026-11-15). pre_pulse_ms needs no clearing — only
    // pre_pulse_count entries of it are ever read.
    mc.boiler_temp_start_dc = INT16_MIN;
    mc.setpoint_touched     = false;
    mc.setpoint_pending     = false;
    mc.pump_line            = PUMP_NONE;
    mc.pump_lead_ms         = PUMP_LEAD_NONE;
    mc.pre_pulse_total      = PRE_PULSES_NONE;
    mc.pre_pulse_flags      = 0;
    mc.pre_pulse_count      = 0;
    mc.machine_timer_dl = timer_carry;
    // The setpoint as it stood when the pour began: an edit DURING the shot
    // applies to the next one. Kept across shots, unlike everything above.
    mc.setpoint_dc = display_live ? last_setpoint_dc : INT16_MIN;
    // An edit begun and not confirmed before this pour: this shot's setpoint is unknown and
    // the server must not carry one onto it, or across it (noteSetpoint).
    // Its OWN flag (v5 SETPOINT_PENDING), not setpoint_touched: a pending
    // brew-temperature edit says nothing about pre-infusion, and touched
    // also stops that carry and condemns a 0f run (review 2026-11-15).
    mc.setpoint_pending = setpoint_pending;
    // It marks ONE shot. The server carries nothing across that shot, so
    // later ones get what was typed on it, or nothing — while a pending
    // that waited for a confirmation stuck for the rest of the boot when
    // the pump's timer ended the blinking (review, 2026-11-15). The
    // setpoint itself stays unknown until the next confirmed edit.
    setpoint_pending = false;
    if (have_boiler && (uint32_t)(now_us - last_boiler_us) < BOILER_MAX_AGE_US) {
      mc.boiler_temp_start_dc = last_boiler_dc;
    }
  }

  // A lever press that runs the pump (1Cup, 2Cup, hot water — never steam),
  // backdated to the press edge, from a witnessed (non-synthetic) event.
  // Each press is remembered whether it STARTED the pump or stopped it: the
  // board latches, so the poller cannot tell the two apart. resolvePump()
  // sorts that out from the shot's own timing.
  // idle_ms (v6): how long the pump had stood idle before this press -- the
  // caller's PulseLog::idleBefore(); PUMP_IDLE_NONE when not known.
  void onPumpPress(uint8_t line, uint32_t pressed_at_ms, uint32_t idle_ms = PUMP_IDLE_NONE) {
    if (line == PUMP_NONE) return;
    presses[press_head] = { line, pressed_at_ms, idle_ms };
    press_head = (uint8_t)((press_head + 1) % N_PRESSES);
    if (press_count < N_PRESSES) press_count++;
  }

  // Call AT POUR START, right after onPourStart(), with the detector's t=0
  // (millis(), the backdated first-drip crossing — known from the moment
  // the detector enters POURING). Attributes the LATEST remembered press at
  // or before t=0 and no more than PUMP_LEAD_MAX_MS before it. Resolving
  // here rather than at completion means presses made during the shot (the
  // stop press, a flush started while it settles) cannot push the start
  // press out of the ring (review, 2026-11-14).
  //
  // KNOWN LIMITS. (1) If this shot's start press was never confirmed (the
  // poller's two-poll debounce can miss a very fast press), an earlier
  // flush's stop press within the window is attributed instead — wrong
  // lever possible, lead too long. Nothing here can see that. (2) A
  // stop-and-restart before first drip (a manual pre-infusion) attributes
  // the restart, so the lead leaves out the first pump run. (3) t=0 is the
  // scale sample's ARRIVAL time, so the lead includes the BLE delay (~10 Hz
  // plus link latency, unmeasured, roughly 100-200 ms).
  static constexpr uint32_t PUMP_LEAD_MAX_MS = 60000;
  void resolvePump(uint32_t t0_ms) {
    mc.pump_line = PUMP_NONE;
    mc.pump_lead_ms = PUMP_LEAD_NONE;
    mc.pump_idle_ms = PUMP_IDLE_NONE;
    mc.machine_on_ms = MACHINE_ON_NONE;
    mc.machine_on_flags = 0;
    bool found = false;
    uint32_t best_lead = 0;
    for (uint8_t i = 0; i < press_count; i++) {
      const Press& p = presses[i];
      uint32_t lead = t0_ms - p.at_ms;
      if ((int32_t)lead < 0 || lead > PUMP_LEAD_MAX_MS) continue;  // after t=0, or too old
      if (!found || lead < best_lead) {
        found = true;
        best_lead = lead;
        mc.pump_line = p.line;
        mc.pump_lead_ms = lead;
        mc.pump_idle_ms = p.idle_ms;
        // On-time at the PRESS, from the bus: only while the machine is on
        // (frames seen within the gap before t=0) and on since before it.
        // Signed: t=0 is backdated to first drip, so the newest frame is
        // normally AFTER it.
        if (bus_seen && (int32_t)(t0_ms - last_bus_ms) <= (int32_t)MACHINE_OFF_GAP_MS
            && (int32_t)(p.at_ms - bus_on_since_ms) >= 0) {
          mc.machine_on_ms = p.at_ms - bus_on_since_ms;
          mc.machine_on_flags = bus_lower_bound ? MACHINE_ON_LOWER_BOUND : 0;
        } else {
          mc.machine_on_ms = MACHINE_ON_NONE;
          mc.machine_on_flags = 0;
        }
      }
    }
  }

  // Call when the shot completes. If pour start found no press, look again:
  // the poller drains after the scale samples in loop(), so a press whose
  // first drip came within ~300 ms (or during a slow loop) is confirmed —
  // backdated to its edge, before t=0 — only after the POURING transition.
  // A press already found at pour start is kept; this only fills a gap.
  // (Resolving solely at pour start lost that case: review, 2026-11-15.)
  void resolvePumpIfUnknown(uint32_t t0_ms) {
    if (mc.pump_line == PUMP_NONE) resolvePump(t0_ms);
  }

  // Call at the POURING transition with millis() then: that is when
  // beginPour() takes inlet_pulses' base, so a pre-drip window ending here
  // meets it — pre_pulse_total + the last inlet_pulses counts the press to
  // the stop with no pulse in neither.
  void notePourConfirmed(uint32_t now_ms) { confirm_ms = now_ms; }

  // Copy the pulses from this shot's press to pour confirmation out of the
  // log, as ms after the press. Call at pour start after resolvePump() and
  // notePourConfirmed() — the log holds ~22 s, so the pulses are surely
  // still there — and again at completion only if pour start found no press
  // (resolvePumpIfUnknown()), when they may be gone: the COMPLETE flag then
  // says so. Both end at the noted confirmation (until review 2026-11-15
  // the fallback ended at t=0, ~300 ms early). The window is clamped to
  // 65535 ms so no offset saturates. Does nothing without a press, so the
  // record says "not captured" rather than "no pulses".
  void capturePrePulses(const PulseLog& log, uint32_t t0_ms) {
    if (mc.pump_line == PUMP_NONE || mc.pump_lead_ms == PUMP_LEAD_NONE) return;
    const uint32_t from_ms = t0_ms - mc.pump_lead_ms;
    uint32_t end_ms = confirm_ms;
    if ((int32_t)(end_ms - t0_ms) < 0) end_ms = t0_ms;           // never before t=0
    if (end_ms - from_ms > 0xFFFFu) end_ms = from_ms + 0xFFFFu;  // offsets fit u16
    bool complete = false;
    uint16_t copied = 0;
    uint16_t total = log.extract(from_ms, end_ms, mc.pre_pulse_ms,
                                 PRE_PULSES_MAX, &copied, &complete);
    mc.pre_pulse_total = total >= PRE_PULSES_NONE ? (uint16_t)(PRE_PULSES_NONE - 1) : total;
    mc.pre_pulse_count = copied;
    mc.pre_pulse_flags = complete ? PRE_PULSES_COMPLETE : 0;
  }
  bool prePulsesCaptured() const { return mc.pre_pulse_total != PRE_PULSES_NONE; }

  // Switch sensing turned off or back on: remembered presses describe a
  // tap state that may no longer hold, so forget them.
  void clearPresses() { press_head = press_count = 0; }

  const MachineContext& context() const { return mc; }

private:
  struct Press { uint8_t line; uint32_t at_ms; uint32_t idle_ms; };
  static constexpr uint8_t N_PRESSES = 4;
  Press   presses[N_PRESSES] = {};
  uint8_t press_head = 0;
  uint8_t press_count = 0;
  uint32_t confirm_ms = 0;
  // v6 on-time state (onBusFrame).
  bool     bus_seen = false;
  bool     blind_pending = false;
  bool     bus_lower_bound = true;
  uint32_t bus_on_since_ms = 0;
  uint32_t last_bus_ms = 0;
  uint32_t watch_since_ms = 0;

  MachineContext mc;
  int16_t  last_boiler_dc = INT16_MIN;
  int16_t  last_setpoint_dc = INT16_MIN;
  bool     setpoint_pending = false;         // an edit begun, not confirmed (see noteSetpoint)
  int16_t  shown_setpoint_c = INT16_MIN;     // SETPOINT value now shown, not yet blinked
  int16_t  blinked_setpoint_c = INT16_MIN;   // the last value that blinked

  // The setpoint is the value that BLINKS. An edit ends with the display
  // blinking the new value for ~3 s (2026-10-03 buttons and setup-menu
  // captures: six or more blinks), so a value counts once it has been shown
  // as SETPOINT and then blanked, TWICE IN A ROW with the same value. That
  // keeps out a ramp's passing values (no blank between them), the steady
  // frames after the blink stops — which still decode as SETPOINT for
  // ~0.6 s and could be the boiler after a large edit — and a single
  // misread frame (review, 2026-11-15: the first version took whatever
  // SETPOINT frame came last). The range check keeps out anything that is
  // not a brew temperature; the SET UP menu's values never decoded as
  // SETPOINT. TARGET (steam's 165, the 95 it cools back to) is the machine
  // moving its own target and is never taken. NOT persisted: after a
  // reboot it is unknown until the next edit, and the server carries the
  // last value meanwhile.
  void noteSetpoint(const DisplayBus::Reading& r) {
    if (r.mode == DisplayBus::Mode::SETPOINT) return;          // shown; see onReading
    if (r.mode == DisplayBus::Mode::BLANK) {
      if (shown_setpoint_c == INT16_MIN) return;               // still the same blank
      const int16_t v = shown_setpoint_c;
      shown_setpoint_c = INT16_MIN;
      const bool in_range = inSetpointRange(v);
      const int16_t prev = blinked_setpoint_c;
      const bool prev_deviant = deviant(prev);
      if (v == prev && in_range) {
        last_setpoint_dc = (int16_t)(v * 10);
        setpoint_pending = false;
      }
      // TWO blinks in a row that are not the remembered value and do not
      // agree: an edit is passing through values. One alone is ignored — a
      // misread frame on an edit's last blink set this for the rest of the
      // boot (review, 2026-11-15); a real new value confirms on its second
      // blink anyway, and a lever pressed before that is caught by the
      // TIMER rule below.
      else if (deviant(v) && prev_deviant)
        // A DIFFERENT value blinked: an edit is under way (or a misread),
        // so the remembered setpoint is no longer known to be the machine's
        // until the new one confirms. Without this a shot pulled in the
        // second before confirmation recorded the pre-edit value as the
        // machine's, and so did every shot after it (review, 2026-11-15).
        // The steady frames after a blink stops are never followed by a
        // blank, so they cannot clear it.
        //
        // Clearing alone only relabelled the old value: the server then
        // carried it from the last shot (review, 2026-11-15). So the edit is
        // also PENDING, and a shot pulled while it is carries the
        // SETPOINT_PENDING flag (onPourStart): the server records no brew
        // temperature on it and carries none across it until a value is
        // confirmed, or typed on a later shot.
      {
        last_setpoint_dc = INT16_MIN;
        setpoint_pending = true;
      }
      blinked_setpoint_c = v;
      return;
    }
    // The pump's TIMER cutting off a run whose latest value was a different
    // one: the lever was pressed mid-edit, and the new value will never get
    // to confirm. Pending, so the old value is not stamped as the machine's
    // (review, 2026-11-15: an edit's first two blinks decode as
    // TEMPERATURE, so a one-degree edit could reach the lever with a single
    // SETPOINT blink, and the old value went out as 'display' on every shot
    // until the next edit). A TEMPERATURE frame ending the run — the normal
    // end of an edit — leaves a lone different blink ignored: a misread.
    // KNOWN LIMITS: a lever pressed before the new value has shown as
    // SETPOINT even once (~1 s after the press) is not seen; nor is one
    // pressed while a re-edit's first new value shows during the confirmed
    // value's own blinking, before it blanks (the blinked value decides);
    // and one decoded non-TIMER frame between the first two TIMER frames
    // resets the run (review, 2026-11-15 — the decoded button bits would
    // settle both, and are not used yet).
    //
    // TWO TIMER frames in a row, like timer_confirmed above: a stray TIMER
    // frame was seen live, so the first one leaves the run as it is. And the
    // value that BLINKED decides, the shown one only if nothing has blinked
    // yet in this run: after an edit confirms, the display can keep showing
    // a different number for ~0.6 s (the steady tail), and a lever pressed
    // then threw the confirmed edit away (review, 2026-11-15).
    if (r.mode == DisplayBus::Mode::TIMER) {
      if (prev_frame_mode != DisplayBus::Mode::TIMER) return;
      const int16_t latest = blinked_setpoint_c != INT16_MIN ? blinked_setpoint_c : shown_setpoint_c;
      if (deviant(latest)) {
        last_setpoint_dc = INT16_MIN;
        setpoint_pending = true;
      }
    }
    shown_setpoint_c = blinked_setpoint_c = INT16_MIN;         // anything else breaks the run
  }
  static bool inSetpointRange(int16_t c) { return c >= SETPOINT_MIN_C && c <= SETPOINT_MAX_C; }
  // A dialled brew temperature that is not the remembered setpoint.
  bool deviant(int16_t c) const { return inSetpointRange(c) && (int32_t)c * 10 != last_setpoint_dc; }
  uint32_t last_boiler_us = 0;
  bool     have_boiler = false;
  DisplayBus::Mode last_frame_mode = DisplayBus::Mode::NONE;
  DisplayBus::Mode prev_frame_mode = DisplayBus::Mode::NONE;
  uint32_t last_frame_us = 0;
  bool     have_frame = false;
};
