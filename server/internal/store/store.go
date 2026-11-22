// Package store owns the SQLite file: migrations, ingest writes, and the
// read queries the API serves. modernc.org/sqlite keeps the binary CGO-free.
package store

import (
	"database/sql"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"log"
	"sort"
	"time"

	_ "modernc.org/sqlite"

	"espressolog/internal/record"
)

type Store struct {
	db *sql.DB
}

func Open(path string, migrations embed.FS) (*Store, error) {
	// One writer: SQLite likes a single connection, and this workload is a
	// handful of shots a day.
	db, err := sql.Open("sqlite", path+"?_pragma=journal_mode(WAL)&_pragma=foreign_keys(ON)&_pragma=busy_timeout(5000)")
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	s := &Store{db: db}
	if err := s.migrate(migrations); err != nil {
		db.Close()
		return nil, err
	}
	if n, err := s.backfillPreinfusion(); err != nil {
		db.Close()
		return nil, fmt.Errorf("backfill pre-infusion: %w", err)
	} else if n > 0 {
		log.Printf("store: (re-)measured pre-infusion on %d shot(s) from their pulses", n)
	}
	return s, nil
}

func (s *Store) Close() error { return s.db.Close() }

// migrate applies migrations/NNN_*.sql in order, tracking progress in
// PRAGMA user_version. Files are whole-script, applied transactionally.
func (s *Store) migrate(migrations embed.FS) error {
	names, err := fs.Glob(migrations, "migrations/*.sql")
	if err != nil {
		return err
	}
	sort.Strings(names)

	var version int
	if err := s.db.QueryRow("PRAGMA user_version").Scan(&version); err != nil {
		return err
	}
	for i, name := range names {
		n := i + 1
		if n <= version {
			continue
		}
		script, err := migrations.ReadFile(name)
		if err != nil {
			return err
		}
		tx, err := s.db.Begin()
		if err != nil {
			return err
		}
		if _, err := tx.Exec(string(script)); err != nil {
			tx.Rollback()
			return fmt.Errorf("migration %s: %w", name, err)
		}
		if _, err := tx.Exec(fmt.Sprintf("PRAGMA user_version = %d", n)); err != nil {
			tx.Rollback()
			return err
		}
		if err := tx.Commit(); err != nil {
			return err
		}
	}
	return nil
}

// IngestResult reports what an upload became.
type IngestResult struct {
	Duplicate  bool
	ShotID     int64
	WeighingID int64
}

// Ingest stores one decoded record plus its raw bytes, idempotently on
// (device, boot_id, seq). A duplicate returns Duplicate=true and no error —
// the device retries lost 200s by design.
func (s *Store) Ingest(deviceID string, r *record.Record, raw []byte, receivedAt time.Time) (IngestResult, error) {
	var res IngestResult
	h := &r.Header

	tx, err := s.db.Begin()
	if err != nil {
		return res, err
	}
	defer tx.Rollback()

	var exists int
	err = tx.QueryRow(`SELECT COUNT(*) FROM ingest_record WHERE device_id=? AND boot_id=? AND seq=?`,
		deviceID, h.BootID, h.Seq).Scan(&exists)
	if err != nil {
		return res, err
	}
	if exists > 0 {
		res.Duplicate = true
		return res, nil
	}

	scaleID, err := ensureScale(tx, h.ScaleMacString())
	if err != nil {
		return res, err
	}

	// started_at: the device clock when it had SNTP; the arrival time
	// otherwise. The raw record keeps started_at_millis + boot_id, so a
	// smarter backfill can refine unsynced records later.
	startedAt := receivedAt.UTC()
	if h.TimeValid == 1 && h.StartedAtUnixMs > 0 {
		startedAt = time.UnixMilli(int64(h.StartedAtUnixMs)).UTC()
	}

	now := receivedAt.UTC().Format(time.RFC3339)

	switch h.RecordType {
	case record.TypeShot:
		var excluded bool
		res.ShotID, excluded, err = insertShot(tx, r, scaleID, startedAt, now)
		// A record excluded at ingest — a placement, a reject, a fault — is
		// not the shot a weighing was for. Giving it the dose took it from
		// the real shot pulled seconds later: set the cup back after weighing
		// and the placement claimed the 18 g (review, 2026-11-15). Rejects
		// too: almost every one is a knock or a drip, not a shot that choked
		// (a second review, the same day, reversed a draft that let them keep
		// it). The late-weighing path skips the same records by their stored
		// ingest reason (attributeWeighingToLaterShot).
		if err == nil && !excluded {
			err = attributeDoseToShot(tx, res.ShotID, startedAt)
		}
	case record.TypeWeighing:
		res.WeighingID, err = insertWeighing(tx, h, scaleID, startedAt)
		if err == nil {
			err = attributeWeighingToLaterShot(tx, res.WeighingID, mg(h.GramsMg), startedAt)
		}
	default:
		err = fmt.Errorf("unknown record type %d", h.RecordType)
	}
	if err != nil {
		return res, err
	}

	_, err = tx.Exec(`
		INSERT INTO ingest_record (device_id, boot_id, seq, record_type, received_at, time_valid, shot_id, weighing_id, raw)
		VALUES (?,?,?,?,?,?,?,?,?)`,
		deviceID, h.BootID, h.Seq, h.RecordType, now, h.TimeValid,
		nullID(res.ShotID), nullID(res.WeighingID), raw)
	if err != nil {
		return res, err
	}
	return res, tx.Commit()
}

func nullID(id int64) any {
	if id == 0 {
		return nil
	}
	return id
}

// ensureScale finds the equipment row for a scale's BLE address, creating a
// stub on first sight so ingest never blocks on manual data entry.
func ensureScale(tx *sql.Tx, mac string) (any, error) {
	if mac == "00:00:00:00:00:00" {
		return nil, nil
	}
	var id int64
	err := tx.QueryRow(`SELECT id FROM equipment WHERE ble_address = ?`, mac).Scan(&id)
	if err == sql.ErrNoRows {
		r, err := tx.Exec(`INSERT INTO equipment (kind, label, make, ble_address)
			VALUES ('scale', 'Bookoo Themis ' || ?, 'Bookoo', ?)`, mac, mac)
		if err != nil {
			return nil, err
		}
		id, _ = r.LastInsertId()
		return id, nil
	}
	if err != nil {
		return nil, err
	}
	return id, nil
}

// defaultContext resolves the machine, grinder and its most recent grind
// epoch (seeded by migration 003) so ingested shots reference them from the
// start. Any of these may be nil on an unseeded database — that degrades the
// row, never fails the ingest.
// startedAt bounds the epoch for the same reason it bounds the recipe: a
// replayed shot must be filed under the epoch in force when it was PULLED.
// Without it a shot recovered after a burr change lands in the epoch created
// after it happened — wrong under invariant 5, since v_dialin groups on
// grind_epoch_id, and it then sends defaultRecipe hunting the wrong epoch for
// a dial. This was the last "as of now" query on the ingest path.
func defaultContext(tx *sql.Tx, startedAt time.Time) (machineID, grinderID, epochID any) {
	at := startedAt.UTC().Format(time.RFC3339)
	var id int64
	if tx.QueryRow(`SELECT id FROM equipment WHERE kind='machine' ORDER BY id LIMIT 1`).Scan(&id) == nil {
		machineID = id
	}
	if tx.QueryRow(`SELECT id FROM equipment WHERE kind='grinder' ORDER BY id LIMIT 1`).Scan(&id) == nil {
		grinderID = id
		if tx.QueryRow(`SELECT id FROM grind_epoch
		                 WHERE grinder_id=? AND started_at <= ?
		                 ORDER BY started_at DESC, id DESC LIMIT 1`, id, at).Scan(&id) == nil {
			epochID = id
		}
	}
	return
}

// The recipe a shot was pulled on, inferred rather than asked for — the same
// bargain as dose attribution. Both values are defaults a human can correct,
// never a claim that a human confirmed them; bean_source records which.
//
//	bean  the loaded bag (bean.loaded_at, newest wins).
//	grind carried forward from the last shot IN THE SAME GRIND EPOCH,
//	      because the grinder is physically still wherever it was last set.
//	      There is no "current grind" to store — the last shot IS the record
//	      of it.
//
// The epoch scope is hard invariant 5, not tidiness: a dial reading is
// relative to a zero point that moves whenever the burrs come out, so
// carrying 7.9 across a burr change would assert a setting that never
// existed. Across a boundary this degrades to NULL, which is the honest
// answer — the first shot of a new epoch genuinely has no known dial until
// someone says so.
//
// Both may be nil: no bag loaded, or no shot in this epoch has carried a
// grind. A nil must leave the column NULL rather than invent a value.
// startedAt bounds both queries: the recipe must be the one in force WHEN THE
// SHOT WAS PULLED, not when the record happened to arrive. Those differ more
// often than they look like they would — the firmware spools to flash and
// uploads only on confirmed success, so a WiFi outage delivers a backlog
// hours later, and the replay tool re-ingests archived curves long after the
// fact. Unbounded, replaying an old capture after loading today's bag stamps
// every recovered shot with today's coffee and marks it 'loaded', which reads
// as authoritative.
// brewTemp and preinfusion are carried the same way as grindDial but WITHOUT
// the epoch scope, because an epoch boundary is a burr change — it says
// nothing about the PID setpoint or the pre-infusion time, which are machine
// settings and survive it.
//
// Added 2026-10-12. Until then only grind_dial was carried, and the capture
// screen made up the difference by PATCHing a seeded brew_temp_c and
// preinfusion_s on every save whether or not anyone had touched them — which
// is how 18 of 80 shots came to carry a dose labelled "a human confirmed
// this" that nobody typed. Stopping the UI writing them left the two columns
// with no source at all, so the carry-forward has to exist somewhere: here,
// at ingest, where it is at least honest about being an inference and is
// bounded by the shot's own start time.
// thisShotEditedAParameter: the incoming record's own setpoint_touched. A shot
// that recorded an edit must NOT be stamped with the pre-edit settings, and
// the bound below cannot see that on its own — defaultRecipe runs before this
// row exists, so the MAX(started_at) subquery finds no edit to stop at and
// carries the superseded value onto the very shot that reports the change.
// Every later shot then inherits it THROUGH that row, defeating the guard
// entirely. Found 2026-10-12 by the first test ever written for this function,
// after four rounds of human review had passed over it.
func defaultRecipe(tx *sql.Tx, epochID any, startedAt time.Time, thisShotEditedAParameter bool) (beanID, grindDial, brewTemp, preinfusion any) {
	at := startedAt.UTC().Format(time.RFC3339)
	var id int64
	if tx.QueryRow(`SELECT id FROM bean
	                 WHERE loaded_at IS NOT NULL AND loaded_at <= ?
	                 ORDER BY loaded_at DESC, id DESC LIMIT 1`, at).Scan(&id) == nil {
		beanID = id
	}
	if epochID != nil {
		var dial float64
		if tx.QueryRow(`SELECT grind_dial FROM shot
		                 WHERE grind_dial IS NOT NULL AND grind_epoch_id = ?
		                   AND started_at <= ?
		                 ORDER BY started_at DESC, id DESC LIMIT 1`, epochID, at).Scan(&dial) == nil {
			grindDial = dial
		}
	}
	// KNOWN GAP, and it is not closable here: this guard only works when the
	// edit record is THE ONE BEING INGESTED or already in the table. Reverse
	// the arrival order and it is bypassed — a shot pulled AFTER the edit but
	// ingested BEFORE it carries the pre-edit value (there is no edit row yet
	// to stop at), and every later shot then finds that row sitting at
	// started_at > the edit and carries the stale value forward indefinitely.
	//
	// "Across boots order is arbitrary" cuts both ways and only one direction
	// is handled. The root cause is that the carry is STAMPED at ingest and
	// nothing re-evaluates it. Since migration 012 an inference CAN be told
	// from a confirmation (brew_temp_source / preinfusion_source = 'carried'),
	// but nothing yet unwrites one retroactively. Closing it needs one of:
	//
	//   (a) compute the carry at READ time (a view) instead of stamping it, or
	//   (b) have the ingest of a setpoint_touched record NULL any
	//       later-pulled 'carried' rows (the source columns exist since 012).
	//
	// A v5 record that saw the setpoint dialled (brew_temp_source 'display')
	// does not depend on any of this: its value is the machine's own.
	//
	// Both are schema changes and neither belongs in a deploy that is already
	// running an irreversible migration. TestRecipeCarryKnownGapOnReversedArrival
	// documents the case and will start failing the moment it is fixed. The
	// same gap applies to setpoint_pending (v5): a pending shot that arrives
	// after a later one does not unwrite that one's carried temperature.
	//
	// STOP AT A RECORDED PARAMETER EDIT, for BOTH values.
	//
	// KNOW WHAT THE FLAG ACTUALLY RECORDS BEFORE TRUSTING THIS GUARD.
	// `main.cpp` calls clearSetpointTouched() on the transition into POURING,
	// deliberately ("a new shot starts with a clean slate, otherwise one tweak
	// at the start of a session condemns every shot after it"). So the flag
	// that reaches this column means "a parameter was edited BETWEEN pour
	// start and shot completion" — while the display is showing the brew
	// timer. An edit made between shots, which is the realistic case, is
	// latched and then cleared at the next pour start and never lands on any
	// row. In practice this guard therefore fires rarely — *for real edits*.
	// (A brew-temperature edit still BLINKING, unconfirmed, at pour start is
	// the one between-shots edit that does land, and on its own column,
	// setpoint_pending, v5 — not here: see the brew-temperature carry below.)
	// BEFORE FIRMWARE 0.9.9 the decoder latched on ANY two blank frames
	// within BLINK_WINDOW_US, so every steam cycle latched too (CLAUDE.md,
	// the display-bus blink paragraph). A shot from that firmware that saw a
	// post-steam blink after pump-off reached this guard and lost its
	// carried brew_temp_c / preinfusion_s — and, through the `since` floor
	// below, so did every later shot until a value was typed. Whether any
	// shot did is unchecked. 0.9.9 latches on blank onsets and steam does
	// not latch.
	//
	// That is a firmware-side question, not a reason to drop the guard: when
	// the flag IS set it is the machine's own word that a setting moved, and
	// carrying across it would be wrong. But do not read a database full of
	// setpoint_touched = 0 as "no edits ever happened".
	//
	// setpoint_touched latches on the display blanking twice inside
	// BLINK_WINDOW_US, plus `PrG`. A draft of this called it "the machine's
	// own statement that the brew temperature was being changed" and therefore
	// exempted preinfusion_s. That is wrong twice over: CLAUDE.md's SET UP
	// table lists `Pr` (pre-infusion, 0-5 s) as a parameter edited by the same
	// gesture and rendered by the same blanking, so the flag is evidence of a
	// parameter edit IN GENERAL — at least as informative about preinfusion as
	// about temperature. Exempting it carried preinfusion across exactly the
	// event the guard exists to stop.
	//
	// COMPARE started_at, NOT id. A draft used `id > (SELECT MAX(id) ...)`,
	// and id is INGEST order while started_at is PULL order. They diverge as
	// documented normal behaviour: the spool uploads in filename order,
	// Spool::oldest() says "across boots order is arbitrary", and a WiFi
	// outage — the whole reason the spool exists — delivers a backlog out of
	// chronological sequence. Measured: a shot pulled before an edit but
	// ingested after it carried a pre-edit setpoint straight across.
	//
	// AND USE >=, NOT >. The edit shot itself can hold the NEW value — when a
	// human typed it — and is then the only row that does. Excluding it meant
	// brew_temp_c went NULL on the edit shot and stayed NULL on every shot
	// after it forever, since nothing downstream would ever have a value to
	// carry. Measured. Its MACHINE values ('display', 'flow') predate the edit
	// and are excluded below instead.
	since := `COALESCE((SELECT MAX(started_at) FROM shot
	                     WHERE setpoint_touched = 1 AND started_at <= ?), '')`
	// The brew temperature alone also stops at a shot pulled while a
	// temperature edit was still unconfirmed (setpoint_pending, v5): the
	// value before it is the pre-edit one. >= so a value typed ON that shot
	// still carries. Pre-infusion is not affected (review, 2026-11-15).
	sincePending := `COALESCE((SELECT MAX(started_at) FROM shot
	                     WHERE setpoint_pending = 1 AND started_at <= ?), '')`
	if thisShotEditedAParameter {
		// Only brew_temp_c and preinfusion_s: beanID and grindDial are already
		// assigned above and stay. setpoint_touched latches on the machine's
		// DISPLAY, and CLAUDE.md's SET UP menu holds no grinder parameter, so
		// an edit says nothing about the dial or the bag. (A draft's comment
		// here said "the settings this shot ran with are unknown", which reads
		// as though all four go NULL.)
		return
	}
	// A shot that recorded an edit is NOT a carry source unless a human typed
	// its value (or it predates 012 and has no source, when only a human
	// could have put it there — the guard above gave it no carry). A
	// 'display' value is the setpoint as it stood at POUR START, before the
	// edit; a 'flow' run was measured before it. (A touched shot is never
	// given a carry — see the guard above — so it holds no 'carried' value.)
	// Taking either carried the pre-edit value onto every later shot (review,
	// 2026-11-15). COALESCE: a v1 record has no setpoint_touched at all, and
	// NOT (NULL AND ...) is NULL, which dropped it as a source.
	var t float64
	if tx.QueryRow(`SELECT brew_temp_c FROM shot
	                 WHERE brew_temp_c IS NOT NULL AND started_at <= ?
	                   AND NOT (COALESCE(setpoint_touched, 0) = 1 AND COALESCE(brew_temp_source, '') = 'display')
	                   AND started_at >= ? AND started_at >= `+since+`
	                   AND started_at >= `+sincePending+`
	                 ORDER BY started_at DESC, id DESC LIMIT 1`,
		at, staleFloor(startedAt), at, at).Scan(&t) == nil {
		brewTemp = t
	}
	var pre float64
	if tx.QueryRow(`SELECT preinfusion_s FROM shot
	                 WHERE preinfusion_s IS NOT NULL AND started_at <= ?
	                   AND NOT (COALESCE(setpoint_touched, 0) = 1 AND COALESCE(preinfusion_source, '') = 'flow')
	                   AND started_at >= ? AND started_at >= `+since+`
	                 ORDER BY started_at DESC, id DESC LIMIT 1`,
		at, staleFloor(startedAt), at).Scan(&pre) == nil {
		preinfusion = pre
	}
	return
}

// How far back an inferred machine setting may reach. Without this the carry
// is unbounded: a shot ingested a YEAR after the last recorded value inherits
// it, and an inferred temperature reads like a confirmed one to anything that
// does not check brew_temp_source (012; there was no source column before). That is the
// very distinction migration 009 exists to restore, so leaving the carry
// unbounded would move fabrication from the UI to the server and drop its
// audit trail on the way.
//
// 14 days is long enough to cover a holiday and short enough that a long gap
// degrades to NULL.
//
// BE CLEAR WHAT THIS DOES NOT DO, because a draft claimed it: it does NOT
// bound "time since a human confirmed the value". insertShot writes the
// INFERRED value into the new row, so the next ingest reads an inference as
// its source and the clock measures the gap between consecutive SHOTS. For
// anyone pulling several a day the bound never fires and one confirmation
// propagates indefinitely (marked 'carried' since 012, but still carried). The parameter-edit guard above is the
// real protection; this is only a staleness floor for a long silence.
const recipeMaxAge = 14 * 24 * time.Hour

func staleFloor(startedAt time.Time) string {
	return startedAt.UTC().Add(-recipeMaxAge).Format(time.RFC3339)
}

func insertShot(tx *sql.Tx, rec *record.Record, scaleID any, startedAt time.Time, now string) (int64, bool, error) {
	h, samples := &rec.Header, rec.Samples
	var stoppedBy, excludeReason any
	excluded := 0
	if reason := excludeReasonAtIngest(h); reason != "" {
		excluded = 1
		excludeReason = reason
	}
	if h.Fault() {
		stoppedBy = "fault"
	}
	machineID, grinderID, epochID := defaultContext(tx, startedAt)
	beanID, grindDial, brewTemp, preinfusion := defaultRecipe(tx, epochID, startedAt, h.SetpointTouched())
	brewTempSource, preinfusionSource := sourceIf(brewTemp, "carried"), sourceIf(preinfusion, "carried")
	// A measurement outranks a carry. The setpoint the display showed being
	// dialled (v5) is the machine's own word for this shot's setpoint — and
	// it holds even when this shot recorded an edit, since it is the value
	// as it stood at pour start. The pre-infusion run is measured from this
	// shot's own pulses.
	if c, ok := h.Setpoint(); ok {
		brewTemp, brewTempSource = c, "display"
	} else if h.SetpointPending() {
		// An edit was blinking, unconfirmed, at pour start: the setpoint is
		// unknown — not the last one, which is the pre-edit value.
		brewTemp, brewTempSource = nil, nil
	}
	var preinfusionRun any
	if run, ok := measuredPreinfusion(rec); ok {
		preinfusionRun = run
		if pr, ok := preinfusionSetting(run); ok {
			preinfusion, preinfusionSource = pr, "flow"
		} else {
			// Measured, but no Pr the machine offers: a carried setting
			// beside a run that contradicts it would read as confirmed.
			preinfusion, preinfusionSource = nil, nil
		}
	}
	// 'loaded' only if a bag actually was: an absent bean must stay NULL
	// with no provenance rather than claim an attribution that never happened.
	var beanSource any
	if beanID != nil {
		beanSource = "loaded"
	}

	// Format v2 (0e) carries what the machine's own display said. A v1
	// record predates the display bus, and so does a v2 record written with
	// the bus switched off — both leave these NULL, which is the difference
	// between "no reading" and "zero".
	var boilerTempC, machineTimerS, setpointTouched, setpointPending any
	if c, ok := h.BoilerTempStartC(); ok {
		boilerTempC = c
	}
	if h.MachineTimerDl > 0 {
		machineTimerS = float64(h.MachineTimerDl) / 10.0
	}
	if h.FormatVersion >= 2 {
		// Only meaningful once the firmware could latch it at all.
		setpointTouched = boolToInt(h.SetpointTouched())
	}
	if h.FormatVersion >= 5 {
		setpointPending = boolToInt(h.SetpointPending())
	}
	// Detector 0a.7: something landed on the scale mid-record (kept out of
	// the yield while settling, only flagged during the pour). NULL for an
	// older detector, which cannot say.
	var disturbed any
	if d, known := h.Disturbed(); known {
		disturbed = boolToInt(d)
	}

	// Format v3 (0c) carries the lever press that started the pump. Lever A
	// is the shot lever, so its two directions map onto the schema's
	// switch_direction ('up' = 1Cup, 'down' = 2Cup); a hot-water draw into
	// the cup has no direction the column can hold and stays NULL.
	// first_drip_ms is the schema's "pump-on to first liquid": the press's
	// lead over the detector's t=0, which IS the first drip. NOT on stop_ms's
	// time base — stop_ms counts from first drip — which is why v_shot's
	// pour_s is stop_ms alone from migration 010 on.
	// pump_line keeps the RAW byte for every v3 record (invariant 3), so a
	// hot-water draw and a value no firmware writes stay distinguishable even
	// though both leave switch_direction NULL.
	// Format v4: the pulse train from the press to pour confirmation. NULL
	// total means "not captured", which is not the same fact as 0 pulses;
	// PrePulses() also refuses it unless the record claims a live flow tap,
	// the same gate inlet_pulses has below.
	var prePulseTotal, prePulseComplete any
	preMs, preTotal, preComplete, preOK := rec.PrePulses()
	if preOK {
		prePulseTotal = preTotal
		prePulseComplete = boolToInt(preComplete)
	}

	var switchDirection, firstDripMs, pumpLine any
	if h.FormatVersion >= 3 {
		pumpLine = int(h.PumpLine)
	}
	if line, lead, ok := h.PumpStart(); ok {
		switch line {
		case record.Pump1Cup:
			switchDirection = "up"
		case record.Pump2Cup:
			switchDirection = "down"
		}
		firstDripMs = lead
	}

	r, err := tx.Exec(`
		INSERT INTO shot (
			started_at, tz_offset_min, scale_id,
			machine_id, grinder_id, grind_epoch_id,
			bean_id, bean_source, grind_dial, brew_temp_c, preinfusion_s,
			brew_temp_source, preinfusion_source, preinfusion_run_s,
			stop_ms, stopped_by,
			yield_at_stop_g, yield_final_g, settle_offset_g,
			peak_flow_gps, mean_flow_gps, flow_win_start_ms, flow_win_end_ms,
			boiler_temp_start_c, machine_timer_s, setpoint_touched, setpoint_pending,
			disturbed,
			switch_direction, first_drip_ms, pump_line,
			pre_pulse_total, pre_pulse_complete,
			detector_version, firmware_version,
			excluded, exclude_reason, created_at
		) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`,
		// .UTC() to match every READ site. Equivalent today only because
		// Ingest hands this an already-UTC time; the moment anything passes a
		// zoned one, stored strings gain "+02:00" and MAX(started_at) — which
		// the parameter-edit guard above now depends on — stops being
		// chronological.
		startedAt.UTC().Format(time.RFC3339), h.TzOffsetMin, scaleID,
		machineID, grinderID, epochID,
		beanID, beanSource, grindDial, brewTemp, preinfusion,
		brewTempSource, preinfusionSource, preinfusionRun,
		h.StopMs, stoppedBy,
		mg(h.YieldAtStopMg), mg(h.YieldFinalMg), mg(h.SettleOffsetMg),
		mg(h.PeakFlowMgps), mg(h.MeanFlowMgps), h.FlowWinStartMs, h.FlowWinEndMs,
		boilerTempC, machineTimerS, setpointTouched, setpointPending,
		disturbed,
		switchDirection, firstDripMs, pumpLine,
		prePulseTotal, prePulseComplete,
		h.Detector(), h.Firmware(),
		excluded, excludeReason, now)
	if err != nil {
		return 0, false, err
	}
	shotID, err := r.LastInsertId()
	if err != nil {
		return 0, false, err
	}

	if len(preMs) > 0 {
		pst, err := tx.Prepare(`INSERT INTO shot_pre_pulse (shot_id, n, ms_after_press) VALUES (?,?,?)`)
		if err != nil {
			return 0, false, err
		}
		for n, ms := range preMs {
			if _, err := pst.Exec(shotID, n, ms); err != nil {
				pst.Close()
				return 0, false, err
			}
		}
		pst.Close()
	}

	// inlet_pulses is a placeholder 0 in every record written before the 0d
	// tap was wired — v1 by format, and v2 whenever the tap was simply off —
	// so a stored 0 would be indistinguishable from "no water moved". The
	// header flag is the only thing that separates them, which is why this
	// is keyed on FlowTap() and not on the format version. temp_dc draws the
	// same distinction with INT16_MIN.
	// OR REPLACE: BLE notifications can arrive in a same-millisecond burst,
	// producing two samples with equal t_ms — the later reading wins rather
	// than failing the whole ingest (the raw record keeps both regardless).
	stmt, err := tx.Prepare(`INSERT OR REPLACE INTO shot_sample (shot_id, t_ms, weight_mg, inlet_pulses, temp_dc) VALUES (?,?,?,?,?)`)
	if err != nil {
		return 0, false, err
	}
	defer stmt.Close()
	for _, smp := range samples {
		var temp any
		if smp.TempDc != record.TempNone {
			temp = smp.TempDc
		}
		var pulses any
		// Version-guarded for the same reason setpoint_touched is above: bit 4
		// only means anything from v2 onward, and a v1 record presenting it —
		// from a flipped bit in flash, a padded upload, or a hand-built
		// record — would otherwise turn 220 placeholder zeros into a
		// measurement reading "no water moved". That is the exact confusion
		// this flag exists to prevent, so it must not be the way in.
		if h.FormatVersion >= 2 && h.FlowTap() {
			pulses = smp.InletPulses
		}
		if _, err := stmt.Exec(shotID, smp.TMs, smp.WeightMg, pulses, temp); err != nil {
			return 0, false, err
		}
	}
	return shotID, excluded == 1, nil
}

func insertWeighing(tx *sql.Tx, h *record.Header, scaleID any, observedAt time.Time) (int64, error) {
	r, err := tx.Exec(`INSERT INTO weighing (scale_id, observed_at, grams, stable_ms)
		VALUES (?,?,?,?)`,
		scaleID, observedAt.Format(time.RFC3339), mg(h.GramsMg), h.StableMs)
	if err != nil {
		return 0, err
	}
	return r.LastInsertId()
}

func mg(v int32) float64 { return float64(v) / 1000.0 }

// --- retroactive dose attribution --------------------------------------------
// The UI never declares intent before a weighing; instead, when a shot
// closes, the last unattributed dose-plausible weighing before it becomes
// its dose. Earlier candidates in the window stay as superseded rows — when
// a dose looks wrong you want to see what the scale actually saw. The
// 12–24 g plausibility window lives here, not in the firmware.

const (
	doseWindow = 4 * time.Hour // a dose ground longer ago than this is stale
	doseMinG   = 12.0
	doseMaxG   = 24.0
)

// excludeReasonAtIngest is the one place a record's ingest-time exclusion
// is decided ("" = not excluded). insertShot stores it and reports it, and
// the arrival path's dose attribution reads that report rather than
// re-deriving it. First match wins, so a faulted placement is a fault.
func excludeReasonAtIngest(h *record.Header) string {
	switch {
	case h.Fault():
		return reasonFault
	case h.Reject():
		return reasonReject
	case h.Placement():
		return reasonPlacement
	}
	return ""
}

// The exclude_reason values written at ingest. attributeWeighingToLaterShot
// matches stored rows against them, so CHANGING ONE STRANDS EVERY ROW
// ALREADY STORED WITH THE OLD TEXT — add a migration if one ever changes.
const (
	reasonFault     = "fault: cup removed mid-pour"
	reasonReject    = "reject: under 5 g"
	reasonPlacement = "cup placement: the detector judged it a cup set down, not a pour"
)

func attributeDoseToShot(tx *sql.Tx, shotID int64, startedAt time.Time) error {
	started := startedAt.UTC().Format(time.RFC3339)
	cutoff := startedAt.Add(-doseWindow).UTC().Format(time.RFC3339)

	var wID int64
	var grams float64
	var observed string
	err := tx.QueryRow(`
		SELECT id, grams, observed_at FROM weighing
		WHERE shot_id IS NULL AND superseded = 0
		  AND grams BETWEEN ? AND ?
		  AND observed_at <= ? AND observed_at > ?
		ORDER BY observed_at DESC LIMIT 1`,
		doseMinG, doseMaxG, started, cutoff).Scan(&wID, &grams, &observed)
	if err == sql.ErrNoRows {
		return nil
	}
	if err != nil {
		return err
	}
	return applyAttribution(tx, shotID, wID, grams, observed, cutoff)
}

// Out-of-order arrival: a weighing whose shot was ingested first (upload
// retries can reorder) claims the first still-doseless shot after it.
// It skips the records the arrival path skips — those excluded AT INGEST
// (fault, reject, placement), keyed on their stored reason — and nothing
// else: a shot the user excluded by hand (a prep error, "discarded at
// capture") still used the coffee and can take its dose, and a false reject
// the user un-excludes becomes eligible (review 3, 2026-11-15: keyed on the
// editable `excluded` alone, the two paths disagreed after any edit).
// KNOWN, older than this: with reordered uploads it takes the earliest
// weighing to ARRIVE, not the last one before the shot.
func attributeWeighingToLaterShot(tx *sql.Tx, weighingID int64, grams float64, observedAt time.Time) error {
	if grams < doseMinG || grams > doseMaxG {
		return nil
	}
	observed := observedAt.UTC().Format(time.RFC3339)
	horizon := observedAt.Add(doseWindow).UTC().Format(time.RFC3339)

	var shotID int64
	err := tx.QueryRow(`
		SELECT id FROM shot
		WHERE dose_ground_g IS NULL
		  AND NOT (excluded = 1 AND COALESCE(exclude_reason, '') IN (?, ?, ?))
		  AND started_at >= ? AND started_at < ?
		ORDER BY started_at ASC LIMIT 1`,
		reasonFault, reasonReject, reasonPlacement, observed, horizon).Scan(&shotID)
	if err == sql.ErrNoRows {
		return nil
	}
	if err != nil {
		return err
	}
	cutoff := observedAt.Add(-doseWindow).UTC().Format(time.RFC3339)
	return applyAttribution(tx, shotID, weighingID, grams, observed, cutoff)
}

func applyAttribution(tx *sql.Tx, shotID, weighingID int64, grams float64, observed, cutoff string) error {
	if _, err := tx.Exec(`UPDATE weighing SET shot_id=?, role='dose_ground', attributed_by='auto' WHERE id=?`,
		shotID, weighingID); err != nil {
		return err
	}
	// Earlier unclaimed candidates in the window were re-placings — the last
	// one won. Keep them, marked.
	if _, err := tx.Exec(`
		UPDATE weighing SET superseded = 1
		WHERE shot_id IS NULL AND superseded = 0
		  AND grams BETWEEN ? AND ?
		  AND observed_at < ? AND observed_at > ?`,
		doseMinG, doseMaxG, observed, cutoff); err != nil {
		return err
	}
	// Never clobber a dose the user already entered by hand.
	_, err := tx.Exec(`UPDATE shot SET dose_ground_g=?, dose_source='measured' WHERE id=? AND dose_ground_g IS NULL`,
		grams, shotID)
	return err
}

// --- capture writes ---------------------------------------------------------
// User-entered fields arrive as JSON objects; only whitelisted columns pass.
// Measured fields (yield, flow, timings) are never writable this way — the
// device is the authority on those.

var shotUserFields = map[string]bool{
	"bean_id": true, "grind_dial": true, "dose_ground_g": true, "dose_in_g": true,
	"preinfusion_s": true, "brew_temp_c": true, "target_yield_g": true, "target_time_s": true,
	"prep_deviation": true, "notes": true, "excluded": true, "exclude_reason": true,
	"from_frozen": true, "purge_g": true,
}

var tastingFields = map[string]bool{
	"overall": true, "balance": true, "acidity": true, "sweetness": true,
	"bitterness": true, "body": true, "defects": true, "descriptors": true,
	"notes": true, "drink": true,
}

var beanFields = map[string]bool{
	"roaster": true, "name": true, "origin": true, "region": true, "producer": true,
	"varietal": true, "altitude": true, "process": true, "roast_level": true, "roast_date": true,
	"bag_size_g": true, "price_cents": true, "currency": true, "opened_at": true,
	"frozen_at": true, "dose_count": true, "portion_target_g": true,
	"finished_at": true, "url": true, "notes": true,
}

func filterFields(fields map[string]any, allowed map[string]bool) (cols []string, vals []any) {
	for k, v := range fields {
		if allowed[k] {
			cols = append(cols, k)
			vals = append(vals, v)
		}
	}
	return
}

// ErrNotFound lets a handler distinguish "that row does not exist" from a
// database failure, so a broken DB cannot masquerade as a 404.
var ErrNotFound = errors.New("no such row")

// LoadBean records which bag is in the hopper. Ingest reads this to attribute
// every subsequent shot, so it is the one tap that replaces tagging every
// shot individually — and it is pressed when you open a bag, not while
// holding a portafilter.
//
// Loading is a timestamp rather than a boolean so the newest wins and the
// history of which bag was loaded when survives. Nothing is ever unloaded;
// a bag stops being current when another is loaded.
func (s *Store) LoadBean(id int64) error {
	res, err := s.db.Exec(`UPDATE bean SET loaded_at = ? WHERE id = ?`,
		time.Now().UTC().Format(time.RFC3339), id)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return fmt.Errorf("%w: %d", ErrNotFound, id)
	}
	return nil
}

// UpdateShot patches user-entered columns on one shot. A manual dose entry
// also stamps dose_source, and naming a bean stamps bean_source, so an
// analysis can always tell a human's value from an inferred one.
func (s *Store) UpdateShot(id int64, fields map[string]any) error {
	cols, vals := filterFields(fields, shotUserFields)
	if _, ok := fields["dose_ground_g"]; ok {
		cols = append(cols, "dose_source")
		vals = append(vals, "manual")
	}
	// A human naming the bean outranks whatever was inferred at ingest, and
	// the provenance has to move with it — otherwise a corrected shot still
	// looks guessed, and an analysis filtering on 'user' would discard the
	// one value it should trust most.
	if v, ok := fields["bean_id"]; ok {
		cols = append(cols, "bean_source")
		if v == nil {
			// Clearing the bean clears its provenance with it. Marking a
			// NULL as 'user' would claim someone confirmed there was no
			// bean, which is not what an empty field means.
			vals = append(vals, nil)
		} else {
			vals = append(vals, "user")
		}
	}
	// A typed brew temperature or pre-infusion is the user's word, and so is
	// clearing one: 'user' with a NULL value is a deliberate blank, which
	// the pre-infusion backfill must not refill on the next restart (review,
	// 2026-11-15 — it did, when a clear also cleared the source).
	for field, src := range map[string]string{"brew_temp_c": "brew_temp_source", "preinfusion_s": "preinfusion_source"} {
		if _, ok := fields[field]; ok {
			cols = append(cols, src)
			vals = append(vals, "user")
		}
	}
	if len(cols) == 0 {
		return fmt.Errorf("no updatable fields in request")
	}
	set := ""
	for i, c := range cols {
		if i > 0 {
			set += ", "
		}
		set += c + " = ?"
	}
	vals = append(vals, time.Now().UTC().Format(time.RFC3339), id)
	res, err := s.db.Exec("UPDATE shot SET "+set+", updated_at = ? WHERE id = ?", vals...)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return sql.ErrNoRows
	}
	return nil
}

// InsertTasting adds a tasting row for a shot. Separate row on purpose: a
// missing tasting is visibly different from a bad shot.
func (s *Store) InsertTasting(shotID int64, fields map[string]any) (int64, error) {
	cols, vals := filterFields(fields, tastingFields)
	q := "INSERT INTO tasting (shot_id, tasted_at"
	placeholders := "?, ?"
	args := []any{shotID, time.Now().UTC().Format(time.RFC3339)}
	for i, c := range cols {
		q += ", " + c
		placeholders += ", ?"
		args = append(args, vals[i])
	}
	r, err := s.db.Exec(q+") VALUES ("+placeholders+")", args...)
	if err != nil {
		return 0, err
	}
	return r.LastInsertId()
}

func (s *Store) Beans() ([]map[string]any, error) {
	return s.queryJSON(`SELECT * FROM bean ORDER BY finished_at IS NOT NULL, roast_date DESC, id DESC`)
}

func (s *Store) InsertBean(fields map[string]any) (int64, error) {
	cols, vals := filterFields(fields, beanFields)
	if fields["roaster"] == nil || fields["name"] == nil {
		return 0, fmt.Errorf("roaster and name are required")
	}
	q := "INSERT INTO bean ("
	placeholders := ""
	for i, c := range cols {
		if i > 0 {
			q += ", "
			placeholders += ", "
		}
		q += c
		placeholders += "?"
	}
	r, err := s.db.Exec(q+") VALUES ("+placeholders+")", vals...)
	if err != nil {
		return 0, err
	}
	return r.LastInsertId()
}

// DeleteShot removes a shot with its samples and tastings (schema cascades).
// The raw device record in ingest_record survives with shot_id nulled —
// deleting an interpretation never deletes the measurement.
func (s *Store) DeleteShot(id int64) error {
	res, err := s.db.Exec(`DELETE FROM shot WHERE id = ?`, id)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return sql.ErrNoRows
	}
	return nil
}

// UpdateBean patches whitelisted bean columns (bean has no updated_at).
func (s *Store) UpdateBean(id int64, fields map[string]any) error {
	cols, vals := filterFields(fields, beanFields)
	if len(cols) == 0 {
		return fmt.Errorf("no updatable fields in request")
	}
	set := ""
	for i, c := range cols {
		if i > 0 {
			set += ", "
		}
		set += c + " = ?"
	}
	vals = append(vals, id)
	res, err := s.db.Exec("UPDATE bean SET "+set+" WHERE id = ?", vals...)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return sql.ErrNoRows
	}
	return nil
}

// --- read side -------------------------------------------------------------

// Shots returns recent rows from v_shot as generic JSON objects — the view
// is the single definition of derived values, so the API adds nothing to it.
func (s *Store) Shots(limit int) ([]map[string]any, error) {
	return s.queryJSON(`SELECT * FROM v_shot ORDER BY started_at DESC LIMIT ?`, limit)
}

func (s *Store) Shot(id int64) (map[string]any, []map[string]any, error) {
	rows, err := s.queryJSON(`SELECT * FROM v_shot WHERE id = ?`, id)
	if err != nil || len(rows) == 0 {
		return nil, nil, err
	}
	samples, err := s.queryJSON(`SELECT t_ms, weight_mg, inlet_pulses, temp_dc FROM shot_sample WHERE shot_id = ? ORDER BY t_ms`, id)
	if err != nil {
		return nil, nil, err
	}
	return rows[0], samples, nil
}

func (s *Store) queryJSON(query string, args ...any) ([]map[string]any, error) {
	rows, err := s.db.Query(query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	cols, err := rows.Columns()
	if err != nil {
		return nil, err
	}
	var out []map[string]any
	for rows.Next() {
		vals := make([]any, len(cols))
		ptrs := make([]any, len(cols))
		for i := range vals {
			ptrs[i] = &vals[i]
		}
		if err := rows.Scan(ptrs...); err != nil {
			return nil, err
		}
		m := make(map[string]any, len(cols))
		for i, c := range cols {
			if b, ok := vals[i].([]byte); ok {
				m[c] = string(b)
			} else {
				m[c] = vals[i]
			}
		}
		out = append(out, m)
	}
	return out, rows.Err()
}

// AmbientPending is one shot still waiting for its room reading.
type AmbientPending struct {
	ID        int64
	StartedAt time.Time
	// ClockValid is false when the board had no clock time for the shot, so
	// StartedAt is the upload time (Ingest) and says nothing about the pour.
	// (Ingest also uses the upload time for time_valid = 1 with a zero
	// timestamp; the firmware never writes that, and it is not caught here.)
	ClockValid bool
	// BadTime: started_at does not parse; StartedAt is zero. Reported, not an
	// error, so one bad row cannot stop every other lookup.
	BadTime bool
}

// ShotsNeedingAmbient returns up to limit shots that have not been looked up
// in Home Assistant yet (ambient_source IS NULL), newest first so a shot
// just pulled is filled before the archive backfill. A row whose started_at
// does not parse comes back with BadTime set, never as an error: one bad row
// must not stop every other lookup (review 2026-11-22).
func (s *Store) ShotsNeedingAmbient(limit int) ([]AmbientPending, error) {
	rows, err := s.db.Query(`SELECT id, started_at,
	                                NOT EXISTS (SELECT 1 FROM ingest_record ir
	                                             WHERE ir.shot_id = shot.id AND ir.time_valid = 0)
	                           FROM shot
	                          WHERE ambient_source IS NULL
	                          ORDER BY started_at DESC, id DESC LIMIT ?`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []AmbientPending
	for rows.Next() {
		var p AmbientPending
		var ts string
		if err := rows.Scan(&p.ID, &ts, &p.ClockValid); err != nil {
			return nil, err
		}
		if p.StartedAt, err = time.Parse(time.RFC3339, ts); err != nil {
			p.StartedAt, p.BadTime = time.Time{}, true
		}
		out = append(out, p)
	}
	return out, rows.Err()
}

// SetAmbient stores a room reading and where it came from. Only a row not
// looked up yet is written, so a concurrent or repeated lookup cannot
// overwrite an earlier answer.
func (s *Store) SetAmbient(id int64, tempC, rh *float64, source string) error {
	_, err := s.db.Exec(`UPDATE shot SET ambient_c = ?, ambient_rh = ?, ambient_source = ?
	                     WHERE id = ? AND ambient_source IS NULL`,
		nullFloat(tempC), nullFloat(rh), source, id)
	return err
}

func nullFloat(v *float64) any {
	if v == nil {
		return nil
	}
	return *v
}

// Healthy pings the database.
func (s *Store) Healthy() error {
	var one int
	return s.db.QueryRow("SELECT 1").Scan(&one)
}

// MarshalJSONRows is a helper for handlers.
func MarshalJSONRows(rows []map[string]any) ([]byte, error) {
	if rows == nil {
		rows = []map[string]any{}
	}
	return json.Marshal(rows)
}

func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

// sourceIf is the provenance for a value: src when there is one, NULL with it.
func sourceIf(v any, src string) any {
	if v == nil {
		return nil
	}
	return src
}

// measuredPreinfusion measures the pre-infusion run from a shot record's own
// pre-drip pulses, for a lever-A shot only (a hot-water draw has no
// pre-infusion). See preinfusion.go.
func measuredPreinfusion(rec *record.Record) (float64, bool) {
	line, lead, ok := rec.Header.PumpStart()
	if !ok || (line != record.Pump1Cup && line != record.Pump2Cup) {
		return 0, false
	}
	ms, total, complete, ok := rec.PrePulses()
	if !ok {
		return 0, false
	}
	return measurePreinfusion(ms, total, complete, int64(lead))
}

// backfillPreinfusion (re-)measures pre-infusion for every lever-A shot
// with a pulse train, at every Open: preinfusion_run_s always, so a change
// to the thresholds re-measures the archive, and preinfusion_s where it is
// the server's to set — empty with no source (0.11.0's v4 shots, ingested
// before 012), 'carried', or 'flow' itself. NEVER 'user' (typed, or
// deliberately cleared) and never a pre-012 value with no source, which a
// human may have typed. A 'flow' row the new thresholds cannot measure loses
// its value, and so does a 'flow' or 'carried' row whose measured run maps
// to no Pr the machine offers. Returns how many rows changed; a no-op once settled.
func (s *Store) backfillPreinfusion() (int, error) {
	rows, err := s.db.Query(`
		SELECT id, pre_pulse_total, pre_pulse_complete, first_drip_ms,
		       preinfusion_s, preinfusion_source, preinfusion_run_s FROM shot
		WHERE pre_pulse_total IS NOT NULL AND pump_line IN (1, 2)
		  AND first_drip_ms IS NOT NULL`)
	if err != nil {
		return 0, err
	}
	type cand struct {
		id, total, drip int64
		complete        bool
		pre, run        sql.NullFloat64
		src             sql.NullString
	}
	var cands []cand
	for rows.Next() {
		var c cand
		var complete sql.NullInt64
		if err := rows.Scan(&c.id, &c.total, &complete, &c.drip, &c.pre, &c.src, &c.run); err != nil {
			rows.Close()
			return 0, err
		}
		c.complete = complete.Valid && complete.Int64 == 1
		cands = append(cands, c)
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return 0, err
	}
	n := 0
	for _, c := range cands {
		var ms []uint16
		pr, err := s.db.Query(`SELECT ms_after_press FROM shot_pre_pulse WHERE shot_id = ? ORDER BY n`, c.id)
		if err != nil {
			return n, err
		}
		for pr.Next() {
			var v int64
			if err := pr.Scan(&v); err != nil {
				pr.Close()
				return n, err
			}
			ms = append(ms, uint16(v))
		}
		pr.Close()
		run, measured := measurePreinfusion(ms, int(c.total), c.complete, c.drip)
		newRun := sql.NullFloat64{Float64: run, Valid: measured}
		newPre, newSrc := c.pre, c.src
		ours := c.src.String == "flow" || c.src.String == "carried" || (!c.src.Valid && !c.pre.Valid)
		if ours {
			if setting, ok := preinfusionSetting(run); measured && ok {
				newPre = sql.NullFloat64{Float64: setting, Valid: true}
				newSrc = sql.NullString{String: "flow", Valid: true}
			} else if c.src.String == "flow" || measured {
				// A 'flow' value the thresholds no longer support, or a
				// measured run no Pr matches beside a carried setting.
				newPre, newSrc = sql.NullFloat64{}, sql.NullString{}
			}
		}
		if newRun == c.run && newPre == c.pre && newSrc == c.src {
			continue
		}
		if _, err := s.db.Exec(`UPDATE shot SET preinfusion_run_s = ?, preinfusion_s = ?, preinfusion_source = ? WHERE id = ?`,
			newRun, newPre, newSrc, c.id); err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}
