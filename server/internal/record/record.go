// Package record decodes the firmware's binary spool records — the exact
// bytes the ESP32 writes to flash and POSTs verbatim (see
// firmware/src/spool.h, which is the authoritative definition).
package record

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"hash/crc32"
)

const (
	Magic = 0x4C505345 // 'ESPL'
	// FormatVersion is what the firmware writes TODAY. Decode accepts every
	// version in headerLens, because v1 records exist both spooled on
	// devices and as fixtures, and a format bump must never strand a shot
	// (invariant 6).
	FormatVersion = 2

	TypeShot     = 1
	TypeWeighing = 2

	FlagFault     = 0x01
	FlagTruncated = 0x02
	FlagReject    = 0x04 // detected but <5 g; stored for visibility, excluded
	// v2. Someone entered temperature programming or edited a value while
	// this shot was recorded. Latched by the firmware, because PrG is a
	// transient banner and the blink stops the moment they walk away.
	FlagSetpointTouched = 0x08

	headerLenV1 = 109
	headerLenV2 = 113 // v1 + boiler_temp_start_dc (int16) + machine_timer_dl (uint16)
	sampleLen   = 10

	// TempNone is INT16_MIN in the firmware: "no reading".
	TempNone = -32768
)

// headerLens maps a format version to its on-wire header size.
var headerLens = map[uint8]int{1: headerLenV1, 2: headerLenV2}

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

// Header mirrors the CURRENT spool_header_t field-for-field, and is also the
// canonical in-memory form: a v1 record decodes into it with the v2-only
// fields left at their "not available" values. binary.Read consumes it
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
	GramsMg           int32
	StableMs          uint32
	SampleCount       uint16
	Crc32             uint32
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
}

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
			GramsMg:           v1.GramsMg, StableMs: v1.StableMs,
			SampleCount: v1.SampleCount, Crc32: v1.Crc32,
		}
	case 2:
		if err := binary.Read(bytes.NewReader(raw), binary.LittleEndian, &h); err != nil {
			return nil, fmt.Errorf("header v2: %w", err)
		}
	}

	if int(h.HeaderLen) != hLen {
		return nil, fmt.Errorf("unexpected header length %d for v%d", h.HeaderLen, version)
	}
	want := hLen + int(h.SampleCount)*sampleLen
	if len(raw) != want {
		return nil, fmt.Errorf("length mismatch: %d bytes for %d samples (want %d)", len(raw), h.SampleCount, want)
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
	return r, nil
}
