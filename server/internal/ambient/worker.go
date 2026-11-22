package ambient

import (
	"context"
	"log"
	"time"

	"espressolog/internal/store"
)

// Interval between passes while things work, and how many lookups one pass
// makes. A shot just pulled is filled within one interval (newest first);
// the archive backfill drains at Batch per pass, ~200 shots in ten minutes.
const (
	Interval    = 30 * time.Second
	Batch       = 10
	MaxInterval = 10 * time.Minute // a pass that failed entirely backs off to this
	// Scan is how many pending shots one pass looks at, so shots whose
	// lookups keep failing (backed off below) cannot hide the ones behind
	// them.
	Scan = 500
	// A shot whose lookup failed is retried after RetryMin, doubling up to
	// RetryMax: one shot that always fails must not stall the backfill
	// (review 2026-11-22).
	RetryMin = time.Minute
	RetryMax = 6 * time.Hour
)

// Store is what the worker needs from store.Store.
type Store interface {
	ShotsNeedingAmbient(limit int) ([]store.AmbientPending, error)
	SetAmbient(id int64, tempC, rh *float64, source string) error
}

// Looker is what the worker needs from Client, so tests can fake it.
type Looker interface {
	Validate(ctx context.Context) error
	At(ctx context.Context, t time.Time) (Reading, error)
}

// Worker fills ambient readings. Ingest never waits on it: a shot is stored
// first and gets its room reading afterwards (invariant 6), and an outage of
// Home Assistant costs time, not data.
type Worker struct {
	St    Store
	HL    Looker
	Now   func() time.Time
	retry map[int64]backoff
}

type backoff struct {
	next time.Time
	wait time.Duration
}

// Run validates the configuration, then fills readings until ctx ends. It
// logs when its state changes (working, failing, recovered), not on every
// pass, and backs off while every lookup fails.
func (w *Worker) Run(ctx context.Context) {
	if w.Now == nil {
		w.Now = time.Now
	}
	wait := Interval
	sleep := func() bool {
		select {
		case <-ctx.Done():
			return false
		case <-time.After(wait):
			return true
		}
	}
	// Nothing is written until the sensors are known to exist with the
	// right units.
	logged := ""
	for {
		err := w.HL.Validate(ctx)
		if err == nil {
			break
		}
		if msg := err.Error(); msg != logged {
			log.Printf("ambient: not starting: %v (retrying)", err)
			logged = msg
		}
		wait = min(wait*2, MaxInterval)
		if !sleep() {
			return
		}
	}
	log.Printf("ambient: Home Assistant sensors checked; filling room readings")
	wait = Interval
	failing := false
	for {
		n, tried, err := w.Pass(ctx)
		switch {
		case err != nil || (tried > 0 && n == 0):
			if !failing {
				log.Printf("ambient: lookups failing (%v); backing off", err)
				failing = true
			}
			wait = min(wait*2, MaxInterval)
		default:
			if failing {
				log.Printf("ambient: lookups working again")
				failing = false
			}
			if n > 0 {
				log.Printf("ambient: filled %d shot(s)", n)
			}
			wait = Interval
		}
		if !sleep() {
			return
		}
	}
}

// Pass makes up to Batch lookups among the pending shots that are due, and
// returns how many it filled, how many it tried, and the last lookup error.
// A failing shot is skipped for its backoff, and the pass goes on.
func (w *Worker) Pass(ctx context.Context) (filled, tried int, lastErr error) {
	if w.retry == nil {
		w.retry = map[int64]backoff{}
	}
	if w.Now == nil {
		w.Now = time.Now
	}
	pending, err := w.St.ShotsNeedingAmbient(Scan)
	if err != nil {
		return 0, 0, err
	}
	now := w.Now()
	for _, p := range pending {
		if tried >= Batch {
			break
		}
		if b, ok := w.retry[p.ID]; ok && now.Before(b.next) {
			continue
		}
		if !p.ClockValid {
			if err := w.St.SetAmbient(p.ID, nil, nil, SourceNoClock); err != nil {
				return filled, tried, err
			}
			filled++
			continue
		}
		tried++
		r, err := w.HL.At(ctx, p.StartedAt)
		if err != nil {
			b := w.retry[p.ID]
			b.wait = min(max(b.wait*2, RetryMin), RetryMax)
			b.next = now.Add(b.wait)
			w.retry[p.ID] = b
			lastErr = err
			continue
		}
		if err := w.St.SetAmbient(p.ID, r.TempC, r.RH, r.Source); err != nil {
			return filled, tried, err
		}
		delete(w.retry, p.ID)
		filled++
	}
	return filled, tried, lastErr
}
