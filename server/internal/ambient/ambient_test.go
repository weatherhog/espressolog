package ambient

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"espressolog/internal/store"

	"github.com/coder/websocket"
)

const (
	tempID = "sensor.room_temperature"
	rhID   = "sensor.room_humidity"
	token  = "test-token"
)

var shotAt = time.Date(2026, 11, 22, 8, 9, 30, 0, time.UTC)

// fakeHA serves the three Home Assistant APIs the client uses and checks the
// requests' shape: history is the body for /api/history/period, stats the
// "result" of the statistics call, units the unit_of_measurement each
// entity reports on /api/states (missing: 404).
type fakeHA struct {
	t       *testing.T
	history string
	stats   map[string][]map[string]any
	units   map[string]string
	noClass map[string]bool
	rh      bool      // the client is configured with the RH entity
	statsAt time.Time // the moment whose hour the statistics call must ask for
	// statsFrom/statsTo, when set, are the window the call must ask for
	// instead (Validate's probe).
	statsFrom, statsTo time.Time
	wsFail             bool // drop the WebSocket before answering
}

func (f *fakeHA) serve() *httptest.Server {
	t := f.t
	mux := http.NewServeMux()
	auth := func(w http.ResponseWriter, r *http.Request) bool {
		if r.Header.Get("Authorization") != "Bearer "+token {
			w.WriteHeader(http.StatusUnauthorized)
			return false
		}
		return true
	}
	mux.HandleFunc("/api/states/", func(w http.ResponseWriter, r *http.Request) {
		if !auth(w, r) {
			return
		}
		id := strings.TrimPrefix(r.URL.Path, "/api/states/")
		unit, ok := f.units[id]
		if !ok {
			w.WriteHeader(http.StatusNotFound)
			w.Write([]byte(`{"message":"Entity not found."}`))
			return
		}
		attrs := map[string]string{"unit_of_measurement": unit, "state_class": "measurement"}
		if f.noClass[id] {
			delete(attrs, "state_class")
		}
		json.NewEncoder(w).Encode(map[string]any{"entity_id": id, "state": "21.0", "attributes": attrs})
	})
	mux.HandleFunc("/api/history/period/", func(w http.ResponseWriter, r *http.Request) {
		if !auth(w, r) {
			return
		}
		start := strings.TrimPrefix(r.URL.Path, "/api/history/period/")
		if want := shotAt.Add(-StateLookback).Format(time.RFC3339); start != want {
			t.Errorf("history start %q, want %q", start, want)
		}
		q := r.URL.Query()
		if q.Get("end_time") != shotAt.Format(time.RFC3339) {
			t.Errorf("history end_time %q", q.Get("end_time"))
		}
		if _, ok := q["no_attributes"]; !ok {
			t.Error("history without no_attributes")
		}
		if !strings.Contains(q.Get("filter_entity_id"), tempID) {
			t.Errorf("history query without the temperature entity: %s", r.URL.RawQuery)
		}
		if f.rh && !strings.Contains(q.Get("filter_entity_id"), rhID) {
			t.Errorf("history query without the humidity entity: %s", r.URL.RawQuery)
		}
		w.Write([]byte(f.history))
	})
	mux.HandleFunc("/api/websocket", func(w http.ResponseWriter, r *http.Request) {
		if f.wsFail {
			w.WriteHeader(http.StatusBadGateway)
			return
		}
		c, err := websocket.Accept(w, r, nil)
		if err != nil {
			return
		}
		defer c.CloseNow()
		ctx := r.Context()
		send := func(v any) { b, _ := json.Marshal(v); c.Write(ctx, websocket.MessageText, b) }
		recv := func() map[string]any {
			_, b, err := c.Read(ctx)
			if err != nil {
				return nil
			}
			var m map[string]any
			json.Unmarshal(b, &m)
			return m
		}
		send(map[string]string{"type": "auth_required"})
		if m := recv(); m == nil || m["access_token"] != token {
			send(map[string]string{"type": "auth_invalid"})
			return
		}
		send(map[string]string{"type": "auth_ok"})
		m := recv()
		if m == nil || m["type"] != "recorder/statistics_during_period" {
			return
		}
		at := f.statsAt
		if at.IsZero() {
			at = shotAt
		}
		hour := at.Truncate(time.Hour)
		from, to := hour, hour.Add(time.Hour)
		if !f.statsFrom.IsZero() {
			from, to = f.statsFrom, f.statsTo
		}
		if m["start_time"] != from.Format(time.RFC3339) || m["end_time"] != to.Format(time.RFC3339) {
			t.Errorf("statistics window %v..%v, want %v..%v", m["start_time"], m["end_time"], from, to)
		}
		if m["period"] != "hour" {
			t.Errorf("statistics period %v", m["period"])
		}
		if ty, _ := m["types"].([]any); len(ty) != 1 || ty[0] != "mean" {
			t.Errorf("statistics types %v, want [mean]", m["types"])
		}
		ids, _ := m["statistic_ids"].([]any)
		if len(ids) == 0 || ids[0] != tempID || (f.rh && (len(ids) < 2 || ids[1] != rhID)) {
			t.Errorf("statistic_ids %v", m["statistic_ids"])
		}
		if u, _ := m["units"].(map[string]any); u["temperature"] != "°C" {
			t.Errorf("statistics units %v, want temperature °C", m["units"])
		}
		res := f.stats
		if res == nil {
			res = map[string][]map[string]any{}
		}
		send(map[string]any{"id": m["id"], "type": "result", "success": true, "result": res})
	})
	return httptest.NewServer(mux)
}

func newFake(t *testing.T, history string, stats map[string][]map[string]any) *fakeHA {
	return &fakeHA{t: t, history: history, stats: stats,
		units: map[string]string{tempID: "°C", rhID: "%"}, noClass: map[string]bool{}}
}

// The shots in these tests are a day old unless a test says otherwise, so
// the hourly fallback is allowed.
var testNow = shotAt.Add(24 * time.Hour)

func client(srv *httptest.Server, rh bool) *Client {
	cfg := Config{URL: srv.URL, Token: token, TempEntity: tempID}
	if rh {
		cfg.RHEntity = rhID
	}
	c := New(cfg)
	c.Now = func() time.Time { return testNow }
	return c
}

func (f *fakeHA) serveFor(rh bool) *httptest.Server { f.rh = rh; return f.serve() }

func TestStateIsTheValueInForceAtTheShot(t *testing.T) {
	history := `[
	 [{"entity_id":"sensor.room_temperature","state":"21.0","last_changed":"2026-11-22T07:00:00+00:00"},
	  {"entity_id":"sensor.room_temperature","state":"21.4","last_changed":"2026-11-22T08:05:00.123456+00:00"},
	  {"entity_id":"sensor.room_temperature","state":"23.0","last_changed":"2026-11-22T08:10:00+00:00"}],
	 [{"entity_id":"sensor.room_humidity","state":"48","last_changed":"2026-11-22T06:00:00+00:00"}]
	]`
	srv := newFake(t, history, nil).serveFor(true)
	defer srv.Close()
	r, err := client(srv, true).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceState || deref(r.TempC) != 21.4 || deref(r.RH) != 48.0 {
		t.Errorf("got %+v temp=%v rh=%v, want ha_state 21.4 / 48 (08:10 is after the shot)",
			r, deref(r.TempC), deref(r.RH))
	}
}

// Review 2026-11-22: a sensor unavailable AT the shot must not pass its last
// numeric value, hours old, off as exact -- it falls back to the hour's mean.
func TestUnavailableAtTheShotFallsBackToTheHour(t *testing.T) {
	history := `[
	 [{"entity_id":"sensor.room_temperature","state":"21.0","last_changed":"2026-11-22T03:00:00+00:00"},
	  {"entity_id":"sensor.room_temperature","state":"unavailable","last_changed":"2026-11-22T03:30:00+00:00"}]
	]`
	srv := newFake(t, history, map[string][]map[string]any{tempID: {{"mean": 20.3}}}).serve()
	defer srv.Close()
	r, err := client(srv, false).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceHourly || deref(r.TempC) != 20.3 {
		t.Errorf("got %+v temp=%v, want ha_hourly 20.3, not the stale 21.0", r, deref(r.TempC))
	}
}

func TestPurgedHistoryFallsBackToTheHourlyMean(t *testing.T) {
	srv := newFake(t, `[]`, map[string][]map[string]any{
		tempID: {{"mean": 20.3}}, rhID: {{"mean": 51.2}},
	}).serveFor(true)
	defer srv.Close()
	r, err := client(srv, true).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceHourly || deref(r.TempC) != 20.3 || deref(r.RH) != 51.2 {
		t.Errorf("got %+v temp=%v rh=%v, want ha_hourly 20.3 / 51.2", r, deref(r.TempC), deref(r.RH))
	}
}

// Review 2026-11-22: a temperature from history and no humidity there takes
// the humidity from the hour's mean instead of dropping it.
func TestAMissingHumidityIsFilledFromTheHour(t *testing.T) {
	history := `[[{"entity_id":"sensor.room_temperature","state":"21.4","last_changed":"2026-11-22T08:00:00+00:00"}]]`
	srv := newFake(t, history, map[string][]map[string]any{rhID: {{"mean": 50.0}}}).serveFor(true)
	defer srv.Close()
	r, err := client(srv, true).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceState || deref(r.TempC) != 21.4 || deref(r.RH) != 50.0 {
		t.Errorf("got %+v temp=%v rh=%v, want ha_state 21.4 with rh 50 from the hour", r, deref(r.TempC), deref(r.RH))
	}
}

func TestNothingAnywhereIsNoneNotAnError(t *testing.T) {
	srv := newFake(t, `[]`, nil).serve()
	defer srv.Close()
	r, err := client(srv, false).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceNone || r.TempC != nil {
		t.Errorf("got %+v, want none", r)
	}
}

func TestABadTokenIsAnErrorNotNone(t *testing.T) {
	srv := newFake(t, `[]`, nil).serve()
	defer srv.Close()
	c := New(Config{URL: srv.URL, Token: "wrong", TempEntity: tempID})
	if _, err := c.At(context.Background(), shotAt); err == nil {
		t.Error("a refused token must be an error, so the shot is retried rather than marked none")
	}
}

// Reviews 2026-11-22: a typo in an entity id, a sensor in the wrong unit,
// one without long-term statistics, or a statistics connection that gives
// no mean all stop the worker before it writes anything.
func TestValidateRefusesWhatWouldWriteNoneAcrossTheArchive(t *testing.T) {
	// The probe asks for the last 48 h and takes ANY non-null mean: here the
	// first hours are empty (an outage then), the third has one.
	f := newFake(t, `[]`, map[string][]map[string]any{tempID: {{"mean": nil}, {"mean": nil}, {"mean": 20.0}}})
	f.statsTo = testNow.Truncate(time.Hour)
	f.statsFrom = f.statsTo.Add(-ProbeWindow)
	srv := f.serve()
	defer srv.Close()
	if err := client(srv, false).Validate(context.Background()); err != nil {
		t.Errorf("good configuration refused: %v", err)
	}
	bad := client(srv, false)
	bad.cfg.TempEntity = "sensor.room_temprature"
	if err := bad.Validate(context.Background()); err == nil || !strings.Contains(err.Error(), "does not exist") {
		t.Errorf("typo accepted: %v", err)
	}
	f.noClass[tempID] = true
	if err := client(srv, false).Validate(context.Background()); err == nil || !strings.Contains(err.Error(), "state_class") {
		t.Errorf("a sensor without statistics accepted: %v", err)
	}
	f.noClass[tempID] = false
	f.stats = nil
	if err := client(srv, false).Validate(context.Background()); err == nil || !strings.Contains(err.Error(), "probe") {
		t.Errorf("no hourly mean accepted: %v", err)
	}
	f.units[tempID] = "°F"
	if err := client(srv, false).Validate(context.Background()); err == nil {
		t.Error("a sensor in °F accepted")
	}
}

// Review 2026-11-22: a shot younger than its hour's summary, missing a value,
// is retried -- not written 'none' because the hour's mean does not exist yet.
func TestARecentShotMissingAValueIsRetriedNotNone(t *testing.T) {
	srv := newFake(t, `[]`, nil).serve()
	defer srv.Close()
	c := client(srv, false)
	c.Now = func() time.Time { return shotAt.Add(30 * time.Minute) }
	if _, err := c.At(context.Background(), shotAt); !errors.Is(err, ErrTooRecent) {
		t.Errorf("err = %v, want ErrTooRecent", err)
	}
}

func TestHealthChangesOnlyOnEvidence(t *testing.T) {
	cases := []struct {
		from Health
		r    PassResult
		to   Health
		logs bool
	}{
		{Working, PassResult{Tried: 0}, Working, false},
		{Failing, PassResult{Tried: 0}, Failing, false}, // all in backoff: not "working again"
		{Working, PassResult{Tried: 3, Failed: 3, LastErr: errors.New("x")}, Failing, true},
		{Working, PassResult{Tried: 1, Failed: 1}, Working, false},            // one stuck shot is not an outage
		{Failing, PassResult{Tried: 2, Failed: 2, Marked: 1}, Failing, false}, // a no_clock mark is no evidence
		{Failing, PassResult{Tried: 3, Failed: 3}, Failing, false},
		{Failing, PassResult{Tried: 3, Filled: 1, Failed: 2}, Working, true},
		{Working, PassResult{Tried: 3, Filled: 1, Failed: 2}, Working, false},
	}
	for i, c := range cases {
		to, msg := Next(c.from, c.r)
		if to != c.to || (msg != "") != c.logs {
			t.Errorf("case %d: %v -> %v %q, want %v logs=%v", i, c.from, to, msg, c.to, c.logs)
		}
	}
}

type fakeStore struct {
	pending []store.AmbientPending
	set     map[int64]string
}

func (f *fakeStore) ShotsNeedingAmbient(limit int) ([]store.AmbientPending, error) {
	var out []store.AmbientPending
	for _, p := range f.pending {
		if _, done := f.set[p.ID]; !done && len(out) < limit {
			out = append(out, p)
		}
	}
	return out, nil
}

func (f *fakeStore) SetAmbient(id int64, _, _ *float64, source string) error {
	f.set[id] = source
	return nil
}

// fakeLooker fails for the shot ids in bad, answers 21 °C otherwise.
type fakeLooker struct{ bad map[time.Time]bool }

func (f *fakeLooker) Validate(context.Context) error { return nil }
func (f *fakeLooker) At(_ context.Context, t time.Time) (Reading, error) {
	if f.bad[t] {
		return Reading{}, errors.New("unreachable")
	}
	v := 21.0
	return Reading{TempC: &v, Source: SourceState}, nil
}

// Review 2026-11-22: one shot that always fails must not stall the rest --
// it is skipped for its backoff and the pass goes on; a shot without clock
// time is marked, not looked up.
func TestAFailingShotDoesNotStallTheOthers(t *testing.T) {
	fs := &fakeStore{set: map[int64]string{}}
	at := func(i int) time.Time { return shotAt.Add(-time.Duration(i) * time.Hour) }
	for i := 1; i <= 6; i++ {
		fs.pending = append(fs.pending, store.AmbientPending{ID: int64(i), StartedAt: at(i), ClockValid: i != 5})
	}
	now := shotAt
	w := &Worker{St: fs, HL: &fakeLooker{bad: map[time.Time]bool{at(2): true}}, Now: func() time.Time { return now }}
	r := w.Pass(context.Background())
	if r.LastErr == nil || r.Filled != 4 || r.Marked != 1 || r.Tried != 5 || r.Failed != 1 {
		t.Errorf("%+v; want 4 filled, 1 marked no_clock, 5 tried, 1 failed, the error reported", r)
	}
	if fs.set[5] != SourceNoClock || fs.set[6] != SourceState {
		t.Errorf("set=%v; want 5 no_clock and 6 (behind the failing 2) filled", fs.set)
	}
	if _, ok := fs.set[2]; ok {
		t.Error("the failing shot was marked")
	}
	// Within its backoff the failing shot is not tried again.
	now = shotAt.Add(30 * time.Second)
	if r := w.Pass(context.Background()); r.Tried != 0 {
		t.Errorf("retried within the backoff: %+v", r)
	}
	now = shotAt.Add(2 * time.Minute)
	if r := w.Pass(context.Background()); r.Tried != 1 {
		t.Errorf("not retried after the backoff: %+v", r)
	}
}

// Review 2026-11-22: with the temperature in hand, a failed humidity lookup
// is retried, not written short.
func TestAFailedHumidityLookupIsRetriedNotWrittenShort(t *testing.T) {
	f := newFake(t, `[[{"entity_id":"sensor.room_temperature","state":"21.4","last_changed":"2026-11-22T08:00:00+00:00"}]]`, nil)
	f.wsFail = true
	srv := f.serveFor(true)
	defer srv.Close()
	if _, err := client(srv, true).At(context.Background(), shotAt); err == nil {
		t.Error("a failed humidity lookup was written short instead of retried")
	}
}

// tooRecentLooker says ErrTooRecent once, then fails for real.
type tooRecentLooker struct{ n int }

func (l *tooRecentLooker) Validate(context.Context) error { return nil }
func (l *tooRecentLooker) At(context.Context, time.Time) (Reading, error) {
	l.n++
	if l.n == 1 {
		return Reading{}, ErrTooRecent
	}
	return Reading{}, errors.New("unreachable")
}

// Review 2026-11-22: waiting out ErrTooRecent is not a failure and does not
// start the backoff -- the first real failure after it backs off RetryMin.
func TestTooRecentIsAWaitNotABackoff(t *testing.T) {
	fs := &fakeStore{set: map[int64]string{}, pending: []store.AmbientPending{{ID: 1, StartedAt: shotAt, ClockValid: true}}}
	now := shotAt
	w := &Worker{St: fs, HL: &tooRecentLooker{}, Now: func() time.Time { return now }}
	if r := w.Pass(context.Background()); r.Tried != 0 || r.Failed != 0 {
		t.Errorf("too recent counted: %+v", r)
	}
	now = now.Add(RecentRetry)
	if r := w.Pass(context.Background()); r.Failed != 1 {
		t.Errorf("real failure not counted: %+v", r)
	}
	if b := w.retry[1]; b.wait != RetryMin {
		t.Errorf("first backoff after a too-recent wait = %v, want %v", b.wait, RetryMin)
	}
}

// failingStore fails every write.
type failingStore struct{ fakeStore }

func (f *failingStore) SetAmbient(int64, *float64, *float64, string) error {
	return errors.New("database is locked")
}

// Review 2026-11-22: a write that fails is a failure, so it is logged.
func TestAFailedWriteCountsAsAFailure(t *testing.T) {
	fs := &failingStore{fakeStore{set: map[int64]string{}, pending: []store.AmbientPending{{ID: 1, StartedAt: shotAt, ClockValid: true}}}}
	w := &Worker{St: fs, HL: &fakeLooker{}, Now: func() time.Time { return shotAt }}
	if r := w.Pass(context.Background()); r.Failed != 1 || r.LastErr == nil {
		t.Errorf("%+v; want the write counted as a failure", r)
	}
}

func deref(p *float64) float64 {
	if p == nil {
		return -1
	}
	return *p
}
