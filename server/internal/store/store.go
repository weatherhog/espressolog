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
	case record.TypeWeighing:
		res.WeighingID, err = insertWeighing(tx, h, scaleID, startedAt)
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

func insertShot(tx *sql.Tx, h *record.Header, samples []record.Sample, scaleID any, startedAt time.Time, now string) (int64, error) {
	var stoppedBy, excludeReason any
	excluded := 0
	if h.Fault() {
		stoppedBy = "fault"
		excluded = 1
		excludeReason = "fault: cup removed mid-pour"
	}

	r, err := tx.Exec(`
		INSERT INTO shot (
			started_at, tz_offset_min, scale_id,
			stop_ms, stopped_by,
			yield_at_stop_g, yield_final_g, settle_offset_g,
			peak_flow_gps, mean_flow_gps, flow_win_start_ms, flow_win_end_ms,
			detector_version, firmware_version,
			excluded, exclude_reason, created_at
		) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`,
		startedAt.Format(time.RFC3339), h.TzOffsetMin, scaleID,
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
