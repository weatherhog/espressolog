-- Until now the "loaded bag" lived only in one browser's localStorage
-- (web/src/App.jsx), so the server never learned it. A shot arrived with
-- machine, grinder and grind epoch attributed automatically by
-- defaultContext, but with no bean and no grind — and the ONLY path that
-- ever recorded either was a human opening the Capture screen.
--
-- That made tagging load-bearing for analysis, which contradicts the rule
-- this project is built on: every required tap before coffee is a tax paid
-- while holding a portafilter, and it is how logging habits die. An
-- untagged shot was not missing a label nobody typed; it was missing one
-- the system already knew and had nowhere to put.
--
-- bean.loaded_at makes the loaded bag server-side state. The loaded bag is
-- the one with the most recent loaded_at. Ingest stamps it onto every
-- incoming shot, exactly as it already stamps the grind epoch.
ALTER TABLE bean ADD COLUMN loaded_at TEXT;

-- How bean_id — and the grind_dial carried forward alongside it — got there:
--
--   loaded    attributed at ingest from the loaded bag. A good default, and
--             wrong only if a bag was swapped without pressing Load.
--   user      a human set or confirmed it, via Capture or an edit.
--   backfill  filled retroactively from a neighbouring shot in time.
--
-- Mirrors dose_source. An analysis needing certainty can require 'user';
-- 0f decides for itself whether an inferred bean is good enough. Without
-- this column an inferred value would be indistinguishable from a
-- confirmed one, which is the part that would quietly corrupt a noise floor.
ALTER TABLE shot ADD COLUMN bean_source TEXT
  CHECK (bean_source IN ('loaded','user','backfill') OR bean_source IS NULL);

-- Every bean already on a shot got there through the Capture screen.
UPDATE shot SET bean_source = 'user' WHERE bean_id IS NOT NULL;
