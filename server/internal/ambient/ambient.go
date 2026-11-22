// Package ambient reads the room temperature and humidity at a moment in
// time from Home Assistant, for shot.ambient_c / shot.ambient_rh.
//
// Home Assistant CONSUMES this system's data and never stores it
// (CLAUDE.md, Architecture); reading a room sensor FROM it is the other
// direction and does not change that.
//
// Two sources, tried in this order:
//
//	SourceState  the last recorded state at or before the moment, from the
//	             REST history API. Exact, but Home Assistant purges raw
//	             states after its recorder's purge_keep_days (10 by default).
//	SourceHourly the mean of the hour containing the moment, from the
//	             long-term statistics (WebSocket recorder/statistics_during_period),
//	             which Home Assistant keeps indefinitely for any sensor with a
//	             state_class. Coarser: a room moves little within an hour.
//
// Nothing here is configured in the repo: the URL, token and entity ids come
// from the environment of the server process (see Config).
package ambient

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/coder/websocket"
)

const (
	SourceState  = "ha_state"
	SourceHourly = "ha_hourly"
	// SourceNone: Home Assistant answered and had no temperature for that
	// moment (a humidity may still be returned). Recorded so the worker does
	// not ask again; a network error is NOT this, and is retried.
	SourceNone = "none"
	// SourceNoClock: the shot was recorded without clock time, so its
	// started_at is the upload time; not looked up.
	SourceNoClock = "no_clock"
)

// StateLookback is how far before the moment the history query starts. Home
// Assistant returns the state in force at the start of the window as its
// first entry, so any window works; a short one keeps the answer small.
const StateLookback = 6 * time.Hour

// Config comes from the environment:
//
//	ESPRESSOLOG_HA_URL      base URL, e.g. https://homeassistant.example
//	ESPRESSOLOG_HA_TOKEN    a long-lived access token
//	ESPRESSOLOG_HA_TEMP     entity id of the temperature sensor (°C)
//	ESPRESSOLOG_HA_RH       entity id of the humidity sensor (%), optional
type Config struct {
	URL, Token, TempEntity, RHEntity string
}

// ConfigFromEnv returns the configuration and whether it is complete enough
// to use (URL, token and the temperature entity).
func ConfigFromEnv() (Config, bool) {
	c := Config{
		URL:        strings.TrimRight(os.Getenv("ESPRESSOLOG_HA_URL"), "/"),
		Token:      os.Getenv("ESPRESSOLOG_HA_TOKEN"),
		TempEntity: os.Getenv("ESPRESSOLOG_HA_TEMP"),
		RHEntity:   os.Getenv("ESPRESSOLOG_HA_RH"),
	}
	return c, c.URL != "" && c.Token != "" && c.TempEntity != ""
}

// Reading is what Home Assistant had for one moment. Nil fields were not
// available; Source says which API answered, or SourceNone.
type Reading struct {
	TempC  *float64
	RH     *float64
	Source string
}

type Client struct {
	cfg  Config
	http *http.Client
}

func New(cfg Config) *Client {
	return &Client{cfg: cfg, http: &http.Client{Timeout: 15 * time.Second}}
}

func (c *Client) entities() []string {
	if c.cfg.RHEntity == "" {
		return []string{c.cfg.TempEntity}
	}
	return []string{c.cfg.TempEntity, c.cfg.RHEntity}
}

// Validate checks once, before anything is written, that the configured
// entities exist and report the units the columns hold: °C and %. Without
// it a typo in an entity id reads as "no data" -- empty history, empty
// statistics -- and would mark every shot 'none' for good (review
// 2026-11-22).
func (c *Client) Validate(ctx context.Context) error {
	want := map[string]string{c.cfg.TempEntity: "°C"}
	if c.cfg.RHEntity != "" {
		want[c.cfg.RHEntity] = "%"
	}
	for id, unit := range want {
		req, err := http.NewRequestWithContext(ctx, http.MethodGet,
			c.cfg.URL+"/api/states/"+url.PathEscape(id), nil)
		if err != nil {
			return err
		}
		req.Header.Set("Authorization", "Bearer "+c.cfg.Token)
		resp, err := c.http.Do(req)
		if err != nil {
			return err
		}
		var st struct {
			Attributes struct {
				Unit string `json:"unit_of_measurement"`
			} `json:"attributes"`
		}
		err = json.NewDecoder(resp.Body).Decode(&st)
		resp.Body.Close()
		switch {
		case resp.StatusCode == http.StatusNotFound:
			return fmt.Errorf("entity %s does not exist", id)
		case resp.StatusCode != http.StatusOK:
			return fmt.Errorf("entity %s: HTTP %d", id, resp.StatusCode)
		case err != nil:
			return fmt.Errorf("entity %s: %w", id, err)
		case st.Attributes.Unit != unit:
			return fmt.Errorf("entity %s reports %q, want %q", id, st.Attributes.Unit, unit)
		}
	}
	return nil
}

// At returns the reading for moment t. An error means Home Assistant could
// not be asked (network, auth, a malformed answer); the caller retries later.
// Each value comes from the exact state where there is one, else from the
// hour's mean; Source names where the TEMPERATURE came from.
func (c *Client) At(ctx context.Context, t time.Time) (Reading, error) {
	r, err := c.state(ctx, t)
	if err != nil {
		return Reading{}, err
	}
	if r.TempC != nil {
		r.Source = SourceState
	}
	if r.TempC == nil || (r.RH == nil && c.cfg.RHEntity != "") {
		h, err := c.hourly(ctx, t)
		if err != nil {
			return Reading{}, err
		}
		if r.TempC == nil && h.TempC != nil {
			r.TempC, r.Source = h.TempC, SourceHourly
		}
		if r.RH == nil {
			r.RH = h.RH
		}
	}
	if r.TempC == nil {
		r.Source = SourceNone
	}
	return r, nil
}

type histEntry struct {
	EntityID    string `json:"entity_id"`
	State       string `json:"state"`
	LastChanged string `json:"last_changed"`
}

// state asks the REST history API for [t − StateLookback, t] and takes, per
// entity, the last numeric state at or before t. Purged history returns no
// entries, which is "nothing", not an error.
func (c *Client) state(ctx context.Context, t time.Time) (Reading, error) {
	start := t.Add(-StateLookback).UTC().Format(time.RFC3339)
	q := url.Values{}
	q.Set("end_time", t.UTC().Format(time.RFC3339))
	q.Set("filter_entity_id", strings.Join(c.entities(), ","))
	q.Set("no_attributes", "")
	u := c.cfg.URL + "/api/history/period/" + url.PathEscape(start) + "?" + q.Encode()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u, nil)
	if err != nil {
		return Reading{}, err
	}
	req.Header.Set("Authorization", "Bearer "+c.cfg.Token)
	resp, err := c.http.Do(req)
	if err != nil {
		return Reading{}, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return Reading{}, fmt.Errorf("history: HTTP %d", resp.StatusCode)
	}
	var lists [][]histEntry
	if err := json.NewDecoder(resp.Body).Decode(&lists); err != nil {
		return Reading{}, fmt.Errorf("history: %w", err)
	}
	var r Reading
	for _, list := range lists {
		v := lastAtOrBefore(list, t)
		if v == nil || len(list) == 0 {
			continue
		}
		switch list[0].EntityID {
		case c.cfg.TempEntity:
			r.TempC = v
		case c.cfg.RHEntity:
			r.RH = v
		}
	}
	return r, nil
}

// lastAtOrBefore returns the value of the state IN FORCE at t -- the last
// entry whose last_changed is at or before t -- or nil when that state is
// not a number ("unavailable", "unknown"). An older numeric value is NOT
// reached back for: a sensor that went unavailable hours before the shot
// would pass a stale reading off as exact (review 2026-11-22); the caller
// falls back to the hour's mean instead.
func lastAtOrBefore(list []histEntry, t time.Time) *float64 {
	var in *histEntry
	for i := range list {
		at, err := time.Parse(time.RFC3339Nano, list[i].LastChanged)
		if err != nil || at.After(t) {
			continue
		}
		in = &list[i]
	}
	if in == nil {
		return nil
	}
	v, err := strconv.ParseFloat(in.State, 64)
	if err != nil {
		return nil
	}
	return &v
}

// hourly asks the WebSocket API for the long-term statistics mean of the
// hour containing t.
func (c *Client) hourly(ctx context.Context, t time.Time) (Reading, error) {
	wsURL := "ws" + strings.TrimPrefix(c.cfg.URL, "http") + "/api/websocket"
	ctx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		return Reading{}, fmt.Errorf("statistics: %w", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")
	conn.SetReadLimit(4 << 20)

	read := func() (map[string]json.RawMessage, error) {
		_, b, err := conn.Read(ctx)
		if err != nil {
			return nil, err
		}
		var m map[string]json.RawMessage
		return m, json.Unmarshal(b, &m)
	}
	write := func(v any) error {
		b, err := json.Marshal(v)
		if err != nil {
			return err
		}
		return conn.Write(ctx, websocket.MessageText, b)
	}
	typeOf := func(m map[string]json.RawMessage) string {
		var s string
		_ = json.Unmarshal(m["type"], &s)
		return s
	}

	m, err := read() // auth_required
	if err != nil {
		return Reading{}, fmt.Errorf("statistics: %w", err)
	}
	if typeOf(m) != "auth_required" {
		return Reading{}, fmt.Errorf("statistics: unexpected %q", typeOf(m))
	}
	if err := write(map[string]string{"type": "auth", "access_token": c.cfg.Token}); err != nil {
		return Reading{}, err
	}
	if m, err = read(); err != nil {
		return Reading{}, fmt.Errorf("statistics: %w", err)
	}
	if typeOf(m) != "auth_ok" {
		return Reading{}, errors.New("statistics: authentication refused")
	}
	hour := t.UTC().Truncate(time.Hour)
	if err := write(map[string]any{
		"id":            1,
		"type":          "recorder/statistics_during_period",
		"start_time":    hour.Format(time.RFC3339),
		"end_time":      hour.Add(time.Hour).Format(time.RFC3339),
		"statistic_ids": c.entities(),
		"period":        "hour",
		"types":         []string{"mean"},
		// Ask for the columns' own units; Home Assistant converts.
		"units": map[string]string{"temperature": "°C"},
	}); err != nil {
		return Reading{}, err
	}
	for {
		if m, err = read(); err != nil {
			return Reading{}, fmt.Errorf("statistics: %w", err)
		}
		if typeOf(m) != "result" {
			continue
		}
		var ok bool
		_ = json.Unmarshal(m["success"], &ok)
		if !ok {
			return Reading{}, fmt.Errorf("statistics: %s", string(m["error"]))
		}
		var res map[string][]struct {
			Mean *float64 `json:"mean"`
		}
		if err := json.Unmarshal(m["result"], &res); err != nil {
			return Reading{}, fmt.Errorf("statistics: %w", err)
		}
		var r Reading
		if rows := res[c.cfg.TempEntity]; len(rows) > 0 {
			r.TempC = rows[0].Mean
		}
		if c.cfg.RHEntity != "" {
			if rows := res[c.cfg.RHEntity]; len(rows) > 0 {
				r.RH = rows[0].Mean
			}
		}
		return r, nil
	}
}
