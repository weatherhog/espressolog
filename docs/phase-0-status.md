# Phase 0 status — as of 2026-09-02

Phase 0a (scale logging, stages 0–6) is **complete and in production**.
This file is the resume point: read it (and CLAUDE.md) before continuing.

## What runs where

| Piece | Where | State |
|---|---|---|
| Firmware 0.6.2, detector 0a.4 | ESP32-S3 on a USB charger by the machine | both Bookoos bound: yield `aa:bb:cc:dd:ee:01`, dose `aa:bb:cc:dd:ee:02` |
| Go server + SQLite + PWA | Proxmox LXC `espressolog`, Debian 13, `espressolog.lan` (DHCP-reserved) | systemd `espressolog.service`, db at `/var/lib/espressolog/espressolog.db` |
| HTTPS | Caddy on the same LXC, `https://espresso.example.com` | Let's Encrypt via Cloudflare DNS-01; CF token in `/etc/caddy/env` |
| DNS | AdGuard Home rewrite `espresso.example.com → espressolog.lan` | resolution is LAN-only; no public A record (challenge TXT only) |
| Deploys | `make deploy` in `server/` (web+binary), `pio run -t upload` in `firmware/` | ESP talks plain HTTP to `:8080` directly, never through Caddy |

## What works end-to-end (all verified with real shots)

- Shot detection (0a.4: baseline arming — no tare; cup-lift ends the shot
  — no 7 s wait; slow-drip tolerant, bump-tolerant), spool to flash, upload with delete-on-confirm, idempotent
  ingest, automatic **dose attribution** from the grinder scale
  (first production shot: 37.4 g / 24.4 s, dose 18.2 g `measured`)
- Live pull screen (ghost trace, editable target box), shots list with
  overlay + detail cards, capture flow (grind/dose/preinfusion/temp sticky,
  taste chips), beans with autocomplete / "same again" / edit, shot delete
  (raw record always survives in `ingest_record`)
- Failure paths proven live: server outage → spool+backoff → self-delivery;
  duplicate uploads deduped; same-ms BLE samples collapsed

## Daily workflow (the only rules)

1. Wake the scales (they sleep ~9 min idle; reconnect is automatic).
2. Weigh the dose on the grinder scale, lift it off — that's the weighing.
3. Cup on the drip-tray scale; one second of stillness arms the detector
   (0a.3 arms on ANY stable weight — taring is optional, nice for the
   display only). Board LED: green = armed, blue = recording.
4. Pull the shot; lift the cup whenever it's done (0a.4 ends the shot on
   a ~2 s cup lift — no need to wait; a quick bump is ignored).
5. Tasting notes on the Capture tab (4 taps). Everything else is automatic.

## Next milestones, in order

1. **0f — noise floor** (trigger: ~30 shots, one recipe, changing nothing).
   Build the first `analysis/` notebook against the SQLite file: std dev of
   shot time and mean flow on the fixed recipe. That number is the Phase-3
   deadband and the go/no-go on closed-loop control. CLAUDE.md: the most
   important milestone and the one that gets skipped.
2. **0b — display bus** (trigger: logic analyser arrives; ordered: USB-C
   FX2LP clone + hook clips + resistor kit ~€30). Photograph everything,
   then PulseView on J5 during heat-up; hunt the byte that tracks the
   displayed temperature. Abandon cheaply if opaque.
3. **0c — switch sensing** (after 0b's machine-open session identified the
   J2/J4/J5 connector types → order JST pigtails + 30 AWG wire then).
   100 K/200 K divider on PB5/PB6, gives real shot boundaries and
   `first_drip_ms`.
4. **0d — flowmeter**, **0e — temperature** (if 0b cracked it), per
   docs/phase-0-plan.md.

## Known quirks (details in the auto-memory notes)

- Opening the **COM/UART port resets the board**; the native USB port
  doesn't. Never reflash mid-brew-session (cost one shot already).
- Chip claims 16 MB flash and truly has it, but **partitions must stay
  below 8 MB** (boot-loops otherwise) — documented in CLAUDE.md.
- Scale-only `settle_offset_g` is often ~0 (solenoid dump kills the tail);
  its Phase-1 meaning needs 0c's pump-off reference.
- A missed shot is recoverable if a serial logger was running:
  `firmware/test/host/replay` (did it once, shot 3).
- CF token renewal path: token lives only in `/etc/caddy/env`; update via
  a real terminal (not the `!` prefix — no TTY), then `systemctl restart caddy`.
