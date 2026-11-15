-- Where brew_temp_c and preinfusion_s came from (firmware 0.12.0). Until now
-- neither had a source column, so a value carried forward at ingest could
-- not be told from one a human typed (see defaultRecipe in store.go, and
-- 009). From here:
--
--   brew_temp_source    'display'  the setpoint last dialled in on the
--                                  machine's front panel, read off the
--                                  display bus (spool v5 setpoint_dc)
--                       'carried'  copied at ingest from an earlier shot
--                       'user'     typed in the PWA — or cleared there: a
--                                  NULL value with 'user' is a deliberate
--                                  blank, which nothing refills
--   preinfusion_source  'flow'     the Pr setting the measured pump-on run
--                                  corresponds to, the run rounded to whole
--                                  seconds (store/preinfusion.go)
--                       'carried'  copied at ingest from an earlier shot
--                       'user'     as above
--
-- preinfusion_run_s is the measurement itself: the pump-on run before the
-- pause, in seconds (shot 179: 5.1 at Pr = 5). DERIVED from shot_pre_pulse
-- (invariant 3 keeps the pulses raw), and re-derived at every server start,
-- so a change to the thresholds re-measures the archive. NULL = not
-- measurable.
--
-- NULL source = unknown provenance: every row before this migration,
-- whatever its value. Nothing is backfilled as 'carried' or 'user', because
-- 009 showed this table cannot tell those apart after the fact.
-- preinfusion_s IS backfilled from the pulse train where one exists and the
-- column is empty (store.go, backfillPreinfusion), as 'flow'.
ALTER TABLE shot ADD COLUMN brew_temp_source TEXT
  CHECK (brew_temp_source IN ('display', 'carried', 'user'));
ALTER TABLE shot ADD COLUMN preinfusion_source TEXT
  CHECK (preinfusion_source IN ('flow', 'carried', 'user'));
ALTER TABLE shot ADD COLUMN preinfusion_run_s REAL;
