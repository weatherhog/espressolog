-- Milestone 0e: the first facts the machine tells us about itself.
--
-- Everything in `shot` until now was either measured by a scale or typed in
-- by a human. These three come off the machine's own front display, decoded
-- at J5 (see CLAUDE.md). Spool format v2 carries them; v1 records predate
-- the display bus and leave all three NULL, which is honest rather than
-- zero.

-- The temperature the machine was showing immediately before the pump
-- started.
--
-- READ THIS BEFORE TRUSTING THE VALUE. An earlier version of this comment
-- said "NOT brew_temp_c ... this is what the machine said it had", i.e.
-- that it is the real boiler. Measurement on 2026-10-05 undercut that:
--   * below ~88 C the display demonstrably tracks the boiler -- it climbs
--     one degree at a time and jitters +-1 C, so a sub-88 reading here IS
--     a real temperature;
--   * at the setpoint value it holds +-0 C for a minute at a time, which
--     no PID boiler does, and during post-steam cooldown it blinks ` 95`
--     while the boiler is demonstrably hotter than that.
-- This column is sampled immediately before a pour, i.e. almost always in
-- that second range. So in practice it means "at setpoint", and it cannot
-- be distinguished from brew_temp_c when it reads the setpoint value.
-- A capture with the boiler pulled off setpoint and allowed to recover
-- would settle it. See docs/phase-0-status.md.
--
-- It is read from before the pour and not during it, because the machine
-- takes the display over for its shot timer the moment the pump runs — so
-- a mid-shot boiler temperature is not available from this source at all,
-- and shot_sample.temp_dc will be NULL for most of a shot.
ALTER TABLE shot ADD COLUMN boiler_temp_start_c REAL;

-- The machine's OWN shot timer, in seconds. Its pump-on to pump-off
-- measurement, which is a truer boundary than the scale's plateau: stopping
-- a cycle by hand stops the timer, so it tracks real pump time rather than
-- counting out the board's stored duration.
--
-- It does NOT say which switch direction ran. 1Cup and 2Cup are
-- byte-identical on this bus, so distinguishing a clean cycle from a shot
-- still needs J2 sensing (0c) and switch_direction stays for that.
ALTER TABLE shot ADD COLUMN machine_timer_s REAL;

-- Someone entered temperature programming, or edited a value, while this
-- shot was being recorded. A setpoint change silently invalidates a
-- noise-floor run: it moves the brew temperature without leaving any other
-- trace, and 0f explicitly requires one setpoint across the whole run.
--
-- Latched by the firmware rather than sampled, because the `PrG` banner is
-- transient — two frames out of 372 in a capture where the adjustment ran
-- for seven seconds.
ALTER TABLE shot ADD COLUMN setpoint_touched INTEGER;
