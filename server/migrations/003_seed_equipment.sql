-- ------------------------------------------------------------
-- Seed the known hardware. Single-user system, the hardware is
-- documented in CLAUDE.md — making rows for it here means every
-- ingested shot can reference machine/grinder/epoch from day one
-- instead of accumulating NULLs that need backfilling.
--
-- The initial grind epoch anchors invariant 5: a grind setting is
-- only meaningful relative to a zero point. First burr clean adds
-- a new epoch row (reason='burr_clean'); nothing edits this one.
-- ------------------------------------------------------------
INSERT INTO equipment (kind, label, make, model)
VALUES ('machine', 'Ascaso Dream PID', 'Ascaso', 'Dream PID');

INSERT INTO equipment (kind, label, make, model)
VALUES ('grinder', 'Eureka Mignon Single Dose', 'Eureka', 'Mignon Single Dose');

INSERT INTO grind_epoch (grinder_id, started_at, reason, notes)
SELECT id, datetime('now'), 'initial', 'seeded with the schema'
FROM equipment WHERE kind = 'grinder' AND model = 'Mignon Single Dose';
