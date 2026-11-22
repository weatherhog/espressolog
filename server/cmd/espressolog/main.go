// espressolog server: binary ingest from the ESP32 into SQLite, JSON reads
// for the PWA. Single static binary, no CGO — deploys as one file.
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"log"
	"net/http"
	"path/filepath"
	"time"

	espressolog "espressolog"
	"espressolog/internal/ambient"
	"espressolog/internal/api"
	"espressolog/internal/live"
	"espressolog/internal/store"
)

func main() {
	addr := flag.String("addr", ":8080", "listen address")
	dbPath := flag.String("db", "espressolog.db", "SQLite database file")
	checkAmbient := flag.Bool("check-ambient", false,
		"check the Home Assistant room-sensor configuration from the environment, "+
			"look up two sample moments, print the results and exit; writes nothing")
	flag.Parse()

	if *checkAmbient {
		os.Exit(runAmbientCheck())
	}

	st, err := store.Open(*dbPath, espressolog.Migrations)
	if err != nil {
		log.Fatalf("store: %v", err)
	}
	defer st.Close()

	// Room temperature and humidity from Home Assistant, filled after
	// ingest. Configured by environment only (deploy/espressolog.env.example);
	// without it the worker does not run and the columns stay NULL.
	if cfg, ok := ambient.ConfigFromEnv(); ok {
		go (&ambient.Worker{St: st, HL: ambient.New(cfg)}).Run(context.Background())
		log.Printf("ambient: reading %s from Home Assistant", cfg.TempEntity)
	} else {
		log.Printf("ambient: not configured (ESPRESSOLOG_HA_URL/_TOKEN/_TEMP); room readings off")
	}

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

// runAmbientCheck is -check-ambient: the worker's own startup check plus two
// lookups, against the real Home Assistant, touching no database. The token
// is never printed.
func runAmbientCheck() int {
	cfg, ok := ambient.ConfigFromEnv()
	if !ok {
		fmt.Println("not configured: need ESPRESSOLOG_HA_URL, _TOKEN and _TEMP in the environment")
		return 2
	}
	c := ambient.New(cfg)
	ctx := context.Background()
	if err := c.Validate(ctx); err != nil {
		fmt.Printf("check FAILED: %v\n", err)
		return 1
	}
	fmt.Printf("check ok: %s / %s exist, units right, statistics answer\n", cfg.TempEntity, cfg.RHEntity)
	show := func(p *float64) string {
		if p == nil {
			return "-"
		}
		return fmt.Sprintf("%.2f", *p)
	}
	for _, at := range []time.Time{time.Now().Add(-3 * time.Hour), time.Now().AddDate(0, -1, 0)} {
		r, err := c.At(ctx, at)
		if err != nil {
			fmt.Printf("%s: error: %v\n", at.UTC().Format(time.RFC3339), err)
			continue
		}
		fmt.Printf("%s: %s C, %s %%RH (%s)\n", at.UTC().Format(time.RFC3339), show(r.TempC), show(r.RH), r.Source)
	}
	return 0
}
