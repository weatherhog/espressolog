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

// loadShotRecord returns the golden shot record and the time it started.
func loadShotRecord(t *testing.T) (*record.Record, time.Time) {
	t.Helper()
	raw, err := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")
	if err != nil {
		t.Fatal(err)
	}
	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatal(err)
	}
	return rec, time.UnixMilli(int64(rec.Header.StartedAtUnixMs))
}

// currentEpoch is the epoch defaultContext will pick. Derived rather than
// assumed: migration 003 seeds the initial epoch with datetime('now'), which
// is later than the golden record's timestamps, so "insert a newer epoch"
// has to mean newer than THAT, not newer than the shot.
func currentEpoch(t *testing.T, st *Store) int64 {
	t.Helper()
	var id int64
	// Scoped to the first grinder by id, mirroring defaultContext. Equivalent
	// today because migration 003 seeds exactly one grinder, but a helper
	// that derives from a different query than production would drift
	// silently the moment a second one appeared.
	if err := st.db.QueryRow(`SELECT e.id FROM grind_epoch e
	                           WHERE e.grinder_id = (SELECT id FROM equipment
	                                                  WHERE kind='grinder' ORDER BY id LIMIT 1)
	                           ORDER BY e.started_at DESC, e.id DESC LIMIT 1`).Scan(&id); err != nil {
		t.Fatal(err)
	}
	return id
}

// The point of the loaded bag: a shot pulled and simply left alone must
// still know its recipe. Before this, bean and grind arrived only if a human
// opened the Capture screen, which made an untagged shot useless for
// analysis even though the system already knew what was in the hopper.
func TestIngestAttributesLoadedBean(t *testing.T) {
	st := openTestStore(t)
	dev := "aa:bb:cc:dd:ee:03"
	rec, at := loadShotRecord(t)
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")

	var beanID int64
	if err := st.db.QueryRow(
		`INSERT INTO bean (roaster, name) VALUES ('R','B') RETURNING id`).Scan(&beanID); err != nil {
		t.Fatal(err)
	}
	if err := st.LoadBean(beanID); err != nil {
		t.Fatal(err)
	}
	// LoadBean stamps now; the golden shot was pulled in August. Backdate so
	// the bag was actually in the hopper when the shot happened — the recipe
	// is bounded by the shot's own timestamp.
	if _, err := st.db.Exec(`UPDATE bean SET loaded_at=? WHERE id=?`,
		at.Add(-24*time.Hour).UTC().Format(time.RFC3339), beanID); err != nil {
		t.Fatal(err)
	}
	// A grind only exists if an earlier shot in THIS epoch carried one.
	if _, err := st.db.Exec(
		`INSERT INTO shot (started_at, grind_dial, grind_epoch_id, created_at) VALUES (?,7.9,?,?)`,
		at.Add(-time.Hour).UTC().Format(time.RFC3339), currentEpoch(t, st),
		at.UTC().Format(time.RFC3339)); err != nil {
		t.Fatal(err)
	}

	res, err := st.Ingest(dev, rec, raw, at)
	if err != nil {
		t.Fatal(err)
	}

	var gotBean int64
	var gotSource string
	var gotGrind float64
	if err := st.db.QueryRow(
		`SELECT bean_id, bean_source, grind_dial FROM shot WHERE id=?`, res.ShotID).
		Scan(&gotBean, &gotSource, &gotGrind); err != nil {
		t.Fatal(err)
	}
	if gotBean != beanID {
		t.Errorf("bean_id = %d, want %d (the loaded bag)", gotBean, beanID)
	}
	if gotSource != "loaded" {
		t.Errorf("bean_source = %q, want \"loaded\"", gotSource)
	}
	if gotGrind != 7.9 {
		t.Errorf("grind_dial = %v, want 7.9 carried forward", gotGrind)
	}
}

// With no bag loaded the columns must stay NULL. Inventing a bean would be
// worse than leaving it blank: an analysis cannot tell a fabricated value
// from a real one, and bean_source would be claiming an attribution that
// never happened.
func TestIngestWithNoLoadedBeanLeavesNull(t *testing.T) {
	st := openTestStore(t)
	rec, at := loadShotRecord(t)
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")

	// A bean that exists but was never loaded. Without this the test passes
	// even if the WHERE loaded_at IS NOT NULL filter is dropped entirely,
	// because there would be no bean row to wrongly attribute.
	if _, err := st.db.Exec(`INSERT INTO bean (roaster,name) VALUES ('R','never loaded')`); err != nil {
		t.Fatal(err)
	}

	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, at)
	if err != nil {
		t.Fatal(err)
	}
	var beanID, source any
	if err := st.db.QueryRow(
		`SELECT bean_id, bean_source FROM shot WHERE id=?`, res.ShotID).Scan(&beanID, &source); err != nil {
		t.Fatal(err)
	}
	if beanID != nil || source != nil {
		t.Errorf("bean_id=%v bean_source=%v, want both NULL", beanID, source)
	}
}

// A human naming the bean outranks the inference, and the provenance has to
// move with it — otherwise a corrected shot still reads as guessed.
func TestUpdateShotBeanMarksUser(t *testing.T) {
	st := openTestStore(t)
	rec, at := loadShotRecord(t)
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")

	var beanID int64
	st.db.QueryRow(`INSERT INTO bean (roaster, name) VALUES ('R','B') RETURNING id`).Scan(&beanID)
	if err := st.LoadBean(beanID); err != nil {
		t.Fatal(err)
	}
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, at)
	if err != nil {
		t.Fatal(err)
	}

	if err := st.UpdateShot(res.ShotID, map[string]any{"bean_id": beanID}); err != nil {
		t.Fatal(err)
	}
	var source string
	st.db.QueryRow(`SELECT bean_source FROM shot WHERE id=?`, res.ShotID).Scan(&source)
	if source != "user" {
		t.Errorf("after user edit bean_source = %q, want \"user\"", source)
	}

	// Clearing the bean clears the provenance: marking NULL as 'user' would
	// claim someone confirmed there was no bean.
	if err := st.UpdateShot(res.ShotID, map[string]any{"bean_id": nil}); err != nil {
		t.Fatal(err)
	}
	var cleared any
	st.db.QueryRow(`SELECT bean_source FROM shot WHERE id=?`, res.ShotID).Scan(&cleared)
	if cleared != nil {
		t.Errorf("after clearing the bean, bean_source = %v, want NULL", cleared)
	}
}

// Loading another bag moves the hopper. Asserted through Ingest rather than
// by re-running defaultRecipe's query in the test body: a test that restates
// the implementation cannot fail when the implementation changes, which is
// the one job it has here.
func TestLoadBeanNewestWins(t *testing.T) {
	st := openTestStore(t)
	rec, at := loadShotRecord(t)
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")

	var a, b int64
	st.db.QueryRow(`INSERT INTO bean (roaster,name) VALUES ('R','A') RETURNING id`).Scan(&a)
	st.db.QueryRow(`INSERT INTO bean (roaster,name) VALUES ('R','B') RETURNING id`).Scan(&b)

	if err := st.LoadBean(a); err != nil {
		t.Fatal(err)
	}
	if err := st.LoadBean(b); err != nil {
		t.Fatal(err)
	}
	// Both LoadBean calls land in the same second, so without distinct
	// timestamps the id tiebreak alone decides it and the loaded_at ordering
	// is never exercised — ORDER BY loaded_at ASC would still pass. Both must
	// also predate the shot, since the recipe is bounded by shot time.
	if _, err := st.db.Exec(`UPDATE bean SET loaded_at=? WHERE id=?`,
		at.Add(-48*time.Hour).UTC().Format(time.RFC3339), b); err != nil {
		t.Fatal(err)
	}
	if _, err := st.db.Exec(`UPDATE bean SET loaded_at=? WHERE id=?`,
		at.Add(-24*time.Hour).UTC().Format(time.RFC3339), a); err != nil {
		t.Fatal(err)
	}

	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, at)
	if err != nil {
		t.Fatal(err)
	}
	var got int64
	if err := st.db.QueryRow(`SELECT bean_id FROM shot WHERE id=?`, res.ShotID).Scan(&got); err != nil {
		t.Fatal(err)
	}
	if got != a {
		t.Errorf("ingest attributed bean %d, want %d (the most recently loaded)", got, a)
	}
	if err := st.LoadBean(9999); err == nil {
		t.Error("loading a nonexistent bean should fail")
	}
}

// Hard invariant 5: a dial reading is relative to a zero point that moves
// when the burrs come out, so it must not be carried across a grind epoch.
// NULL is the honest answer for the first shot of a new epoch.
func TestGrindDoesNotCrossEpoch(t *testing.T) {
	st := openTestStore(t)
	rec, at := loadShotRecord(t)
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")

	var grinder int64
	if err := st.db.QueryRow(`SELECT id FROM equipment WHERE kind='grinder' ORDER BY id LIMIT 1`).
		Scan(&grinder); err != nil {
		t.Fatal(err)
	}
	// A shot carrying a dial, in the epoch about to be superseded.
	if _, err := st.db.Exec(
		`INSERT INTO shot (started_at, grind_dial, grind_epoch_id, created_at) VALUES (?,7.9,?,?)`,
		at.Add(-time.Hour).UTC().Format(time.RFC3339), currentEpoch(t, st),
		at.UTC().Format(time.RFC3339)); err != nil {
		t.Fatal(err)
	}
	// Burrs come out BETWEEN the two shots. The epoch lookup is bounded by
	// shot time, so an epoch dated after this shot would (correctly) not
	// apply to it — the boundary has to fall inside the interval to be the
	// boundary this test is about.
	if _, err := st.db.Exec(
		`INSERT INTO grind_epoch (grinder_id, started_at, reason) VALUES (?,?,'burr_clean')`,
		grinder, at.Add(-30*time.Minute).UTC().Format(time.RFC3339)); err != nil {
		t.Fatal(err)
	}

	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, at)
	if err != nil {
		t.Fatal(err)
	}
	var dial any
	if err := st.db.QueryRow(`SELECT grind_dial FROM shot WHERE id=?`, res.ShotID).Scan(&dial); err != nil {
		t.Fatal(err)
	}
	if dial != nil {
		t.Errorf("grind_dial = %v across an epoch boundary, want NULL", dial)
	}
}

// A spooled or replayed upload must inherit the recipe in force WHEN IT WAS
// PULLED, not whatever is current when it finally arrives. The firmware
// uploads only on confirmed success, so an outage delivers a backlog hours
// later, and the replay tool re-ingests archived curves long after the fact.
// Unbounded, replaying an archive after loading today's bag would stamp every
// recovered shot with today's coffee and mark it 'loaded'.
func TestIngestDoesNotInheritFromTheFuture(t *testing.T) {
	st := openTestStore(t)
	rec, at := loadShotRecord(t)
	raw, _ := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")

	var old, recent int64
	st.db.QueryRow(`INSERT INTO bean (roaster,name) VALUES ('R','in the hopper then') RETURNING id`).Scan(&old)
	st.db.QueryRow(`INSERT INTO bean (roaster,name) VALUES ('R','opened later') RETURNING id`).Scan(&recent)
	if _, err := st.db.Exec(`UPDATE bean SET loaded_at=? WHERE id=?`,
		at.Add(-48*time.Hour).UTC().Format(time.RFC3339), old); err != nil {
		t.Fatal(err)
	}
	// A bag loaded AFTER this shot was pulled. It must not be attributed.
	if _, err := st.db.Exec(`UPDATE bean SET loaded_at=? WHERE id=?`,
		at.Add(48*time.Hour).UTC().Format(time.RFC3339), recent); err != nil {
		t.Fatal(err)
	}
	// Likewise a dial set by a LATER shot in the same epoch.
	if _, err := st.db.Exec(
		`INSERT INTO shot (started_at, grind_dial, grind_epoch_id, created_at) VALUES (?,3.3,?,?)`,
		at.Add(time.Hour).UTC().Format(time.RFC3339), currentEpoch(t, st),
		at.UTC().Format(time.RFC3339)); err != nil {
		t.Fatal(err)
	}
	epochThen := currentEpoch(t, st)
	// And a burr change that happened AFTER this shot was pulled. Filing the
	// shot under it would be wrong twice over: wrong epoch under invariant 5,
	// and it sends the dial lookup hunting an epoch the shot was never in.
	var grinder int64
	if err := st.db.QueryRow(`SELECT id FROM equipment WHERE kind='grinder' ORDER BY id LIMIT 1`).
		Scan(&grinder); err != nil {
		t.Fatal(err)
	}
	if _, err := st.db.Exec(
		`INSERT INTO grind_epoch (grinder_id, started_at, reason) VALUES (?,?,'burr_clean')`,
		grinder, at.Add(24*time.Hour).UTC().Format(time.RFC3339)); err != nil {
		t.Fatal(err)
	}

	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	var gotBean int64
	var gotDial any
	if err := st.db.QueryRow(`SELECT bean_id, grind_dial FROM shot WHERE id=?`, res.ShotID).
		Scan(&gotBean, &gotDial); err != nil {
		t.Fatal(err)
	}
	if gotBean != old {
		t.Errorf("attributed bean %d, want %d — the bag loaded before the shot", gotBean, old)
	}
	if gotDial != nil {
		t.Errorf("grind_dial = %v, want NULL — 3.3 was set by a later shot", gotDial)
	}
	var gotEpoch int64
	if err := st.db.QueryRow(`SELECT grind_epoch_id FROM shot WHERE id=?`, res.ShotID).
		Scan(&gotEpoch); err != nil {
		t.Fatal(err)
	}
	if gotEpoch != epochThen {
		t.Errorf("filed under epoch %d, want %d — the one in force when it was pulled",
			gotEpoch, epochThen)
	}
}
