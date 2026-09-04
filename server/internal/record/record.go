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
	Magic         = 0x4C505345 // 'ESPL'
	FormatVersion = 1

	TypeShot     = 1
	TypeWeighing = 2

	FlagFault     = 0x01
	FlagTruncated = 0x02
	FlagReject    = 0x04 // detected but <5 g; stored for visibility, excluded

	headerLen = 109
	sampleLen = 10

	// TempNone is INT16_MIN in the firmware: "no reading".
	TempNone = -32768
)

// Header mirrors spool_header_t field-for-field. binary.Read consumes it
// sequentially, so Go struct alignment never enters into it.
type Header struct {
	Magic            uint32
	FormatVersion    uint8
	RecordType       uint8
	HeaderLen        uint16
	BootID           uint32
	Seq              uint32
	StartedAtUnixMs  uint64
	StartedAtMillis  uint32
	TzOffsetMin      int16
	TimeValid        uint8
	ScaleRole        uint8
	ScaleMac         [6]byte
	Flags            uint8
	DetectorVersion  [8]byte
	FirmwareVersion  [16]byte
	StopMs           uint32
	YieldAtStopMg    int32
	YieldFinalMg     int32
	SettleOffsetMg   int32
	PeakFlowMgps     int32
	MeanFlowMgps     int32
	FlowWinStartMs   uint32
	FlowWinEndMs     uint32
	GramsMg          int32
	StableMs         uint32
	SampleCount      uint16
	Crc32            uint32
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

func (h *Header) Fault() bool     { return h.Flags&FlagFault != 0 }
func (h *Header) Truncated() bool { return h.Flags&FlagTruncated != 0 }
func (h *Header) Reject() bool    { return h.Flags&FlagReject != 0 }

// Decode parses and validates one complete record. The CRC is computed the
// way the firmware does: header with the crc field zeroed, then the samples.
func Decode(raw []byte) (*Record, error) {
	if len(raw) < headerLen {
		return nil, fmt.Errorf("record too short: %d bytes", len(raw))
	}
	var h Header
	if err := binary.Read(bytes.NewReader(raw), binary.LittleEndian, &h); err != nil {
		return nil, fmt.Errorf("header: %w", err)
	}
	if h.Magic != Magic {
		return nil, fmt.Errorf("bad magic %08x", h.Magic)
	}
	if h.FormatVersion != FormatVersion {
		return nil, fmt.Errorf("unsupported format version %d", h.FormatVersion)
	}
	if int(h.HeaderLen) != headerLen {
		return nil, fmt.Errorf("unexpected header length %d", h.HeaderLen)
	}
	want := headerLen + int(h.SampleCount)*sampleLen
	if len(raw) != want {
		return nil, fmt.Errorf("length mismatch: %d bytes for %d samples (want %d)", len(raw), h.SampleCount, want)
	}

	crcInput := make([]byte, len(raw))
	copy(crcInput, raw)
	// crc32 is the last header field: zero it in place for the check.
	for i := headerLen - 4; i < headerLen; i++ {
		crcInput[i] = 0
	}
	if got := crc32.ChecksumIEEE(crcInput); got != h.Crc32 {
		return nil, fmt.Errorf("crc mismatch: got %08x want %08x", got, h.Crc32)
	}

	r := &Record{Header: h, Samples: make([]Sample, h.SampleCount)}
	if h.SampleCount > 0 {
		if err := binary.Read(bytes.NewReader(raw[headerLen:]), binary.LittleEndian, &r.Samples); err != nil {
			return nil, fmt.Errorf("samples: %w", err)
		}
	}
	return r, nil
}
