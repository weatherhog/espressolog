package ambient

import (
	"context"
	"log"
	"time"

	"espressolog/internal/store"
)

// Interval between passes, and how many shots one pass looks up. A shot
// just pulled is filled within one interval (newest first); the archive
// backfill drains at Batch per pass, ~200 shots in ten minutes.
const (
	Interval = 30 * time.Second
	Batch    = 10
)

// Store is what the worker needs from store.Store.
type Store interface {
	ShotsNeedingAmbient(limit int) ([]store.AmbientPending, error)
	SetAmbient(id int64, tempC, rh *float64, source string) error
}

// Looker is what the worker needs from Client, so tests can fake it.
type Looker interface {
	At(ctx context.Context, t time.Time) (Reading, error)
}

// Run fills ambient readings until ctx ends. A lookup error stops the pass
// and is retried next pass -- it never marks a shot, so an outage of Home
// Assistant costs time, not data. Ingest never waits on this: a shot is
// stored first and gets its room reading afterwards (invariant 6).
func Run(ctx context.Context, st Store, hl Looker) {
	t := time.NewTicker(Interval)
	defer t.Stop()
	for {
		if n, err := Pass(ctx, st, hl); err != nil {
			log.Printf("ambient: %v (filled %d this pass; retrying)", err, n)
		} else if n > 0 {
			log.Printf("ambient: filled %d shot(s)", n)
		}
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}
	}
}

// Pass looks up one batch and returns how many shots it filled.
func Pass(ctx context.Context, st Store, hl Looker) (int, error) {
	pending, err := st.ShotsNeedingAmbient(Batch)
	if err != nil {
		return 0, err
	}
	n := 0
	for _, p := range pending {
		r, err := hl.At(ctx, p.StartedAt)
		if err != nil {
			return n, err
		}
		if err := st.SetAmbient(p.ID, r.TempC, r.RH, r.Source); err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}
