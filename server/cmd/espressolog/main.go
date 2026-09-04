// espressolog server: binary ingest from the ESP32 into SQLite, JSON reads
// for the PWA. Single static binary, no CGO — deploys as one file.
package main

import (
	"flag"
	"log"
	"net/http"
	"path/filepath"
	"time"

	espressolog "espressolog"
	"espressolog/internal/api"
	"espressolog/internal/live"
	"espressolog/internal/store"
)

func main() {
	addr := flag.String("addr", ":8080", "listen address")
	dbPath := flag.String("db", "espressolog.db", "SQLite database file")
	flag.Parse()

	st, err := store.Open(*dbPath, espressolog.Migrations)
	if err != nil {
		log.Fatalf("store: %v", err)
	}
	defer st.Close()

	// Raw-stream journal lives next to the database (StateDirectory on the LXC).
	liveDir := filepath.Join(filepath.Dir(*dbPath), "live")
	srv := &http.Server{
		Addr:    *addr,
		Handler: api.New(st, live.New(liveDir)),
		// Header timeout only: Read/WriteTimeout would put deadlines on the
		// long-lived live WebSockets. Regular handlers finish in milliseconds.
		ReadHeaderTimeout: 10 * time.Second,
	}
	log.Printf("espressolog listening on %s, db %s", *addr, *dbPath)
	log.Fatal(srv.ListenAndServe())
}
