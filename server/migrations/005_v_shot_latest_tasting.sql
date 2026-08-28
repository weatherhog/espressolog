-- v_shot joined ALL tastings, so a shot tasted twice rendered as two rows.
-- Join only the most recent tasting — the schema wants re-tasting to be
-- possible (an empty tasting is different from a bad one), the view wants
-- one row per shot. tasting_id is exposed so the UI can tell "never
-- captured" from "captured without a rating".
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
  (s.stop_ms - s.first_drip_ms) / 1000.0                        AS pour_s,
  s.stop_ms / 1000.0                                            AS total_s,
  t.id AS tasting_id, t.overall, t.balance, t.notes AS taste_notes
FROM shot s
LEFT JOIN bean b ON b.id = s.bean_id
LEFT JOIN tasting t ON t.id = (
  SELECT id FROM tasting WHERE shot_id = s.id
  ORDER BY tasted_at DESC, id DESC LIMIT 1
);
