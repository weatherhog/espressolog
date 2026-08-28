package record

import (
	"os"
	"testing"
)

// The fixtures are real records captured from the device (fw 0.4.0-stage4,
// the CLI's `fake` command): one written with SNTP synced, one without.
// They pin the wire format — if Decode drifts from spool.h, these fail.

func load(t *testing.T, name string) []byte {
	t.Helper()
	raw, err := os.ReadFile("testdata/" + name)
	if err != nil {
		t.Fatal(err)
	}
	return raw
}

func TestDecodeRealRecordTimeValid(t *testing.T) {
	r, err := Decode(load(t, "shot-fw040-timevalid.bin"))
	if err != nil {
		t.Fatal(err)
	}
	h := &r.Header
	if h.RecordType != TypeShot {
		t.Errorf("type = %d, want shot", h.RecordType)
	}
	if h.BootID != 0xa6d3b063 || h.Seq != 0 {
		t.Errorf("identity = %08x-%d", h.BootID, h.Seq)
	}
	if h.TimeValid != 1 || h.StartedAtUnixMs == 0 {
		t.Errorf("time_valid=%d unix_ms=%d, want synced", h.TimeValid, h.StartedAtUnixMs)
	}
	if h.TzOffsetMin != 120 { // CEST at capture time
		t.Errorf("tz = %d, want 120", h.TzOffsetMin)
	}
	if h.YieldFinalMg != 36800 {
		t.Errorf("yield = %d mg, want 36800", h.YieldFinalMg)
	}
	if h.Detector() != "0a.1" || h.Firmware() != "0.4.0-stage4" {
		t.Errorf("versions = %q / %q", h.Detector(), h.Firmware())
	}
	if h.ScaleMacString() != "aa:bb:cc:dd:ee:01" {
		t.Errorf("scale mac = %s", h.ScaleMacString())
	}
	if len(r.Samples) != 220 {
		t.Fatalf("samples = %d, want 220", len(r.Samples))
	}
	if r.Samples[0].TMs != 0 || r.Samples[219].TMs != 21900 {
		t.Errorf("sample times = %d..%d", r.Samples[0].TMs, r.Samples[219].TMs)
	}
	if r.Samples[219].TempDc != TempNone {
		t.Errorf("temp = %d, want none", r.Samples[219].TempDc)
	}
}

func TestDecodeRealRecordNoTime(t *testing.T) {
	r, err := Decode(load(t, "shot-fw040-notime.bin"))
	if err != nil {
		t.Fatal(err)
	}
	if r.Header.TimeValid != 0 || r.Header.StartedAtUnixMs != 0 {
		t.Errorf("expected unsynced record, got time_valid=%d unix_ms=%d",
			r.Header.TimeValid, r.Header.StartedAtUnixMs)
	}
}

func TestDecodeRejectsCorruption(t *testing.T) {
	raw := load(t, "shot-fw040-timevalid.bin")

	flipped := append([]byte(nil), raw...)
	flipped[200] ^= 0xFF // a sample byte
	if _, err := Decode(flipped); err == nil {
		t.Error("corrupted sample passed CRC")
	}

	if _, err := Decode(raw[:80]); err == nil {
		t.Error("truncated header accepted")
	}
	if _, err := Decode(raw[:500]); err == nil {
		t.Error("truncated samples accepted")
	}
}
