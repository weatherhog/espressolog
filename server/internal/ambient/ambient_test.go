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

// fakeHA serves the two Home Assistant APIs the client uses. history is the
// JSON body for /api/history/period; stats is the "result" of the
// statistics call (nil: none).
func fakeHA(t *testing.T, history string, stats map[string][]map[string]any) *httptest.Server {
	t.Helper()
	mux := http.NewServeMux()
	mux.HandleFunc("/api/history/period/", func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer "+token {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		if !strings.Contains(r.URL.Query().Get("filter_entity_id"), tempID) {
			t.Errorf("history query without the temperature entity: %s", r.URL.RawQuery)
		}
		w.Write([]byte(history))
	})
	mux.HandleFunc("/api/websocket", func(w http.ResponseWriter, r *http.Request) {
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
		res := stats
		if res == nil {
			res = map[string][]map[string]any{}
		}
		send(map[string]any{"id": m["id"], "type": "result", "success": true, "result": res})
	})
	return httptest.NewServer(mux)
}

func client(srv *httptest.Server, rh bool) *Client {
	cfg := Config{URL: srv.URL, Token: token, TempEntity: tempID}
	if rh {
		cfg.RHEntity = rhID
	}
	return New(cfg)
}

var shotAt = time.Date(2026, 11, 22, 8, 9, 30, 0, time.UTC)

func TestStateIsTheLastValueAtOrBeforeTheShot(t *testing.T) {
	history := `[
	 [{"entity_id":"sensor.room_temperature","state":"21.0","last_changed":"2026-11-22T07:00:00+00:00"},
	  {"entity_id":"sensor.room_temperature","state":"unavailable","last_changed":"2026-11-22T07:30:00+00:00"},
	  {"entity_id":"sensor.room_temperature","state":"21.4","last_changed":"2026-11-22T08:05:00+00:00"},
	  {"entity_id":"sensor.room_temperature","state":"23.0","last_changed":"2026-11-22T08:10:00+00:00"}],
	 [{"entity_id":"sensor.room_humidity","state":"48","last_changed":"2026-11-22T06:00:00+00:00"}]
	]`
	srv := fakeHA(t, history, nil)
	defer srv.Close()
	r, err := client(srv, true).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceState || r.TempC == nil || *r.TempC != 21.4 || r.RH == nil || *r.RH != 48 {
		t.Errorf("got %+v temp=%v rh=%v, want ha_state 21.4 / 48 (the 08:10 value is after the shot, 'unavailable' is skipped)",
			r, deref(r.TempC), deref(r.RH))
	}
}

func TestPurgedHistoryFallsBackToTheHourlyMean(t *testing.T) {
	srv := fakeHA(t, `[]`, map[string][]map[string]any{
		tempID: {{"start": 0, "mean": 20.3}},
		rhID:   {{"start": 0, "mean": 51.2}},
	})
	defer srv.Close()
	r, err := client(srv, true).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceHourly || deref(r.TempC) != 20.3 || deref(r.RH) != 51.2 {
		t.Errorf("got %+v temp=%v rh=%v, want ha_hourly 20.3 / 51.2", r, deref(r.TempC), deref(r.RH))
	}
}

func TestNothingAnywhereIsNoneNotAnError(t *testing.T) {
	srv := fakeHA(t, `[]`, nil)
	defer srv.Close()
	r, err := client(srv, false).At(context.Background(), shotAt)
	if err != nil {
		t.Fatal(err)
	}
	if r.Source != SourceNone || r.TempC != nil || r.RH != nil {
		t.Errorf("got %+v, want none with no values", r)
	}
}

func TestABadTokenIsAnErrorNotNone(t *testing.T) {
	srv := fakeHA(t, `[]`, nil)
	defer srv.Close()
	c := New(Config{URL: srv.URL, Token: "wrong", TempEntity: tempID})
	if _, err := c.At(context.Background(), shotAt); err == nil {
		t.Error("a refused token must be an error, so the shot is retried rather than marked none")
	}
}

type fakeStore struct {
	pending []store.AmbientPending
	set     map[int64]string
}

func (f *fakeStore) ShotsNeedingAmbient(limit int) ([]store.AmbientPending, error) {
	if len(f.pending) > limit {
		return f.pending[:limit], nil
	}
	return f.pending, nil
}

func (f *fakeStore) SetAmbient(id int64, _, _ *float64, source string) error {
	f.set[id] = source
	return nil
}

type fakeLooker struct{ failAfter int }

func (f *fakeLooker) At(context.Context, time.Time) (Reading, error) {
	if f.failAfter == 0 {
		return Reading{}, errors.New("unreachable")
	}
	f.failAfter--
	v := 21.0
	return Reading{TempC: &v, Source: SourceState}, nil
}

func TestAPassStopsAtTheFirstErrorWithoutMarkingTheRest(t *testing.T) {
	fs := &fakeStore{set: map[int64]string{}}
	for i := int64(1); i <= 5; i++ {
		fs.pending = append(fs.pending, store.AmbientPending{ID: i})
	}
	n, err := Pass(context.Background(), fs, &fakeLooker{failAfter: 2})
	if err == nil || n != 2 || len(fs.set) != 2 {
		t.Errorf("n=%d err=%v set=%v; want 2 filled, then the error, nothing else marked", n, err, fs.set)
	}
}

func deref(p *float64) any {
	if p == nil {
		return nil
	}
	return *p
}
