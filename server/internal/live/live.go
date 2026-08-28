// Package live fans the device's real-time sample stream out to browsers.
// Strictly ephemeral: nothing here is stored, and nothing downstream may
// depend on it — the spool is the record, the tablet is a screen.
package live

import (
	"context"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/coder/websocket"
)

type Hub struct {
	mu      sync.Mutex
	clients map[chan []byte]bool
}

func New() *Hub {
	return &Hub{clients: make(map[chan []byte]bool)}
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
