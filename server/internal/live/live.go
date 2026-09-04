// Package live fans the device's real-time sample stream out to browsers.
// Strictly ephemeral: nothing here is stored, and nothing downstream may
// depend on it — the spool is the record, the tablet is a screen.
package live

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/coder/websocket"
)

type Hub struct {
	mu      sync.Mutex
	clients map[chan []byte]bool

	// Raw-stream journal: every device frame appended to <logDir>/YYYY-MM-DD.ndjson
	// with a server timestamp. The scales only stream while awake (~30 min a
	// day), so this is ~1 MB/day — and it turns "a shot went missing, why?"
	// from guesswork into a replay (firmware/test/host/replay). Best-effort:
	// a journal write failure never touches the live fan-out.
	logDir  string
	logDay  string
	logFile *os.File
}

// New creates a hub. logDir "" disables the raw-stream journal.
func New(logDir string) *Hub {
	if logDir != "" {
		if err := os.MkdirAll(logDir, 0o755); err != nil {
			log.Printf("live: journal disabled, cannot create %s: %v", logDir, err)
			logDir = ""
		}
	}
	return &Hub{clients: make(map[chan []byte]bool), logDir: logDir}
}

// journal appends one device frame; rotates the file at the day boundary.
func (h *Hub) journal(frame []byte) {
	if h.logDir == "" {
		return
	}
	now := time.Now()
	day := now.Format("2006-01-02")
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.logFile == nil || day != h.logDay {
		if h.logFile != nil {
			h.logFile.Close()
		}
		f, err := os.OpenFile(filepath.Join(h.logDir, day+".ndjson"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
		if err != nil {
			log.Printf("live: journal open failed: %v", err)
			h.logFile = nil
			return
		}
		h.logFile = f
		h.logDay = day
	}
	// {"ts":<unix ms>,"f":<frame>} — frame is already JSON from the device.
	fmt.Fprintf(h.logFile, "{\"ts\":%d,\"f\":%s}\n", now.UnixMilli(), frame)
}

// Broadcast sends one frame to every connected browser. Slow clients get
// their frame dropped rather than stalling the fan-out — this is a live
// view, a missed sample is meaningless.
func (h *Hub) Broadcast(msg []byte) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for ch := range h.clients {
		select {
		case ch <- msg:
		default:
		}
	}
}

func (h *Hub) subscribe() chan []byte {
	ch := make(chan []byte, 64)
	h.mu.Lock()
	h.clients[ch] = true
	h.mu.Unlock()
	return ch
}

func (h *Hub) unsubscribe(ch chan []byte) {
	h.mu.Lock()
	delete(h.clients, ch)
	h.mu.Unlock()
}

// DeviceHandler accepts the ESP32's connection and rebroadcasts each frame
// verbatim. One device; a second connection simply also broadcasts.
func (h *Hub) DeviceHandler() http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		c, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
		if err != nil {
			return
		}
		defer c.CloseNow()
		log.Printf("live: device connected from %s", r.RemoteAddr)
		c.SetReadLimit(1024)
		for {
			_, data, err := c.Read(r.Context())
			if err != nil {
				log.Printf("live: device disconnected: %v", err)
				return
			}
			h.journal(data)
			h.Broadcast(data)
		}
	}
}

// BrowserHandler streams broadcast frames to one viewer.
func (h *Hub) BrowserHandler() http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		c, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
		if err != nil {
			return
		}
		defer c.CloseNow()
		ch := h.subscribe()
		defer h.unsubscribe(ch)

		ctx := r.Context()
		go func() { // read pump: notice the tab closing
			for {
				if _, _, err := c.Read(ctx); err != nil {
					return
				}
			}
		}()
		for msg := range ch {
			wctx, cancel := context.WithTimeout(ctx, 2*time.Second)
			err := c.Write(wctx, websocket.MessageText, msg)
			cancel()
			if err != nil {
				return
			}
		}
	}
}
