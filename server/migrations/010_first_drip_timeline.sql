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
--   pour_s          first drip -> stop. stop_ms alone. Equal to total_s;
--                   kept because the name is what a reader looks for.
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
-- first_drip_ms.
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
  -- The sub-5 g abort assigns no stop_ms at all, so it lands here as
  -- total_s = 0.0. NEITHER v_shot definition filters excluded (v_dialin
  -- does), so filter it yourself, as analysis/overshoot.py does.
  -- machine_timer_s is the machine's own pump-on to pump-off time and
  -- carries none of stop_ms's path-dependence.
  s.stop_ms / 1000.0                                            AS pour_s,
  s.stop_ms / 1000.0                                            AS total_s,
  (s.first_drip_ms + s.stop_ms) / 1000.0                        AS pump_to_stop_s,
  t.id AS tasting_id, t.overall, t.balance, t.notes AS taste_notes
FROM shot s
LEFT JOIN bean b ON b.id = s.bean_id
LEFT JOIN tasting t ON t.id = (
  SELECT id FROM tasting WHERE shot_id = s.id
  ORDER BY tasted_at DESC, id DESC LIMIT 1
);
