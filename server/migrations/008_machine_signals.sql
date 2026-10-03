-- Milestone 0e: the first facts the machine tells us about itself.
--
-- Everything in `shot` until now was either measured by a scale or typed in
-- by a human. These three come off the machine's own front display, decoded
-- at J5 (see CLAUDE.md). Spool format v2 carries them; v1 records predate
-- the display bus and leave all three NULL, which is honest rather than
-- zero.

-- The boiler temperature the machine was showing immediately before the
-- pump started. NOT brew_temp_c, which is the PID setpoint someone typed
-- in and may never have been reached. This is what the machine said it had.
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
