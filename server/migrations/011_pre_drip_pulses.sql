-- The flowmeter pulse train from the lever press to pour confirmation
-- (firmware 0.11.0, spool format v4): the pre-infusion phase. shot_sample's
-- inlet_pulses window opens at pour confirmation, so until now every pulse
-- before it — the pump's first run, the pause, the resume — was uncounted.
-- The two windows meet: pre_pulse_total plus the shot's last inlet_pulses
-- counts every pulse from the press to the stop.
--
-- Stored RAW (invariant 3): one row per pulse, its time in ms after the
-- press. Pump-on duration, pause length and resume are derived in analysis
-- from the gaps, so the definition of "pre-infusion" can change without
-- rewriting a single row.
--
-- shot.pre_pulse_total is how many pulses lay in the window — more than
-- the rows if the firmware's 256-entry array filled. NULL = not captured
-- (no press seen, flow tap off, a record older than v4), which is NOT the
-- same as 0 (captured, and no pulse came before pour confirmation).
-- shot.pre_pulse_complete is 1 when the firmware's pulse log lost none of
-- the window, 0 when the total is only a lower bound.
ALTER TABLE shot ADD COLUMN pre_pulse_total INTEGER;
ALTER TABLE shot ADD COLUMN pre_pulse_complete INTEGER;

CREATE TABLE shot_pre_pulse (
  shot_id        INTEGER NOT NULL REFERENCES shot(id) ON DELETE CASCADE,
  n              INTEGER NOT NULL,    -- 0-based, oldest first
  ms_after_press INTEGER NOT NULL,
  PRIMARY KEY (shot_id, n)
);
