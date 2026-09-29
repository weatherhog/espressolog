-- Migration 003 seeded the initial grind epoch with datetime('now'), i.e.
-- whenever the database happened to be created. That is the wrong semantics:
-- reason='initial' means "how the grinder has always been, as far as anyone
-- recorded", not "since I installed this software". The burrs were not fitted
-- the day the schema was.
--
-- It only became load-bearing once the epoch lookup was bounded by the shot's
-- own started_at (so a replayed shot is filed under the epoch in force when it
-- was pulled, per hard invariant 5). With a 'now'-dated seed that bound leaves
-- every shot older than the database with no epoch at all — and therefore no
-- carried dial — which is an artefact of install order, not a fact about the
-- grinder.
--
-- Backdating is safe: the initial epoch is by definition the earliest, so
-- moving it earlier cannot reorder anything. 'T' separator to match every
-- other timestamp in the schema; 003 used a space, and these columns are
-- compared as strings.
-- Unconditional: on a fresh database there are no shots, so a guard of the
-- form "earlier than the first shot" compares against NULL, never fires, and
-- leaves a new install with the same bug this migration exists to remove.
UPDATE grind_epoch SET started_at = '2000-01-01T00:00:00Z' WHERE reason = 'initial';

-- Shots ingested before their epoch existed were stamped NULL. File them under
-- the epoch actually in force at the time, now that one covers them.
UPDATE shot
   SET grind_epoch_id = (
         SELECT e.id FROM grind_epoch e
          WHERE e.started_at <= shot.started_at
          ORDER BY e.started_at DESC, e.id DESC LIMIT 1)
 WHERE grind_epoch_id IS NULL
   AND started_at IS NOT NULL;
