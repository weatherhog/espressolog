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
   tap rebuilt first — the in-line T-piece (3S balance lead, see below) is now
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
   (mainboard is `I.3957`, matching the board silkscreen) — worth knowing,
   but no longer the first choice for the T-tap. A **3S LiPo balance
   extension lead** is male-to-female XH on cable, wired 1:1, and 4-pin,
   so it drops straight in line at J5; `I.4312` is the fallback if a
   generic connector turns out not to mate. See `docs/shopping-list.md`,
   which is the buying document. (The repo's schematic uses generic KiCad
   symbols and names no connector part, which is why this took measuring.)

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

   **All four conductors are branched, but only three are terminated.**

   | J5 pin | Net | At the T-piece | At the box |
   |---|---|---|---|
   | 1 | `VSS` | branch, no resistor | ground |
   | 2 | `ESD1` | 8.2K | 15K shunt -> GPIO4 |
   | 3 | `ESD4` | 8.2K | 15K shunt -> GPIO5 |
   | 4 | `Disp-P4` | 8.2K | **nothing — labelled test point** |

   An earlier draft left `Disp-P4` untapped as the cautious choice. It is
   the opposite: this T-piece is on the critical path for the `Disp-P4`
   button test, so a tap that omits it needs a *second* tap built into the
   only 3S lead there is. Sensing the line through a divider is explicitly
   safe per CLAUDE.md — the hazard is driving it.

   **Why it is branched but not terminated.** `ESD1`/`ESD4` idle on known
   4.7K pull-ups, so a 23.2K divider leaves them at 4.16 V and still
   reading high. **What pulls `Disp-P4` high is not recorded**, and the
   schematic path to the MCU runs through R32 = 100K. If that pull-up is
   weak, a 23.2K divider drags the line under 1 V and the MCU sees a button
   held down permanently — not actuation, but a false signal fed into the
   machine continuously, which invariant 1 still forbids. The 0b capture
   only proves the line tolerates an analyser's near-infinite input
   impedance; it says nothing about 23K.

   **DONE 2026-10-03 — the test was run.** `Disp-P4` is not the button line
   and not the display answering: across 344 s of live bus, including every
   button press, the whole menu and both brew directions, it moves only
   during the 204.5 ms after the supply rails come up. See CLAUDE.md for
   the full interval list and what is and is not explained by it. The
   branch stays an unterminated test point.

   **The buttons turned up in the 67-bit frames instead**, on the same two
   wires — bits 49 and 58, active low. See CLAUDE.md for the mapping and
   semantics. No extra GPIO, no divider on the bidirectional line.

   **`ESD4` (pin 3) is the clock, `ESD1` (pin 2) the data** — also settled
   2026-10-03, after being unrecorded since 0b.

   **Where each part physically sits:**

   ```
   inside the machine          |  cable out       |  project box, outside
   ---------------------------------------------------------------------
   J5 -[T-piece]- 8.2K inline  |  3 x 26 AWG      |  2 x 15K to GND
        |                      |  silicone,       |  ESP32-S3 GPIO4/5/GND
        +- machine harness     |  twisted pairs   |  USB charger (isolated)
   ```

   **Route this cable AWAY from the mains loom**, and twist each signal
   with a ground return. Splitting the divider is right for fault
   protection but it moves the design's lowest-margin node onto 50 cm of
   unshielded wire: in the open-drain case the pin sits at 2.69 V against
   a VIH of 2.48 V, so **210 mV of headroom**, on a node whose Thevenin
   impedance is 8.2K ∥ 15K ≈ 5.3 kΩ. The machine's own loom carries the
   heater line and a random-phase BTA204S triac; a capacitively coupled
   commutation spike above 210 mV corrupts frames intermittently, which is
   the worst kind of fault to chase.

   Two remedies if it happens, cheapest first. **Raise the shunt leg to
   16.4K (2 × 8.2K in series)**: the pin then sits at 2.80 V instead of
   2.69 V, so the margin goes 210 mV → 325 mV, loading is unchanged
   (171 µA against 179 µA), and the push-pull worst case is 3.33 V, still
   well under the 3.6 V absolute max. Only then reach for **~470 pF from
   each GPIO to GND at the box**: 5.3 kΩ × 470 pF = 2.5 µs against a 36 µs
   clock high, so it settles in a fifth of the pulse and puts the corner at
   64 kHz, below triac spike content. Do not go much above 1 nF.

   The internal-pull-down warning applies to both: with a 16.4K shunt the
   45K pull-down drags the pin to 2.41 V, below VIH, exactly as it does
   with 15K.

   **The 8.2K series resistors go at the T-piece, not in the box.** Solder
   each into the stripped window so the branch is
   `[J5 conductor] - [8.2K] - [26 AWG out]`, heat-shrink over it. The
   failure to design for is a conductor inside the tap cable chafing
   through to the ground return running beside it in the same bundle, or
   to another conductor at a crushed cable gland. With the resistor at the
   far end that puts a J5 signal straight to ground — display stops, and
   if anything upstream is driving push-pull it is driving into a short.
   Disturbing the machine, which invariant 1 forbids.

   With 8.2K at the source the same fault is current-bounded: under 0.6 mA
   if the line is driven from 5 V, 0.39 mA if it is only held up by the
   board's 4.7K pull-up. **Not a non-event though** — in the pull-up case
   the J5 node becomes a divider and sags to 5 × 8.2/(4.7+8.2) = **3.18 V**
   against the 3.5 V the display needs, so it may misread until the fault
   is found. The point is that it is a soft fault instead of a short on the
   bus. Series resistance belongs at the source because part of its job is
   protecting what is upstream from everything downstream, and the same
   8.2K also bounds clamp-diode current to ~0.5 mA if the ESP32 is
   unpowered while the machine is on. The 15K shunts stay in the box,
   where they are reworkable.

   **Ground takes no series resistor.** It is the reference both shunts
   return through, so an 8.2K there lifts the ESP32 ground ~1.9 V (1.85 V
   open-drain, 2.07 V push-pull, solving the two parallel 23.2K branches
   against the common 8.2K) and cross-couples the two channels. An earlier
   draft said 3.5 V, computed from the no-lift current and ignoring that
   the lift itself reduces that current.

   **Sharing ground is safe** because the machine's 5 V comes from an
   isolated IRM-03-5 and the ESP32 runs from its own isolated supply —
   neither is earth-referenced, so there is no loop. The realistic way to
   break that is **not** the power supply but the **USB data cable**:
   plugging the ESP32 into a laptop on a 3-pin adapter, or a dock, earths
   its ground while the tap is connected. Low hazard, since the machine's
   5 V is an isolated SMPS secondary, but it completes a loop straight
   into the noise problem above — so reflash with the tap unplugged.
   Note also that a Class II charger's Y-capacitor leaves the ESP32 ground
   floating near half-mains with a small leakage current: "isolated" is
   true, "quiet" is not.

   **Cable-to-box connector: cut the 6S lead in half.** A balance extension
   is complementary end to end by construction, so halving it gives a male
   pigtail and a female pigtail that mate — you are re-joining the lead you
   cut. Put the **female (socket) half on the machine side** so the pins
   that can be live are shrouded. Use three conductors; cut the other four
   back flush and heat-shrink them individually so they cannot bridge.
   **The 5S is the practice piece** for the strip-and-solder — the pack has
   one of each, so write the allocation down rather than rediscovering it
   with a bag of cut leads.

   **Size the box for three harnesses.** 0c adds J2 (two dividers, on PB5
   and PB6 only — the other three pins of J2 need not reach the box) and
   0d adds J4; they all land here. A box that fits only this one is a job
   done twice.

   More than convenience: **J2 carries no ground** (+5 V, Steam, 1Cup,
   2Cup, Water), so 0c's dividers have to reference the ground that comes
   back on J5's `VSS`. That makes this tap's ground conductor load-bearing
   for 0c as well as 0e, and it is why one box is the right shape rather
   than three.

   Safe either way the line is driven: 3.23 V at the pin if push-pull,
   2.69 V if open-drain via the board's 4.7K pull-ups (VIH 2.48 V), and the
   machine's own high never drops below 4.16 V against a display needing
   3.5 V. **Do not enable an internal pull-down** — 45K across the 15K leg
   drops the open-drain case to 2.33 V and the pin stops reading high.
   **The T-piece is built and verified** (2026-10-03). The machine runs
   normally through it and the tap decodes the live display with 0 %
   corrupt frames — which holds with or without the shunts; see
   `analysis/captures/README.md`.

   Then `display on` (persisted in NVS) and check `status`.

   **Two firmware gaps, both found by the 2026-10-03 captures and NEITHER
   fixed yet.** `display.{h,cpp}` predates knowing any of this.

   1. **Boot produces readings that are not temperatures.** From
      `-cold-boot.sr`: the bus runs ~4.1 s of irregular non-frame traffic
      (single-bit blips, one 564-bit burst) before framing starts, then
      shows blank, then **`  0`**, then the real value.

      The `  0` is harmless in itself: it arrives as a standalone 66-bit
      frame and `DIGIT_BITS == 120`, so `decodeFrame()` rejects it. **The
      frame that does reach `temp_c` is the 564-bit init burst** — capped
      to `MAX_BITS = 192`, offsets 106/115/124 land inside it, all three
      fields read `0000000`, and a blank leading field is taken as "two
      digits, always a temperature", giving `temp_c = 0`. That burst is
      the case to add to `display_test.cpp`; there is no guard today.

      (An earlier version of this note said the hazard was a power-on
      ` 88`. That reading came from a capture taken mid-warm-up and was
      most likely a real 88 °C — see CLAUDE.md. The hazard is real; the
      value was wrong.)

   2. **`PrG` frames are silently discarded.** `glyph()` returns 0 for
      anything outside its 12-entry digit table and `decodeFrame()` then
      drops the whole frame as garbled. So the one display state that
      says "the brew temperature is being changed" — the event this whole
      exercise was justified by — never reaches the firmware. The 67-bit
      button frames are not parsed either, which is the same gap from the
      other direction.

   `analysis/captures/2026-10-03-display-bus-buttons-separated.sr` is the
   fixture for both. Neither needs the bench.

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
   | J2 | `BOTONE` | 5 | +5 V, Steam, 1Cup, 2Cup, Water — `XHP-5` |
   | J4 | — | 3 | flowmeter `Flow-P1`/`P2` — `XHP-3` |
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
