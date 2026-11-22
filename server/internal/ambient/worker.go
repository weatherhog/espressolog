package ambient

import (
	"context"
	"errors"
	"fmt"
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
	// RecentRetry: how soon a shot too young for its hour's mean is tried
	// again (ErrTooRecent).
	RecentRetry = 15 * time.Minute
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

// Revalidate is how often the sensors are checked again while running: a
// sensor renamed or switched to another unit stops the writes within about
// an hour -- the check runs between passes, so it can come up to one
// backed-off sleep plus one pass late (review 2026-11-22).
const Revalidate = time.Hour

// Worker fills ambient readings. Ingest never waits on it: a shot is stored
// first and gets its room reading afterwards (invariant 6), and an outage of
// Home Assistant costs time, not data.
type Worker struct {
	St    Store
	HL    Looker
	Now   func() time.Time
	retry map[int64]backoff
	noted map[int64]bool // shots whose failure (or bad started_at) was logged
}

type backoff struct {
	next time.Time
	wait time.Duration
}

// PassResult is what one pass did. Filled counts Home Assistant answers
// written; Marked counts shots written without asking it (no_clock), which
// say nothing about whether it works (review 2026-11-22).
type PassResult struct {
	Filled, Marked, Tried, Failed int
	LastErr                       error
}

// Health is the worker's logged state.
type Health int

const (
	Working Health = iota
	Failing
)

// Next decides the state after a pass, and what to log. Pure, for tests.
// A pass that tried nothing (everything pending is in its backoff, or
// nothing is pending) changes nothing: it is not evidence either way
// (review 2026-11-22: it read as "working again" while Home Assistant was
// down). Working again needs a shot actually filled.
//
//	One shot that keeps failing while nothing else is tried is not an
//
// outage: it has its own backoff, and treating it as one slowed every new
// shot to the 10-minute interval (review 2026-11-22). So Failing needs more
// than one failure in a pass with no fill.
func Next(h Health, r PassResult) (Health, string) {
	switch {
	case r.Tried == 0:
		return h, ""
	case r.Filled == 0 && r.Failed > 1:
		if h == Working {
			return Failing, fmt.Sprintf("ambient: lookups failing (%v); backing off", r.LastErr)
		}
		return Failing, ""
	case r.Filled > 0 && h == Failing:
		return Working, "ambient: lookups working again"
	}
	return h, ""
}

// Run validates the configuration, then fills readings until ctx ends,
// re-validating every Revalidate. It logs when its state changes, not on
// every pass, and backs off while every lookup fails.
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
	for {
		// Nothing is written until the sensors check out -- at start, and
		// again every Revalidate.
		logged := ""
		for {
			err := w.HL.Validate(ctx)
			if err == nil {
				break
			}
			if msg := err.Error(); msg != logged {
				log.Printf("ambient: not writing: %v (retrying)", err)
				logged = msg
			}
			wait = min(wait*2, MaxInterval)
			if !sleep() {
				return
			}
		}
		log.Printf("ambient: Home Assistant sensors checked; filling room readings")
		validated := w.Now()
		wait = Interval
		health := Working
		for w.Now().Sub(validated) < Revalidate {
			r := w.Pass(ctx)
			var msg string
			prev := health
			health, msg = Next(health, r)
			if msg != "" {
				log.Print(msg)
			}
			if prev == Failing && health == Working {
				// The outage is over: shots it pushed back should not wait out
				// backoffs of up to RetryMax.
				w.retry = map[int64]backoff{}
			}
			if r.Filled+r.Marked > 0 {
				log.Printf("ambient: filled %d shot(s), %d without clock time", r.Filled, r.Marked)
			}
			if health == Failing {
				wait = min(wait*2, MaxInterval)
			} else {
				wait = Interval
			}
			if !sleep() {
				return
			}
		}
	}
}

// Pass makes up to Batch lookups among the pending shots that are due.
// A failing shot is skipped for its backoff, and the pass goes on; its first
// failure is logged once, so a shot that keeps failing is never silent
// (review 2026-11-22). ErrTooRecent is not a failure: the shot waits.
func (w *Worker) Pass(ctx context.Context) (r PassResult) {
	if w.retry == nil {
		w.retry = map[int64]backoff{}
	}
	if w.noted == nil {
		w.noted = map[int64]bool{}
	}
	if w.Now == nil {
		w.Now = time.Now
	}
	pending, err := w.St.ShotsNeedingAmbient(Scan)
	if err != nil {
		r.LastErr = err
		r.Failed = 1
		r.Tried = 1
		return r
	}
	now := w.Now()
	for _, p := range pending {
		if r.Tried >= Batch {
			break
		}
		if b, ok := w.retry[p.ID]; ok && now.Before(b.next) {
			continue
		}
		if p.BadTime {
			if !w.noted[p.ID] {
				log.Printf("ambient: shot %d: started_at does not parse; skipped", p.ID)
				w.noted[p.ID] = true
			}
			continue
		}
		if !p.ClockValid {
			if err := w.St.SetAmbient(p.ID, nil, nil, SourceNoClock); err != nil {
				// A write that fails is a failure: counted, so it is logged
				// and backed off (review 2026-11-22).
				r.Tried++
				r.Failed++
				r.LastErr = err
				return r
			}
			r.Marked++
			continue
		}
		r.Tried++
		reading, err := w.HL.At(ctx, p.StartedAt)
		if err != nil {
			b := w.retry[p.ID]
			if errors.Is(err, ErrTooRecent) {
				// Not a failure, and not the start of a backoff: only the
				// next try moves (review 2026-11-22).
				b.next = now.Add(RecentRetry)
				w.retry[p.ID] = b
				r.Tried--
				continue
			}
			b.wait = min(max(b.wait*2, RetryMin), RetryMax)
			b.next = now.Add(b.wait)
			w.retry[p.ID] = b
			if !w.noted[p.ID] {
				log.Printf("ambient: shot %d: %v (retrying with backoff)", p.ID, err)
				w.noted[p.ID] = true
			}
			r.Failed++
			r.LastErr = err
			continue
		}
		if err := w.St.SetAmbient(p.ID, reading.TempC, reading.RH, reading.Source); err != nil {
			r.Failed++
			r.LastErr = err
			return r
		}
		delete(w.retry, p.ID)
		delete(w.noted, p.ID)
		r.Filled++
	}
	return r
}
