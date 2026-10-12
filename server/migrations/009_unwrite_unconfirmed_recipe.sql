-- Undo recipe values that were written as if a human had confirmed them.
--
-- WHAT HAPPENED. The capture screen seeded grind/dose/preinfusion/temp from
-- the previous shot (or from hard-coded literals when there was no previous
-- shot) and then PATCHed all of them on save whether or not they had been
-- touched. The server reads a dose arriving that way as dose_source='manual',
-- which means "a human confirmed this". Nobody had.
--
-- The evidence, from the 2026-09-29 snapshot:
--   * 18 of 80 shots carried dose_source='manual' with NO weighing row.
--   * 17 of them held the identical value 18.109. That is shot 79's real
--     measured dose, pulled 2026-09-28 at 07:10 — and all 17 were tagged in
--     one batch session the same morning between 10:19:35 and 10:20:07,
--     working backwards through the untagged list. Each was seeded from that
--     morning's shot and saved.
--   * 1 (shot 3, tagged 2026-08-28 with no previous shot) held the capture
--     screen's hard-coded literals: dial 2.4, temp 93, preinfusion 0, dose
--     18.0. The real settings are 7.9, 95 and 5, so its whole recipe is
--     fabricated. It is the only shot in the database carrying that triple;
--     the other 57 dialled shots all read 7.9 / 95 / 5.
--
-- WHAT THIS DOES NOT TOUCH. The 17 carry the CORRECT dial, temp and
-- preinfusion — they were carried forward from shots where those were right,
-- so the values are true even though unconfirmed. Nulling them would destroy
-- real information. Only the dose is another shot's number.
--
-- THE PREDICATE IS PROVABLE, not merely plausible. The capture stepper writes
-- at most 2 decimal places, so a 'manual' dose with 3 is arithmetically
-- impossible as human input. That is why this matches on the rounding rather
-- than on "manual with no weighing", which would also catch a dose somebody
-- genuinely typed on an unweighed shot.
--
-- WHAT IT WILL MISS, and it is most of the newer damage. A fabricated dose
-- carries the PREVIOUS shot's measured value, and in the 2026-09-29 snapshot
-- 29 of 48 measured doses (60 %) have two decimals or fewer. Any bogus row
-- seeded from one of those is arithmetically indistinguishable from human
-- input and survives this migration. The 17 rows here all carry 18.109 only
-- because they were tagged in one batch from one seed; shots after the
-- snapshot were each seeded from a different preceding dose, so expect
-- roughly 60 % of that damage to remain. Cleaning it would need per-shot
-- judgement, not a predicate -- and the capture screen no longer creates new
-- cases, so the population is closed.
--
-- VERIFIED ON 80 ROWS, RUNS AGAINST MORE. The snapshot is 2026-09-29 at
-- user_version 6; production is at 8 with ~30 more shots. Statement 2 rests
-- on 2.4 / 93.0 / 0.0 being unique, checked on the snapshot only. BEFORE
-- DEPLOYING, run this on a copy of the live database:
--
--   -- statement 2:
--   SELECT id, grind_dial, brew_temp_c, preinfusion_s, dose_ground_g
--     FROM shot WHERE grind_dial=2.4 AND brew_temp_c=93.0 AND preinfusion_s=0.0
--       AND dose_source='manual'
--       AND NOT EXISTS (SELECT 1 FROM weighing w WHERE w.shot_id=shot.id);
--
--   -- statement 1, AND SAVE THE OUTPUT:
--   SELECT id, dose_ground_g FROM shot
--    WHERE dose_source='manual' AND dose_ground_g IS NOT NULL
--      AND ROUND(dose_ground_g,2) != dose_ground_g;
--
-- If the first returns anything but shot 3, statement 2 is nulling real data.
--
-- SAVE THE SECOND RESULT SET SOMEWHERE. The "recoverable from shot 79" note
-- below is true of the SNAPSHOT, where every matched row held 18.109. The
-- predicate is generic, and this migration's own "WHAT IT WILL MISS" section
-- explains that post-snapshot rows were each seeded from a DIFFERENT preceding
-- dose — so on production it may null values that are neither 18.109 nor
-- recoverable from anywhere else in the database. Those (id, dose) pairs are
-- the only copy.
--
-- REVERSIBLE. The value removed here is 18.109 throughout, recoverable from
-- shot 79. Shot 3's removed recipe is the literal 2.4 / 93 / 0 / 18.0.
--
-- The leak itself is fixed in web/src/Capture.jsx (2026-10-12): every field
-- is now compared against its seed and sent only when it differs.

-- ORDER MATTERS. Statement 2 runs FIRST because statement 1 clears
-- dose_source, and statement 2's predicate requires dose_source = 'manual'.
-- Run the other way round, a row matching both would have its dose nulled and
-- then be skipped, keeping the fabricated 2.4 / 93 / 0 recipe. No such row
-- exists in the 2026-09-29 snapshot; one could exist in production.

UPDATE shot
   SET dose_ground_g = NULL,
       dose_source   = NULL,
       grind_dial    = NULL,
       brew_temp_c   = NULL,
       preinfusion_s = NULL
 WHERE grind_dial    = 2.4
   AND brew_temp_c   = 93.0
   AND preinfusion_s = 0.0
   AND dose_source   = 'manual'
   AND NOT EXISTS (SELECT 1 FROM weighing w WHERE w.shot_id = shot.id);

UPDATE shot
   SET dose_ground_g = NULL,
       dose_source   = NULL
 WHERE dose_source = 'manual'
   AND dose_ground_g IS NOT NULL
   AND ROUND(dose_ground_g, 2) != dose_ground_g;
