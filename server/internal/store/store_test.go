package store

import (
	"bytes"
	"database/sql"
	"encoding/binary"
	"hash/crc32"
	"os"
	"path/filepath"
	"strings"
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

// v2BuildRecord assembles a record the way the firmware does, carrying the
// v2 machine fields. Deliberately not a fixture file: the point is to
// exercise the machine fields, and a hand-built record makes it obvious what
// is being claimed. Since 2026-11-14 it is written as the current format — the
// in-memory Header is the v3 layout — with "no press seen" in the v3 fields;
// v2's own byte layout is covered by internal/record's tests.
func v2BuildRecord(t *testing.T, boilerDc int16, timerDl uint16, touched bool) ([]byte, *record.Record) {
	t.Helper()
	h := record.Header{
		Magic: record.Magic, FormatVersion: 6, RecordType: record.TypeShot,
		HeaderLen: 134, SetpointDc: record.TempNone,
		MachineOnMs: record.MachineOnNone, PumpIdleMs: record.PumpIdleNone, BootID: 0xfeed0001, Seq: 1,
		PumpLine: record.PumpNone, PumpLeadMs: record.PumpLeadNone,
		PrePulseTotal:   record.PrePulsesNone,
		StartedAtUnixMs: uint64(time.Now().UnixMilli()), TimeValid: 1,
		StopMs: 21000, YieldAtStopMg: 35800, YieldFinalMg: 36000,
		PeakFlowMgps: 2100, MeanFlowMgps: 1710,
		FlowWinStartMs: 3000, FlowWinEndMs: 18000,
		BoilerTempStartDc: boilerDc, MachineTimerDl: timerDl,
		SampleCount: 1,
	}
	if touched {
		h.Flags |= record.FlagSetpointTouched
	}
	copy(h.DetectorVersion[:], "0a.6")
	copy(h.FirmwareVersion[:], "0.8.0")

	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, &h); err != nil {
		t.Fatal(err)
	}
	if err := binary.Write(&buf, binary.LittleEndian, []record.Sample{
		{TMs: 0, WeightMg: 0, InletPulses: 0, TempDc: record.TempNone},
	}); err != nil {
		t.Fatal(err)
	}
	raw := buf.Bytes()
	for i := 134 - 4; i < 134; i++ {
		raw[i] = 0
	}
	binary.LittleEndian.PutUint32(raw[134-4:], crc32.ChecksumIEEE(raw))

	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatalf("built record does not decode: %v", err)
	}
	return raw, rec
}

// The machine's own facts have to survive the whole path: display bus ->
// spool header -> HTTP -> shot row. This is the first data in the schema
// that neither a scale measured nor a human typed.
func TestIngestStoresMachineSignals(t *testing.T) {
	raw, rec := v2BuildRecord(t, 952, 210, true)
	st := openTestStore(t)

	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}

	var boiler, timer sql.NullFloat64
	var touched sql.NullInt64
	if err := st.db.QueryRow(
		`SELECT boiler_temp_start_c, machine_timer_s, setpoint_touched FROM shot WHERE id = ?`,
		res.ShotID).Scan(&boiler, &timer, &touched); err != nil {
		t.Fatal(err)
	}
	if !boiler.Valid || boiler.Float64 != 95 {
		t.Errorf("boiler_temp_start_c = %v, want 95", boiler)
	}
	if !timer.Valid || timer.Float64 != 21.0 {
		t.Errorf("machine_timer_s = %v, want 21.0 (210 tenths)", timer)
	}
	if !touched.Valid || touched.Int64 != 1 {
		t.Errorf("setpoint_touched = %v, want 1", touched)
	}
}

// "The display bus was off" must read as NULL, not as zero degrees in a
// zero-second shot. A v1 record says the same thing by predating 0e.
func TestIngestLeavesMachineSignalsNullWhenUnavailable(t *testing.T) {
	// v2 record, display bus off: firmware writes TempNone and 0.
	raw, rec := v2BuildRecord(t, record.TempNone, 0, false)
	st := openTestStore(t)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	var boiler, timer sql.NullFloat64
	var touched sql.NullInt64
	if err := st.db.QueryRow(
		`SELECT boiler_temp_start_c, machine_timer_s, setpoint_touched FROM shot WHERE id = ?`,
		res.ShotID).Scan(&boiler, &timer, &touched); err != nil {
		t.Fatal(err)
	}
	if boiler.Valid {
		t.Errorf("boiler_temp_start_c = %v, want NULL", boiler.Float64)
	}
	if timer.Valid {
		t.Errorf("machine_timer_s = %v, want NULL", timer.Float64)
	}
	if !touched.Valid || touched.Int64 != 0 {
		t.Errorf("setpoint_touched = %v, want 0 (v2 can say 'not touched')", touched)
	}

	// v1 record: predates the display bus entirely, so even the flag is
	// unknown rather than false.
	v1raw, err := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")
	if err != nil {
		t.Fatal(err)
	}
	v1rec, err := record.Decode(v1raw)
	if err != nil {
		t.Fatal(err)
	}
	res2, err := st.Ingest("aa:bb:cc:dd:ee:03", v1rec, v1raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if err := st.db.QueryRow(
		`SELECT boiler_temp_start_c, machine_timer_s, setpoint_touched FROM shot WHERE id = ?`,
		res2.ShotID).Scan(&boiler, &timer, &touched); err != nil {
		t.Fatal(err)
	}
	if boiler.Valid || timer.Valid || touched.Valid {
		t.Errorf("v1 record claims machine data: boiler=%v timer=%v touched=%v", boiler, timer, touched)
	}
}

// v2BuildFlowRecord assembles a genuine format-2 shot the way the firmware
// does, with `n` samples whose inlet_pulses count 0,1,2,… and the flow-tap
// flag set or clear. It goes through record.Decode, so the CRC and header
// length are exercised rather than bypassed — the first version of the test
// below mutated an already-decoded v1 fixture, which asserted the store's
// behaviour on a v1-record-with-a-v2-flag: a combination no firmware has ever
// written, and the one shape the version guard in insertShot now rejects.
func v2BuildFlowRecord(t *testing.T, seq uint32, flowTap bool, n int) ([]byte, *record.Record) {
	t.Helper()
	h := record.Header{
		Magic: record.Magic, FormatVersion: 6, RecordType: record.TypeShot,
		HeaderLen: 134, SetpointDc: record.TempNone,
		MachineOnMs: record.MachineOnNone, PumpIdleMs: record.PumpIdleNone, BootID: 0xfeed0002, Seq: seq,
		PumpLine: record.PumpNone, PumpLeadMs: record.PumpLeadNone,
		PrePulseTotal:   record.PrePulsesNone,
		StartedAtUnixMs: uint64(time.Now().UnixMilli()), TimeValid: 1,
		StopMs: 21000, YieldAtStopMg: 35800, YieldFinalMg: 36000,
		PeakFlowMgps: 2100, MeanFlowMgps: 1710,
		FlowWinStartMs: 3000, FlowWinEndMs: 18000,
		BoilerTempStartDc: 950, MachineTimerDl: 210,
		SampleCount: uint16(n),
	}
	if flowTap {
		h.Flags |= record.FlagFlowTap
	}
	copy(h.DetectorVersion[:], "0a.6")
	copy(h.FirmwareVersion[:], "0.9.0")

	// Deliberately NOT co-linear with TMs or WeightMg. An earlier version used
	// InletPulses: i against TMs: i*100, so binding smp.TMs/100 by mistake read
	// back identically and the test passed — it could not tell which field the
	// ingest had actually stored.
	pulses := []uint16{0, 3, 3, 7, 19, 19, 20, 41, 97, 98, 250, 251}
	if n > len(pulses) {
		t.Fatalf("v2BuildFlowRecord: n=%d exceeds the %d fixture values", n, len(pulses))
	}
	samples := make([]record.Sample, n)
	for i := range samples {
		samples[i] = record.Sample{
			TMs: uint16(i * 100), WeightMg: int32(i * 150),
			InletPulses: pulses[i], TempDc: record.TempNone,
		}
	}

	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, &h); err != nil {
		t.Fatal(err)
	}
	if err := binary.Write(&buf, binary.LittleEndian, samples); err != nil {
		t.Fatal(err)
	}
	raw := buf.Bytes()
	for i := 134 - 4; i < 134; i++ {
		raw[i] = 0
	}
	binary.LittleEndian.PutUint32(raw[134-4:], crc32.ChecksumIEEE(raw))

	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatalf("built record does not decode: %v", err)
	}
	return raw, rec
}

// 0d. inlet_pulses has existed in sample_t since 0a as a placeholder 0, so
// the format version alone cannot say whether a zero is a measurement — only
// the FlagFlowTap header bit can. Both directions are asserted because the
// ingest previously bound a literal NULL for every record, which made a
// correct-looking archive out of counted pulses and would not have failed a
// single existing test.
func TestIngestFlowPulses(t *testing.T) {
	st := openTestStore(t)
	dev := "aa:bb:cc:dd:ee:03"

	// Tap off: the bytes are placeholders, whatever they happen to hold.
	rawOff, recOff := v2BuildFlowRecord(t, 1, false, 12)
	res, err := st.Ingest(dev, recOff, rawOff, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	_, samples, err := st.Shot(res.ShotID)
	if err != nil {
		t.Fatal(err)
	}
	for i, s := range samples {
		if s["inlet_pulses"] != nil {
			t.Fatalf("tap off: sample %d stored pulses anyway: %v", i, s["inlet_pulses"])
		}
	}

	// Tap live: the same field is now a reading and must reach the archive —
	// including sample 0's legitimate zero, which must be 0 and not NULL.
	rawOn, recOn := v2BuildFlowRecord(t, 2, true, 12)
	res2, err := st.Ingest(dev, recOn, rawOn, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	_, samples2, err := st.Shot(res2.ShotID)
	if err != nil {
		t.Fatal(err)
	}
	want := []int64{0, 3, 3, 7, 19, 19, 20, 41, 97, 98, 250, 251}
	for i, s := range samples2 {
		got, ok := s["inlet_pulses"].(int64)
		if !ok || got != want[i] {
			t.Fatalf("tap live: sample %d = %v, want %d", i, s["inlet_pulses"], want[i])
		}
	}
}

// A v1 record can only carry bit 4 through corruption or a hand-built blob,
// and it must not be honoured: v1 predates the flowmeter entirely, so its
// placeholder zeros would become a measurement meaning "no water moved".
func TestIngestFlowPulsesRejectsV1Flag(t *testing.T) {
	raw, err := os.ReadFile("../record/testdata/shot-fw040-timevalid.bin")
	if err != nil {
		t.Fatal(err)
	}
	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatal(err)
	}
	if rec.Header.FormatVersion != 1 {
		t.Fatalf("fixture is v%d; this test needs a v1 record", rec.Header.FormatVersion)
	}
	rec.Header.Flags |= record.FlagFlowTap

	st := openTestStore(t)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	_, samples, err := st.Shot(res.ShotID)
	if err != nil {
		t.Fatal(err)
	}
	if samples[0]["inlet_pulses"] != nil {
		t.Errorf("v1 record honoured a v2 flag: %v", samples[0]["inlet_pulses"])
	}
}

// pumpRecord builds a v3 shot record carrying a lever press (0c).
func pumpRecord(t *testing.T, seq uint32, line uint8, leadMs uint32) ([]byte, *record.Record) {
	t.Helper()
	return pumpRecordWith(t, seq, line, leadMs, nil, nil)
}

// pumpRecordWith is pumpRecord with a hook on the header before encoding
// and the v4 pulse offsets to put in the record's tail.
func pumpRecordWith(t *testing.T, seq uint32, line uint8, leadMs uint32, mutate func(*record.Header), offsets []uint16) ([]byte, *record.Record) {
	t.Helper()
	h := record.Header{
		Magic: record.Magic, FormatVersion: 6, RecordType: record.TypeShot,
		HeaderLen: 134, SetpointDc: record.TempNone,
		MachineOnMs: record.MachineOnNone, PumpIdleMs: record.PumpIdleNone, BootID: 0xfeed0003, Seq: seq,
		StartedAtUnixMs: uint64(time.Now().UnixMilli()) + uint64(seq), TimeValid: 1,
		StopMs: 21000, YieldAtStopMg: 35800, YieldFinalMg: 36000,
		PeakFlowMgps: 2100, MeanFlowMgps: 1710,
		FlowWinStartMs: 3000, FlowWinEndMs: 18000,
		BoilerTempStartDc: 950, MachineTimerDl: 288,
		PumpLine: line, PumpLeadMs: leadMs,
		PrePulseTotal: record.PrePulsesNone,
		SampleCount:   1,
	}
	copy(h.DetectorVersion[:], "0a.6")
	copy(h.FirmwareVersion[:], "0.11.0")
	if mutate != nil {
		mutate(&h)
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, &h); err != nil {
		t.Fatal(err)
	}
	if err := binary.Write(&buf, binary.LittleEndian, []record.Sample{
		{TMs: 0, WeightMg: 0, InletPulses: 0, TempDc: record.TempNone},
	}); err != nil {
		t.Fatal(err)
	}
	if len(offsets) > 0 {
		tail := make([]uint16, (len(offsets)+4)/5*5)
		for k := range tail {
			tail[k] = 0xFFFF
		}
		copy(tail, offsets)
		if err := binary.Write(&buf, binary.LittleEndian, tail); err != nil {
			t.Fatal(err)
		}
	}
	raw := buf.Bytes()
	binary.LittleEndian.PutUint32(raw[134-4:], crc32.ChecksumIEEE(raw))
	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatalf("built record does not decode: %v", err)
	}
	return raw, rec
}

// The lever press that started the pump reaches the shot row: lever A's
// direction as switch_direction, the press's lead over first drip as
// first_drip_ms. And v_shot's timing stays on stop_ms's own time base:
// pour_s is first drip -> stop, pump_to_stop_s adds the lead. Before
// migration 010, pour_s subtracted the lead from a time that already
// counted from first drip.
func TestIngestPumpStart(t *testing.T) {
	cases := []struct {
		name      string
		line      uint8
		lead      uint32
		direction any // string or nil
		firstDrip any // int64 or nil
	}{
		{"2Cup", record.Pump2Cup, 8200, "down", int64(8200)},
		{"1Cup", record.Pump1Cup, 6100, "up", int64(6100)},
		{"hot water", record.PumpWater, 4000, nil, int64(4000)},
		{"no press", record.PumpNone, record.PumpLeadNone, nil, nil},
		// No firmware writes these; refused, but the raw byte is kept.
		{"unknown line", 7, 5000, nil, nil},
		{"lead beyond the window", record.Pump2Cup, 600000, nil, nil},
	}
	st := openTestStore(t)
	for i, c := range cases {
		raw, rec := pumpRecord(t, uint32(i+1), c.line, c.lead)
		res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
		if err != nil {
			t.Fatalf("%s: %v", c.name, err)
		}
		var dir sql.NullString
		var drip, rawLine sql.NullInt64
		var pour, total sql.NullFloat64
		var pump sql.NullFloat64
		if err := st.db.QueryRow(
			`SELECT switch_direction, first_drip_ms, pour_s, total_s, pump_to_stop_s, pump_line FROM v_shot WHERE id = ?`,
			res.ShotID).Scan(&dir, &drip, &pour, &total, &pump, &rawLine); err != nil {
			t.Fatalf("%s: %v", c.name, err)
		}
		if (c.direction == nil) != !dir.Valid || (dir.Valid && dir.String != c.direction) {
			t.Errorf("%s: switch_direction = %v, want %v", c.name, dir, c.direction)
		}
		if (c.firstDrip == nil) != !drip.Valid || (drip.Valid && drip.Int64 != c.firstDrip) {
			t.Errorf("%s: first_drip_ms = %v, want %v", c.name, drip, c.firstDrip)
		}
		if !rawLine.Valid || rawLine.Int64 != int64(c.line) {
			t.Errorf("%s: pump_line = %v, want raw %d", c.name, rawLine, c.line)
		}
		if !total.Valid || total.Float64 != 21.0 {
			t.Errorf("%s: total_s = %v, want 21.0", c.name, total)
		}
		// pour_s is first drip -> stop, and NULL where the press was not seen,
		// so "pour_s IS NOT NULL" keeps meaning "switch-sensed".
		if c.firstDrip == nil {
			if pour.Valid {
				t.Errorf("%s: pour_s = %v, want NULL", c.name, pour.Float64)
			}
		} else if !pour.Valid || pour.Float64 != 21.0 {
			t.Errorf("%s: pour_s = %v, want 21.0", c.name, pour)
		}
		if c.firstDrip == nil {
			if pump.Valid {
				t.Errorf("%s: pump_to_stop_s = %v, want NULL", c.name, pump.Float64)
			}
		} else if want := (float64(c.firstDrip.(int64)) + 21000) / 1000; !pump.Valid || pump.Float64 != want {
			t.Errorf("%s: pump_to_stop_s = %v, want %v", c.name, pump, want)
		}
	}
}

// v2 records are what is spooled on the board when 0.10.0 is flashed, so the
// whole ingest path — not just the decoder — must still take them, with the
// v3 columns left NULL. Built in v2's own byte layout, by hand: record's
// headerV2 is unexported on purpose.
func TestIngestV2RecordStillWorks(t *testing.T) {
	v2 := struct {
		Magic             uint32
		FormatVersion     uint8
		RecordType        uint8
		HeaderLen         uint16
		BootID            uint32
		Seq               uint32
		StartedAtUnixMs   uint64
		StartedAtMillis   uint32
		TzOffsetMin       int16
		TimeValid         uint8
		ScaleRole         uint8
		ScaleMac          [6]byte
		Flags             uint8
		DetectorVersion   [8]byte
		FirmwareVersion   [16]byte
		StopMs            uint32
		YieldAtStopMg     int32
		YieldFinalMg      int32
		SettleOffsetMg    int32
		PeakFlowMgps      int32
		MeanFlowMgps      int32
		FlowWinStartMs    uint32
		FlowWinEndMs      uint32
		BoilerTempStartDc int16
		MachineTimerDl    uint16
		GramsMg           int32
		StableMs          uint32
		SampleCount       uint16
		Crc32             uint32
	}{
		Magic: record.Magic, FormatVersion: 2, RecordType: record.TypeShot,
		HeaderLen: 113, BootID: 0xfeed0004, Seq: 1,
		StartedAtUnixMs: uint64(time.Now().UnixMilli()), TimeValid: 1,
		StopMs: 21000, YieldAtStopMg: 35800, YieldFinalMg: 36000,
		BoilerTempStartDc: 950, MachineTimerDl: 210, SampleCount: 1,
	}
	copy(v2.DetectorVersion[:], "0a.6")
	copy(v2.FirmwareVersion[:], "0.9.9")
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, &v2); err != nil {
		t.Fatal(err)
	}
	if err := binary.Write(&buf, binary.LittleEndian, []record.Sample{
		{TMs: 0, WeightMg: 0, InletPulses: 0, TempDc: record.TempNone},
	}); err != nil {
		t.Fatal(err)
	}
	raw := buf.Bytes()
	if len(raw) != 113+10 {
		t.Fatalf("built %d bytes, want 123", len(raw))
	}
	binary.LittleEndian.PutUint32(raw[113-4:], crc32.ChecksumIEEE(raw))
	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatalf("v2 record does not decode: %v", err)
	}
	st := openTestStore(t)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	var timer sql.NullFloat64
	var dir sql.NullString
	var drip, line, preTotal sql.NullInt64
	var pour sql.NullFloat64
	if err := st.db.QueryRow(
		`SELECT machine_timer_s, switch_direction, first_drip_ms, pump_line, pour_s, pre_pulse_total FROM v_shot WHERE id = ?`,
		res.ShotID).Scan(&timer, &dir, &drip, &line, &pour, &preTotal); err != nil {
		t.Fatal(err)
	}
	if preTotal.Valid {
		t.Errorf("v2 record stored pre_pulse_total = %d, want NULL (not captured, not zero)", preTotal.Int64)
	}
	if !timer.Valid || timer.Float64 != 21.0 {
		t.Errorf("v2 machine_timer_s = %v, want 21.0", timer)
	}
	if dir.Valid || drip.Valid || line.Valid || pour.Valid {
		t.Errorf("v2 record filled v3 columns: dir=%v drip=%v line=%v pour=%v", dir, drip, line, pour)
	}
}

// The pulse train from the press to first drip reaches the database raw, one
// row per pulse, and "not captured" stays distinguishable from "captured, no
// pulses".
func TestIngestPrePulses(t *testing.T) {
	st := openTestStore(t)
	cases := []struct {
		name     string
		total    uint16
		ms       []uint16
		complete bool
		wantNull bool
	}{
		{"captured", 4, []uint16{200, 300, 400, 5000}, true, false},
		{"captured, log overflowed", 300, []uint16{100, 150}, false, false},
		{"captured, no pulses", 0, nil, true, false},
		{"not captured", record.PrePulsesNone, nil, false, true},
	}
	for i, c := range cases {
		raw, rec := pumpRecordWith(t, uint32(100+i), record.Pump2Cup, 8200, func(h *record.Header) {
			h.PrePulseTotal = c.total
			h.PrePulseCount = uint16(len(c.ms))
			h.Flags |= record.FlagFlowTap
			if c.complete {
				h.PrePulseFlags = record.PrePulsesComplete
			}
		}, c.ms)
		res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
		if err != nil {
			t.Fatalf("%s: %v", c.name, err)
		}
		var total, complete sql.NullInt64
		if err := st.db.QueryRow(`SELECT pre_pulse_total, pre_pulse_complete FROM shot WHERE id = ?`,
			res.ShotID).Scan(&total, &complete); err != nil {
			t.Fatalf("%s: %v", c.name, err)
		}
		if c.wantNull {
			if total.Valid || complete.Valid {
				t.Errorf("%s: total=%v complete=%v, want NULL both", c.name, total, complete)
			}
		} else if !total.Valid || total.Int64 != int64(c.total) || !complete.Valid || (complete.Int64 == 1) != c.complete {
			t.Errorf("%s: total=%v complete=%v, want %d %v", c.name, total, complete, c.total, c.complete)
		}
		rows, err := st.db.Query(`SELECT n, ms_after_press FROM shot_pre_pulse WHERE shot_id = ? ORDER BY n`, res.ShotID)
		if err != nil {
			t.Fatal(err)
		}
		var got []uint16
		for rows.Next() {
			var n int
			var ms uint16
			if err := rows.Scan(&n, &ms); err != nil {
				t.Fatal(err)
			}
			if n != len(got) {
				t.Errorf("%s: row n=%d out of order", c.name, n)
			}
			got = append(got, ms)
		}
		rows.Close()
		if len(got) != len(c.ms) {
			t.Fatalf("%s: %d pulse rows, want %d", c.name, len(got), len(c.ms))
		}
		for j := range got {
			if got[j] != c.ms[j] {
				t.Errorf("%s: pulse %d = %d ms, want %d", c.name, j, got[j], c.ms[j])
			}
		}
	}
}

// A record the firmware flagged as a cup placement is stored — never lost —
// but excluded, with a reason that says why.
func TestIngestPlacementIsExcluded(t *testing.T) {
	st := openTestStore(t)
	raw, rec := pumpRecordWith(t, 200, record.Pump2Cup, 8200, func(h *record.Header) {
		h.Flags |= record.FlagPlacement
	}, nil)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	var excluded int
	var reason sql.NullString
	if err := st.db.QueryRow(`SELECT excluded, exclude_reason FROM shot WHERE id = ?`, res.ShotID).
		Scan(&excluded, &reason); err != nil {
		t.Fatal(err)
	}
	if excluded != 1 || !strings.Contains(reason.String, "placement") {
		t.Errorf("placement shot: excluded=%d reason=%q, want 1 and a placement reason", excluded, reason.String)
	}

	// The same bit on a record that predates it means nothing.
	old := record.Header{FormatVersion: 3, Flags: record.FlagPlacement}
	if old.Placement() {
		t.Error("a v3 header claims a placement")
	}
}

// Detector 0a.7's DISTURBED bit is stored as disturbed = 1, a clear bit from
// 0a.7 as 0, and any bit from an older detector as NULL -- it never set the
// flag, so it cannot say. Never an exclusion.
func TestIngestDisturbed(t *testing.T) {
	st := openTestStore(t)
	for i, c := range []struct {
		name     string
		detector string
		flag     bool
		want     sql.NullInt64
	}{
		{"0a.7 disturbed", "0a.7", true, sql.NullInt64{Int64: 1, Valid: true}},
		{"0a.7 clean", "0a.7", false, sql.NullInt64{Int64: 0, Valid: true}},
		{"0a.12 disturbed", "0a.12", true, sql.NullInt64{Int64: 1, Valid: true}},
		{"0a.6 bit set", "0a.6", true, sql.NullInt64{}},
		{"0a.6 clear", "0a.6", false, sql.NullInt64{}},
		{"0b.1 disturbed", "0b.1", true, sql.NullInt64{Int64: 1, Valid: true}},
		{"empty version", "", true, sql.NullInt64{}},
		{"0a.8-rc disturbed", "0a.8-rc", true, sql.NullInt64{Int64: 1, Valid: true}},
		{"dev build", "dev", true, sql.NullInt64{}},
		{"1a.0 disturbed", "1a.0", true, sql.NullInt64{Int64: 1, Valid: true}},
	} {
		raw, rec := pumpRecordWith(t, uint32(300+i), record.Pump2Cup, 8200, func(h *record.Header) {
			h.DetectorVersion = [8]byte{}
			copy(h.DetectorVersion[:], c.detector)
			if c.flag {
				h.Flags |= record.FlagDisturbed
			}
		}, nil)
		res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
		if err != nil {
			t.Fatalf("%s: %v", c.name, err)
		}
		var got sql.NullInt64
		var excluded int
		if err := st.db.QueryRow(`SELECT disturbed, excluded FROM shot WHERE id = ?`, res.ShotID).
			Scan(&got, &excluded); err != nil {
			t.Fatal(err)
		}
		if got != c.want {
			t.Errorf("%s: disturbed = %v, want %v", c.name, got, c.want)
		}
		if excluded != 0 {
			t.Errorf("%s: excluded = %d, want 0 -- a disturbed shot is real", c.name, excluded)
		}
	}
}

// SetAmbient fills a shot once: a later lookup of the same shot (a
// concurrent pass, a retry) cannot overwrite the first answer, and the shot
// leaves ShotsNeedingAmbient.
func TestSetAmbientWritesOnce(t *testing.T) {
	st := openTestStore(t)
	raw, rec := pumpRecordWith(t, 400, record.Pump2Cup, 8200, nil, nil)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	p, err := st.ShotsNeedingAmbient(10)
	if err != nil || len(p) != 1 || p[0].ID != res.ShotID {
		t.Fatalf("pending = %+v, %v; want the new shot", p, err)
	}
	t1, rh := 21.4, 48.0
	if err := st.SetAmbient(res.ShotID, &t1, &rh, "ha_state"); err != nil {
		t.Fatal(err)
	}
	t2 := 30.0
	if err := st.SetAmbient(res.ShotID, &t2, nil, "ha_hourly"); err != nil {
		t.Fatal(err)
	}
	var c, r sql.NullFloat64
	var src sql.NullString
	if err := st.db.QueryRow(`SELECT ambient_c, ambient_rh, ambient_source FROM shot WHERE id = ?`,
		res.ShotID).Scan(&c, &r, &src); err != nil {
		t.Fatal(err)
	}
	if c.Float64 != 21.4 || r.Float64 != 48 || src.String != "ha_state" {
		t.Errorf("got %v / %v / %v, want the first answer kept", c, r, src)
	}
	if p, _ := st.ShotsNeedingAmbient(10); len(p) != 0 {
		t.Errorf("still pending: %+v", p)
	}
}

// v6 warm-up fields land in machine_on_s / machine_on_lower_bound /
// s_since_flush; "not known" is NULL, never 0 (migration 015).
func TestIngestWarmUp(t *testing.T) {
	st := openTestStore(t)
	cases := []struct {
		onMs, idleMs uint32
		flags        uint8
		on, lb, idle sql.NullFloat64
	}{
		{300000, 25000, 0, sql.NullFloat64{Float64: 300, Valid: true}, sql.NullFloat64{Float64: 0, Valid: true}, sql.NullFloat64{Float64: 25, Valid: true}},
		{119000, record.PumpIdleNone, record.MachineOnLowerBound, sql.NullFloat64{Float64: 119, Valid: true}, sql.NullFloat64{Float64: 1, Valid: true}, sql.NullFloat64{}},
		{record.MachineOnNone, record.PumpIdleNone, 0, sql.NullFloat64{}, sql.NullFloat64{}, sql.NullFloat64{}},
	}
	for i, c := range cases {
		raw, rec := pumpRecordWith(t, uint32(420+i), record.Pump2Cup, 8200, func(h *record.Header) {
			h.MachineOnMs, h.MachineOnFlags, h.PumpIdleMs = c.onMs, c.flags, c.idleMs
		}, nil)
		res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
		if err != nil {
			t.Fatal(err)
		}
		var on, lb, idle sql.NullFloat64
		if err := st.db.QueryRow(`SELECT machine_on_s, machine_on_lower_bound, s_since_flush FROM shot WHERE id = ?`,
			res.ShotID).Scan(&on, &lb, &idle); err != nil {
			t.Fatal(err)
		}
		if on != c.on || lb != c.lb || idle != c.idle {
			t.Errorf("case %d: on=%v lb=%v idle=%v, want %v %v %v", i, on, lb, idle, c.on, c.lb, c.idle)
		}
	}
}

// A shot recorded without clock time is reported as such: its started_at is
// the upload time, so the room then says nothing about the pour.
func TestShotsNeedingAmbientReportsClockValidity(t *testing.T) {
	st := openTestStore(t)
	raw, rec := pumpRecordWith(t, 410, record.Pump2Cup, 8200, func(h *record.Header) {
		h.TimeValid = 0
	}, nil)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	p, err := st.ShotsNeedingAmbient(10)
	if err != nil || len(p) != 1 || p[0].ID != res.ShotID || p[0].ClockValid || p[0].BadTime {
		t.Errorf("pending = %+v, %v; want the shot with ClockValid false", p, err)
	}
	if _, err := st.db.Exec(`UPDATE shot SET started_at = 'not a time' WHERE id = ?`, res.ShotID); err != nil {
		t.Fatal(err)
	}
	if p, err := st.ShotsNeedingAmbient(10); err != nil || len(p) != 1 || !p[0].BadTime {
		t.Errorf("pending = %+v, %v; want BadTime, not an error", p, err)
	}
}

// A record excluded at ingest — a placement (a cup set back after weighing,
// shot 180), a reject (a knock, a flat abort under 5 g) or a legacy fault —
// is stored excluded and must not take the dose from the real shot pulled
// after it, in either arrival order (reviews 2026-11-15: placements took
// it, then a draft let rejects keep one and the knock took the real 18 g).
func TestExcludedRecordsTakeNoDose(t *testing.T) {
	dev := "aa:bb:cc:dd:ee:03"
	base := time.Now().Add(-time.Hour).Truncate(time.Second)
	doseOf := func(st *Store, id int64) (sql.NullFloat64, int) {
		var g sql.NullFloat64
		var excl int
		if err := st.db.QueryRow(`SELECT dose_ground_g, excluded FROM shot WHERE id = ?`, id).Scan(&g, &excl); err != nil {
			t.Fatal(err)
		}
		return g, excl
	}
	for _, flag := range []struct {
		name string
		bit  uint8
	}{{"placement", record.FlagPlacement}, {"reject", record.FlagReject}, {"fault", record.FlagFault}} {
		for _, order := range []string{"weighing first", "weighing last"} {
			st := openTestStore(t)
			w := weighingRecord(1, 18000, base.Add(-time.Minute))
			early := func(h *record.Header) {
				h.StartedAtUnixMs = uint64(base.Add(-30 * time.Second).UnixMilli())
				h.Flags |= flag.bit
			}
			shot := func(h *record.Header) { h.StartedAtUnixMs = uint64(base.UnixMilli()) }
			xraw, xrec := pumpRecordWith(t, 300, record.Pump2Cup, 8200, early, nil)
			sraw, srec := pumpRecordWith(t, 301, record.Pump2Cup, 8200, shot, nil)
			if order == "weighing first" {
				if _, err := st.Ingest(dev, w, []byte{2}, base); err != nil {
					t.Fatal(err)
				}
			}
			xres, err := st.Ingest(dev, xrec, xraw, base)
			if err != nil {
				t.Fatal(err)
			}
			sres, err := st.Ingest(dev, srec, sraw, base)
			if err != nil {
				t.Fatal(err)
			}
			if order == "weighing last" {
				if _, err := st.Ingest(dev, w, []byte{2}, base); err != nil {
					t.Fatal(err)
				}
			}
			if g, excl := doseOf(st, xres.ShotID); excl != 1 || g.Valid {
				t.Errorf("%s, %s: excluded=%d dose=%v, want excluded and no dose", flag.name, order, excl, g)
			}
			if g, _ := doseOf(st, sres.ShotID); !g.Valid || g.Float64 != 18.0 {
				t.Errorf("%s, %s: the real shot's dose = %v, want 18", flag.name, order, g)
			}
		}
	}
}

// A shot the USER excluded (a prep error) still used the coffee: a weighing
// that arrives after it is ingested still goes to it, not to a later shot.
func TestUserExcludedShotKeepsLateDose(t *testing.T) {
	dev := "aa:bb:cc:dd:ee:03"
	base := time.Now().Add(-time.Hour).Truncate(time.Second)
	st := openTestStore(t)
	shot := func(h *record.Header) { h.StartedAtUnixMs = uint64(base.UnixMilli()) }
	raw, rec := pumpRecordWith(t, 300, record.Pump2Cup, 8200, shot, nil)
	res, err := st.Ingest(dev, rec, raw, base)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := st.db.Exec(`UPDATE shot SET excluded = 1, exclude_reason = 'prep error' WHERE id = ?`, res.ShotID); err != nil {
		t.Fatal(err)
	}
	if _, err := st.Ingest(dev, weighingRecord(1, 18000, base.Add(-time.Minute)), []byte{2}, base); err != nil {
		t.Fatal(err)
	}
	var g sql.NullFloat64
	if err := st.db.QueryRow(`SELECT dose_ground_g FROM shot WHERE id = ?`, res.ShotID).Scan(&g); err != nil {
		t.Fatal(err)
	}
	if !g.Valid || g.Float64 != 18.0 {
		t.Errorf("user-excluded shot's late dose = %v, want 18", g)
	}
}

// The brew setpoint and the pre-infusion reach the row as measurements with
// their provenance (firmware 0.12.0, migration 012): the setpoint the display
// showed being dialled as 'display'; the pump-on run measured from the
// pre-drip pulses as preinfusion_run_s, and the whole-second Pr it
// corresponds to as preinfusion_s 'flow' — and the next shot without either
// carries them as 'carried'. A hot-water draw is not measured, and a typed
// or cleared value becomes 'user'.
func TestIngestMeasuredRecipe(t *testing.T) {
	st := openTestStore(t)
	dev := "aa:bb:cc:dd:ee:03"
	base := time.Now().Add(-time.Hour).Truncate(time.Second)
	at := func(d time.Duration, extra func(*record.Header)) func(*record.Header) {
		return func(h *record.Header) {
			h.StartedAtUnixMs = uint64(base.Add(d).UnixMilli())
			if extra != nil {
				extra(h)
			}
		}
	}
	train := shot179()
	measured := func(h *record.Header) {
		h.SetpointDc = 930
		h.Flags |= record.FlagFlowTap
		h.PrePulseTotal, h.PrePulseCount, h.PrePulseFlags = uint16(len(train)), uint16(len(train)), record.PrePulsesComplete
	}
	type rowT struct {
		temp, pre, run sql.NullFloat64
		tsrc, psrc     sql.NullString
	}
	row := func(id int64) rowT {
		t.Helper()
		var r rowT
		if err := st.db.QueryRow(`SELECT brew_temp_c, brew_temp_source, preinfusion_s, preinfusion_source, preinfusion_run_s FROM shot WHERE id = ?`, id).
			Scan(&r.temp, &r.tsrc, &r.pre, &r.psrc, &r.run); err != nil {
			t.Fatal(err)
		}
		return r
	}
	ingest := func(seq uint32, line uint8, mut func(*record.Header), offsets []uint16) int64 {
		t.Helper()
		raw, rec := pumpRecordWith(t, seq, line, 9855, mut, offsets)
		res, err := st.Ingest(dev, rec, raw, base)
		if err != nil {
			t.Fatal(err)
		}
		return res.ShotID
	}

	a := ingest(1, record.Pump2Cup, at(0, measured), train)
	if r := row(a); r.temp.Float64 != 93 || r.tsrc.String != "display" || r.pre.Float64 != 5 || r.psrc.String != "flow" || r.run.Float64 != 5.1 {
		t.Errorf("measured shot: %+v; want 93 display, Pr 5 flow, run 5.1", r)
	}
	// Next shot: no setpoint seen (a reboot), no pulse train.
	b := ingest(2, record.Pump2Cup, at(time.Minute, nil), nil)
	if r := row(b); r.temp.Float64 != 93 || r.tsrc.String != "carried" || r.pre.Float64 != 5 || r.psrc.String != "carried" || r.run.Valid {
		t.Errorf("following shot: %+v; want 93 carried, 5 carried, no run", r)
	}
	// A hot-water draw's pulses are not a pre-infusion.
	w := ingest(3, record.PumpWater, at(2*time.Minute, measured), train)
	if r := row(w); r.run.Valid || r.psrc.String == "flow" {
		t.Errorf("hot-water draw measured: %+v", r)
	}
	// A shot that recorded an edit DURING it: the firmware stamped the
	// setpoint as at pour start — the pre-edit 93 — and it measured its own
	// run. It stores those, but must not pass them on: the next shot without
	// a measurement carries nothing (review, 2026-11-15: it carried 93).
	c := ingest(4, record.Pump2Cup, at(3*time.Minute, func(h *record.Header) {
		measured(h)
		h.Flags |= record.FlagSetpointTouched
	}), train)
	if r := row(c); r.temp.Float64 != 93 || r.tsrc.String != "display" {
		t.Errorf("edited shot: %+v; want its own 93 display", r)
	}
	d := ingest(5, record.Pump2Cup, at(4*time.Minute, nil), nil)
	if r := row(d); r.temp.Valid || r.pre.Valid {
		t.Errorf("shot after an edit carried the pre-edit values: %+v", r)
	}
	// Typed values are the user's, and so is clearing one.
	if err := st.UpdateShot(b, map[string]any{"brew_temp_c": 94.0, "preinfusion_s": nil}); err != nil {
		t.Fatal(err)
	}
	if r := row(b); r.temp.Float64 != 94 || r.tsrc.String != "user" || r.pre.Valid || r.psrc.String != "user" {
		t.Errorf("after PATCH: %+v; want 94 user, cleared by the user", r)
	}
}

// Pre-infusion is (re-)measured from the stored pulses at every Open: a v4
// shot ingested before measuring existed gets it, a 'flow' row follows the
// current thresholds, and a value the USER cleared stays cleared (review,
// 2026-11-15: the backfill refilled it on every restart).
func TestBackfillPreinfusion(t *testing.T) {
	st := openTestStore(t)
	train := shot179()
	raw, rec := pumpRecordWith(t, 1, record.Pump2Cup, 9855, func(h *record.Header) {
		h.Flags |= record.FlagFlowTap
		h.PrePulseTotal, h.PrePulseCount, h.PrePulseFlags = uint16(len(train)), uint16(len(train)), record.PrePulsesComplete
	}, train)
	res, err := st.Ingest("aa:bb:cc:dd:ee:03", rec, raw, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	read := func() (pre, run sql.NullFloat64, src sql.NullString) {
		t.Helper()
		if err := st.db.QueryRow(`SELECT preinfusion_s, preinfusion_run_s, preinfusion_source FROM shot WHERE id = ?`, res.ShotID).Scan(&pre, &run, &src); err != nil {
			t.Fatal(err)
		}
		return
	}
	// As 0.11.0's shots stand on production: no value, no source, no run.
	if _, err := st.db.Exec(`UPDATE shot SET preinfusion_s = NULL, preinfusion_source = NULL, preinfusion_run_s = NULL WHERE id = ?`, res.ShotID); err != nil {
		t.Fatal(err)
	}
	if n, err := st.backfillPreinfusion(); err != nil || n != 1 {
		t.Fatalf("backfill = %d, %v; want 1", n, err)
	}
	if pre, run, src := read(); pre.Float64 != 5 || run.Float64 != 5.1 || src.String != "flow" {
		t.Errorf("backfilled %v / %v (%v), want Pr 5, run 5.1, flow", pre, run, src)
	}
	if n, _ := st.backfillPreinfusion(); n != 0 {
		t.Errorf("second backfill touched %d rows", n)
	}
	// A stale 'flow' value is re-derived — the setting, not only the run.
	if _, err := st.db.Exec(`UPDATE shot SET preinfusion_s = 3, preinfusion_run_s = 2.9 WHERE id = ?`, res.ShotID); err != nil {
		t.Fatal(err)
	}
	if _, err := st.backfillPreinfusion(); err != nil {
		t.Fatal(err)
	}
	if pre, run, src := read(); pre.Float64 != 5 || run.Float64 != 5.1 || src.String != "flow" {
		t.Errorf("stale flow row re-derived to %v / %v (%v), want 5 / 5.1 flow", pre, run, src)
	}
	// A pre-012 value with no source may have been typed: never touched,
	// though its run is measured.
	if _, err := st.db.Exec(`UPDATE shot SET preinfusion_s = 2.5, preinfusion_source = NULL, preinfusion_run_s = NULL WHERE id = ?`, res.ShotID); err != nil {
		t.Fatal(err)
	}
	if _, err := st.backfillPreinfusion(); err != nil {
		t.Fatal(err)
	}
	if pre, run, src := read(); pre.Float64 != 2.5 || src.Valid || run.Float64 != 5.1 {
		t.Errorf("pre-012 value overwritten: %v / %v (%v), want 2.5 unsourced, run 5.1", pre, run, src)
	}
	// A 'flow' value the train no longer supports is cleared with its run.
	if _, err := st.db.Exec(`UPDATE shot SET preinfusion_s = 5, preinfusion_source = 'flow', pre_pulse_complete = 0 WHERE id = ?`, res.ShotID); err != nil {
		t.Fatal(err)
	}
	if _, err := st.backfillPreinfusion(); err != nil {
		t.Fatal(err)
	}
	if pre, run, src := read(); pre.Valid || run.Valid || src.Valid {
		t.Errorf("unmeasurable flow row kept: %v / %v (%v)", pre, run, src)
	}
	if _, err := st.db.Exec(`UPDATE shot SET pre_pulse_complete = 1 WHERE id = ?`, res.ShotID); err != nil {
		t.Fatal(err)
	}
	if _, err := st.backfillPreinfusion(); err != nil {
		t.Fatal(err)
	}
	// Cleared by the user: stays cleared, though the run is still measured.
	if err := st.UpdateShot(res.ShotID, map[string]any{"preinfusion_s": nil}); err != nil {
		t.Fatal(err)
	}
	if _, err := st.backfillPreinfusion(); err != nil {
		t.Fatal(err)
	}
	if pre, run, src := read(); pre.Valid || src.String != "user" || run.Float64 != 5.1 {
		t.Errorf("after a user clear and a restart: %v / %v (%v), want empty, user, run 5.1", pre, run, src)
	}
}

// A measured run that maps to no Pr the machine offers (5.5 s and up) must
// not sit beside a carried setting that contradicts it: at ingest and in the
// backfill the setting goes empty, the run stays. And a v1 row — no
// setpoint_touched at all — is still a carry source (review, 2026-11-15: NOT
// (NULL AND ...) dropped it).
func TestUnmappableRunAndV1CarrySource(t *testing.T) {
	st := openTestStore(t)
	dev := "aa:bb:cc:dd:ee:03"
	base := time.Now().Add(-time.Hour).Truncate(time.Second)
	long := append(dense(100, 5900), dense(8000, 9000)...) // a 5.8 s run, then the resume
	measured := func(d time.Duration) func(*record.Header) {
		return func(h *record.Header) {
			h.StartedAtUnixMs = uint64(base.Add(d).UnixMilli())
			h.Flags |= record.FlagFlowTap
			h.PrePulseTotal, h.PrePulseCount, h.PrePulseFlags = uint16(len(long)), uint16(len(long)), record.PrePulsesComplete
		}
	}
	plain := func(d time.Duration) func(*record.Header) {
		return func(h *record.Header) { h.StartedAtUnixMs = uint64(base.Add(d).UnixMilli()) }
	}
	ingest := func(seq uint32, mut func(*record.Header), offsets []uint16) int64 {
		t.Helper()
		raw, rec := pumpRecordWith(t, seq, record.Pump2Cup, 9500, mut, offsets)
		res, err := st.Ingest(dev, rec, raw, base)
		if err != nil {
			t.Fatal(err)
		}
		return res.ShotID
	}
	type rowT struct {
		temp, pre, run sql.NullFloat64
		tsrc, psrc     sql.NullString
	}
	row := func(id int64) rowT {
		t.Helper()
		var r rowT
		if err := st.db.QueryRow(`SELECT brew_temp_c, brew_temp_source, preinfusion_s, preinfusion_source, preinfusion_run_s FROM shot WHERE id = ?`, id).
			Scan(&r.temp, &r.tsrc, &r.pre, &r.psrc, &r.run); err != nil {
			t.Fatal(err)
		}
		return r
	}

	// A typed setting to carry: Pr 3, 93 C. Made a v1-shaped row: no
	// setpoint_touched, and a 'display' brew temperature.
	a := ingest(1, plain(0), nil)
	if err := st.UpdateShot(a, map[string]any{"preinfusion_s": 3.0}); err != nil {
		t.Fatal(err)
	}
	if _, err := st.db.Exec(`UPDATE shot SET brew_temp_c = 93, brew_temp_source = 'display', setpoint_touched = NULL WHERE id = ?`, a); err != nil {
		t.Fatal(err)
	}

	b := ingest(2, measured(time.Minute), long)
	if r := row(b); r.pre.Valid || r.psrc.Valid || r.run.Float64 != 5.8 {
		t.Errorf("unmappable run at ingest: %+v; want no setting, run 5.8", r)
	}
	if r := row(b); r.temp.Float64 != 93 || r.tsrc.String != "carried" {
		t.Errorf("v1-shaped 'display' row not carried from: %+v", r)
	}

	// The backfill: a 'carried' setting beside such a run is cleared too.
	if _, err := st.db.Exec(`UPDATE shot SET preinfusion_s = 3, preinfusion_source = 'carried', preinfusion_run_s = NULL WHERE id = ?`, b); err != nil {
		t.Fatal(err)
	}
	if _, err := st.backfillPreinfusion(); err != nil {
		t.Fatal(err)
	}
	if r := row(b); r.pre.Valid || r.psrc.Valid || r.run.Float64 != 5.8 {
		t.Errorf("unmappable run after backfill: %+v; want no setting, run 5.8", r)
	}
}

// A shot pulled while a brew-temperature edit was still blinking (v5
// SETPOINT_PENDING): no brew temperature on it and none carried across it —
// but pre-infusion carries as usual, and a value typed on the pending shot
// carries forward (review, 2026-11-15: pending first rode on
// setpoint_touched, which also cut the pre-infusion carry).
func TestSetpointPendingStopsOnlyTheTemperatureCarry(t *testing.T) {
	st := openTestStore(t)
	dev := "aa:bb:cc:dd:ee:03"
	base := time.Now().Add(-time.Hour).Truncate(time.Second)
	at := func(d time.Duration, flags uint8) func(*record.Header) {
		return func(h *record.Header) {
			h.StartedAtUnixMs = uint64(base.Add(d).UnixMilli())
			h.Flags |= flags
		}
	}
	ingest := func(seq uint32, mut func(*record.Header)) int64 {
		t.Helper()
		raw, rec := pumpRecordWith(t, seq, record.Pump2Cup, 9500, mut, nil)
		res, err := st.Ingest(dev, rec, raw, base)
		if err != nil {
			t.Fatal(err)
		}
		return res.ShotID
	}
	read := func(id int64) (temp, pre sql.NullFloat64, pending sql.NullInt64) {
		t.Helper()
		if err := st.db.QueryRow(`SELECT brew_temp_c, preinfusion_s, setpoint_pending FROM shot WHERE id = ?`, id).Scan(&temp, &pre, &pending); err != nil {
			t.Fatal(err)
		}
		return
	}
	a := ingest(1, at(0, 0))
	if err := st.UpdateShot(a, map[string]any{"brew_temp_c": 93.0, "preinfusion_s": 3.0}); err != nil {
		t.Fatal(err)
	}
	p := ingest(2, at(time.Minute, record.FlagSetpointPending))
	if temp, pre, pending := read(p); temp.Valid || pre.Float64 != 3 || pending.Int64 != 1 {
		t.Errorf("pending shot: temp %v pre %v pending %v; want no temp, pre 3, pending 1", temp, pre, pending)
	}
	n := ingest(3, at(2*time.Minute, 0))
	if temp, pre, pending := read(n); temp.Valid || pre.Float64 != 3 || pending.Int64 != 0 {
		t.Errorf("shot after pending: temp %v pre %v pending %v; want no temp carried across, pre 3", temp, pre, pending)
	}
	if err := st.UpdateShot(p, map[string]any{"brew_temp_c": 96.0}); err != nil {
		t.Fatal(err)
	}
	m := ingest(4, at(3*time.Minute, 0))
	if temp, _, _ := read(m); temp.Float64 != 96 {
		t.Errorf("a value typed on the pending shot did not carry: %v", temp)
	}
}

// Migration 016: v_shot_end tells a settle that ran its full window from one
// a lift ended, from the stored samples alone -- 7 s after a flat stop, 5 s
// after a forced one (stop_ms >= 60000), capped below the t_ms ceiling --
// and says NULL where it cannot, including any detector it does not name.
func TestShotEndView(t *testing.T) {
	st := openTestStore(t)
	for _, c := range []struct {
		name     string
		detector any
		stopMs   int
		lastMs   int // -1: no samples
		wantTail sql.NullInt64
		want     sql.NullInt64
	}{
		{"full settle", "0a.8", 18000, 25000, sql.NullInt64{Int64: 7000, Valid: true}, sql.NullInt64{Int64: 0, Valid: true}},
		{"0a.8 lift 3 s into settling", "0a.8", 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{Int64: 1, Valid: true}},
		{"0a.7 lift (stop_ms overwritten)", "0a.7", 25900, 25900, sql.NullInt64{Int64: 0, Valid: true}, sql.NullInt64{Int64: 1, Valid: true}},
		{"forced stop, full settle", "0a.8", 60000, 65000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{Int64: 0, Valid: true}},
		{"forced stop, lifted", "0a.8", 60000, 61500, sql.NullInt64{Int64: 1500, Valid: true}, sql.NullInt64{Int64: 1, Valid: true}},
		{"0a.5 full settle", "0a.5", 18000, 25100, sql.NullInt64{Int64: 7100, Valid: true}, sql.NullInt64{Int64: 0, Valid: true}},
		{"0a.4: no fixed stop window", "0a.4", 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{}},
		{"no detector version", nil, 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{}},
		{"sub-5 g abort, stop_ms 0", "0a.8", 0, 9000, sql.NullInt64{Int64: 9000, Valid: true}, sql.NullInt64{}},
		{"no samples", "0a.8", 18000, -1, sql.NullInt64{}, sql.NullInt64{}},
		// Review of 3447d49: a settle past t_ms = 65535 stores a short tail.
		{"forced stop after a gap, full settle", "0a.8", 60600, 65500, sql.NullInt64{Int64: 4900, Valid: true}, sql.NullInt64{Int64: 0, Valid: true}},
		{"late flat stop, full settle", "0a.8", 59000, 65500, sql.NullInt64{Int64: 6500, Valid: true}, sql.NullInt64{Int64: 0, Valid: true}},
		{"forced stop after a gap, lifted", "0a.8", 60600, 63000, sql.NullInt64{Int64: 2400, Valid: true}, sql.NullInt64{Int64: 1, Valid: true}},
		// Named versions only: anything else cannot be judged.
		{"0a.8-rc", "0a.8-rc", 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{Int64: 1, Valid: true}},
		{"empty version", "", 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{}},
		{"dev build", "dev", 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{}},
		{"a later detector", "0a.9", 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{}},
		{"0a.50 is not 0a.5", "0a.50", 18000, 23000, sql.NullInt64{Int64: 5000, Valid: true}, sql.NullInt64{}},
	} {
		r, err := st.db.Exec(`INSERT INTO shot (started_at, stop_ms, detector_version, created_at)
			VALUES ('2026-11-23T08:00:00Z', ?, ?, '2026-11-23T08:00:00Z')`, c.stopMs, c.detector)
		if err != nil {
			t.Fatal(err)
		}
		id, _ := r.LastInsertId()
		if c.lastMs >= 0 {
			for _, tms := range []int{0, c.lastMs / 2, c.lastMs} {
				if _, err := st.db.Exec(`INSERT OR IGNORE INTO shot_sample (shot_id, t_ms, weight_mg) VALUES (?,?,0)`,
					id, tms); err != nil {
					t.Fatal(err)
				}
			}
		}
		var tail, lifted sql.NullInt64
		if err := st.db.QueryRow(`SELECT settle_tail_ms, ended_by_lift FROM v_shot_end WHERE shot_id = ?`, id).
			Scan(&tail, &lifted); err != nil {
			t.Fatal(err)
		}
		if tail != c.wantTail || lifted != c.want {
			t.Errorf("%s: tail %v, ended_by_lift %v; want %v, %v", c.name, tail, lifted, c.wantTail, c.want)
		}
	}
}
