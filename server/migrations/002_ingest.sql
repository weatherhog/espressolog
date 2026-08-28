-- ------------------------------------------------------------
-- Ingest bookkeeping. One row per record the firmware ever
-- uploaded, keyed by (device, boot_id, seq) — the idempotency
-- key. A retry of a lost 200 hits the UNIQUE and returns 2xx
-- without a duplicate shot.
--
-- The raw record bytes are kept verbatim. 3.5 KB per shot is
-- nothing, and it means a decoder bug can be repaired months
-- later by re-parsing the archive instead of losing the data —
-- the byte stream the device sent IS the measurement.
-- ------------------------------------------------------------
CREATE TABLE ingest_record (
  id           INTEGER PRIMARY KEY,
  device_id    TEXT NOT NULL,           -- ESP32 WiFi MAC
  boot_id      INTEGER NOT NULL,
  seq          INTEGER NOT NULL,
  record_type  INTEGER NOT NULL,        -- 1 shot, 2 weighing
  received_at  TEXT NOT NULL,
  time_valid   INTEGER NOT NULL,        -- device had SNTP when it recorded
  shot_id      INTEGER REFERENCES shot(id)     ON DELETE SET NULL,
  weighing_id  INTEGER REFERENCES weighing(id) ON DELETE SET NULL,
  raw          BLOB NOT NULL,
  UNIQUE (device_id, boot_id, seq)
);
