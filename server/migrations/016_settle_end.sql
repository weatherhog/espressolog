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
-- THE CEILING. t_ms is a uint16 and appendSample() drops samples past
-- 65535, so a settle running past it (a forced stop that a sample gap
-- pushed beyond 60 s, or a flat stop confirmed late) stores a short tail
-- however it ended. The threshold is therefore capped at 64535 - stop_ms:
-- a curve reaching the last second before the ceiling counts as settled.
--
-- KNOWN MISSES:
--  - 0a.8: the flat stop is backdated to the newest ring sample at or
--    before confirm - 2 s, so after a sample gap it sits further back, and
--    a lift in the last (that much) of the settle window passes as a full
--    settle. Bounded by the gap.
--  - at the ceiling: a lift in the last second before t_ms = 65535 passes
--    as a full settle; and a full settle whose samples stopped short of
--    the cap -- a sample gap across it (stop_ms 60600, a gap from 64400 to
--    65900: tail 3800 against a cap of 3935) -- reads as a lift.
--  - 0a.8 firmware ends a shot on the clock when the yield scale goes
--    quiet for 3 s (ShotDetector::tick()): while settling, or in a pour
--    65 s on (spooled as a fault, excluded at ingest). Such a curve ends
--    early and reads 1 here. That is what the
--    column means -- the settle was cut short -- but the cause can be the
--    scale rather than a lift.
-- There is no field to do better with.
--
--   settle_tail_ms  last stored sample's t_ms - stop_ms
--   ended_by_lift   1: the curve ends before the settle window could have
--                      closed -- a lift, during the pour or while settling
--                      (or, from 0a.8, a scale that went quiet: above)
--                   0: settled its full window
--                   NULL: cannot say -- no samples, stop_ms 0 (the sub-5 g
--                      flat abort assigns none), or a detector this view
--                      does not name
--
-- Named, not excluded: 0a.5 to 0a.8, with or without a suffix ('0a.8-rc').
-- Their STOP_WINDOW_MS = 2000, SETTLE_MS = 5000 and MAX_SHOT_MS = 60000
-- (firmware/src/detector.h); 0a.4 and older had no fixed stop window. Any
-- other version -- a later detector, '', 'dev' -- reads NULL until a
-- migration that has checked its constants adds it here.
CREATE VIEW v_shot_end AS
SELECT
  s.id                       AS shot_id,
  e.last_t_ms - s.stop_ms    AS settle_tail_ms,
  CASE
    WHEN e.last_t_ms IS NULL OR s.stop_ms IS NULL OR s.stop_ms = 0 THEN NULL
    WHEN s.detector_version IS NULL
      OR NOT (s.detector_version GLOB '0a.[5-8]'
              OR s.detector_version GLOB '0a.[5-8][^0-9]*') THEN NULL
    ELSE e.last_t_ms - s.stop_ms <
         MIN(CASE WHEN s.stop_ms >= 60000 THEN 5000 ELSE 7000 END,
             64535 - s.stop_ms)
  END                        AS ended_by_lift
FROM shot s
LEFT JOIN (SELECT shot_id, MAX(t_ms) AS last_t_ms
             FROM shot_sample GROUP BY shot_id) e ON e.shot_id = s.id;
