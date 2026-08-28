// Package store owns the SQLite file: migrations, ingest writes, and the
// read queries the API serves. modernc.org/sqlite keeps the binary CGO-free.
package store

import (
	"database/sql"
	"embed"
	"encoding/json"
	"fmt"
	"io/fs"
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
		res.ShotID, err = insertShot(tx, h, r.Samples, scaleID, startedAt, now)
		if err == nil {
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
func defaultContext(tx *sql.Tx) (machineID, grinderID, epochID any) {
	var id int64
	if tx.QueryRow(`SELECT id FROM equipment WHERE kind='machine' ORDER BY id LIMIT 1`).Scan(&id) == nil {
		machineID = id
	}
	if tx.QueryRow(`SELECT id FROM equipment WHERE kind='grinder' ORDER BY id LIMIT 1`).Scan(&id) == nil {
		grinderID = id
		if tx.QueryRow(`SELECT id FROM grind_epoch WHERE grinder_id=? ORDER BY started_at DESC, id DESC LIMIT 1`, id).Scan(&id) == nil {
			epochID = id
		}
	}
	return
}

func insertShot(tx *sql.Tx, h *record.Header, samples []record.Sample, scaleID any, startedAt time.Time, now string) (int64, error) {
	var stoppedBy, excludeReason any
	excluded := 0
	if h.Fault() {
		stoppedBy = "fault"
		excluded = 1
		excludeReason = "fault: cup removed mid-pour"
	}
	machineID, grinderID, epochID := defaultContext(tx)

	r, err := tx.Exec(`
		INSERT INTO shot (
			started_at, tz_offset_min, scale_id,
			machine_id, grinder_id, grind_epoch_id,
			stop_ms, stopped_by,
			yield_at_stop_g, yield_final_g, settle_offset_g,
			peak_flow_gps, mean_flow_gps, flow_win_start_ms, flow_win_end_ms,
			detector_version, firmware_version,
			excluded, exclude_reason, created_at
		) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`,
		startedAt.Format(time.RFC3339), h.TzOffsetMin, scaleID,
		machineID, grinderID, epochID,
		h.StopMs, stoppedBy,
		mg(h.YieldAtStopMg), mg(h.YieldFinalMg), mg(h.SettleOffsetMg),
		mg(h.PeakFlowMgps), mg(h.MeanFlowMgps), h.FlowWinStartMs, h.FlowWinEndMs,
		h.Detector(), h.Firmware(),
		excluded, excludeReason, now)
	if err != nil {
		return 0, err
	}
	shotID, err := r.LastInsertId()
	if err != nil {
		return 0, err
	}

	// Format v1 predates the flowmeter (0d): its inlet_pulses bytes are a
	// placeholder 0, not a measurement, so they land as NULL. temp_dc
	// INT16_MIN likewise means "no reading".
	stmt, err := tx.Prepare(`INSERT INTO shot_sample (shot_id, t_ms, weight_mg, inlet_pulses, temp_dc) VALUES (?,?,?,NULL,?)`)
	if err != nil {
		return 0, err
	}
	defer stmt.Close()
	for _, smp := range samples {
		var temp any
		if smp.TempDc != record.TempNone {
			temp = smp.TempDc
		}
		if _, err := stmt.Exec(shotID, smp.TMs, smp.WeightMg, temp); err != nil {
			return 0, err
		}
	}
	return shotID, nil
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
		  AND started_at >= ? AND started_at < ?
		ORDER BY started_at ASC LIMIT 1`,
		observed, horizon).Scan(&shotID)
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

// UpdateShot patches user-entered columns on one shot. A manual dose entry
// also stamps dose_source so analysis can tell it from a weighed one.
func (s *Store) UpdateShot(id int64, fields map[string]any) error {
	cols, vals := filterFields(fields, shotUserFields)
	if _, ok := fields["dose_ground_g"]; ok {
		cols = append(cols, "dose_source")
		vals = append(vals, "manual")
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
