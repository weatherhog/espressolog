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

	// The scale self-registered from its BLE MAC.
	rows, err := st.queryJSON(`SELECT kind, ble_address FROM equipment`)
	if err != nil {
		t.Fatal(err)
	}
	if len(rows) != 1 || rows[0]["ble_address"] != "aa:bb:cc:dd:ee:01" {
		t.Errorf("equipment = %+v", rows)
	}

	// Same scale again must not create a second equipment row.
	rec.Header.Seq++ // pretend it's the next record
	if _, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, now); err != nil {
		t.Fatal(err)
	}
	rows, _ = st.queryJSON(`SELECT COUNT(*) AS n FROM equipment`)
	if rows[0]["n"].(int64) != 1 {
		t.Errorf("scale registered twice")
	}
}
