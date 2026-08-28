package store

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	espressolog "espressolog"
	"espressolog/internal/record"
)

func openTestStore(t *testing.T) *Store {
	t.Helper()
	st, err := Open(filepath.Join(t.TempDir(), "test.db"), espressolog.Migrations)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { st.Close() })
	return st
}

// weighingRecord builds a type-2 record the way the firmware would emit it.
func weighingRecord(seq uint32, gramsMg int32, at time.Time) *record.Record {
	return &record.Record{Header: record.Header{
		Magic: record.Magic, FormatVersion: record.FormatVersion,
		RecordType: record.TypeWeighing, BootID: 0x1111, Seq: seq,
		StartedAtUnixMs: uint64(at.UnixMilli()), TimeValid: 1,
		ScaleRole: 1, GramsMg: gramsMg, StableMs: 3000,
	}}
}

func TestDoseAttribution(t *testing.T) {
	raw, err := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")
	if err != nil {
		t.Fatal(err)
	}
	shotRec, err := record.Decode(raw)
	if err != nil {
		t.Fatal(err)
	}
	shotAt := time.UnixMilli(int64(shotRec.Header.StartedAtUnixMs))

	st := openTestStore(t)
	dev := "aa:bb:cc:dd:ee:03"

	// Two dose-plausible weighings (re-placed cup) and one implausible one
	// (the 36 g yield cup) before the shot.
	if _, err := st.Ingest(dev, weighingRecord(1, 18000, shotAt.Add(-3*time.Minute)), []byte{2}, shotAt); err != nil {
		t.Fatal(err)
	}
	if _, err := st.Ingest(dev, weighingRecord(2, 18100, shotAt.Add(-2*time.Minute)), []byte{2}, shotAt); err != nil {
		t.Fatal(err)
	}
	if _, err := st.Ingest(dev, weighingRecord(3, 36500, shotAt.Add(-1*time.Minute)), []byte{2}, shotAt); err != nil {
		t.Fatal(err)
	}

	res, err := st.Ingest(dev, shotRec, raw, shotAt.Add(30*time.Second))
	if err != nil {
		t.Fatal(err)
	}

	shot, _, err := st.Shot(res.ShotID)
	if err != nil {
		t.Fatal(err)
	}
	// The LAST plausible weighing wins; the 36.5 g one is out of window.
	if got := shot["dose_ground_g"]; got != 18.1 {
		t.Errorf("dose_ground_g = %v, want 18.1", got)
	}
	if got := shot["dose_source"]; got != "measured" {
		t.Errorf("dose_source = %v", got)
	}
	rows, _ := st.queryJSON(`SELECT id, shot_id, superseded, attributed_by FROM weighing ORDER BY id`)
	if rows[0]["superseded"].(int64) != 1 || rows[0]["shot_id"] != nil {
		t.Errorf("earlier weighing not superseded: %+v", rows[0])
	}
	if rows[1]["shot_id"] == nil || rows[1]["attributed_by"] != "auto" {
		t.Errorf("winning weighing not attributed: %+v", rows[1])
	}
	if rows[2]["shot_id"] != nil || rows[2]["superseded"].(int64) != 0 {
		t.Errorf("implausible weighing touched: %+v", rows[2])
	}
}

func TestDoseAttributionOutOfOrder(t *testing.T) {
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")
	shotRec, _ := record.Decode(raw)
	shotAt := time.UnixMilli(int64(shotRec.Header.StartedAtUnixMs))

	st := openTestStore(t)
	dev := "aa:bb:cc:dd:ee:03"

	// Shot arrives first (upload reorder), then its weighing.
	res, err := st.Ingest(dev, shotRec, raw, shotAt.Add(30*time.Second))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := st.Ingest(dev, weighingRecord(9, 17800, shotAt.Add(-2*time.Minute)), []byte{2}, shotAt); err != nil {
		t.Fatal(err)
	}
	shot, _, _ := st.Shot(res.ShotID)
	if got := shot["dose_ground_g"]; got != 17.8 {
		t.Errorf("late weighing not attributed: dose = %v", got)
	}
}

// BLE bursts can stamp two samples with the same millisecond — the ingest
// must survive that (last reading wins), not 500 the whole record.
func TestIngestDuplicateSampleTimestamps(t *testing.T) {
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")
	rec, _ := record.Decode(raw)
	rec.Samples[10].TMs = rec.Samples[9].TMs // same-ms burst
	st := openTestStore(t)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatalf("duplicate t_ms failed the ingest: %v", err)
	}
	_, samples, _ := st.Shot(res.ShotID)
	if len(samples) != len(rec.Samples)-1 {
		t.Errorf("samples = %d, want %d (one collapsed)", len(samples), len(rec.Samples)-1)
	}
}

func TestIngestRealRecord(t *testing.T) {
	raw, err := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")
	if err != nil {
		t.Fatal(err)
	}
	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatal(err)
	}

	st := openTestStore(t)
	now := time.Now()

	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, now)
	if err != nil {
		t.Fatal(err)
	}
	if res.Duplicate || res.ShotID == 0 {
		t.Fatalf("first ingest: %+v", res)
	}

	// Retry of a lost 200 must be a clean duplicate, not an error or a row.
	res2, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, now)
	if err != nil {
		t.Fatal(err)
	}
	if !res2.Duplicate {
		t.Fatalf("second ingest not deduped: %+v", res2)
	}

	shot, samples, err := st.Shot(res.ShotID)
	if err != nil {
		t.Fatal(err)
	}
	if shot == nil {
		t.Fatal("shot not readable via v_shot")
	}
	if got := shot["yield_final_g"].(float64); got != 36.8 {
		t.Errorf("yield_final_g = %v", got)
	}
	// started_at must come from the device's synced clock, not receipt time.
	if got := shot["started_at"].(string); got[:4] != "2026" {
		t.Errorf("started_at = %v", got)
	}
	if len(samples) != 220 {
		t.Errorf("samples = %d", len(samples))
	}
	// v1 records: no flowmeter, no temp — must land as NULL, not zero.
	if samples[0]["inlet_pulses"] != nil || samples[0]["temp_dc"] != nil {
		t.Errorf("placeholders not NULLed: %+v", samples[0])
	}

	// The scale self-registered from its BLE MAC (alongside the machine and
	// grinder that migration 003 seeds).
	rows, err := st.queryJSON(`SELECT kind, ble_address FROM equipment WHERE kind='scale'`)
	if err != nil {
		t.Fatal(err)
	}
	if len(rows) != 1 || rows[0]["ble_address"] != "aa:bb:cc:dd:ee:01" {
		t.Errorf("scale equipment = %+v", rows)
	}

	// Ingested shots pick up the seeded machine/grinder/epoch defaults.
	if shot["machine_id"] == nil || shot["grinder_id"] == nil || shot["grind_epoch_id"] == nil {
		t.Errorf("default context not attached: machine=%v grinder=%v epoch=%v",
			shot["machine_id"], shot["grinder_id"], shot["grind_epoch_id"])
	}

	// Same scale again must not create a second equipment row.
	rec.Header.Seq++ // pretend it's the next record
	if _, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, now); err != nil {
		t.Fatal(err)
	}
	rows, _ = st.queryJSON(`SELECT COUNT(*) AS n FROM equipment WHERE kind='scale'`)
	if rows[0]["n"].(int64) != 1 {
		t.Errorf("scale registered twice")
	}
}
