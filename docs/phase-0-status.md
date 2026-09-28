# Phase 0 status — as of 2026-09-15

Phase 0a (scale logging, stages 0–6) is **complete and in production**.
This file is the resume point: read it (and CLAUDE.md) before continuing.

## What runs where

| Piece | Where | State |
|---|---|---|
| Firmware 0.7.0 (built, FLASH PENDING), detector 0a.6 | ESP32-S3 on a USB charger by the machine | both Bookoos bound: yield `aa:bb:cc:dd:ee:01`, dose `aa:bb:cc:dd:ee:02`. 0.7.0 adds the display-bus reader, **off by default** — nothing changes until the tap is wired and `display on` is issued. |
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

1. **0f — noise floor — DONE 2026-09-28.** `analysis/0f-noise-floor.ipynb`
   (run it against a fresh DB snapshot; the notebook says how to take one).

   **σ = 0.21 g/s on mean flow (12.2 %) and 3.43 s on shot time (14.2 %)**,
   over n=46: Moonwalker @ 7.9, one grind epoch, detector 0a.6 only, 46 of
   78 recorded shots. Robust estimates agree (MAD × 1.4826), flow shows no
   drift across five weeks, and bean age is not a confound
   (corr = +0.13 over 16–39 days off roast).

   Two filters carry most of the weight and should not be dropped when this
   is re-run: pooling detector versions inflates σ for no physical reason
   (0a.4 recorded a cup lift as the stop), and shots 25/26 were one pull the
   detector split in two — now flagged `excluded` in the database.

   **Read for Phase 3:** a controller resolving less than ~12 % is chasing
   noise. But this bundles grind, distribution, tamp and puck with the
   machine, so the achievable improvement is a fraction of it, not all of
   it. And `total_s` is still first-drip-to-plateau rather than
   pump-to-pump — 0b's display timer now offers the real boundary, so
   **re-run the notebook once that timer is logged**: the flow figure should
   stand, the time figure may shrink.

2. **0b — display bus — DONE 2026-09-28.** The bus is decoded and the
   display is readable from the wire. Protocol, probe point and bit
   offsets are in CLAUDE.md; the decoder is `tools/decode-display-bus.py`
   and the raw captures are in `analysis/captures/`. Validation: the
   decoded shot timer ran `001`→`169` over 16.892 s of capture time
   against a displayed 16.9 s — 8 ms of agreement — and the static
   capture rendered ` 95` in all 2267 frames, matching the display.
   Kit that worked: SeenGreat SG-NANO-DLA-A + `sigrok-cli`, four wire
   stubs soldered to the J5 pads, 8.2 K in series per channel and 1.5 K
   in the ground lead (no multimeter needed — see the note below).
   **The stubs were removed on 2026-09-28**, so any further capture needs a
   tap rebuilt first — the in-line T-piece (Ascaso harness `I.4312`) is now
   on the critical path for the `Disp-P4` button test, not just a tidiness
   improvement.

   Three things that cost time and should not cost it twice: the pad with
   thermal-relief spokes into the copper pour is ground *regardless* of
   wire colour and regardless of which pad is square; sigrok's `i2c`
   decoder will produce confident garbage (endless writes to address 0x00)
   on what is actually plain synchronous serial; and J5 carries **no +5 V**
   — it is ground plus three signals, so the constant-high channel is an
   idle signal, not a supply (pinout now in CLAUDE.md, from the traced
   schematic in `techdregs/Ascaso_Dream_PID_Electronics`).

   Ascaso's own part number for the display harness is **`I.4312`**
   (mainboard is `I.3957`, matching the board silkscreen). Buying that
   spare is the cheapest route to an exact-fit mating connector for an
   in-line T-tap — the repo's schematic uses generic KiCad symbols and
   names no connector part.

   Consequence for the roadmap: idle, the display shows boiler
   temperature — that is **0e** essentially solved, pending firmware to
   decode it live. During a brew it switches to a tenths-of-a-second shot
   timer, which is the machine's own pump-on/pump-off boundary and much
   of what **0c** was for. Reassess 0c's priority before ordering its
   parts.

3. **0e — temperature / display bus into the log — IN PROGRESS.**
   Firmware 0.7.0 reads the bus: `firmware/src/display.{h,cpp}` is pure
   logic in the ShotDetector mould (no I/O, no clock of its own), an ISR
   stamps clock edges into a ring and `loop()` is the single consumer, so
   framing is done by the same `feedEdge()` the host tests exercise and no
   flash read can happen in the ISR. `firmware/test/host/display_test.cpp`
   runs 8 scenarios against **real recorded frames** from
   `analysis/captures/`, not synthetic bit patterns.

   **Wiring (not yet built).** Two GPIOs, one divider per line, because the
   bus is 5 V and the ESP32 is not 5 V tolerant:

   ```
   J5 signal --[ 8.2K ]--+-- GPIO4  (clock)
                         +--[ 15K ]-- GND
   J5 signal --[ 8.2K ]--+-- GPIO5  (data)
                         +--[ 15K ]-- GND
   J5 ground ------------------- ESP32 GND
   ```

   Safe either way the line is driven: 3.23 V at the pin if push-pull,
   2.69 V if open-drain via the board's 4.7K pull-ups (VIH 2.48 V), and the
   machine's own high never drops below 4.16 V against a display needing
   3.5 V. **Do not enable an internal pull-down** — 45K across the 15K leg
   drops the open-drain case to 2.33 V and the pin stops reading high.
   The stubs soldered to J5 for the 0b capture have since been removed, so
   this needs the T-piece (or new stubs) before anything can be captured.

   Then `display on` (persisted in NVS) and check `status`.

   **Still to do after that:** decide what gets stored. `sample_t` already
   reserves `temp_dc`, and the shot timer is a truer boundary than the
   scale's plateau — but temperature is *not* readable during a brew
   (the display is showing the timer), so brew-temperature telemetry is
   not available from this source. Re-run `analysis/0f-noise-floor.ipynb`
   once the timer is logged; the shot-time figure may shrink.

4. **0c — switch sensing.** The switch harness is **J2**, silkscreen
   `BOTONE`, **5 pins**: +5 V, Steam, 1Cup, 2Cup, Water. Divider
   **150 K / 220 K** into the GPIO (5 V → 2.97 V, 13.5 µA).

   **NOT 220 K / 220 K**, which an earlier note here recommended: that
   gives 2.50 V against an ESP32-S3 VIH of 2.475 V — 2.60 V if the 3.3 V
   rail sits 5 % high — so a pressed button could read as not pressed.

   Gives real shot boundaries and `first_drip_ms` — but that is the small
   half. **J2 is the only actuation path on the machine**, so it is also
   how a shot ever gets stopped automatically. 0b's display timer does make
   0c redundant for *boundaries*; it does not make J2 redundant, and an
   earlier note here wrongly implied it might.

   Phase 1 stop-at-weight works by pulsing the 2Cup line when the scale
   hits target. Two constraints from CLAUDE.md that shape the hardware:
   there is **no fail-safe-open** (the board latches, so stopping needs an
   active pulse, never an interrupter), and **anything that can hold the
   line silently reprograms the machine** (hold >2 s rewrites that
   direction's stored timer). Hence the mandated hardware one-shot — the
   firmware must be physically incapable of holding.

   **Feasibility — measured, `analysis/overshoot.py`.** Overshoot after the
   stop is most predictable at **+2 s: +0.20 g, σ 0.071 g** (n=38).
   Trigger 0.2 g below target, read at +2 s, land inside **±0.14 g at 2σ**
   — a third of a ±0.5 g tolerance. Stop-at-weight is viable.
   It is a *threshold trigger*, not a control loop, so 0f's 12 % flow
   noise floor does **not** gate it.

   Measured later the spread doubles (σ 0.12 at 5 s) because the Bookoo's
   load cell drifts under a hot cup.

   **DECIDED 2026-09-28: `SETTLE_MS` stays at 5000 ms — don't relitigate.**
   Shortening it to 2500 would tighten `yield_final_g` (σ 0.07 vs 0.12) but
   redefines the field and breaks comparability across all 46 logged shots,
   for a metric nobody is optimising. It costs nothing for stop-at-weight:
   `SETTLE_MS` governs what is *logged*, while a Phase 1 controller reads
   the scale at +2 s for its own trigger. The two are independent.

   The earlier note here (median +0.40 g over n=46) was computed on a
   contaminated population and is superseded. **Seven of those 46 shots
   have no samples after `stop_ms` at all** — the cup was lifted and the
   detector finalised at the peak, so their `settle_offset_g = 0` is
   structural, not measured. `overshoot.py` detects them by requiring a
   ≥4 s tail rather than trusting the field. Shot 37 is also excluded:
   a −3.10 g step then +2.00 g inside 180 ms (something knocked the cup)
   followed by a slow lift the 5 g threshold never caught.

   **Connector census** (from the traced schematic, so one order covers
   the rest of phase 0):

   | Ref | Silkscreen | Pins | Carries |
   |---|---|---|---|
   | J5 | — | 4 | GND, `ESD1`, `ESD4`, `Disp-P4` — **JST XH, `XHP-4`** (verified) |
   | J2 | `BOTONE` | 5 | +5 V, Steam, 1Cup, 2Cup, Water |
   | J4 | — | 3 | flowmeter `Flow-P1`/`P2` |
   | J3 | — | 2 | NTC thermistor |
   | J1 | — | 9 | **mains and loads — never touch** |
5. **0d — flowmeter**, **0e — temperature** (if 0b cracked it), per
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
