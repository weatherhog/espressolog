// Package api is the HTTP surface: binary ingest from the ESP32 and JSON
// reads for the PWA (and curl).
package api

import (
	"database/sql"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"strconv"
	"strings"
	"time"

	"espressolog/internal/record"
	"espressolog/internal/store"
	"espressolog/internal/web"
)

const maxRecordBytes = 64 << 10 // header + 2048 samples is ~20 KB; 64 KB is generous

func New(st *store.Store) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("POST /api/v1/ingest", ingest(st))
	mux.HandleFunc("GET /api/v1/shots", listShots(st))
	mux.HandleFunc("GET /api/v1/shots/{id}", getShot(st))
	mux.HandleFunc("PATCH /api/v1/shots/{id}", patchShot(st))
	mux.HandleFunc("POST /api/v1/shots/{id}/tasting", postTasting(st))
	mux.HandleFunc("GET /api/v1/beans", listBeans(st))
	mux.HandleFunc("POST /api/v1/beans", postBean(st))
	mux.HandleFunc("PATCH /api/v1/beans/{id}", patchBean(st))
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		if err := st.Healthy(); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Write([]byte("ok\n"))
	})
	mux.Handle("/", web.Handler())
	return mux
}

func ingest(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		raw, err := io.ReadAll(io.LimitReader(r.Body, maxRecordBytes+1))
		if err != nil {
			http.Error(w, "read: "+err.Error(), http.StatusBadRequest)
			return
		}
		if len(raw) > maxRecordBytes {
			http.Error(w, "record too large", http.StatusRequestEntityTooLarge)
			return
		}

		rec, err := record.Decode(raw)
		if err != nil {
			// 4xx on purpose: the firmware quarantines the file instead of
			// retrying a record the server will never accept.
			log.Printf("ingest: reject from %s: %v", r.Header.Get("X-Device-Id"), err)
			http.Error(w, "decode: "+err.Error(), http.StatusUnprocessableEntity)
			return
		}

		deviceID := strings.ToLower(strings.ReplaceAll(r.Header.Get("X-Device-Id"), "-", ":"))
		if deviceID == "" {
			deviceID = "unknown"
		}

		res, err := st.Ingest(deviceID, rec, raw, time.Now())
		if err != nil {
			log.Printf("ingest: store error: %v", err)
			http.Error(w, "store: "+err.Error(), http.StatusInternalServerError)
			return
		}
		if res.Duplicate {
			log.Printf("ingest: duplicate %s %08x-%d", deviceID, rec.Header.BootID, rec.Header.Seq)
			w.WriteHeader(http.StatusOK) // a retried lost 200 — already stored
			return
		}
		log.Printf("ingest: %s %08x-%d type=%d -> shot=%d weighing=%d (%d samples, final %.2fg)",
			deviceID, rec.Header.BootID, rec.Header.Seq, rec.Header.RecordType,
			res.ShotID, res.WeighingID, rec.Header.SampleCount,
			float64(rec.Header.YieldFinalMg)/1000)
		w.WriteHeader(http.StatusCreated)
	}
}

func listShots(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		limit := 50
		if q := r.URL.Query().Get("limit"); q != "" {
			if n, err := strconv.Atoi(q); err == nil && n > 0 && n <= 1000 {
				limit = n
			}
		}
		rows, err := st.Shots(limit)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSONRows(w, rows)
	}
}

func getShot(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		id, err := strconv.ParseInt(r.PathValue("id"), 10, 64)
		if err != nil {
			http.Error(w, "bad id", http.StatusBadRequest)
			return
		}
		shot, samples, err := st.Shot(id)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		if shot == nil {
			http.Error(w, "not found", http.StatusNotFound)
			return
		}
		if samples == nil {
			samples = []map[string]any{}
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]any{"shot": shot, "samples": samples})
	}
}

func readJSONBody(w http.ResponseWriter, r *http.Request) (map[string]any, bool) {
	var fields map[string]any
	if err := json.NewDecoder(io.LimitReader(r.Body, 64<<10)).Decode(&fields); err != nil {
		http.Error(w, "bad json: "+err.Error(), http.StatusBadRequest)
		return nil, false
	}
	return fields, true
}

func pathID(w http.ResponseWriter, r *http.Request) (int64, bool) {
	id, err := strconv.ParseInt(r.PathValue("id"), 10, 64)
	if err != nil {
		http.Error(w, "bad id", http.StatusBadRequest)
		return 0, false
	}
	return id, true
}

func patchShot(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		id, ok := pathID(w, r)
		if !ok {
			return
		}
		fields, ok := readJSONBody(w, r)
		if !ok {
			return
		}
		if err := st.UpdateShot(id, fields); err != nil {
			code := http.StatusInternalServerError
			if err == sql.ErrNoRows {
				code = http.StatusNotFound
			}
			http.Error(w, err.Error(), code)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	}
}

func postTasting(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		id, ok := pathID(w, r)
		if !ok {
			return
		}
		fields, ok := readJSONBody(w, r)
		if !ok {
			return
		}
		tid, err := st.InsertTasting(id, fields)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusCreated)
		json.NewEncoder(w).Encode(map[string]any{"id": tid})
	}
}

func listBeans(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		rows, err := st.Beans()
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSONRows(w, rows)
	}
}

func postBean(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		fields, ok := readJSONBody(w, r)
		if !ok {
			return
		}
		id, err := st.InsertBean(fields)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusCreated)
		json.NewEncoder(w).Encode(map[string]any{"id": id})
	}
}

func patchBean(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		id, ok := pathID(w, r)
		if !ok {
			return
		}
		fields, ok := readJSONBody(w, r)
		if !ok {
			return
		}
		if err := st.UpdateBean(id, fields); err != nil {
			code := http.StatusInternalServerError
			if err == sql.ErrNoRows {
				code = http.StatusNotFound
			}
			http.Error(w, err.Error(), code)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	}
}

func writeJSONRows(w http.ResponseWriter, rows []map[string]any) {
	body, err := store.MarshalJSONRows(rows)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Write(body)
}
