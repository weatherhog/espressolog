package store

import "math"

// Pre-infusion measured from the flowmeter pulse train between the lever
// press and pour confirmation (shot_pre_pulse, spool v4+).
//
// The machine's `Pr` setting runs the pump for that many seconds, pauses it,
// then runs it again. The meter sits on the suction side, so the pump-on run
// shows as dense pulses (~25/s), followed by a short coasting tail as the
// turbine slows, then silence, then the resume. Shot 179 (Pr = 5 s,
// 2026-11-15): dense from 117 to 5212 ms, coasting at 5370, 5670 and
// 6266 ms, silent until 8194 ms. That run measures 5.1 s.
//
// DERIVED, NOT RAW (invariant 3): the offsets stay in shot_pre_pulse and
// this can be recomputed from them if the definition changes. The two
// thresholds come from that one shot and are the first thing to revisit if
// a measurement disagrees with the setting.
const (
	// A gap this long is the pause, not a missed pulse.
	preinfusionPauseMinMs = 1000
	// A gap longer than this at the END of the run is the turbine coasting,
	// not the pump: shot 179's pump-on gaps are ~36 ms, its coasting gaps
	// 158, 300 and 596 ms.
	preinfusionRunGapMaxMs = 150
)

// measurePreinfusion returns the pump-on time of the pre-infusion run in
// seconds (0.1 s resolution) and whether the train supports a measurement.
// offsets are ms after the press, oldest first; total and complete are
// pre_pulse_total and pre_pulse_complete; firstDripMs is the press-to-first-
// drip lead (first_drip_ms), negative if unknown.
//
// The pause is the first gap of at least preinfusionPauseMinMs — or, when
// first drip came during the pause (the puck dripping while it soaks), a
// silence of that long between the last pulse and first drip. No pause with
// the whole window in hand means the pump ran straight through: measured 0.
// Anything incomplete, truncated, or with too few pulses is not measured.
func measurePreinfusion(offsets []uint16, total int, complete bool, firstDripMs int64) (float64, bool) {
	if !complete || total != len(offsets) || len(offsets) < 2 {
		return 0, false
	}
	end := -1 // index of the last pulse before the pause
	for i := 1; i < len(offsets); i++ {
		if int(offsets[i])-int(offsets[i-1]) >= preinfusionPauseMinMs {
			end = i - 1
			break
		}
	}
	if end < 0 {
		last := int64(offsets[len(offsets)-1])
		if firstDripMs >= 0 && firstDripMs-last >= preinfusionPauseMinMs {
			end = len(offsets) - 1
		} else {
			return 0, true
		}
	}
	// Strip the coasting tail.
	for end > 0 && int(offsets[end])-int(offsets[end-1]) > preinfusionRunGapMaxMs {
		end--
	}
	if end == 0 {
		return 0, false // a run of one pulse is not a run
	}
	s := float64(int(offsets[end])-int(offsets[0])) / 1000
	return math.Round(s*10) / 10, true
}
