-- first_drip_ms becomes real (firmware 0.10.0, spool format v3, 0c): the
-- lead of the lever press that started the pump over the detector's t=0.
-- The schema always meant "pump-on to first liquid" by it, and that is what
-- it now holds.
--
-- BUT stop_ms IS NOT ON A PUMP-ON TIME BASE. The detector's t=0 is the
-- backdated first drip, so stop_ms already counts from first drip. 001 and
-- 005 defined pour_s as (stop_ms - first_drip_ms), which assumed a pump-on
-- base that never existed; it was harmless only because first_drip_ms was
-- always NULL. Left alone, every v3 shot would get pour_s = drip-to-stop
-- minus the pump lead, which is nothing.
--
-- So, from here:
--   pour_s          first drip -> stop, stop_ms alone — but only where the
--                   press was seen (first_drip_ms NOT NULL), so "pour_s IS
--                   NOT NULL" still means "switch-sensed", as it did while
--                   pour_s was always NULL.
--   total_s         unchanged (stop_ms / 1000), so analysis built on it
--                   (0f's noise floor) keeps its meaning.
--   pump_to_stop_s  NEW: pump-on -> stop, first_drip_ms + stop_ms. NULL
--                   when the press was not seen (switches off, pre-v3, or no
--                   press within 60 s before first drip).
-- stop_ms's path-dependence (backdated ~2 s on a normal stop, not when
-- truncated, the lift instant when the cup was lifted) carries into all
-- three; see the stop_ms comment on table shot and 005.
--
-- switch_direction is filled for lever A only: 'up' = 1Cup, 'down' = 2Cup.
-- A hot-water draw into the cup leaves it NULL but still gets
-- first_drip_ms. pump_line keeps the raw byte the record carried (0 none,
-- 1 1Cup, 2 2Cup, 3 hot water) for every v3 record, NULL before v3, so a
-- hot-water draw and a value no firmware writes are not the same NULL.
ALTER TABLE shot ADD COLUMN pump_line INTEGER;

DROP VIEW v_shot;
CREATE VIEW v_shot AS
SELECT
  s.*,
  b.roaster, b.name AS bean_name, b.process, b.roast_level,
  ROUND(julianday(s.started_at) - julianday(b.roast_date))      AS days_off_roast,
  ROUND(julianday(s.started_at) - julianday(b.opened_at))       AS days_open,
  s.yield_final_g / NULLIF(s.dose_ground_g, 0)                  AS ratio,
  COALESCE(s.dose_in_g, b.portion_target_g) - s.dose_ground_g   AS retention_g,
  CASE WHEN s.dose_in_g IS NULL THEN 'estimated' ELSE 'measured' END AS retention_basis,
  -- All three inherit stop_ms's path-dependence: ~2 s backdated on a normal
  -- stop, NOT backdated when truncated = 1, and the lift instant -- up to
  -- STOP_WINDOW_MS + SETTLE_MS (~7 s) late -- when the cup was lifted
  -- while settling, UP TO 0a.7 (from 0a.8 such a lift keeps the stop; a
  -- lift during the pour is still the last kept sample). Up to 0a.7
  -- those cases are NOT exclusive: a truncated shot can also be a lift.
  -- v_shot_end (migration 016) says which shots a lift ended.
  -- fact:stop-ms-lift-bound STOP_WINDOW_MS + SETTLE_MS|~7 s
  -- The sub-5 g abort assigns no stop_ms at all, so it lands here as
  -- total_s = 0.0. NO v_shot definition filters excluded (v_dialin does),
  -- so filter it yourself, as analysis/overshoot.py does.
  -- machine_timer_s is the machine's own pump-on to pump-off time and
  -- carries none of stop_ms's path-dependence.
  CASE WHEN s.first_drip_ms IS NOT NULL
       THEN s.stop_ms / 1000.0 END                              AS pour_s,
  s.stop_ms / 1000.0                                            AS total_s,
  (s.first_drip_ms + s.stop_ms) / 1000.0                        AS pump_to_stop_s,
  t.id AS tasting_id, t.overall, t.balance, t.notes AS taste_notes
FROM shot s
LEFT JOIN bean b ON b.id = s.bean_id
LEFT JOIN tasting t ON t.id = (
  SELECT id FROM tasting WHERE shot_id = s.id
  ORDER BY tasted_at DESC, id DESC LIMIT 1
);
