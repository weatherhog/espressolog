package store

import "testing"

// shot179 is the real pre-drip train of shot 179 in shape: Pr = 5 s, dense
// pulses 117..5212 ms, a coasting tail, a 1.9 s pause, the resume.
func shot179() []uint16 {
	var o []uint16
	for t := 117; t <= 5212; t += 36 {
		o = append(o, uint16(t))
	}
	o[len(o)-1] = 5212
	o = append(o, 5370, 5670, 6266)
	for t := 8194; t <= 10108; t += 36 {
		o = append(o, uint16(t))
	}
	return o
}

// dense returns pulses every 36 ms from a to b inclusive-ish.
func dense(a, b int) []uint16 {
	var o []uint16
	for t := a; t <= b; t += 36 {
		o = append(o, uint16(t))
	}
	return o
}

func TestMeasurePreinfusion(t *testing.T) {
	o := shot179()
	upToPause := o[:len(dense(117, 5212))+3] // the run and its coast, cut before the resume
	noisy := append(append(append([]uint16{}, o[:len(upToPause)]...), 7200), o[len(upToPause):]...)
	cases := []struct {
		name     string
		offsets  []uint16
		total    int
		complete bool
		drip     int64
		want     float64
		ok       bool
	}{
		{"shot 179: pause, coasting tail stripped", o, len(o), true, 9855, 5.1, true},
		{"incomplete log", o, len(o), false, 9855, 0, false},
		{"truncated, pause inside the stored pulses", o, len(o) + 40, true, 14000, 5.1, true},
		{"truncated, no pause stored", dense(100, 9000), 400, true, 16000, 0, false},
		{"first drip during the pause, 1.8 s after the pump stopped (review case b)", upToPause, len(upToPause), true, 7000, 5.1, true},
		{"first drip during the pump-on run (review case a)", dense(100, 3200), len(dense(100, 3200)), true, 3000, 0, false},
		{"one noise pulse splits the pause", noisy, len(noisy), true, 9855, 0, false},
		{"pump straight through for 7 s: no pre-infusion", dense(100, 7100), len(dense(100, 7100)), true, 7000, 0, true},
		{"pump straight through but only 4 s: unknown", dense(100, 4100), len(dense(100, 4100)), true, 4000, 0, false},
		{"too few pulses", []uint16{100}, 1, true, 3000, 0, false},
		{"nothing before the pause but one pulse", []uint16{100, 2000, 2040}, 3, true, 3000, 0, false},
		{"a 0.4 s run before a pause is not Pr 0", append(dense(100, 500), dense(3000, 4000)...), len(dense(100, 500)) + len(dense(3000, 4000)), true, 4500, 0, false},
	}
	for _, c := range cases {
		got, ok := measurePreinfusion(c.offsets, c.total, c.complete, c.drip)
		if ok != c.ok || got != c.want {
			t.Errorf("%s: got %v %v, want %v %v", c.name, got, ok, c.want, c.ok)
		}
	}
	for run, want := range map[float64]float64{5.1: 5, 0: 0, 2.6: 3, 4.4: 4} {
		if got, ok := preinfusionSetting(run); !ok || got != want {
			t.Errorf("setting for %v s = %v %v, want %v", run, got, ok, want)
		}
	}
	if _, ok := preinfusionSetting(5.6); ok {
		t.Error("a 5.6 s run maps to a Pr the machine does not offer")
	}
}
