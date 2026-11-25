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
// machine_on_flags bit 0: machine_on_ms is only a LOWER BOUND -- the first
// display frame came within POWERUP_SILENCE_MS (0.5 s) of this board starting
// to watch WITH WHOLE CAPTURE (it booted, or the tap was switched on, into a
// running machine; or the display ring dropped edges too close before the
// frame to rule out a missed earlier one). MachineContextTracker::onFirstFrame
// and firstFrameSilence. Only the first frame after watch start is judged
// this way; a later power cycle is never flagged.
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
  // v6: ms from the machine's first counted display frame (~4.1 s after it
  // was powered; MachineContextTracker::onFirstFrame) to this shot's pump
  // press; MACHINE_ON_NONE = not known, e.g. the display tap off or no
  // press. And machine_on_flags.
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
  // A run must have counted up this recently at pour start to be carried
  // (onPourStart). A running timer rises every 100 ms; frames come every
  // ~41 ms and loop() takes ~50 ms a pass, so 0.5 s has room for a lost pair
  // and is well inside the ~5 s the display holds a final value.
  static constexpr uint32_t TIMER_LIVE_US = 500000;    // 0.5 s
  // Two TIMER frames are the same run when they date its start -- at_us
  // minus the reading -- this close. The estimate jitters by about the
  // display's 100 ms tick plus the 41 ms frame period; a misread digit
  // moves it by a second or more.
  static constexpr uint32_t RUN_AGREE_US = 300000;     // 0.3 s
  // A run's latest counted frame must be this recent for a frame to continue
  // it or to be judged against it. Dropouts last seconds; the display clock
  // is ~0.5 % off real time (docs/machine-display-bus.md), so a run's date
  // drifts ~0.17 s over this (measured on the 2026-10-03 captures), inside
  // RUN_AGREE_US. And micros() wraps every
  // ~71.6 min: a date compared across that could match a new brew to an old
  // run (review of d24b655).
  static constexpr uint32_t RUN_RECENT_US = 30000000;  // 30 s

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
    if (fresh) onTimerFrame(r);
    if (fresh) noteSetpoint(r);
    if (touched_latch) mc.setpoint_touched = true;
  }

  // THE TIMER, frame by frame (fresh frames only; a re-fed frame adds
  // nothing).
  //   EVERY TIMER FRAME DATES ITS RUN'S START: est = at_us − reading. A
  // frame COUNTS only when the frame just before it was TIMER too and the
  // two dates agree within RUN_AGREE_US. A real run gives the same date
  // frame after frame; a misdecoded seconds digit moves it by a second or
  // more, so such a misread counts for nothing, and neither does the frame
  // after it. A tenths digit off by one or two passes the pair rule; the
  // rise rule below keeps it out of the run's value.
  // (2026-11-13: one idle frame decoded as TIMER " 95" between two
  // temperature frames and, taken alone, overwrote a 28 s shot with 9.5 s.)
  //   A RUN IS ITS DATE, TAKEN WHILE IT COUNTS UP: a counted frame whose date
  // matches the run's latest RISING frame continues it -- across a dropout or
  // a loop() stall. THE DISPLAY HOLDS THE FINAL VALUE ~5 s AFTER PUMP-OFF
  // (2026-10-03 captures: 105 for 5.2 s, 210 for 5.1 s; review of 059ef1e),
  // and a held value's date walks forward with the clock, so a frame showing
  // the run's own maximum keeps the run without moving its date.
  //   ANYTHING ELSE IS A CANDIDATE, and becomes the run only once a later
  // counted frame with the same date reads HIGHER -- a run that counts. A
  // real restart, flush or brew does so within ~0.2 s; a misread that
  // repeats (two idle " 95" frames, the same wrong digit twice) does not,
  // so it never replaces the run, never locks the real one out, and never
  // becomes a shot's timer. (Reviews of 0.16.1, 2026-11-25: a bound on high
  // readings, a two-frame restart, a gap rule and a "no run predates the
  // last" rule each left a frame that could set a run's value or date.)
  //   last_rise_us: when the run last counted up. onPourStart() carries the
  // run only if that was within TIMER_LIVE_US -- a pump running now -- never
  // a held final value, and never what the last shot stored (review of
  // 3f34199).
  void onTimerFrame(const DisplayBus::Reading& r) {
    if (r.mode != DisplayBus::Mode::TIMER) {
      have_prev_est = false;
      // Retire a run unseen for RUN_RECENT_US. Idle frames come every ~41 ms,
      // so while the machine is on this happens long before micros() wraps
      // (~71.6 min) and an old run's times could look recent again (review
      // of 9734611). With the machine off the bus is silent and nothing
      // retires it; carrying it then needs a pour within 0.5 s of a wrap
      // (docs/machine-display-bus.md, known limits).
      if (have_glass_run && (uint32_t)(r.at_us - run_seen_us) > RUN_RECENT_US)
        have_glass_run = false;
      return;
    }
    const uint32_t est = r.at_us - (uint32_t)r.timer_dl * 100000u;
    const bool counts = have_prev_est && near(est, prev_est);
    // A RISE NEEDS BOTH FRAMES OF THE PAIR: the value the run has surely
    // reached is the lower of the two. A digit misread by a tenth or two
    // moves a frame's date by only 0.1-0.2 s -- inside RUN_AGREE_US -- so
    // the pair rule alone let one frame read 106 for 105 raise the stored
    // timer and refresh liveness (review of 9734611, injected into the
    // 2026-10-03 captures). Each value shows on 2-3 frames, so this costs
    // one frame per tick.
    const uint16_t sure_dl = r.timer_dl < prev_dl ? r.timer_dl : prev_dl;
    prev_est = est;
    prev_dl = r.timer_dl;
    have_prev_est = true;
    if (!counts) return;
    const bool recent = have_glass_run
                     && (uint32_t)(r.at_us - run_seen_us) <= RUN_RECENT_US;
    bool new_run = false;
    if (recent && near(est, run_last_est)) {
      // A HELD run has stopped: once the display has shown its final value
      // for longer than RUN_AGREE_US (run_held, below) the pump is off, and a
      // run never resumes -- a restart is a new date. So nothing raises it
      // after that: a misread pair of +N s about N s into the hold matches
      // its date and was taken as a rise, into the shot or a draw (review of
      // 6aa4443, real captures).
      if (sure_dl > run_dl && !run_held) {          // counting: the run goes on
        run_dl = sure_dl;
        run_last_est = est;
        last_rise_us = r.at_us;
        if ((int32_t)(est - run_start_us) < 0) run_start_us = est;
      } else {
        // Its date, but no rise: nothing to store. Falling through let a
        // misread pair whose date matched (+N s, ~N s into the hold) push
        // the old run into a hot-water draw's empty pour (review of
        // 6aa4443, real captures).
        run_seen_us = r.at_us;
        return;
      }
    } else if (recent && r.timer_dl == run_dl) {
      run_seen_us = r.at_us;                        // the final value, held
      run_held = true;
      return;
    } else if (have_cand && near(est, cand_last_est)
               && (uint32_t)(r.at_us - cand_seen_us) <= RUN_RECENT_US) {
      if (sure_dl <= cand_dl) { cand_seen_us = r.at_us; return; }
      // The candidate counted up: it is the run now.
      new_run = true;
      have_glass_run = true;
      run_dl = sure_dl;
      run_held = false;
      run_start_us = (int32_t)(cand_start_us - est) < 0 ? cand_start_us : est;
      run_last_est = est;
      last_rise_us = r.at_us;
      have_cand = false;
    } else {
      have_cand = true;
      cand_dl = r.timer_dl;
      cand_start_us = cand_last_est = est;
      cand_seen_us = r.at_us;
      return;
    }
    run_seen_us = r.at_us;
    if (!stopped) {
      // DURING THE POUR the shot takes the latest run: a new run is a new
      // brew (a manual stop-and-restart takes the restart). Every run of the
      // pour is kept, oldest dropped past N_RUNS, so onPourStop() can go back
      // to the one in force at the stop however many came after it (a scale
      // lost mid-pour is completed up to 65 s on).
      if (new_run || n_runs == 0) pushRun();   // n_runs 0: none since pour start
      runs[n_runs - 1] = { run_start_us, run_dl };
      mc.machine_timer_dl = run_dl;
    } else if (new_run) {
      // AFTER THE STOP another run that counts (a flush pressed while
      // SETTLING) means the shot's pump run is over: frozen for good.
      timer_follow = false;
    } else if (timer_follow) {
      // The run in force at the stop goes on counting -- the pump can
      // outlast a flat scale.
      mc.machine_timer_dl = run_dl;
    }
  }
  static bool near(uint32_t a, uint32_t b) {
    const int32_t d = (int32_t)(a - b);
    return (d < 0 ? -d : d) <= (int32_t)RUN_AGREE_US;
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

  // v6: MACHINE ON-TIME, from the display bus, which is silent while the
  // machine is switched off (no frames at all, 2026-11-22). The first frame
  // since watching started goes to onFirstFrame() (below); later frames to
  // onBusFrame() with millis() and the time of the PREVIOUS poll, which
  // found no new frame.
  //   Only WITNESSED silence counts: frames are decoded when loop() polls, so
  // a loop blocked for 10 s or more (selftest, a drive sweep, a slow upload)
  // looks like a silent bus without being one. The gap that matters is from
  // the last frame to the last poll that still saw none -- a blocked loop
  // adds nothing to it (reviews 2026-11-22: a flag for "the board was blind"
  // lingered while the machine was off and hid the next real power-on, and
  // missed stalls just under 10 s).
  //   THE POWER-ON SHAPE (docs/machine-display-bus.md, power-on table, two
  // boots byte-identical): rails up at 0; a ~1.1 s initialisation burst the
  // decoder rejects; then ~3 s with NO transactions while U2.4 shows; the
  // first counted frame at +4.1 s. A running machine sends a frame every
  // ~41 ms. So, for the FIRST frame since watching started (boot,
  // watchBusAgain) -- onFirstFrame():
  //   - at least POWERUP_SILENCE_MS of whole capture before it: a power-up,
  //     seen whole or in part -- including board and machine powered up by
  //     one switched strip, where the board misses the burst (operator,
  //     2026-11-22). Not flagged. Judged on the FRAME'S OWN TIME (the
  //     decoder's, from the ISR edge stamps), not when loop() got to it;
  //     the tap is enabled first thing in setup() and the ring is drained
  //     through the 2 s wait and between bring-up steps (not during the
  //     LittleFS mount), so the +4.1 s frames are not lost to a ring full of
  //     the init burst. Silence counts only from when capture was whole:
  //     the tap's enable, or the drain after the ring's latest drop -- so a
  //     drop during the init burst does not hide the silence after it, and
  //     a frame drained from a full ring is not mistaken for the first.
  //   - sooner: the machine was already running; MACHINE_ON_LOWER_BOUND.
  //   Later frames -- onBusFrame(): one after more than MACHINE_OFF_GAP_MS
  // of witnessed silence is a power cycle. The threshold stays long because
  // a running bus has dropped frames for seconds for causes not yet
  // identified (CLAUDE.md, 0e). KNOWN LIMIT: such a dropout of 0.5 s or more
  // starting right at watch start would read a running machine as a
  // power-up; nothing shows it has happened, nothing rules it out.
  //   RAW, invariant 3: machine_on counts from the first COUNTED frame. The
  // machine was powered ~STARTUP_MS before it (0.1 s or so either way:
  // skip_first can spend the first frame on the watch-start path); analysis
  // adds it. 0.15.0 and 0.15.1 values are therefore on the same footing.
  //   A frame is stamped when polled in onBusFrame(), so a power cycle
  // during a blocked loop is anchored at the end of the block.
  static constexpr uint32_t MACHINE_OFF_GAP_MS = 10000;
  static constexpr uint32_t POWERUP_SILENCE_MS = 500;
  static constexpr uint32_t STARTUP_MS = 4100;   // documentation: rails up to first frame
  // The first frame since watching started, at its own time frame_ms;
  // silence_ms since watching started (0 when frames may have been lost);
  // poll_ms when it was polled. last_bus_ms is the POLL time, as everywhere
  // else, so the blind time between the frame and its poll never counts as
  // witnessed silence (review 2026-11-22).
  void onFirstFrame(uint32_t frame_ms, uint32_t silence_ms, uint32_t poll_ms) {
    bus_on_since_ms = frame_ms;
    bus_lower_bound = silence_ms < POWERUP_SILENCE_MS;
    bus_seen = true;
    last_bus_ms = poll_ms;
  }
  bool busSeen() const { return bus_seen; }
  // The silence to judge a first frame on: how long the bus was WATCHED
  // WHOLE before it -- frame_ms − capture_ok_ms, where capture_ok_ms is the
  // tap's enable or the drain that emptied the ring after its latest drop
  // (main.cpp). A frame from before capture_ok_ms (drained from the ring
  // after a later drop) gets 0. Signed, so a wait of 24.8 days or more also
  // reads 0 -- a lower bound, the safe direction. Pure, for the host tests.
  static uint32_t firstFrameSilence(uint32_t frame_ms, uint32_t capture_ok_ms) {
    const int32_t d = (int32_t)(frame_ms - capture_ok_ms);
    return d > 0 ? (uint32_t)d : 0;
  }
  void onBusFrame(uint32_t now_ms, uint32_t prev_poll_ms) {
    if (!bus_seen) {
      // No first-activity judgement was made (the firmware always makes one,
      // onFirstFrame): poll time counts blind loop time as silence, so the
      // only safe reading is "already running" -- a lower bound.
      (void)prev_poll_ms;
      onFirstFrame(now_ms, 0, now_ms);
      return;
    } else if ((uint32_t)(prev_poll_ms - last_bus_ms) > MACHINE_OFF_GAP_MS) {
      // Unsigned: last_bus_ms was set at a poll no later than prev_poll_ms,
      // and a signed compare would miss a power-on after 24.8 days off.
      bus_on_since_ms = now_ms;                  // watched it go quiet: off, then on again
      bus_lower_bound = false;
    }
    last_bus_ms = now_ms;
  }
  // The display tap was switched or re-attached, or the board just booted:
  // what the bus did before is unknown from here on.
  void watchBusAgain(uint32_t now_ms) {
    bus_seen = false;
    watch_since_ms = now_ms;
  }

  // Call when the detector reaches POURING. Takes the temperature from
  // before the pump started, which is the only time it is readable, and
  // keeps whatever the timer has reached for this brew.
  //   THE TIMER IS KEPT ONLY IF A RUN IS COUNTING NOW -- it counted up
  // within TIMER_LIVE_US -- because then it is this pour's pump (POURING is
  // confirmed while the pump runs). A held final value is not: the display
  // keeps it ~5 s after pump-off (review of 059ef1e: a hot-water draw
  // pressed inside that hold stored the flush's timer). Until 0.16.1 it was
  // kept unconditionally, so a pour with no timer of its own (a hot-water
  // draw, which the display does not time; the display tap off or stale)
  // stored the PREVIOUS brew's or flush's timer (review of the PWA's
  // shot-time headline, 2026-11-25). Dropping it costs nothing while the
  // run goes on: its next counted frames put it back in the pour. It
  // costs the value when the timer has stopped counting by the time POURING
  // is confirmed -- first drip + POUR_HOLD_MS (~300 ms) + BLE and loop
  // latency -- i.e. the pump stopped at or just after first drip: then 0,
  // "no timer seen", not another run.
  //   There is still no onShotWritten(): a reset there was tried and no test
  // could be made to fail without it. The liveness rule above is what case
  // 16 of machine_context_test fails without.
  // display_live: the display bus is on and decoding now. Without it the
  // remembered setpoint is not stamped — an edit could be going unseen —
  // and no timer is carried.
  void onPourStart(uint32_t now_us, bool display_live = true) {
    // Unsigned differences; a run unseen for RUN_RECENT_US is retired in
    // onTimerFrame, so a rise one micros() wrap ago cannot look recent.
    const bool run_live = display_live && have_glass_run
                       && (uint32_t)(now_us - run_seen_us) <= RUN_RECENT_US
                       && (uint32_t)(now_us - last_rise_us) <= TIMER_LIVE_US;
    uint16_t timer_carry = run_live ? run_dl : 0;
    n_runs = 0;
    if (timer_carry > 0) {
      pushRun();
      runs[0] = { run_start_us, run_dl };
    }
    stopped = false;
    timer_follow = false;
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

  // Call when the detector leaves POURING for SETTLING -- the stop is
  // confirmed -- and again at completion, for a shot that finished straight
  // from POURING (a lift during the pour, a scale lost mid-pour). FREEZES THE TIMER AT THE STOP. Until 0.16.1 it was read at
  // completion, after the 5 s settle, so a flush pressed while SETTLING
  // restarted the timer and the shot stored the flush's run (review,
  // 2026-11-25). ms_since_stop: how long ago the detector's (backdated)
  // stop was, millis() now − (started_at_ms + stop_ms); the stop is
  // confirmed STOP_WINDOW_MS after it, so a restart in that window has
  // already replaced the value and is undone here. After this, only the run
  // in force at the stop may keep counting (onTimerFrame). Repeat calls
  // before the next onPourStart() do nothing.
  //   KNOWN LIMITS: a flush pressed after pump-off but before the stop (the
  // cup still dripping, the weight not yet flat) is the latest run AT the
  // stop and is stored as the shot's, followed to its full length, which can
  // be above or below the shot's own; N_RUNS (eight) or more new runs
  // between the stop and this call push the shot's run out (0); the stop
  // time is the scale's BLE arrival time, ~100-200 ms late.
  void onPourStop(uint32_t now_us, uint32_t ms_since_stop) {
    if (stopped) return;
    stopped = true;
    const uint32_t stop_us = now_us - ms_since_stop * 1000u;
    // The newest run of this pour that began at or before the stop. Every
    // entry was live at pour start or began during the pour, so its start is
    // minutes old at most and the signed compare cannot wrap.
    mc.machine_timer_dl = 0;
    timer_follow = false;
    for (int i = (int)n_runs - 1; i >= 0; i--) {
      if ((int32_t)(runs[i].start_us - stop_us) > 0) continue;   // began after the stop
      mc.machine_timer_dl = runs[i].dl;
      // The newest run is the one on the glass: it may keep counting if the
      // pump is still on (onTimerFrame).
      timer_follow = i == (int)n_runs - 1 && runs[i].dl > 0;
      break;
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
  bool     bus_lower_bound = true;
  uint32_t bus_on_since_ms = 0;
  uint32_t last_bus_ms = 0;
  uint32_t watch_since_ms = 0;

  MachineContext mc;
  // Timer-run state (onTimerFrame, onPourStart, onPourStop).
  bool     have_prev_est = false;   // the frame before was TIMER, dating prev_est
  uint32_t prev_est = 0;
  uint16_t prev_dl = 0;             // and read prev_dl
  bool     have_glass_run = false;  // a run has counted up since boot
  uint16_t run_dl = 0;              // the run on the glass: its highest reading
  uint32_t run_start_us = 0;        // its earliest date
  uint32_t run_last_est = 0;        // the date of its latest rising frame
  uint32_t run_seen_us = 0;         // its latest counted frame, held ones too
  bool     run_held = false;        // its final value has been held: it has stopped
  uint32_t last_rise_us = 0;        // when it last counted up
  bool     have_cand = false;       // a counted pair of another date, not yet rising
  uint16_t cand_dl = 0;
  uint32_t cand_start_us = 0;
  uint32_t cand_last_est = 0;
  uint32_t cand_seen_us = 0;
  // The pour's runs, oldest first (onPourStart carries a live one in; each
  // new run before the stop adds one). onPourStop picks from these.
  struct Run { uint32_t start_us; uint16_t dl; };
  static constexpr uint8_t N_RUNS = 8;
  Run      runs[N_RUNS] = {};
  uint8_t  n_runs = 0;
  void pushRun() {
    if (n_runs == N_RUNS) {
      for (uint8_t k = 1; k < N_RUNS; k++) runs[k - 1] = runs[k];
      n_runs--;
    }
    runs[n_runs++] = { run_start_us, run_dl };
  }
  bool     stopped = false;         // onPourStop() seen for this shot
  bool     timer_follow = false;    // after the stop: the stop's run may keep counting
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
    // TWO TIMER frames in a row, like onTimerFrame above: a stray TIMER
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
