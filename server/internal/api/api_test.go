package api

import (
	"bytes"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"

	espressolog "espressolog"
	"espressolog/internal/live"
	"espressolog/internal/store"
)

// These tests exist because of what the API layer did while it had none.
//
// Until 2026-10-12 `mux.Handle("/", web.Handler())` caught every unmatched
// request INCLUDING POSTs, and the SPA fallback answered an unknown path with
// index.html and 200 OK. The firmware reads 2xx as "stored" and deletes its
// only copy of the shot, so a typo in one config string silently destroyed the
// whole spool at one record every couple of seconds. Nothing logged it.
//
// Three rounds of human review found that; no test could have missed it.

func newTestHandler(t *testing.T) http.Handler {
	t.Helper()
	st, err := store.Open(filepath.Join(t.TempDir(), "test.db"), espressolog.Migrations)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { st.Close() })
	return New(st, live.New(t.TempDir()))
}

func do(t *testing.T, h http.Handler, method, path string, body []byte) *httptest.ResponseRecorder {
	t.Helper()
	var r *http.Request
	if body != nil {
		r = httptest.NewRequest(method, path, bytes.NewReader(body))
	} else {
		r = httptest.NewRequest(method, path, nil)
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}

// A request that is not the ingest endpoint must never answer 2xx, because the
// device treats 2xx as permission to delete.
func TestUnknownAPIPathsDoNotLookLikeSuccess(t *testing.T) {
	h := newTestHandler(t)
	for _, c := range []struct{ method, path string }{
		{"POST", "/api/v1/ingestt"},  // typo
		{"POST", "/api/v1/"},         // truncated
		{"POST", "/api/v1/nope"},     // wrong route
		{"GET", "/api/v1/ingest"},    // right path, wrong method
		{"DELETE", "/api/v1/ingest"}, // ditto
	} {
		w := do(t, h, c.method, c.path, []byte("x"))
		if w.Code >= 200 && w.Code < 300 {
			t.Errorf("%s %s returned %d — the device reads 2xx as stored and deletes the shot",
				c.method, c.path, w.Code)
		}
		if w.Header().Get(IngestMarker) != "" {
			t.Errorf("%s %s carried %s; only the ingest handler may set it",
				c.method, c.path, IngestMarker)
		}
	}
}

// The guard above covers /api/ only. An endpoint misconfigured to the site
// root still reaches the SPA and still answers 200 — which is exactly why the
// marker exists and why the device must require it rather than trust 2xx.
func TestSiteRootStillAnswers200AndIsWhyTheMarkerExists(t *testing.T) {
	h := newTestHandler(t)
	w := do(t, h, "POST", "/espressolog/api/v1/ingest", []byte("x"))
	if w.Header().Get(IngestMarker) != "" {
		t.Fatalf("a non-ingest path set %s", IngestMarker)
	}
	// Whatever status the SPA fallback gives, the absence of the marker is
	// what must keep the device from deleting. Assert that directly.
	if w.Code >= 200 && w.Code < 300 && w.Header().Get(IngestMarker) != "" {
		t.Fatal("2xx with a marker from a path that is not ingest")
	}
}

// Registered routes must still reach their handlers — a catch-all that
// shadowed them would be worse than the bug it fixes.
func TestRegisteredRoutesStillRoute(t *testing.T) {
	h := newTestHandler(t)
	for _, c := range []struct{ method, path string }{
		{"GET", "/api/v1/shots"},
		{"GET", "/api/v1/beans"},
		{"GET", "/healthz"},
	} {
		w := do(t, h, c.method, c.path, nil)
		if w.Code == http.StatusNotFound {
			t.Errorf("%s %s 404s — the /api/ catch-all is shadowing a real route",
				c.method, c.path)
		}
	}
}

// A truncated upload is a dropped TCP connection, not a verdict on the record.
// It returned 400 until 2026-10-12, and the device quarantines 4xx into
// /spool/bad where nothing ever re-uploads — so one WiFi hiccup beside a
// vibratory pump permanently binned a good shot.
type erroringBody struct{ n int }

func (e *erroringBody) Read(p []byte) (int, error) {
	if e.n > 0 {
		e.n--
		p[0] = 0x4C
		return 1, nil
	}
	return 0, io.ErrUnexpectedEOF
}

func TestTruncatedUploadIsNotAVerdictOnTheRecord(t *testing.T) {
	h := newTestHandler(t)
	r := httptest.NewRequest("POST", "/api/v1/ingest", &erroringBody{n: 4})
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)

	if w.Code >= 400 && w.Code < 500 {
		t.Errorf("truncated body returned %d; a 4xx tells the device to quarantine "+
			"a record that would upload fine on retry", w.Code)
	}
	if w.Header().Get(IngestMarker) != "" {
		t.Errorf("a read failure carried %s — the device would treat it as a verdict",
			IngestMarker)
	}
}

// 422 is the one status the device acts on irreversibly besides 2xx, so it
// must carry the marker: quarantine is a one-way door.
func TestUndecodableRecordIs422AndMarked(t *testing.T) {
	h := newTestHandler(t)
	w := do(t, h, "POST", "/api/v1/ingest", []byte("not a record at all"))
	if w.Code != http.StatusUnprocessableEntity {
		t.Fatalf("undecodable record returned %d, want 422", w.Code)
	}
	if w.Header().Get(IngestMarker) == "" {
		t.Errorf("422 did not carry %s; the device requires it before quarantining, "+
			"so a 422 from any other service would bin a good shot", IngestMarker)
	}
}

// An oversize body must not be mistaken for success either.
func TestOversizeRecordIsRefused(t *testing.T) {
	h := newTestHandler(t)
	w := do(t, h, "POST", "/api/v1/ingest", bytes.Repeat([]byte{0}, maxRecordBytes+2))
	if w.Code >= 200 && w.Code < 300 {
		t.Errorf("oversize body returned %d", w.Code)
	}
}

// The empty-PATCH 500 is load-bearing: the capture screen relies on it being
// an error, which is why it skips the request when nothing was touched.
func TestEmptyPatchIsAnError(t *testing.T) {
	h := newTestHandler(t)
	w := do(t, h, "PATCH", "/api/v1/shots/1", []byte(`{}`))
	if w.Code >= 200 && w.Code < 300 {
		t.Errorf("PATCH with no fields returned %d; web/src/Capture.jsx guards "+
			"against this and the guard would be dead code", w.Code)
	}
	if strings.Contains(w.Body.String(), "<html") {
		t.Error("PATCH to a real route fell through to the SPA")
	}
}
