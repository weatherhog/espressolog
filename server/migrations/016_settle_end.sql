-- How each shot's settle ended (review of detector 0a.8, 2026-11-23). No
-- record field says whether the cup was lifted, on any detector version,
-- and from 0a.8 the old tells are gone: a lift while settling keeps the
-- backdated stop_ms and stores a real-looking settle_offset_g (the drips
-- before the lift), where up to 0a.7 it overwrote stop_ms with the last
-- sample and stored 0. The spool flag byte is full, so this is derived from
-- the stored samples instead of a format bump.
--
-- THE TEST. A settle that runs its course appends a sample at or after the
-- confirming sample + SETTLE_MS (5 s), and a flat stop is backdated at least
-- STOP_WINDOW_MS (2 s) before that confirming sample -- so the curve ends at
-- least 7 s after stop_ms. A forced stop at MAX_SHOT_MS (60 s) is not
-- backdated, so there it is 5 s; the server does not store the truncated
-- flag, and stop_ms >= 60000 identifies those (a flat stop is always
-- confirmed before 60 s, and a lift during the pour happens before it). Any
-- lift ends the curve sooner: during the pour, and while settling up to
-- 0a.7, at stop_ms itself (tail 0); while settling from 0a.8, before the
-- settle window closes.
--
-- ONE KNOWN MISS, 0a.8 only: the flat stop is backdated to the newest ring
-- sample at or before confirm - 2 s, so after a sample gap it sits further
-- back, and a lift in the last (that much) of the settle window passes as a
-- full settle. Bounded by the gap; there is no field to do better with.
--
--   settle_tail_ms  last stored sample's t_ms - stop_ms
--   ended_by_lift   1: the curve ends before the settle window could have
--                      closed -- a lift, during the pour or while settling
--                   0: settled its full window
--                   NULL: cannot say -- no samples, stop_ms 0 (the sub-5 g
--                      flat abort assigns none), or a detector before 0a.5,
--                      whose stop rule had no fixed STOP_WINDOW_MS
--
-- Valid while STOP_WINDOW_MS = 2000, SETTLE_MS = 5000 and MAX_SHOT_MS =
-- 60000 (firmware/src/detector.h; unchanged from 0a.5 through 0a.8). A
-- detector that changes one must change this view.
CREATE VIEW v_shot_end AS
SELECT
  s.id                       AS shot_id,
  e.last_t_ms - s.stop_ms    AS settle_tail_ms,
  CASE
    WHEN e.last_t_ms IS NULL OR s.stop_ms IS NULL OR s.stop_ms = 0 THEN NULL
    WHEN s.detector_version IS NULL
      OR s.detector_version IN ('0a.1', '0a.2', '0a.3', '0a.4') THEN NULL
    WHEN s.stop_ms >= 60000 THEN e.last_t_ms - s.stop_ms < 5000
    ELSE e.last_t_ms - s.stop_ms < 7000
  END                        AS ended_by_lift
FROM shot s
LEFT JOIN (SELECT shot_id, MAX(t_ms) AS last_t_ms
             FROM shot_sample GROUP BY shot_id) e ON e.shot_id = s.id;
