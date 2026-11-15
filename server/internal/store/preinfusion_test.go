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

func TestMeasurePreinfusion(t *testing.T) {
	o := shot179()
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
		{"truncated at 256 offsets", o, len(o) + 40, true, 9855, 0, false},
		{"no pause: the pump ran straight through", []uint16{100, 140, 180, 220, 260}, 5, true, 400, 0, true},
		{"first drip during the pause", []uint16{100, 140, 180, 3100, 3140}[:3], 3, true, 2500, 0.1, true},
		{"too few pulses", []uint16{100}, 1, true, 3000, 0, false},
		{"nothing before the pause but one pulse", []uint16{100, 2000, 2040}, 3, true, 3000, 0, false},
	}
	for _, c := range cases {
		got, ok := measurePreinfusion(c.offsets, c.total, c.complete, c.drip)
		if ok != c.ok || got != c.want {
			t.Errorf("%s: got %v %v, want %v %v", c.name, got, ok, c.want, c.ok)
		}
	}
}
