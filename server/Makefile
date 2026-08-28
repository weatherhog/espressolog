# Build and deploy the espressolog server.
#
#   make test           run all Go tests
#   make build          native binary (development)
#   make build-linux    static linux/amd64 binary (the deploy target is a
#                       Debian LXC on Proxmox; pure-Go SQLite means no CGO)
#   make deploy         build-linux + install to $(HOST) + restart service
#
# The database lives on the target in /var/lib/espressolog/ and is never
# touched by a deploy.

HOST ?= root@espressolog.lan
BIN  = espressolog-server

test:
	go test ./...

build:
	go build -o $(BIN) ./cmd/espressolog

build-linux:
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -ldflags="-s -w" -o $(BIN)-linux ./cmd/espressolog

deploy: build-linux
	scp $(BIN)-linux $(HOST):/usr/local/bin/$(BIN).new
	scp deploy/espressolog.service $(HOST):/etc/systemd/system/espressolog.service
	ssh $(HOST) 'mv /usr/local/bin/$(BIN).new /usr/local/bin/$(BIN) && systemctl daemon-reload && systemctl enable --now espressolog && systemctl restart espressolog && sleep 1 && systemctl is-active espressolog'

.PHONY: test build build-linux deploy
