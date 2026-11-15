package record

import (
	"bytes"
	"encoding/binary"
	"hash/crc32"
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

// buildV2 assembles a format-2 record the way the firmware does: header
// with crc zeroed, CRC over header+samples, crc written back.
func buildV2(t *testing.T, mutate func(*headerV2)) []byte {
	t.Helper()
	h := headerV2{
		Magic: Magic, FormatVersion: 2, RecordType: TypeShot, HeaderLen: headerLenV2,
		BootID: 0x11223344, Seq: 7, StartedAtMillis: 1000, TimeValid: 0,
		StopMs: 21000, YieldFinalMg: 36000, SampleCount: 2,
		BoilerTempStartDc: 952, MachineTimerDl: 210,
		Flags: FlagSetpointTouched,
	}
	copy(h.DetectorVersion[:], "0a.6")
	copy(h.FirmwareVersion[:], "0.8.0")
	if mutate != nil {
		mutate(&h)
	}
	samples := []Sample{
		{TMs: 0, WeightMg: 0, InletPulses: 0, TempDc: TempNone},
		{TMs: 100, WeightMg: 1500, InletPulses: 0, TempDc: 931},
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, &h); err != nil {
		t.Fatal(err)
	}
	if err := binary.Write(&buf, binary.LittleEndian, samples); err != nil {
		t.Fatal(err)
	}
	raw := buf.Bytes()
	if len(raw) != headerLenV2+2*sampleLen {
		t.Fatalf("built %d bytes, want %d", len(raw), headerLenV2+2*sampleLen)
	}
	for i := headerLenV2 - 4; i < headerLenV2; i++ {
		raw[i] = 0
	}
	binary.LittleEndian.PutUint32(raw[headerLenV2-4:], crc32.ChecksumIEEE(raw))
	return raw
}

// The v2 header is 113 bytes: v1's 109 plus boiler_temp_start_dc and
// machine_timer_dl; v3 is 118, adding pump_line and pump_lead_ms. If any of
// these drifts from spool.h the device and the server disagree about every
// field after the first one that moved.
func TestHeaderLengths(t *testing.T) {
	if got := binary.Size(Header{}); got != headerLenV4 {
		t.Fatalf("binary.Size(Header) = %d, want %d", got, headerLenV4)
	}
	if got := binary.Size(headerV3{}); got != headerLenV3 {
		t.Fatalf("binary.Size(headerV3) = %d, want %d", got, headerLenV3)
	}
	if got := binary.Size(headerV2{}); got != headerLenV2 {
		t.Fatalf("binary.Size(headerV2) = %d, want %d", got, headerLenV2)
	}
	if got := binary.Size(headerV1{}); got != headerLenV1 {
		t.Fatalf("binary.Size(headerV1) = %d, want %d", got, headerLenV1)
	}
}

func TestDecodeV2MachineFields(t *testing.T) {
	r, err := Decode(buildV2(t, nil))
	if err != nil {
		t.Fatal(err)
	}
	h := &r.Header
	if h.FormatVersion != 2 {
		t.Errorf("version = %d, want 2", h.FormatVersion)
	}
	if c, ok := h.BoilerTempStartC(); !ok || c != 95 {
		t.Errorf("boiler temp = %d (ok=%v), want 95 true", c, ok)
	}
	if h.MachineTimerDl != 210 {
		t.Errorf("machine timer = %d, want 210", h.MachineTimerDl)
	}
	if !h.SetpointTouched() {
		t.Error("setpoint-touched flag lost")
	}
	if r.Samples[1].TempDc != 931 {
		t.Errorf("sample temp = %d, want 931", r.Samples[1].TempDc)
	}
}

// buildV3 assembles a format-3 record — what firmware 0.10.0 writes.
func buildV3(t *testing.T, mutate func(*headerV3)) []byte {
	t.Helper()
	h := headerV3{
		Magic: Magic, FormatVersion: 3, RecordType: TypeShot, HeaderLen: headerLenV3,
		BootID: 0x11223344, Seq: 8, StartedAtMillis: 1000,
		StopMs: 21000, YieldFinalMg: 36000, SampleCount: 2,
		BoilerTempStartDc: 950, MachineTimerDl: 288,
		PumpLine: Pump2Cup, PumpLeadMs: 8200,
		Flags: FlagFlowTap,
	}
	copy(h.DetectorVersion[:], "0a.6")
	copy(h.FirmwareVersion[:], "0.10.0")
	if mutate != nil {
		mutate(&h)
	}
	samples := []Sample{
		{TMs: 0, WeightMg: 0, InletPulses: 0, TempDc: TempNone},
		{TMs: 100, WeightMg: 1500, InletPulses: 3, TempDc: TempNone},
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, &h); err != nil {
		t.Fatal(err)
	}
	if err := binary.Write(&buf, binary.LittleEndian, samples); err != nil {
		t.Fatal(err)
	}
	raw := buf.Bytes()
	if len(raw) != headerLenV3+2*sampleLen {
		t.Fatalf("built %d bytes, want %d", len(raw), headerLenV3+2*sampleLen)
	}
	binary.LittleEndian.PutUint32(raw[headerLenV3-4:], crc32.ChecksumIEEE(raw))
	return raw
}

func TestDecodeV3PumpStart(t *testing.T) {
	r, err := Decode(buildV3(t, nil))
	if err != nil {
		t.Fatal(err)
	}
	h := &r.Header
	if line, lead, ok := h.PumpStart(); !ok || line != Pump2Cup || lead != 8200 {
		t.Errorf("pump start = %d %d %v, want 2Cup 8200 true", line, lead, ok)
	}
	// The v2 fields survive the insertion, and so do the fields after it.
	if h.MachineTimerDl != 288 || h.YieldFinalMg != 36000 || h.SampleCount != 2 {
		t.Errorf("fields around the insertion moved: timer=%d yield=%d samples=%d",
			h.MachineTimerDl, h.YieldFinalMg, h.SampleCount)
	}
	if r.Samples[1].InletPulses != 3 {
		t.Errorf("sample pulses = %d, want 3", r.Samples[1].InletPulses)
	}

	// No press seen: "not known", whichever field says so.
	for _, m := range []func(*headerV3){
		func(h *headerV3) { h.PumpLine = PumpNone },
		func(h *headerV3) { h.PumpLeadMs = PumpLeadNone },
	} {
		r, err := Decode(buildV3(t, m))
		if err != nil {
			t.Fatal(err)
		}
		if _, _, ok := r.Header.PumpStart(); ok {
			t.Error("a record with no press claims a pump start")
		}
	}
	// v3 predates the pulse train.
	if _, _, _, ok := r.Header.PrePulses(); ok {
		t.Error("v3 record claims pre-drip pulses")
	}
}

// buildV4 assembles a format-4 record — what firmware 0.11.0 writes.
func buildV4(t *testing.T, mutate func(*Header)) []byte {
	t.Helper()
	h := Header{
		Magic: Magic, FormatVersion: 4, RecordType: TypeShot, HeaderLen: headerLenV4,
		BootID: 0x11223344, Seq: 9, StartedAtMillis: 1000,
		StopMs: 21000, YieldFinalMg: 36000, SampleCount: 2,
		BoilerTempStartDc: 950, MachineTimerDl: 288,
		PumpLine: Pump2Cup, PumpLeadMs: 8200,
		PrePulseTotal: 3, PrePulseFlags: PrePulsesComplete, PrePulseCount: 3,
		Flags: FlagFlowTap,
	}
	h.PrePulseMs[0], h.PrePulseMs[1], h.PrePulseMs[2] = 200, 300, 5000
	copy(h.DetectorVersion[:], "0a.6")
	copy(h.FirmwareVersion[:], "0.11.0")
	if mutate != nil {
		mutate(&h)
	}
	samples := []Sample{
		{TMs: 0, WeightMg: 0, InletPulses: 0, TempDc: TempNone},
		{TMs: 100, WeightMg: 1500, InletPulses: 3, TempDc: TempNone},
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, &h); err != nil {
		t.Fatal(err)
	}
	if err := binary.Write(&buf, binary.LittleEndian, samples); err != nil {
		t.Fatal(err)
	}
	raw := buf.Bytes()
	if len(raw) != headerLenV4+2*sampleLen {
		t.Fatalf("built %d bytes, want %d", len(raw), headerLenV4+2*sampleLen)
	}
	binary.LittleEndian.PutUint32(raw[headerLenV4-4:], crc32.ChecksumIEEE(raw))
	return raw
}

func TestDecodeV4PrePulses(t *testing.T) {
	r, err := Decode(buildV4(t, nil))
	if err != nil {
		t.Fatal(err)
	}
	ms, total, complete, ok := r.Header.PrePulses()
	if !ok || total != 3 || !complete || len(ms) != 3 || ms[2] != 5000 {
		t.Errorf("pre pulses = %v %d %v %v, want [200 300 5000] 3 true true", ms, total, complete, ok)
	}
	// The fields on both sides of the 517-byte block survive it.
	if line, lead, ok := r.Header.PumpStart(); !ok || line != Pump2Cup || lead != 8200 {
		t.Errorf("pump start moved: %d %d %v", line, lead, ok)
	}
	if r.Header.YieldFinalMg != 36000 || r.Header.SampleCount != 2 || r.Samples[1].InletPulses != 3 {
		t.Error("fields after the pulse block moved")
	}
	// Not captured, and inconsistent fields, both read as "not known".
	for _, m := range []func(*Header){
		func(h *Header) { h.PrePulseTotal = PrePulsesNone },
		func(h *Header) { h.PrePulseCount = 300 },
		func(h *Header) { h.PrePulseCount = 5 }, // more stored than in the window
	} {
		r, err := Decode(buildV4(t, m))
		if err != nil {
			t.Fatal(err)
		}
		if _, _, _, ok := r.Header.PrePulses(); ok {
			t.Error("an uncaptured or inconsistent record claims pre-drip pulses")
		}
	}
}

// A v2 record must still decode, its v2 fields intact and the v3 ones "not
// known" — v2 records exist spooled on the board (invariant 6).
func TestDecodeV2ReportsNoPumpStart(t *testing.T) {
	r, err := Decode(buildV2(t, nil))
	if err != nil {
		t.Fatal(err)
	}
	if _, _, ok := r.Header.PumpStart(); ok {
		t.Error("v2 record claims a pump start")
	}
	if r.Header.PumpLeadMs != PumpLeadNone {
		t.Errorf("v2 record's pump lead = %d, want PumpLeadNone", r.Header.PumpLeadMs)
	}
}

// A v1 record must still decode, and must report the v2 fields as "the
// machine did not say" rather than as zero degrees. Losing an old shot to
// a format bump would violate invariant 6.
func TestDecodeV1StillWorksAndReportsNoMachineData(t *testing.T) {
	for _, name := range []string{"shot-fw040-timevalid.bin", "shot-fw040-notime.bin"} {
		r, err := Decode(load(t, name))
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if r.Header.FormatVersion != 1 {
			t.Fatalf("%s: version = %d", name, r.Header.FormatVersion)
		}
		if _, ok := r.Header.BoilerTempStartC(); ok {
			t.Errorf("%s: v1 record claims a boiler temperature", name)
		}
		if r.Header.MachineTimerDl != 0 {
			t.Errorf("%s: v1 record claims a machine timer", name)
		}
		if r.Header.SetpointTouched() {
			t.Errorf("%s: v1 record claims the setpoint was touched", name)
		}
		if _, _, ok := r.Header.PumpStart(); ok {
			t.Errorf("%s: v1 record claims a pump start", name)
		}
	}
}

// An unknown version must be refused, not guessed at.
func TestDecodeRejectsUnknownVersion(t *testing.T) {
	raw := buildV2(t, nil)
	raw[4] = 99
	if _, err := Decode(raw); err == nil {
		t.Fatal("accepted format version 99")
	}
}

// A v2 header claiming v1's length is corrupt, not a v1 record.
func TestDecodeRejectsVersionLengthMismatch(t *testing.T) {
	raw := buildV2(t, func(h *headerV2) { h.HeaderLen = headerLenV1 })
	if _, err := Decode(raw); err == nil {
		t.Fatal("accepted v2 record declaring a v1 header length")
	}
}
