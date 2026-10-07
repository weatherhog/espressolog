-- ============================================================
-- Espresso shot log — Phase 0 (logging only)
-- SQLite. Timestamps are ISO-8601 UTC text; durations are ms;
-- weights in the sample table are integer milligrams.
-- ============================================================

PRAGMA foreign_keys = ON;

-- ------------------------------------------------------------
-- Equipment. One table, discriminated by kind, because baskets
-- and screens get swapped often enough that they need identity
-- and you want to ask "were all my good shots on the 24 g basket?"
-- ------------------------------------------------------------
CREATE TABLE equipment (
  id           INTEGER PRIMARY KEY,
  kind         TEXT NOT NULL CHECK (kind IN
                 ('machine','grinder','burrs','basket','scale','portafilter',
                  'wdt','leveler','tamper','puck_screen')),
  label        TEXT NOT NULL,           -- 'Ascaso Dream PID'
  make         TEXT,
  model        TEXT,
  ble_address  TEXT,                    -- scales: MAC, binds role to device
  role_hint    TEXT,                    -- 'yield' | 'dose'. A HINT, not a rule:
                                        -- attribution must still work when one
                                        -- scale does both jobs, or when the
                                        -- other one's battery is flat.
  spec         TEXT,                    -- JSON: {"basket_g":18,"holes":720}
  acquired_on  TEXT,
  retired_on   TEXT
);

CREATE UNIQUE INDEX ix_equipment_ble ON equipment(ble_address)
  WHERE ble_address IS NOT NULL;

-- ------------------------------------------------------------
-- Grind reference epochs.
--
-- THE IMPORTANT ONE. A grind setting is only meaningful relative
-- to a zero point, and that zero point moves every time the burrs
-- come out for cleaning or get replaced. "2.4" in June and "2.4"
-- in August are not the same grind. Every shot points at the epoch
-- it was pulled under, and you never fit a model across epochs
-- without an explicit offset.
--
-- Phase 2 (stepper motor) slots in here too: same concept, the
-- epoch is just where the homing switch was that time.
-- ------------------------------------------------------------
CREATE TABLE grind_epoch (
  id           INTEGER PRIMARY KEY,
  grinder_id   INTEGER NOT NULL REFERENCES equipment(id),
  burrs_id     INTEGER          REFERENCES equipment(id),
  started_at   TEXT NOT NULL,
  reason       TEXT NOT NULL CHECK (reason IN
                 ('initial','burr_clean','burr_swap','motor_fitted','recalibrated')),
  -- best-guess mapping back to the previous epoch, if you bothered
  offset_from_prev REAL,
  notes        TEXT
);

-- ------------------------------------------------------------
-- Coffee. One row per physical bag.
-- ------------------------------------------------------------
CREATE TABLE bean (
  id           INTEGER PRIMARY KEY,
  roaster      TEXT NOT NULL,
  name         TEXT NOT NULL,
  origin       TEXT,
  region       TEXT,
  producer     TEXT,
  varietal     TEXT,
  process      TEXT,                    -- washed / natural / honey / anaerobic
  roast_level  TEXT CHECK (roast_level IN
                 ('light','medium-light','medium','medium-dark','dark') OR roast_level IS NULL),
  roast_date   TEXT,
  bag_size_g   REAL,
  price_cents  INTEGER,
  currency     TEXT DEFAULT 'EUR',
  opened_at    TEXT,
  frozen_at    TEXT,                    -- when portioned into single doses
  dose_count   INTEGER,                 -- how many portions went in the freezer
  portion_target_g REAL,                -- what you portioned each dose to.
                                        -- Lets retention be estimated as
                                        -- portion_target_g - dose_ground_g
                                        -- without adding a weighing step to
                                        -- the morning. Accuracy is however
                                        -- carefully you portioned.
  finished_at  TEXT,
  url          TEXT,
  notes        TEXT
);

-- ------------------------------------------------------------
-- Prep profile. Your routine is fixed (spiral WDT 10 turns,
-- leveller, tamp, screen), so it lives here once instead of
-- being retyped 400 times. Per-shot deviations go in
-- shot.prep_deviation as free text — if a deviation recurs,
-- promote it to a new profile.
-- ------------------------------------------------------------
CREATE TABLE prep_profile (
  id             INTEGER PRIMARY KEY,
  name           TEXT NOT NULL,
  basket_id      INTEGER REFERENCES equipment(id),
  wdt_tool_id    INTEGER REFERENCES equipment(id),
  wdt_pattern    TEXT,                  -- 'spiral'
  wdt_turns      INTEGER,               -- 10
  use_leveler    INTEGER NOT NULL DEFAULT 0,
  leveler_id     INTEGER REFERENCES equipment(id),
  leveler_depth_mm REAL,
  use_tamp       INTEGER NOT NULL DEFAULT 1,
  tamper_id      INTEGER REFERENCES equipment(id),
  puck_screen_id INTEGER REFERENCES equipment(id),
  use_rdt        INTEGER NOT NULL DEFAULT 0,
  is_default     INTEGER NOT NULL DEFAULT 0,
  created_at     TEXT NOT NULL
);

-- ------------------------------------------------------------
-- The shot.
-- Measured fields are derived from shot_sample but denormalised
-- here so you can query without unrolling every curve. They are
-- reproducible: detector_version says which algorithm produced
-- them, so a better detector later can safely recompute the lot.
-- ------------------------------------------------------------
CREATE TABLE shot (
  id                INTEGER PRIMARY KEY,
  started_at        TEXT NOT NULL,
  tz_offset_min     INTEGER,

  bean_id           INTEGER REFERENCES bean(id),
  machine_id        INTEGER REFERENCES equipment(id),
  grinder_id        INTEGER REFERENCES equipment(id),
  scale_id          INTEGER REFERENCES equipment(id),
  prep_profile_id   INTEGER REFERENCES prep_profile(id),
  prep_deviation    TEXT,

  -- grind
  grind_epoch_id    INTEGER REFERENCES grind_epoch(id),
  grind_dial        REAL,               -- what the collar reads: 2.4
  grind_steps       INTEGER,            -- absolute stepper position; NULL until phase 2

  -- dose. You weigh once, after grinding, because the freezer doses
  -- are pre-portioned. So dose_ground_g is measured and dose_in_g is
  -- usually the bag's portion_target_g rather than a real weighing.
  -- dose_source records which, so an analysis can exclude nominal
  -- doses when that matters.
  from_frozen       INTEGER NOT NULL DEFAULT 0,
  freezer_out_at    TEXT,               -- lets you compute rest-before-grind
  dose_in_g         REAL,               -- beans before grinding
  dose_ground_g     REAL,               -- weighed after grinding
  dose_source       TEXT CHECK (dose_source IN
                      ('measured','portion_nominal','manual') OR dose_source IS NULL),
  purge_g           REAL,

  -- machine settings
  brew_temp_c       REAL,               -- PID setpoint
  preinfusion_s     REAL,
  target_yield_g    REAL,
  target_time_s     REAL,
  -- The Dream's board stores one timer per switch direction, and a
  -- long hold silently overwrites it. Recording which direction was
  -- used and what the timer was set to means a shot that hit the
  -- board's own cutoff is distinguishable from one you stopped, and
  -- an accidental reprogram shows up as a step change in the data
  -- instead of an unexplained cluster of weird shots.
  switch_direction   TEXT CHECK (switch_direction IN ('down','up') OR switch_direction IS NULL),
  programmed_timer_s REAL,

  -- measured (derived from shot_sample)
  first_drip_ms     INTEGER,            -- true puck-resistance signal once the
                                        -- switch is sensed: pump-on to first liquid
  stop_ms           INTEGER,            -- when the pump stopped
  stopped_by        TEXT CHECK (stopped_by IN
                      ('manual','board_timer','auto_weight','fault') OR stopped_by IS NULL),
  s_since_flush     REAL,               -- brew temp proxy on a single boiler
  yield_at_stop_g   REAL,
  yield_final_g     REAL,               -- after drips settle
  settle_offset_g   REAL,               -- final − at_stop. THE PHASE-1 CALIBRATION.
  peak_flow_gps     REAL,
  mean_flow_gps     REAL,               -- over the stable window only
  flow_win_start_ms INTEGER,
  flow_win_end_ms   INTEGER,

  -- environment (nullable; add a sensor later if you care)
  ambient_c         REAL,
  ambient_rh        REAL,

  -- provenance and hygiene
  detector_version  TEXT,
  firmware_version  TEXT,
  excluded          INTEGER NOT NULL DEFAULT 0,
  exclude_reason    TEXT,               -- 'channelled','wrong dose','test','spilled'
  notes             TEXT,
  created_at        TEXT NOT NULL,
  updated_at        TEXT
);

CREATE INDEX ix_shot_time  ON shot(started_at DESC);
CREATE INDEX ix_shot_bean  ON shot(bean_id, started_at DESC);
CREATE INDEX ix_shot_epoch ON shot(grind_epoch_id);

-- ------------------------------------------------------------
-- The curve. Integer units so repeated arithmetic never drifts:
-- Bookoo reports to 0.01 g, so milligrams is lossless.
-- ~300 rows per shot at 10 Hz. A thousand shots is 300k rows,
-- which SQLite does not notice.
--
-- weight_mg is the OUTLET side (scale, after the puck).
-- inlet_pulses is the INLET side (Digmesa on J4, before the puck).
-- The gap between them is water the puck is absorbing rather than
-- passing, so inlet minus outlet over time is a direct read on
-- puck resistance — the closest thing to a pressure profile
-- available without plumbing in a transducer.
--
-- Pulses are stored raw and cumulative, not converted to ml. The
-- ml/pulse factor is calibrated empirically and will be revised;
-- storing the derived value would bake a guess into the archive.
-- ------------------------------------------------------------
CREATE TABLE shot_sample (
  shot_id      INTEGER NOT NULL REFERENCES shot(id) ON DELETE CASCADE,
  t_ms         INTEGER NOT NULL,
  weight_mg    INTEGER NOT NULL,
  -- Cumulative WITHIN THE SHOT (firmware windows it at pour confirmation),
  -- raw pulses, never millilitres. NULL and 0 differ and the difference is
  -- load-bearing: NULL means nothing was counting (no tap, or the tap was
  -- off), 0 means the tap was live and no water had moved yet. The firmware
  -- says which via the FLOW_TAP header flag; a reader that treats NULL as 0
  -- invents measurements.
  inlet_pulses INTEGER,
  pressure_cbar INTEGER,
  temp_dc      INTEGER,                 -- decidegrees C, from the display bus if sniffable
  PRIMARY KEY (shot_id, t_ms)
) WITHOUT ROWID;

-- Flowmeter calibration, versioned. Recalibrate after a descale or
-- a pump change and add a row rather than editing one, so old shots
-- stay interpretable against the factor that was true at the time.
CREATE TABLE flowmeter_calibration (
  id            INTEGER PRIMARY KEY,
  machine_id    INTEGER NOT NULL REFERENCES equipment(id),
  measured_at   TEXT NOT NULL,
  pulses        INTEGER NOT NULL,       -- counted over the calibration run
  grams_water   REAL NOT NULL,          -- weighed on the Bookoo
  ml_per_pulse  REAL GENERATED ALWAYS AS (grams_water / pulses) STORED,
  notes         TEXT
);

-- ------------------------------------------------------------
-- Tasting. Separate row so you can taste later, or twice, and so
-- an empty tasting is visibly different from a bad one.
--
-- `balance` is the field that earns its keep: under / balanced /
-- over is the one axis that maps directly onto a grind move, and
-- it's the label a model can actually learn from. `overall` is
-- the objective function when you get to Bayesian optimisation.
-- ------------------------------------------------------------
CREATE TABLE tasting (
  id           INTEGER PRIMARY KEY,
  shot_id      INTEGER NOT NULL REFERENCES shot(id) ON DELETE CASCADE,
  tasted_at    TEXT NOT NULL,
  overall      INTEGER CHECK (overall BETWEEN 1 AND 10),
  balance      TEXT CHECK (balance IN ('under','balanced','over')),
  acidity      INTEGER CHECK (acidity   BETWEEN 1 AND 5),
  sweetness    INTEGER CHECK (sweetness BETWEEN 1 AND 5),
  bitterness   INTEGER CHECK (bitterness BETWEEN 1 AND 5),
  body         INTEGER CHECK (body      BETWEEN 1 AND 5),
  defects      TEXT,                    -- JSON array: ["astringent","hollow"]
  descriptors  TEXT,                    -- JSON array: ["peach","black tea"]
  notes        TEXT,                    -- free prose — the LLM's input later
  drink        TEXT CHECK (drink IN ('straight','milk') OR drink IS NULL)
);

CREATE INDEX ix_tasting_shot ON tasting(shot_id);

-- ------------------------------------------------------------
-- Maintenance. Cheap to log, and it explains outliers months
-- later ("why did everything go weird in week 12" — you'd
-- descaled and not reset the grind).
-- ------------------------------------------------------------
CREATE TABLE maintenance (
  id           INTEGER PRIMARY KEY,
  equipment_id INTEGER NOT NULL REFERENCES equipment(id),
  performed_at TEXT NOT NULL,
  kind         TEXT NOT NULL,           -- backflush / descale / burr_clean / gasket / screen
  notes        TEXT
);

-- ------------------------------------------------------------
-- Machine events, from optocouplers on the two switch legs.
-- Read-only sensing: nothing here drives anything.
--
-- Every energised period gets a row, whether or not it turned
-- into a logged shot. That matters — flushes, aborted shots and
-- accidental presses all affect the thermal state of a single
-- boiler, and if you only record the shots you keep, the machine's
-- actual history is missing from the record.
--
-- A 'programming' row is inferred from an unusually long hold and
-- is your audit trail for "when did my 60 s timer change".
-- ------------------------------------------------------------
CREATE TABLE machine_event (
  id           INTEGER PRIMARY KEY,
  machine_id   INTEGER NOT NULL REFERENCES equipment(id),
  started_at   TEXT NOT NULL,
  duration_ms  INTEGER,
  direction    TEXT NOT NULL CHECK (direction IN ('down','up')),
  kind         TEXT NOT NULL CHECK (kind IN
                 ('shot','flush','programming','unknown')),
  shot_id      INTEGER REFERENCES shot(id) ON DELETE SET NULL,
  notes        TEXT
);

CREATE INDEX ix_event_time ON machine_event(started_at DESC);
CREATE INDEX ix_event_shot ON machine_event(shot_id);

-- ------------------------------------------------------------
-- Weighings. Every stable reading on any scale, recorded as an
-- anonymous event with no declared meaning.
--
-- THE POINT: the UI never asks "are you about to weigh a dose?"
-- Requiring intent up front is a tap you pay while holding a
-- portafilter, and it is how logging habits die. Instead every
-- stable reading is recorded blind, and when a shot closes it
-- claims the last unattributed weighing before it started.
--
-- A stable event is |dw/dt| < 0.05 g/s for >= 1.5 s, weight > 5 g,
-- no shot in progress, and within a plausibility window (12-24 g
-- for a dose). Re-placing the cup produces several events; the
-- last one wins and the earlier ones stay here unattributed,
-- which is exactly what you want when a dose looks wrong and you
-- need to see what the scale actually saw.
--
-- Works identically whether one scale does both jobs or two split
-- them, so a flat battery degrades the data rather than breaking
-- the workflow.
-- ------------------------------------------------------------
CREATE TABLE weighing (
  id             INTEGER PRIMARY KEY,
  scale_id       INTEGER REFERENCES equipment(id),
  observed_at    TEXT NOT NULL,
  grams          REAL NOT NULL,
  stable_ms      INTEGER,               -- how long it held steady
  role           TEXT CHECK (role IN
                   ('dose_beans','dose_ground','other') OR role IS NULL),
  shot_id        INTEGER REFERENCES shot(id) ON DELETE SET NULL,
  attributed_by  TEXT CHECK (attributed_by IN ('auto','user') OR attributed_by IS NULL),
  superseded     INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX ix_weighing_time ON weighing(observed_at DESC);
CREATE INDEX ix_weighing_shot ON weighing(shot_id);

-- ============================================================
-- Views. Derived values live here, not in columns, so there is
-- exactly one definition of "ratio" in the system.
-- ============================================================

CREATE VIEW v_shot AS
SELECT
  s.*,
  b.roaster, b.name AS bean_name, b.process, b.roast_level,
  ROUND(julianday(s.started_at) - julianday(b.roast_date))      AS days_off_roast,
  ROUND(julianday(s.started_at) - julianday(b.opened_at))       AS days_open,
  s.yield_final_g / NULLIF(s.dose_ground_g, 0)                  AS ratio,
  -- prefer a real weighing, fall back to the bag's portion target
  COALESCE(s.dose_in_g, b.portion_target_g) - s.dose_ground_g   AS retention_g,
  CASE WHEN s.dose_in_g IS NULL THEN 'estimated' ELSE 'measured' END AS retention_basis,
  (s.stop_ms - s.first_drip_ms) / 1000.0                        AS pour_s,
  s.stop_ms / 1000.0                                            AS total_s,
  t.overall, t.balance, t.notes AS taste_notes
FROM shot s
LEFT JOIN bean b     ON b.id = s.bean_id
LEFT JOIN tasting t  ON t.shot_id = s.id;

-- Cumulative coffee through the current burr set, for a
-- "time to clean the burrs" nudge.
CREATE VIEW v_burr_load AS
SELECT
  e.id            AS epoch_id,
  e.started_at,
  COUNT(s.id)     AS shots,
  SUM(s.dose_in_g) / 1000.0 AS kg_through
FROM grind_epoch e
LEFT JOIN shot s ON s.grind_epoch_id = e.id
GROUP BY e.id;

-- Thermal context: how long before each shot the group last saw
-- water, and how long it ran. On a single boiler this is one of
-- the larger uncontrolled variables, and until the switch is
-- sensed it is entirely absent from the record.
CREATE VIEW v_shot_thermal AS
SELECT
  s.id AS shot_id,
  s.started_at,
  (SELECT MAX(e.started_at) FROM machine_event e
     WHERE e.started_at < s.started_at AND e.kind = 'flush')      AS last_flush_at,
  (SELECT e.duration_ms FROM machine_event e
     WHERE e.started_at < s.started_at AND e.kind = 'flush'
     ORDER BY e.started_at DESC LIMIT 1)                          AS last_flush_ms,
  (julianday(s.started_at) -
   julianday((SELECT MAX(e.started_at) FROM machine_event e
                WHERE e.started_at < s.started_at
                  AND e.kind IN ('flush','shot')))) * 86400.0     AS idle_s
FROM shot s;

-- The dial-in trail for whatever bag is in play: grind against
-- flow, in order, excluding the shots you threw away. This is the
-- table the phase-3 controller will be fitted on.
CREATE VIEW v_dialin AS
SELECT
  bean_id, grind_epoch_id, started_at,
  grind_dial, grind_steps, dose_ground_g,
  mean_flow_gps, total_s, ratio, overall, balance
FROM v_shot
WHERE excluded = 0
ORDER BY bean_id, started_at;
