// Package web serves the built PWA from inside the binary — one file to
// deploy, per the architecture. `make web` (server/Makefile) builds the
// Vite app and copies it into dist/ here; the directory is gitignored
// except for a placeholder so plain `go build` still compiles.
package web

import (
	"embed"
	"io/fs"
	"net/http"
	"strings"
)

//go:embed all:dist
var dist embed.FS

func Handler() http.Handler {
	sub, err := fs.Sub(dist, "dist")
	if err != nil {
		panic(err)
	}
	files := http.FileServerFS(sub)
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if _, err := fs.Stat(sub, "index.html"); err != nil {
			http.Error(w, "web UI not built into this binary (run `make web` before building)", http.StatusNotFound)
			return
		}
		// SPA fallback: unknown paths get index.html, assets get themselves.
		p := strings.TrimPrefix(r.URL.Path, "/")
		if p != "" {
			if _, err := fs.Stat(sub, p); err != nil {
				r.URL.Path = "/"
			}
		}
		files.ServeHTTP(w, r)
	})
}
