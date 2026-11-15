package store

import "math"

// Pre-infusion measured from the flowmeter pulse train between the lever
// press and pour confirmation (shot_pre_pulse, spool v4+).
//
// The machine's `Pr` setting (whole seconds, 0-5) runs the pump for that
// long, pauses it, then runs it again. The meter sits on the suction side,
// so the pump-on run shows as dense pulses (~25/s), followed by a short
// coasting tail as the turbine slows, then silence, then the resume. Shot
// 179 (Pr = 5 s, 2026-11-15): dense from 117 to 5212 ms, coasting at 5370,
// 5670 and 6266 ms, silent until 8194 ms. That run measures 5.1 s.
//
// DERIVED, NOT RAW (invariant 3): the offsets stay in shot_pre_pulse, the
// run is stored as preinfusion_run_s, and backfillPreinfusion re-derives
// every 'flow' row at each server start, so changing a threshold here
// re-measures the archive. The thresholds come from that one shot and are
// the first thing to revisit if a measurement disagrees with the setting.
const (
	// A gap this long is the pause, not a missed pulse.
	preinfusionPauseMinMs = 1000
	// A gap longer than this at the END of the run is the turbine coasting,
	// not the pump: shot 179's pump-on gaps are ~36 ms, its coasting gaps
	// 158, 300 and 596 ms.
	preinfusionRunGapMaxMs = 150
	// The window runs from the press to pour confirmation, POUR_HOLD_MS
	// after first drip (detector.h).
	preinfusionWindowTailMs = 300
	// One unbroken dense run at least this long cannot be a pre-infusion
	// (Pr is at most 5 s): the pump ran straight through, which is a
	// measured 0. Anything shorter could be a pre-infusion run that first
	// drip interrupted, and is not measured.
	preinfusionContinuousMinMs = 6000
	// The largest Pr the machine offers.
	preinfusionPrMaxS = 5
)

// measurePreinfusion returns the pump-on time of the pre-infusion run in
// seconds (0.1 s resolution) and whether the train supports a measurement.
// offsets are ms after the press, oldest first — the FIRST of them when the
// firmware's 256-entry array filled; total and complete are pre_pulse_total
// and pre_pulse_complete; firstDripMs is the press-to-first-drip lead
// (first_drip_ms), negative if unknown.
//
// Three shapes are measured, anything else is not (review 2026-11-15: a
// first version returned a measured 0 whenever it found no pause):
//
//  1. a pause — a gap of at least preinfusionPauseMinMs — among the stored
//     pulses: the run before it, coasting tail stripped. Holds for a
//     truncated train too, since the stored pulses are the oldest.
//  2. first drip during the pause: no gap, but the window (to pour
//     confirmation) ends at least preinfusionPauseMinMs after the dense
//     run's end, coasting tail stripped first.
//  3. one unbroken dense run of at least preinfusionContinuousMinMs: no
//     pre-infusion, a measured 0.
func measurePreinfusion(offsets []uint16, total int, complete bool, firstDripMs int64) (float64, bool) {
	if !complete || len(offsets) < 2 || total < len(offsets) {
		return 0, false
	}
	for i := 1; i < len(offsets); i++ {
		if int(offsets[i])-int(offsets[i-1]) >= preinfusionPauseMinMs {
			return runBefore(offsets, i-1)
		}
	}
	if total > len(offsets) {
		return 0, false // the pause may lie beyond the stored pulses
	}
	last := len(offsets) - 1
	end := stripCoast(offsets, last)
	if firstDripMs >= 0 && firstDripMs+preinfusionWindowTailMs-int64(offsets[end]) >= preinfusionPauseMinMs {
		return runBefore(offsets, end)
	}
	for i := 1; i < len(offsets); i++ {
		if int(offsets[i])-int(offsets[i-1]) > preinfusionRunGapMaxMs {
			return 0, false
		}
	}
	if int(offsets[last])-int(offsets[0]) >= preinfusionContinuousMinMs {
		return 0, true
	}
	return 0, false
}

// stripCoast walks back from end over the coasting tail.
func stripCoast(offsets []uint16, end int) int {
	for end > 0 && int(offsets[end])-int(offsets[end-1]) > preinfusionRunGapMaxMs {
		end--
	}
	return end
}

// runBefore measures the run ending at end (inclusive), coasting stripped.
func runBefore(offsets []uint16, end int) (float64, bool) {
	end = stripCoast(offsets, end)
	if end == 0 {
		return 0, false // a run of one pulse is not a run
	}
	s := float64(int(offsets[end])-int(offsets[0])) / 1000
	return math.Round(s*10) / 10, true
}

// preinfusionSetting is the Pr setting a measured run corresponds to: the
// nearest whole second, if it is one the machine offers.
func preinfusionSetting(runS float64) (float64, bool) {
	pr := math.Round(runS)
	if pr < 0 || pr > preinfusionPrMaxS {
		return 0, false
	}
	return pr, true
}
