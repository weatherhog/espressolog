// Package record decodes the firmware's binary spool records — the exact
// bytes the ESP32 writes to flash and POSTs verbatim (see
// firmware/src/spool.h, which is the authoritative definition).
package record

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"
)

// ErrNewerFormat marks a record that looks well-formed but was written in a
// format version this server does not know yet — firmware flashed before the
// server was deployed. It is NOT a verdict on the record: the ingest handler
// answers it with a retryable status, so the device keeps the shot until the
// server catches up, instead of quarantining it (invariant 6). Until
// 2026-11-14 this was a plain error and became a 422, which the device
// treats as a one-way door into /spool/bad.
var ErrNewerFormat = errors.New("record format newer than this server")

// maxPlausibleFormat bounds what counts as "newer" rather than garbage: a
// corrupt version byte of 200 is not a future firmware.
const maxPlausibleFormat = 16

// newerLooksIntact says whether a record in an unknown (newer) format is
// nonetheless intact, using only what every version shares: the 8-byte
// prelude's header_len, a crc32 as the header's LAST field, and 10-byte
// samples after it. Without this, one flipped bit in a v3 record's version
// byte (3 -> 7 or 11) made a CORRUPT record retryable for ever, and since the
// device uploads oldest-first it would stall every shot behind it (review,
// 2026-11-15). A record that fails this is corruption, and gets the 422 the
// device quarantines.
func newerLooksIntact(raw []byte) bool {
	if len(raw) < 8 {
		return false
	}
	hl := int(binary.LittleEndian.Uint16(raw[6:8]))
	if hl < headerLenV1 || len(raw) < hl || (len(raw)-hl)%sampleLen != 0 {
		return false
	}
	want := binary.LittleEndian.Uint32(raw[hl-4 : hl])
	c := make([]byte, len(raw))
	copy(c, raw)
	for i := hl - 4; i < hl; i++ {
		c[i] = 0
	}
	return crc32.ChecksumIEEE(c) == want
}

const (
	Magic = 0x4C505345 // 'ESPL'
	// FormatVersion is what the firmware writes TODAY. Decode accepts every
	// version in headerLens, because v1 records exist both spooled on
	// devices and as fixtures, and a format bump must never strand a shot
	// (invariant 6).
	FormatVersion = 4

	TypeShot     = 1
	TypeWeighing = 2

	FlagFault     = 0x01
	FlagTruncated = 0x02
	FlagReject    = 0x04 // detected but <5 g; stored for visibility, excluded
	// v2. Someone entered temperature programming or edited a value while
	// this shot was recorded. Latched by the firmware, because PrG is a
	// transient banner and the blink stops the moment they walk away.
	FlagSetpointTouched = 0x08
	// FlagFlowTap (v2, 0d): the flowmeter tap was live, so each sample's
	// InletPulses is a measurement. Absent, the field is the placeholder 0
	// it has carried since 0a and means "nothing was counting" — which is
	// not the same fact as "no water moved", and must not reach the archive
	// looking like it.
	FlagFlowTap = 0x10

	headerLenV1 = 109
	headerLenV2 = 113 // v1 + boiler_temp_start_dc (int16) + machine_timer_dl (uint16)
	headerLenV3 = 118 // v2 + pump_line (uint8) + pump_lead_ms (uint32)
	headerLenV4 = 123 // v3 + pre_pulse_total (u16) + flags (u8) + count (u16); offsets in a tail

	// PrePulsesNone is pre_pulse_total's "not captured".
	PrePulsesNone = 0xFFFF
	// PrePulsesComplete (pre_pulse_flags bit 0): the firmware's pulse log
	// lost none of the press-to-drip window, so the total is exact.
	PrePulsesComplete = 0x01
	// PrePulsesMax is the most offsets a v4 tail carries.
	PrePulsesMax = 256
	sampleLen    = 10

	// PumpLeadNone is pump_lead_ms's "not known": switches off, no press in
	// the window, or a pre-v3 record.
	PumpLeadNone = 0xFFFFFFFF

	// PumpLine values, as spool.h writes them (machine_context.h PumpLine).
	PumpNone  = 0
	Pump1Cup  = 1
	Pump2Cup  = 2
	PumpWater = 3

	// TempNone is INT16_MIN in the firmware: "no reading".
	TempNone = -32768
)

// headerLens maps a format version to its on-wire header size.
var headerLens = map[uint8]int{1: headerLenV1, 2: headerLenV2, 3: headerLenV3, 4: headerLenV4}

// headerV1 is the v1 layout, kept verbatim so old records still decode.
// v2 inserted two fields in the middle, so the two cannot share a struct.
type headerV1 struct {
	Magic           uint32
	FormatVersion   uint8
	RecordType      uint8
	HeaderLen       uint16
	BootID          uint32
	Seq             uint32
	StartedAtUnixMs uint64
	StartedAtMillis uint32
	TzOffsetMin     int16
	TimeValid       uint8
	ScaleRole       uint8
	ScaleMac        [6]byte
	Flags           uint8
	DetectorVersion [8]byte
	FirmwareVersion [16]byte
	StopMs          uint32
	YieldAtStopMg   int32
	YieldFinalMg    int32
	SettleOffsetMg  int32
	PeakFlowMgps    int32
	MeanFlowMgps    int32
	FlowWinStartMs  uint32
	FlowWinEndMs    uint32
	GramsMg         int32
	StableMs        uint32
	SampleCount     uint16
	Crc32           uint32
}

// headerV2 is the v2 layout, kept verbatim so v2 records still decode. v3
// inserted two fields after machine_timer_dl, so the two cannot share a
// struct.
type headerV2 struct {
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
}

// headerV3 is the v3 layout, kept verbatim so v3 records still decode. v4
// inserted the pre-drip pulse block after pump_lead_ms.
type headerV3 struct {
	Magic           uint32
	FormatVersion   uint8
	RecordType      uint8
	HeaderLen       uint16
	BootID          uint32
	Seq             uint32
	StartedAtUnixMs uint64
	StartedAtMillis uint32
	TzOffsetMin     int16
	TimeValid       uint8
	ScaleRole       uint8
	ScaleMac        [6]byte
	Flags           uint8
	DetectorVersion [8]byte
	FirmwareVersion [16]byte
	StopMs          uint32
	YieldAtStopMg   int32
	YieldFinalMg    int32
	SettleOffsetMg  int32
	PeakFlowMgps    int32
	MeanFlowMgps    int32
	FlowWinStartMs  uint32
	FlowWinEndMs    uint32
	// v2. BoilerTempStartDc is TempNone and MachineTimerDl is 0 on a v1
	// record — the same values the firmware writes when the display bus is
	// off, so a consumer needs no version check of its own.
	BoilerTempStartDc int16
	MachineTimerDl    uint16
	// v3. PumpLine is PumpNone and PumpLeadMs is PumpLeadNone on a v1 or v2
	// record — the same values the firmware writes when it saw no press.
	PumpLine    uint8
	PumpLeadMs  uint32
	GramsMg     int32
	StableMs    uint32
	SampleCount uint16
	Crc32       uint32
}

// Header mirrors the CURRENT spool_header_t field-for-field, and is also the
// canonical in-memory form: an older record decodes into it with the fields
// it predates left at their "not available" values. binary.Read consumes it
// sequentially, so Go struct alignment never enters into it.
type Header struct {
	Magic           uint32
	FormatVersion   uint8
	RecordType      uint8
	HeaderLen       uint16
	BootID          uint32
	Seq             uint32
	StartedAtUnixMs uint64
	StartedAtMillis uint32
	TzOffsetMin     int16
	TimeValid       uint8
	ScaleRole       uint8
	ScaleMac        [6]byte
	Flags           uint8
	DetectorVersion [8]byte
	FirmwareVersion [16]byte
	StopMs          uint32
	YieldAtStopMg   int32
	YieldFinalMg    int32
	SettleOffsetMg  int32
	PeakFlowMgps    int32
	MeanFlowMgps    int32
	FlowWinStartMs  uint32
	FlowWinEndMs    uint32
	// v2. BoilerTempStartDc is TempNone and MachineTimerDl is 0 on a v1
	// record — the same values the firmware writes when the display bus is
	// off, so a consumer needs no version check of its own.
	BoilerTempStartDc int16
	MachineTimerDl    uint16
	// v3. PumpLine is PumpNone and PumpLeadMs is PumpLeadNone on a v1 or v2
	// record — the same values the firmware writes when it saw no press.
	PumpLine   uint8
	PumpLeadMs uint32
	// v4. PrePulseTotal is PrePulsesNone on an older record — "not
	// captured", the same thing a v4 record says with no press or the flow
	// tap off.
	PrePulseTotal uint16
	PrePulseFlags uint8
	PrePulseCount uint16 // offsets in the record's tail, after the samples
	GramsMg       int32
	StableMs      uint32
	SampleCount   uint16
	Crc32         uint32
}

// Sample mirrors sample_t.
type Sample struct {
	TMs         uint16
	WeightMg    int32
	InletPulses uint16
	TempDc      int16
}

type Record struct {
	Header  Header
	Samples []Sample
	// v4: the pulse offsets from the record's tail (ms after the press),
	// PrePulseCount of them, padding dropped. Empty for older records.
	PrePulseMs []uint16
}

// preTailBytes is the v4 tail's size for count offsets: whole groups of 5
// uint16 (10 bytes), padded with 0xFFFF — so a record still ends on a
// 10-byte boundary after its header, as every format's does.
func preTailBytes(count int) int { return (count + 4) / 5 * 10 }

func cstr(b []byte) string {
	if i := bytes.IndexByte(b, 0); i >= 0 {
		b = b[:i]
	}
	return string(b)
}

func (h *Header) Detector() string { return cstr(h.DetectorVersion[:]) }
func (h *Header) Firmware() string { return cstr(h.FirmwareVersion[:]) }

// ScaleMacString renders the BLE address the way equipment.ble_address
// stores it: lowercase, colon-separated.
func (h *Header) ScaleMacString() string {
	m := h.ScaleMac
	return fmt.Sprintf("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5])
}

func (h *Header) Fault() bool           { return h.Flags&FlagFault != 0 }
func (h *Header) Truncated() bool       { return h.Flags&FlagTruncated != 0 }
func (h *Header) SetpointTouched() bool { return h.Flags&FlagSetpointTouched != 0 }
func (h *Header) FlowTap() bool         { return h.Flags&FlagFlowTap != 0 }

// BoilerTempStartC returns the boiler temperature just before the pump
// started, in whole degrees, and false when the machine did not say — a v1
// record, the display bus off, or no fresh reading. NOT the PID setpoint.
func (h *Header) BoilerTempStartC() (int, bool) {
	if h.BoilerTempStartDc == TempNone {
		return 0, false
	}
	return int(h.BoilerTempStartDc) / 10, true
}
func (h *Header) Reject() bool { return h.Flags&FlagReject != 0 }

// PrePulses returns the flowmeter pulse train from the lever press to pour
// confirmation — ms after the press, oldest first — with how many pulses
// lay in that window in all (more than len(ms) if the firmware's 256-entry
// array filled) and whether none of them was lost. ok is false when not
// captured (no press, flow tap off, a pre-v4 record), when the record does
// not claim a live flow tap, or when the fields are inconsistent — none of
// which any firmware writes as a measurement.
func (r *Record) PrePulses() (ms []uint16, total int, complete bool, ok bool) {
	h := &r.Header
	if h.PrePulseTotal == PrePulsesNone || !h.FlowTap() ||
		int(h.PrePulseCount) > PrePulsesMax || h.PrePulseCount > h.PrePulseTotal ||
		len(r.PrePulseMs) != int(h.PrePulseCount) {
		return nil, 0, false, false
	}
	return r.PrePulseMs, int(h.PrePulseTotal), h.PrePulseFlags&PrePulsesComplete != 0, true
}

// PumpLeadMaxMs mirrors the firmware's PUMP_LEAD_MAX_MS window: a lead
// beyond it is not a press the firmware would have attributed.
const PumpLeadMaxMs = 60000

// PumpStart returns the lever press that started the pump for this shot and
// its lead, in ms, over the detector's t=0 (first drip) — and false when the
// record does not know: switches off, no press within the firmware's
// window, a pre-v3 record, or values no firmware writes (a line outside
// 1-3, a lead beyond the window). The CRC makes those a firmware bug rather
// than corruption; they are refused rather than stored.
func (h *Header) PumpStart() (line uint8, leadMs uint32, ok bool) {
	if h.PumpLine < Pump1Cup || h.PumpLine > PumpWater || h.PumpLeadMs > PumpLeadMaxMs {
		return PumpNone, 0, false
	}
	return h.PumpLine, h.PumpLeadMs, true
}

// Decode parses and validates one complete record, in any format version
// the firmware has ever written. The CRC is computed the way the firmware
// does: header with the crc field zeroed, then the samples.
func Decode(raw []byte) (*Record, error) {
	if len(raw) < 6 {
		return nil, fmt.Errorf("record too short: %d bytes", len(raw))
	}
	magic := binary.LittleEndian.Uint32(raw[0:4])
	if magic != Magic {
		return nil, fmt.Errorf("bad magic %08x", magic)
	}
	version := raw[4]
	hLen, ok := headerLens[version]
	if !ok {
		if version > FormatVersion && version <= maxPlausibleFormat && newerLooksIntact(raw) {
			return nil, fmt.Errorf("%w: version %d", ErrNewerFormat, version)
		}
		return nil, fmt.Errorf("unsupported format version %d", version)
	}
	if len(raw) < hLen {
		return nil, fmt.Errorf("record too short for v%d: %d bytes", version, len(raw))
	}

	var h Header
	switch version {
	case 1:
		var v1 headerV1
		if err := binary.Read(bytes.NewReader(raw), binary.LittleEndian, &v1); err != nil {
			return nil, fmt.Errorf("header v1: %w", err)
		}
		h = Header{
			Magic: v1.Magic, FormatVersion: v1.FormatVersion, RecordType: v1.RecordType,
			HeaderLen: v1.HeaderLen, BootID: v1.BootID, Seq: v1.Seq,
			StartedAtUnixMs: v1.StartedAtUnixMs, StartedAtMillis: v1.StartedAtMillis,
			TzOffsetMin: v1.TzOffsetMin, TimeValid: v1.TimeValid, ScaleRole: v1.ScaleRole,
			ScaleMac: v1.ScaleMac, Flags: v1.Flags,
			DetectorVersion: v1.DetectorVersion, FirmwareVersion: v1.FirmwareVersion,
			StopMs: v1.StopMs, YieldAtStopMg: v1.YieldAtStopMg, YieldFinalMg: v1.YieldFinalMg,
			SettleOffsetMg: v1.SettleOffsetMg, PeakFlowMgps: v1.PeakFlowMgps,
			MeanFlowMgps: v1.MeanFlowMgps, FlowWinStartMs: v1.FlowWinStartMs,
			FlowWinEndMs: v1.FlowWinEndMs,
			// v1 predates the display bus (0e). These are exactly what the
			// firmware writes when it has nothing to report, so nothing
			// downstream needs to know which version it is reading.
			BoilerTempStartDc: TempNone,
			MachineTimerDl:    0,
			PumpLine:          PumpNone,
			PumpLeadMs:        PumpLeadNone,
			PrePulseTotal:     PrePulsesNone,
			GramsMg:           v1.GramsMg, StableMs: v1.StableMs,
			SampleCount: v1.SampleCount, Crc32: v1.Crc32,
		}
	case 2:
		var v2 headerV2
		if err := binary.Read(bytes.NewReader(raw), binary.LittleEndian, &v2); err != nil {
			return nil, fmt.Errorf("header v2: %w", err)
		}
		h = Header{
			Magic: v2.Magic, FormatVersion: v2.FormatVersion, RecordType: v2.RecordType,
			HeaderLen: v2.HeaderLen, BootID: v2.BootID, Seq: v2.Seq,
			StartedAtUnixMs: v2.StartedAtUnixMs, StartedAtMillis: v2.StartedAtMillis,
			TzOffsetMin: v2.TzOffsetMin, TimeValid: v2.TimeValid, ScaleRole: v2.ScaleRole,
			ScaleMac: v2.ScaleMac, Flags: v2.Flags,
			DetectorVersion: v2.DetectorVersion, FirmwareVersion: v2.FirmwareVersion,
			StopMs: v2.StopMs, YieldAtStopMg: v2.YieldAtStopMg, YieldFinalMg: v2.YieldFinalMg,
			SettleOffsetMg: v2.SettleOffsetMg, PeakFlowMgps: v2.PeakFlowMgps,
			MeanFlowMgps: v2.MeanFlowMgps, FlowWinStartMs: v2.FlowWinStartMs,
			FlowWinEndMs:      v2.FlowWinEndMs,
			BoilerTempStartDc: v2.BoilerTempStartDc, MachineTimerDl: v2.MachineTimerDl,
			// v2 predates 0c's pump-start fields: "the machine did not say".
			PumpLine:      PumpNone,
			PumpLeadMs:    PumpLeadNone,
			PrePulseTotal: PrePulsesNone,
			GramsMg:       v2.GramsMg, StableMs: v2.StableMs,
			SampleCount: v2.SampleCount, Crc32: v2.Crc32,
		}
	case 3:
		var v3 headerV3
		if err := binary.Read(bytes.NewReader(raw), binary.LittleEndian, &v3); err != nil {
			return nil, fmt.Errorf("header v3: %w", err)
		}
		h = Header{
			Magic: v3.Magic, FormatVersion: v3.FormatVersion, RecordType: v3.RecordType,
			HeaderLen: v3.HeaderLen, BootID: v3.BootID, Seq: v3.Seq,
			StartedAtUnixMs: v3.StartedAtUnixMs, StartedAtMillis: v3.StartedAtMillis,
			TzOffsetMin: v3.TzOffsetMin, TimeValid: v3.TimeValid, ScaleRole: v3.ScaleRole,
			ScaleMac: v3.ScaleMac, Flags: v3.Flags,
			DetectorVersion: v3.DetectorVersion, FirmwareVersion: v3.FirmwareVersion,
			StopMs: v3.StopMs, YieldAtStopMg: v3.YieldAtStopMg, YieldFinalMg: v3.YieldFinalMg,
			SettleOffsetMg: v3.SettleOffsetMg, PeakFlowMgps: v3.PeakFlowMgps,
			MeanFlowMgps: v3.MeanFlowMgps, FlowWinStartMs: v3.FlowWinStartMs,
			FlowWinEndMs:      v3.FlowWinEndMs,
			BoilerTempStartDc: v3.BoilerTempStartDc, MachineTimerDl: v3.MachineTimerDl,
			PumpLine: v3.PumpLine, PumpLeadMs: v3.PumpLeadMs,
			// v3 predates the pre-drip pulse train: "not captured".
			PrePulseTotal: PrePulsesNone,
			GramsMg:       v3.GramsMg, StableMs: v3.StableMs,
			SampleCount: v3.SampleCount, Crc32: v3.Crc32,
		}
	case 4:
		if err := binary.Read(bytes.NewReader(raw), binary.LittleEndian, &h); err != nil {
			return nil, fmt.Errorf("header v4: %w", err)
		}
	}

	if int(h.HeaderLen) != hLen {
		return nil, fmt.Errorf("unexpected header length %d for v%d", h.HeaderLen, version)
	}
	tail := 0
	if version >= 4 {
		if int(h.PrePulseCount) > PrePulsesMax {
			return nil, fmt.Errorf("pre_pulse_count %d exceeds %d", h.PrePulseCount, PrePulsesMax)
		}
		tail = preTailBytes(int(h.PrePulseCount))
	}
	want := hLen + int(h.SampleCount)*sampleLen + tail
	if len(raw) != want {
		return nil, fmt.Errorf("length mismatch: %d bytes for %d samples and %d pulse offsets (want %d)",
			len(raw), h.SampleCount, h.PrePulseCount, want)
	}

	crcInput := make([]byte, len(raw))
	copy(crcInput, raw)
	// crc32 is the last header field in every version: zero it in place.
	for i := hLen - 4; i < hLen; i++ {
		crcInput[i] = 0
	}
	if got := crc32.ChecksumIEEE(crcInput); got != h.Crc32 {
		return nil, fmt.Errorf("crc mismatch: got %08x want %08x", got, h.Crc32)
	}

	r := &Record{Header: h, Samples: make([]Sample, h.SampleCount)}
	if h.SampleCount > 0 {
		if err := binary.Read(bytes.NewReader(raw[hLen:]), binary.LittleEndian, &r.Samples); err != nil {
			return nil, fmt.Errorf("samples: %w", err)
		}
	}
	if tail > 0 {
		off := hLen + int(h.SampleCount)*sampleLen
		r.PrePulseMs = make([]uint16, h.PrePulseCount)
		for i := range r.PrePulseMs {
			r.PrePulseMs[i] = binary.LittleEndian.Uint16(raw[off+2*i:])
		}
	}
	return r, nil
}
