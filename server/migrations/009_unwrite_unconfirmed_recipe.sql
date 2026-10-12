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
-- REVERSIBLE. The value removed here is 18.109 throughout, recoverable from
-- shot 79. Shot 3's removed recipe is the literal 2.4 / 93 / 0 / 18.0.
--
-- The leak itself is fixed in web/src/Capture.jsx (2026-10-12): every field
-- is now compared against its seed and sent only when it differs.

UPDATE shot
   SET dose_ground_g = NULL,
       dose_source   = NULL
 WHERE dose_source = 'manual'
   AND dose_ground_g IS NOT NULL
   AND ROUND(dose_ground_g, 2) != dose_ground_g;

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
