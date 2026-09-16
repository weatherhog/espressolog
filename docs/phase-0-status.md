# Phase 0 status — as of 2026-09-15

Phase 0a (scale logging, stages 0–6) is **complete and in production**.
This file is the resume point: read it (and CLAUDE.md) before continuing.

## What runs where

| Piece | Where | State |
|---|---|---|
| Firmware 0.6.5 (flash pending), detector 0a.6 | ESP32-S3 on a USB charger by the machine | both Bookoos bound: yield `aa:bb:cc:dd:ee:01`, dose `aa:bb:cc:dd:ee:02` |
| Go server + SQLite + PWA | Proxmox LXC `espressolog`, Debian 13, `espressolog.lan` (DHCP-reserved) | systemd `espressolog.service`, db at `/var/lib/espressolog/espressolog.db` |
| HTTPS | Caddy on the same LXC, `https://espresso.example.com` | Let's Encrypt via Cloudflare DNS-01; CF token in `/etc/caddy/env` |
| DNS | AdGuard Home rewrite `espresso.example.com → espressolog.lan` | resolution is LAN-only; no public A record (challenge TXT only) |
| Deploys | `make deploy` in `server/` (web+binary), `pio run -t upload` in `firmware/` | ESP talks plain HTTP to `:8080` directly, never through Caddy |

## What works end-to-end (all verified with real shots)

- Shot detection (0a.5: baseline arming — no tare; pour detected even
  without stillness; noise-robust stop (flat 0.3 g/2 s); cup-lift ends
  the shot — no 7 s wait; bump-tolerant; rejects uploaded flagged), spool to flash, upload with delete-on-confirm, idempotent
  ingest, automatic **dose attribution** from the grinder scale
  (first production shot: 37.4 g / 24.4 s, dose 18.2 g `measured`)
- Live pull screen (ghost trace, editable target box), shots list with
  overlay + detail cards, capture flow (grind/dose/preinfusion/temp sticky,
  taste chips), beans with autocomplete / "same again" / edit, shot delete
  (raw record always survives in `ingest_record`)
- Failure paths proven live: server outage → spool+backoff → self-delivery;
  duplicate uploads deduped; same-ms BLE samples collapsed
- **Forensics:** the server journals every live frame to
  `/var/lib/espressolog/live/YYYY-MM-DD.ndjson` (~1 MB/day). A missed or
  odd shot is replayable: extract `t,w` for the window → `firmware/test/host/replay`.

## Daily workflow (the only rules)

1. Wake the scales (they sleep ~9 min idle; reconnect is automatic).
2. Weigh the dose on the grinder scale, lift it off — that's the weighing
   (needs ~1.1 s on the scale since fw 0.6.5; was 2.2 s and missed a dose).
3. Cup on the drip-tray scale whenever — Lucas's actual workflow: cup
   preheated with water, portafilter in, machine started, cup emptied and
   set down DURING pre-infusion, then tared as the first drips fall. All
   of that is fine (0a.5/0a.6): no stillness needed, the tare is a rebase,
   pre-tare drips stay in the yield (logged yield reads ~1–2 g above the
   scale display). Board LED: blue = recording.
4. Pull the shot; lift the cup whenever it's done (0a.4 ends the shot on
   a ~2 s cup lift — no need to wait; a quick bump is ignored).
5. Tasting notes on the Capture tab (4 taps). Everything else is automatic.

## Next milestones, in order

1. **0f — noise floor** (trigger: ~30 shots, one recipe, changing nothing).
   Build the first `analysis/` notebook against the SQLite file: std dev of
   shot time and mean flow on the fixed recipe. That number is the Phase-3
   deadband and the go/no-go on closed-loop control. CLAUDE.md: the most
   important milestone and the one that gets skipped.
2. **0b — display bus** — PARTS ARRIVED, BENCH TOOLING READY 2026-09-15
   (SeenGreat SG-NANO-DLA-A rev 1.2 — what a generic "8 channel, CY7C68013A,
   sigrok PulseView, 24 MHz" listing shipped as. Those listings are
   interchangeable on paper and are NOT interchangeable in the input stage:
   WeAct's LogicAnalyzerV1 buffers with a 74LVC541 and is genuinely 5 V
   tolerant, this one does not — see below. Always read the buffer part
   number off the board before probing anything above 3.3 V.
   Plus 10x micro test hooks + resistor kit w/ 220k).
   The analyser is verified end-to-end against nothing but
   mains hum: it enumerates as `fx2lafw` (USB 1d50:608c) with the
   firmware already in EEPROM (no upload needed), sustains 24 MHz on all
   8 channels without overruns, and a finger on a D0 jumper reads back
   50.1 Hz — so the input stage, the channel mapping and the timebase are
   all good. Note it breaks out only GND + D0..D7: there is no VCC pin to
   test against, hence the hum trick.
   **NOT 5 V TOLERANT, despite the shop listing claiming 0-5.5 V.**
   SeenGreat's schematic (SG-NANO-DLA-A-V1.1.pdf): each channel is a bare
   100 R series resistor (RN2/RN4) into a **74HC245PW powered from 3.3 V**,
   no pull-downs, nothing else. HC has input clamp diodes to VCC, so 5 V in
   conducts ~10 mA through the 100 R and clamps the probed line to ~3.9 V.
   The chip survives (abs max +-20 mA) but it DISTURBS THE BUS. On J5's
   4.7 K pull-ups to +5 V that sag leaves only ~0.4 V of VIH margin at the
   display. If J5 measures 5 V, condition it: 10 K/22 K divider per line
   (tap 3.0 V, bus high 4.36 V, ~900 kHz bandwidth) or a 74LVC245/541
   buffer at 3.3 V. NOT the 220 K resistors - those are for 0c's slow
   switch lines; at 220 K the RC with probe capacitance smears every edge.
   Next: photograph internals first,
   then PulseView on J5 during heat-up; hunt the byte that tracks the
   displayed temperature. Abandon cheaply if opaque.
   Host tooling: `sigrok-cli` from Homebrew, plus PulseView built from
   source by `tools/build-pulseview-macos.sh` (there is no Homebrew
   PulseView, and it needs the sigrok git stack — the script explains why).
   Divider for 0c: two 220k in series (5V->2.5V), not the old
   100k/200k. Firmware readout via SWD
   deliberately NOT pursued (RDP1 likely blocks it + brick risk);
   keep as opaque-protocol fallback only, read-only, never erase.

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
