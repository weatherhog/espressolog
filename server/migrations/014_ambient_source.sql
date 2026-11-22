-- Where shot.ambient_c / ambient_rh came from. The columns have existed since
-- 001 ("add a sensor later if you care") and were NULL on every row until
-- 2026-11-22, when the server started reading the room sensor from Home
-- Assistant (internal/ambient):
--
--   'ha_state'   the last recorded state at or before started_at -- exact;
--                Home Assistant keeps raw states ~10 days, so live shots
--   'ha_hourly'  the mean of the hour containing started_at, from Home
--                Assistant's long-term statistics -- how the archive was
--                backfilled; coarser, a room moves little within an hour
--   'none'       Home Assistant answered and had nothing for that moment
--   NULL         not asked yet (or Home Assistant could not be reached;
--                the worker retries)
--
-- The sensor is a room thermometer ~5 m from the machine (operator,
-- 2026-11-22): room temperature, not the air at the machine, which warms
-- while it runs. Which sensor is server configuration, not schema.
ALTER TABLE shot ADD COLUMN ambient_source TEXT
  CHECK (ambient_source IN ('ha_state', 'ha_hourly', 'none'));
