package store

import (
	"bytes"
	"database/sql"
	"encoding/binary"
	"hash/crc32"
	"testing"
	"time"

	"espressolog/internal/record"
)

// defaultRecipe carries brew_temp_c and preinfusion_s onto a new shot, because
// the capture screen stopped writing them once it was fixed to send only what
// the operator touched. That carry is an inference, and these tests pin the two
// bounds on it — both of which were wrong when first written.
//
// The guard was `id > (SELECT MAX(id) ... setpoint_touched = 1)`. `id` is
// INGEST order and `started_at` is PULL order, and they diverge as documented
// normal behaviour: the spool uploads in filename order, "across boots order is
// arbitrary", and a WiFi outage delivers a backlog out of sequence. It was also
// `>` rather than `>=`, which excluded the edit shot itself — the one row that
// holds the NEW setting.

// shotAt builds a v2 shot record pulled at a given time. v2BuildRecord in
// store_test.go hardcodes now() and seq 1, and these tests need control of both.
func shotAt(t *testing.T, seq uint32, at time.Time, touched bool) (*record.Record, time.Time) {
	t.Helper()
	h := record.Header{
		Magic: record.Magic, FormatVersion: 2, RecordType: record.TypeShot,
		HeaderLen: 113, BootID: 0xfeed0002, Seq: seq,
		StartedAtUnixMs: uint64(at.UnixMilli()), TimeValid: 1,
		StopMs: 21000, YieldAtStopMg: 35800, YieldFinalMg: 36000,
		PeakFlowMgps: 2100, MeanFlowMgps: 1710,
		FlowWinStartMs: 3000, FlowWinEndMs: 18000,
		BoilerTempStartDc: record.TempNone, SampleCount: 1,
	}
	if touched {
		h.Flags |= record.FlagSetpointTouched
	}
	copy(h.DetectorVersion[:], "0a.6")
	copy(h.FirmwareVersion[:], "0.9.3")

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
	for i := 113 - 4; i < 113; i++ {
		raw[i] = 0
	}
	binary.LittleEndian.PutUint32(raw[113-4:], crc32.ChecksumIEEE(raw))
	rec, err := record.Decode(raw)
	if err != nil {
		t.Fatalf("built record does not decode: %v", err)
	}
	return rec, at
}

func ingestAt(t *testing.T, st *Store, seq uint32, at time.Time, touched bool) int64 {
	t.Helper()
	rec, _ := shotAt(t, seq, at, touched)
	res, err := st.Ingest("aa:bb:cc:dd:ee:09", rec, []byte{2}, at)
	if err != nil {
		t.Fatal(err)
	}
	return res.ShotID
}

func setRecipe(t *testing.T, st *Store, id int64, temp, preinf float64) {
	t.Helper()
	if err := st.UpdateShot(id, map[string]any{
		"brew_temp_c": temp, "preinfusion_s": preinf,
	}); err != nil {
		t.Fatal(err)
	}
}

func recipeOf(t *testing.T, st *Store, id int64) (sql.NullFloat64, sql.NullFloat64) {
	t.Helper()
	var temp, pre sql.NullFloat64
	if err := st.db.QueryRow(
		`SELECT brew_temp_c, preinfusion_s FROM shot WHERE id = ?`, id).
		Scan(&temp, &pre); err != nil {
		t.Fatal(err)
	}
	return temp, pre
}

// A backlog uploaded after a reboot arrives out of chronological order, so the
// shot with the HIGHEST id can be the one pulled EARLIEST. Bounding the carry
// by id then admits a pre-edit setting and carries it straight across the edit.
func TestRecipeCarryUsesPullOrderNotIngestOrder(t *testing.T) {
	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	// Ingested FIRST (low id), pulled SECOND: the setpoint edit.
	edit := ingestAt(t, st, 1, base, true)
	setRecipe(t, st, edit, 95, 5)

	// Ingested SECOND (high id), pulled FIRST: a pre-edit shot from the backlog.
	old := ingestAt(t, st, 2, base.Add(-24*time.Hour), false)
	setRecipe(t, st, old, 93, 0)

	next := ingestAt(t, st, 3, base.Add(24*time.Hour), false)
	temp, pre := recipeOf(t, st, next)

	if !temp.Valid || temp.Float64 != 95 {
		t.Errorf("brew_temp_c = %v, want 95: a pre-edit setpoint was carried across "+
			"a recorded edit because the guard compared id (ingest order) instead of "+
			"started_at (pull order)", temp)
	}
	if !pre.Valid || pre.Float64 != 5 {
		t.Errorf("preinfusion_s = %v, want 5 (same bug, same cause)", pre)
	}
}

// The edit shot is the only row holding the NEW setting. Excluding it leaves
// nothing downstream with a value, so the column goes NULL and stays NULL on
// every later shot forever.
func TestRecipeCarryIncludesTheEditShotItself(t *testing.T) {
	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	before := ingestAt(t, st, 1, base.Add(-24*time.Hour), false)
	setRecipe(t, st, before, 93, 0)

	edit := ingestAt(t, st, 2, base, true)
	setRecipe(t, st, edit, 95, 5)

	next := ingestAt(t, st, 3, base.Add(24*time.Hour), false)
	temp, _ := recipeOf(t, st, next)

	if !temp.Valid {
		t.Fatal("brew_temp_c is NULL: a strict `>` excluded the edit shot, which is " +
			"the only row with the new setpoint — so the column empties permanently")
	}
	if temp.Float64 != 95 {
		t.Errorf("brew_temp_c = %v, want 95 (the post-edit value)", temp.Float64)
	}
}

// setpoint_touched latches on the display blanking, and CLAUDE.md's SET UP menu
// edits pre-infusion with the same gesture. Exempting preinfusion_s carried it
// across exactly the event the guard exists to stop.
//
// THE EDIT SHOT MUST HOLD NO PREINFUSION VALUE for this to discriminate. A
// first version of this test gave the edit shot a value, so it was also the
// most recent row and the guard never changed the answer — the test passed
// with the guard removed. That is the defect this repo documents about its own
// suite, reproduced in the test written to prevent it.
func TestRecipeCarryBoundsPreinfusionByTheSameEdit(t *testing.T) {
	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	// Pre-edit shot: the only row with a preinfusion value.
	before := ingestAt(t, st, 1, base.Add(-24*time.Hour), false)
	if err := st.UpdateShot(before, map[string]any{"preinfusion_s": 0.0}); err != nil {
		t.Fatal(err)
	}

	// The edit itself records no preinfusion, so the only candidate for a
	// carry is the pre-edit row — which the guard must refuse.
	ingestAt(t, st, 2, base, true)

	next := ingestAt(t, st, 3, base.Add(24*time.Hour), false)
	_, pre := recipeOf(t, st, next)

	if pre.Valid {
		t.Errorf("preinfusion_s = %v, want NULL: a pre-edit value was carried across "+
			"a recorded parameter edit. setpoint_touched is evidence of an edit in "+
			"general — the SET UP menu adjusts pre-infusion with the same gesture — "+
			"so the guard must bound this column too, not brew_temp_c only", pre.Float64)
	}
}

// The mirror of the above for brew_temp_c: with no post-edit value anywhere,
// the column must empty rather than report the superseded setpoint.
func TestRecipeCarryRefusesAPreEditTemperature(t *testing.T) {
	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	before := ingestAt(t, st, 1, base.Add(-24*time.Hour), false)
	if err := st.UpdateShot(before, map[string]any{"brew_temp_c": 93.0}); err != nil {
		t.Fatal(err)
	}
	ingestAt(t, st, 2, base, true) // the edit, no value recorded

	next := ingestAt(t, st, 3, base.Add(24*time.Hour), false)
	temp, _ := recipeOf(t, st, next)

	if temp.Valid {
		t.Errorf("brew_temp_c = %v, want NULL: the machine said the setpoint was "+
			"being changed and this carried the old one across it", temp.Float64)
	}
}

// A long silence must degrade to NULL rather than assert a fortnight-old
// setting as current.
func TestRecipeCarryExpires(t *testing.T) {
	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	old := ingestAt(t, st, 1, base.Add(-20*24*time.Hour), false)
	setRecipe(t, st, old, 93, 0)

	next := ingestAt(t, st, 2, base, false)
	temp, pre := recipeOf(t, st, next)

	if temp.Valid {
		t.Errorf("brew_temp_c = %v after a 20-day gap, want NULL", temp.Float64)
	}
	if pre.Valid {
		t.Errorf("preinfusion_s = %v after a 20-day gap, want NULL", pre.Float64)
	}
}

// And a recent one must still carry, or the bound is simply breaking the feature.
func TestRecipeCarryWorksWithinTheWindow(t *testing.T) {
	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	old := ingestAt(t, st, 1, base.Add(-13*24*time.Hour), false)
	setRecipe(t, st, old, 93, 0)

	next := ingestAt(t, st, 2, base, false)
	temp, pre := recipeOf(t, st, next)

	if !temp.Valid || temp.Float64 != 93 {
		t.Errorf("brew_temp_c = %v at 13 days, want 93 (inside the 14-day window)", temp)
	}
	if !pre.Valid || pre.Float64 != 0 {
		t.Errorf("preinfusion_s = %v at 13 days, want 0", pre)
	}
}

// THE RECOVERY PATH. A review recommended fixing the carry by reverting this
// bound to a strict `>`, which does stop the stale value propagating — but it
// also discards a human confirming the NEW setpoint on the edit shot itself,
// measured: 96 confirmed, NULL carried. Gating the carry at ingest (above) is
// what fixes both halves. This test exists so that recommendation cannot be
// applied later without the regression showing up.
func TestRecipeCarryHonoursAConfirmationOnTheEditShot(t *testing.T) {
	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	before := ingestAt(t, st, 1, base.Add(-24*time.Hour), false)
	setRecipe(t, st, before, 93, 0)

	edit := ingestAt(t, st, 2, base, true)
	// The operator sees the edit and records the new setpoint on that shot.
	if err := st.UpdateShot(edit, map[string]any{"brew_temp_c": 96.0}); err != nil {
		t.Fatal(err)
	}

	next := ingestAt(t, st, 3, base.Add(24*time.Hour), false)
	temp, _ := recipeOf(t, st, next)

	if !temp.Valid || temp.Float64 != 96 {
		t.Errorf("brew_temp_c = %v, want 96: a confirmation made ON the edit shot "+
			"was discarded. A strict `>` on the edit bound does this; the fix is to "+
			"gate the carry at ingest, not to exclude the row", temp)
	}
}

// KNOWN GAP, documented rather than hidden. "Across boots order is arbitrary"
// cuts both ways: the guard handles a shot pulled BEFORE an edit arriving
// after it, but not a shot pulled AFTER an edit arriving BEFORE it. The carry
// is stamped at ingest and nothing re-evaluates it, so the pre-edit value
// lands on that row and then propagates forward through it for ever.
//
// This test asserts the CORRECT behaviour and is skipped. Closing the gap
// (see defaultRecipe's comment: read-time carry, or *_source columns) makes it
// pass, at which point delete the Skip.
func TestRecipeCarryKnownGapOnReversedArrival(t *testing.T) {
	t.Skip("known gap: the carry is stamped at ingest and cannot be retroactively " +
		"unwritten without a provenance column — see defaultRecipe")

	st := openTestStore(t)
	base := time.Date(2026, 10, 2, 8, 0, 0, 0, time.UTC)

	// A pre-edit shot, confirmed by hand.
	p := ingestAt(t, st, 1, base.Add(-48*time.Hour), false)
	setRecipe(t, st, p, 93, 0)

	// Pulled AFTER the edit, but ingested BEFORE it (the backlog arrives out
	// of order). No edit row exists yet, so this inherits 93.
	ingestAt(t, st, 2, base.Add(24*time.Hour), false)

	// The edit record finally arrives. Its own row is correctly NULLed.
	ingestAt(t, st, 3, base, true)

	// But the shot above still holds 93 at started_at > the edit, so this one
	// finds it and carries the superseded setpoint forward.
	next := ingestAt(t, st, 4, base.Add(48*time.Hour), false)
	temp, _ := recipeOf(t, st, next)

	if temp.Valid {
		t.Errorf("brew_temp_c = %v, want NULL: a pre-edit setpoint reached a shot "+
			"pulled after the edit, via a row ingested before it", temp.Float64)
	}
}
