# Phase 0 status — as of 2026-09-15

Phase 0a (scale logging, stages 0–6) is **complete and in production**.
This file is the resume point: read it (and CLAUDE.md) before continuing.

## What runs where

| Piece | Where | State |
|---|---|---|
| Firmware 0.8.0 (built, FLASH PENDING), detector 0a.6 | ESP32-S3 on a USB charger by the machine | both Bookoos bound: yield `aa:bb:cc:dd:ee:01`, dose `aa:bb:cc:dd:ee:02`. The display-bus reader is **off by default** — nothing changes until `display on` is issued. 0.8.0 rewrote it against the 0e captures; see below. |
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
   decoded shot timer ran `001`→`169` over 16.892 s of capture time.
   That is 168 displayed steps, i.e. **16.8 s against 16.892 s — +92 ms**.
   An earlier draft here said "8 ms of agreement" by comparing against the
   final displayed value, 16.9 s, instead of the interval; the same
   mistake is recorded — and corrected — in CLAUDE.md for the 2026-10-03
   captures, where the errors are +106 ms and +30 ms. All three are of the order of the display's 100 ms tick
   plus the ~41 ms frame period, so there is nothing to explain — but the
   agreement is tens of milliseconds, not single digits. The static
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
   Firmware 0.8.0 reads the bus: `firmware/src/display.{h,cpp}` is pure
   logic in the ShotDetector mould (no I/O, no clock of its own), an ISR
   stamps clock edges into a ring and `loop()` is the single consumer, so
   framing is done by the same `feedEdge()` the host tests exercise and no
   flash read can happen in the ISR. `firmware/test/host/display_test.cpp`
   runs 18 scenarios against **real recorded frames** from
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

   **Firmware caught up in 0.8.0** (`display.{h,cpp}`), against the
   2026-10-03 captures as fixtures:

   - Parses the real structure — a 67-bit button frame and a 66-bit digit
     frame — instead of only the merged 133-bit form, so the 1–8 % of
     digit frames that arrive split are no longer dropped. That includes
     the timer's own `000`, which the old decoder never once saw.
   - Reads the **timer/temperature mode bit** instead of guessing from how
     long a reading had held still. `100` is now unambiguous.
   - Decodes the **button state**, and the **letters**, so `PrG` is a
     reading rather than a garbled frame — `DisplayBus::programming()` is
     true while the brew temperature is being changed.
   - Matches frame lengths **exactly**, which stops the ~564-bit power-on
     initialisation burst being clamped to `MAX_BITS` and decoded as a
     reading at all.
   - Treats an **all-blank display as its own mode**, not as 0 °C. This was
     wrong in the first draft of 0.8.0 and caught in review: `' '` was
     skipped in the numeric accumulate, so `"   "` came out as a confident
     0 °C — 99 times in one 30 s capture, because the display blanks
     constantly while a value is being edited.
   - Reads that blink as the signal it is. Two blank frames inside 600 ms
     mean a value is being **adjusted**, so a number shown then is the
     **setpoint being dialled in**, not the boiler — a ramp to 120 used to
     look like a boiler at 120. A lone blank, which is what a cold boot
     emits, is not a blink.
   - Latches `setpointTouched()`. `PrG` is a transient banner — 2 frames in
     372 while an adjustment ran for seven seconds — so a consumer that
     samples a level misses the event entirely. The latch is what belongs
     on a shot record.

   All four are mutation-tested: breaking any one of them fails a named
   test rather than passing quietly.

   **Still to do:** nothing consumes any of this yet. The shot record has
   no field for the machine's own timer, no `setpoint_changed` flag, and
   `sample_t.temp_dc` is still never written. That is the next decision,
   and it is a schema question rather than a firmware one.

   **Still to do after that:** decide what gets stored. `sample_t` already
   reserves `temp_dc`, and the shot timer is a truer boundary than the
   scale's plateau — but temperature is *not* readable during a brew
   (the display is showing the timer), so brew-temperature telemetry is
   not available from this source. Re-run `analysis/0f-noise-floor.ipynb`
   once the timer is logged; the shot-time figure may shrink.

4. **0c — switch sensing. J2 PINOUT MEASURED 2026-10-04.** The harness is
   **J2**, silkscreen `BOTONE`, **5 pins**. Mapped by unpowered continuity
   through the switches, then a powered DC check — no parts, no risk,
   about twenty minutes:

   | Position | Net | MCU pin | Evidence |
   |---|---|---|---|
   | **1** | **+5 V, switch common** | — | present in **all four** continuity pairs; **5.0 V** measured |
   | 2 | Steam | PA10 | **plug inspection**; direction by analogy with lever A — not functionally verified |
   | 3 | 2Cup | PB6 | lever A down |
   | 4 | 1Cup | PB5 | lever A up |
   | 5 | Hot Water | PB4 | **plug inspection**; direction by analogy with lever A — not functionally verified |

   **Two findings that correct standing assumptions:**

   - **There are TWO levers, not one.** Lever A is 1Cup/2Cup, lever B is
     Hot Water/Steam, and they share conductor 1 as a common. CLAUDE.md
     described a single SCI R13-29.
   - **All four function lines are wired.** CLAUDE.md called Steam and
     Water *"present on board, likely unwired on a Dream"*. They are
     wired. Same category as the J4 census: an assumption sitting in the
     hardware-facts section until someone measured.

   Note also that **2Cup sits before 1Cup** in position order, while the
   old list read "+5 V, Steam, 1Cup, 2Cup, Water". That list was never
   positional; reading it as positional swaps the shot with the clean
   cycle.

   Grades of evidence differ and are recorded as such: **1Cup/2Cup is
   functional** (lever operated, continuity observed), **Steam/Water was
   identified by inspecting the plug.** Neither is on the Phase 0 path, so
   inspection is proportionate for the latter.

   **Polarity is established by measurement, not assumed.** The continuity
   test shows a lever closes common to its direction line; the common
   measures 5.0 V; the four direction lines measure 0 V at rest; and
   **across the closed contacts (1→4, 1Cup held) the meter reads 4.95 V
   idle and drops to zero when pressed.** That last one is the same
   physical fact as the one before it, read differentially rather than to
   ground — a corroboration, not an independent third route. So "pressing pulls high, idle
   is low" rests on measurement rather than on the traced schematic — the
   source that was wrong about J4's net count and about Steam/Water being
   unwired.

   **The board's pull-down value is NOT determined.** The drop across the
   closed switch is the drop across R45 (100 Ω), so
   `R_pulldown = 100 × (5 − V)/V` would give it — but the reading was
   "zero" at the meter's resolution, not a millivolt figure. Anything from
   ~50 kΩ upward is consistent, assuming the pen meter resolves 10 mV on
   its 20 V range; at 100 mV resolution the bound would be ~5 kΩ. The
   formula is right if anyone wants to finish the job with a millivolt
   reading. It does not matter for 0c (a 370 K divider
   in parallel with any of those shifts nothing), and it is recorded only
   so nobody later mistakes "drops to zero" for a measured pull-down.

   Divider **150 K / 220 K** into the GPIO (5 V → 2.97 V, 13.5 µA,
   dropping **1.35 mV** across R45). **The 5 V is measured, which is the
   step skipped at J4.**

   **0d rejects this same pair and 0c keeps it — not a contradiction.**
   0c's corner is **+203 mV** (rail −5 %, VIH +5 %, 1 % resistors), where
   J4's is −45 mV. The difference is entirely source impedance: **100 Ω
   here against 9.4 kΩ at J4.** Do not "correct" 0c to 120 K / 220 K on
   the strength of the J4 warning. And unlike
   J4's open-collector output, the switch line is driven through R45's
   **100 Ω**, so a 370 K divider loads it by about a millivolt — the
   unloaded arithmetic is valid here, where at J4 it was not.

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
   | J4 | — | 3 | flowmeter — `XHP-3`. **Measured 2026-10-04: ground (`T`), +5 V (`+`), ONE signal (`#`)** — not two nets. |
   | J3 | — | 2 | NTC thermistor |
   | J1 | — | 9 | **mains and loads — never touch** |
5. **0d — flowmeter — FIRST ATTEMPT FAILED, 2026-10-03.** Recorded because
   it is the **only time Phase 0 has violated hard invariant 1**, and the
   mechanism is still not established.

   **What was measured, and nothing beyond it:**

   - 2 s idle on D0/D1 (`2026-10-03-flowmeter-j4-idle.sr`): **0 edges**,
     D0 steady low, D1 steady high.
   - 90 s spanning a 2Cup cycle (`2026-10-03-flowmeter-j4-null.sr`):
     **0 edges on both channels**, same levels.
   - The machine stopped with **`E01`** on the display.
   - **J4 disconnected, machine power-cycled, 2Cup run again: no error.**

   Those last two lines are the finding: **the tap caused the fault.** The
   *mechanism* is not established and must not be written down as though
   it were. Note the capture contains no transitions at all, so nothing in
   it timestamps the cycle or the fault — that correlation rests on the
   operator's account, not on the file.

   **The captures say almost nothing else, and an earlier draft of this
   section claimed they did.** It argued that the absence of 50 Hz hum
   proved the probes were not floating. **That is refuted by this repo's
   own files.** fx2lafw delivers all eight hardware channels regardless of
   how many are enabled, and these captures store them at `unitsize=1`, so
   the unprobed ones are readable:

   | Capture | D2 | D3 | D4 / D5 | D6 / D7 |
   |---|---|---|---|---|
   | `-flowmeter-j4-idle.sr` | **0 edges, flat** | 0 edges, flat | 50 Hz | 50 Hz |
   | `-flowmeter-j4-null.sr` | **50 Hz hum** | 0 edges, flat | 50 Hz | 50 Hz |
   | `-display-bus-flush-timer.sr` | 0 edges, steady high | 0 edges, flat | **30 / 32 kHz** | 50 Hz |

   An unconnected input picks up whatever is near it — **50 Hz mains
   through to tens of kHz**, the latter almost certainly crosstalk from
   the 9.9 kHz display clock and its harmonics. An earlier version of this
   table said "D4–D7 hum", which is wrong: in the flush capture D4 and D5
   run at 30 and 32 kHz.

   **The decisive row is D2.** It is connected to nothing in either J4
   capture, and it reads **dead flat in one and 50 Hz in the other**, 88 s
   apart in the same session. The signature is not stable on one channel
   within one sitting, let alone diagnostic. Both of the J4 capture's
   signatures — steady low, steady high, no edges — are reproduced here
   by channels on nothing at all. The captures cannot distinguish "probes
   on the wrong nets" from "probes on nothing".

   Two further traps in that reasoning, both worth keeping:

   - On a purely digital capture, **"zero edges" and "no hum" are the same
     measurement, not two.** A line sitting at a rail with 200 mV of
     ripple produces no threshold crossings. The earlier draft counted one
     observation twice and read it as corroboration.
   - The capture was taken with **all eight channels live**, against this
     repo's own written procedure — `tools/decode-display-bus.py` says
     "enable ONLY the channels in use". Five of the six unused channels
     are full of hum. Anyone opening the file in PulseView sees that.

   **The sensor is a DIGMESA** (2026-10-03, read off the body — the label
   is worn and was first read as "DIMASE PSACH"). Its three conductors are
   marked:

   **The three ROLES were measured on 2026-10-04.** The conductor→marking
   mapping is corroborated but still rests on re-reading the worn label;
   `T` is anchored independently by continuity. So the
   symbol-reading below is now a record of how it was guessed, not of what
   is known:

   | Marking | Role | On the 2S T-piece lead | Measured |
   |---|---|---|---|
   | `T` (middle) | **ground** | yellow | continuity beep to J5 pin 1 `VSS`; **no resistance value recorded** |
   | `+` | **+5 V supply** | green | 5.0 V, steady through a flush |
   | `#` | **signal** | black | 0.3 V parked, **2.5 V averaged during flow** |

   The guesses that turned out right: `T` as the IEC earth symbol ⏚ (a
   stem on a bar), `#` as a square-wave symbol ⌍. Both were labelled
   "plausible" and "a guess" before the meter went on them, which is the
   correct amount of confidence to have had.

   Earlier confidence labels here read "high" for both. They were not
   earned: the `#` cell's own text describes a guess, and the label came
   from the same worn plastic that produced "DIMASE PSACH".

   **How it was measured (2026-10-04), with a plain DC pen meter** — no
   frequency mode, no scope, and nothing ever left attached:

   1. **Continuity identifies `T`.** No convention, no power. It also
      cross-checks the markings: the ground *position* matching where `T`
      sits is what lifts that row above a label-reading.

      **This failed at the board header and worked at the T-piece
      window.** Probing all three `B3B-XH-A` header pins against the J5
      pin 1 pad gave **no beep at all**; the same test at the window in
      the 2S lead gave a clean beep on yellow. The failure is
      **unexplained** — plausibly the shrouded header pins or an occupied
      J5 pad, but that is a hypothesis, not a finding. Recorded because
      the retry plan prefers back-probing, and back-probing the header is
      precisely what did not work.
   2. **DC volts during a flush separates `+` from `#`.** While water
      flows `#` averages toward mid-scale and `+` holds at full supply.
      That is what was seen: 5.0 V against 2.5 V.

      An earlier draft said both would **idle near the supply** and that
      `#` would land at exactly half. Neither holds. An open collector at
      rest sits wherever the turbine parked — this one parked **low, at
      0.3 V** — and the mid-scale value depends on a duty figure that is
      **not known for this model**. The contrast is robust; the arithmetic
      on top of it is not.
   3. **`+` is whatever is left.** It **was** probed on 2026-10-04 with a
      20 MΩ DMM — that is how the 5.0 V above was read — and nothing
      happened. So the rule is not "never touch `+`":

      > **`+` is read-only, through a high-impedance meter, and nothing
      > else.** Nothing with an input clamp — no analyser, no GPIO, no
      > divider — **and nothing that sources or sinks current: never
      > power the ESP32 from it, never feed it from another supply, never
      > hang a pull-up on it.** It is the machine's own sensor rail off a
      > 3 W IRM-03-5.

      Two earlier drafts were wrong in opposite directions. "Must not be
      probed" is contradicted by the step above and discards the evidence
      that a high-impedance probe is safe. "Nothing with an input clamp"
      enumerates only *sensing* devices and leaves a **power tap** open —
      the single most tempting thing to do with a 5 V pin on an accessible
      connector. The rule is about **current**, not clamps, which also
      stops it depending on what voltage `+` happens to sit at.

   Two earlier drafts got step 2 wrong in opposite directions: the first
   said `#` would "flicker", which it will not at ~4.8 Hz; the second
   concluded from that it needed Hz mode, which does not follow — the
   mid-scale average *is* the signature. The third draft's error — that
   both non-ground conductors would idle near the supply — is recorded at
   step 2 above, together with the measurement that refutes it.

   **The census row above is wrong to put both nets on this connector.**
   It says J4 carries `Flow-P1`/`Flow-P2` — PA8 *and* PA9, two MCU pins.
   Measurement says ground, supply, and **one** signal. The census is
   traced from `techdregs/Ascaso_Dream_PID_Electronics`, which already
   ships the wrong MCU datasheet; the markings are read off the
   physical part and the meter agrees with them, which is the best
   evidence available even though the label is worn.

   So the standing **"quadrature, or two independent meters?" question is
   a false dichotomy and is now closed: neither.** One sensor, one pulse
   train, one GPIO, one counter.

   **The part, and its electrical data. Read the source column** — the two
   most load-bearing numbers are NOT in hand as primary sources. Every
   Digmesa PDF URL for this family 301-redirects to HTML; the datasheet
   could not be retrieved on 2026-10-03.

   | | |
   |---|---|
   | | | Source |
   |---|---|---|
   | Model | **Digmesa FHKSC**, three-pin | Ascaso-USA listing |
   | Ascaso part | `I.2560` | vendor listings |
   | Digmesa part | `932-9525-B` | vendor listings |
   | Orifice | 1.0 mm, 5 mm barbed | vendor listings |
   | Flow range | 0.033–0.40 L/min | **vendor listings; the datasheet reportedly gives min 0.05 L/min at Ø 1.00 mm — a 50 % disagreement at the low end, which matters for slow pre-infusion** |
   | Output | **NPN open collector**, 0 VDC active, saturation <0.7 V | Daitron + Digmesa product pages, corroborated |
   | Signal load | max 20 mA | as above |
   | Supply | **+3.8 to +20 VDC**, <8 mA | **search summary of an unretrieved datasheet; another secondary source says +2.8 to +24 VDC** |
   | K-factor | **~2400 pulses/L** at Ø 1.00 mm | **search summary, unretrieved — see below** |
   | Accuracy | ± 2.0 % | product pages |

   **The K-factor is ~2400, not a four-digit constant.** Returns for the
   `932-952x-Bxxx` sheet give **2386** at Ø 1.00 mm, but one gives
   **2386 / 2476 / 2436 for 0° / 90° / 180° mounting orientation** and a
   separate FHKSC source gives **2494** — ~4 % spread, and mounting
   orientation is precisely the installation variable Digmesa's own
   calibration warning covers. Quoting one to four significant figures
   would be false precision. Invariant 3 means nothing depends on it.

   **The 3.8 V minimum is the most load-bearing unverified number in this
   entry** — it is what upgrades the brownout hypothesis from speculation
   to "supported". **One secondary source gives +2.8 V instead, and if
   that is right then 3.3 V is in spec and the hypothesis loses its
   mechanism.** Retrieving the PDF settles it; until then the hypothesis
   is weaker than the paragraph below reads.

   An earlier draft of this section used **1300 pulses/L**, taken from a search summary and never
   checked against a datasheet — that is the Ø 1.50 mm figure (Digmesa's
   FHK table gives 1386 p/L / 0.7216 g/pulse for 1.50 mm against 2223 for
   1.00 mm). Everything derived from it was wrong by ~1.8×. **This is the
   same failure the STM32 datasheet note warns about**: the sourcing was
   flagged as secondary and honest, and then the freely available primary
   source was never fetched. Flagging weak sourcing is not a substitute
   for replacing it — which is why the table above now names which rows
   are still only flagged. **Retrieving this datasheet is BLOCKED, not cheap.**
   Every published URL for the `932-952x` family serves HTML rather than a
   PDF (`digmesa.com`, `shop.digmesa.com`), the Christian Berner mirror
   404s, and the Google cache is HTML — checked 2026-10-03 and again with
   browser headers on 2026-10-04. The remaining routes are a distributor
   request or `info@digmesa.com`. Until one works, treat the K-factor and
   the supply minimum as permanently secondary.

   **`E01` is the dose-control fault — confirmed from the manual.**
   Ascaso's own `MAN.29-V10` alarm table gives, across four languages:
   "Dose control fault" / "Fallo control volumétrico" / "Fehler
   Fassungsvermögenskontrolle" / "Défaut du contrôle de volume", with
   `E02` the probe fault. So `E01` is the volumetric path, which is the
   flowmeter.

   Note the manual renders it **`E01`, with no decimal point**, so the dot
   in the operator's "E.01" is probably punctuation rather than a lit
   segment. CLAUDE.md's open decimal-point question stands unanswered.

   A note on how that was argued before: an earlier draft paired a dealer
   customer review with our own tap-on/tap-off test and called the two
   together "persuasive". They are not two views of one claim — the
   review is evidence about *what the code means*, the controlled test is
   evidence about *what caused this particular fault* and would have
   "corroborated" any code equally. The conclusion survived; the reasoning
   did not.

   **Open collector changes what the capture should look like.** The
   sensor only ever pulls *down*; the high level comes from a pull-up the
   board must provide, and `#` **pulses LOW**.

   An earlier draft added "so `#` should idle HIGH — if a capture shows
   the opposite, suspect the tap, not the sensor." **The 2026-10-04
   measurement refutes that**: `#` parked at **0.3 V**, i.e. low, because
   an open collector at rest sits wherever the turbine stopped. The
   heuristic would have told a reader to blame their tap for the one thing
   now shown to be normal. It also
   means the line is only as stiff as that pull-up when high, which is the
   one way a careless load can hold it low and suppress pulses without
   breaking anything visibly.

   **The leading hypothesis is that the sensor lost its supply, and the
   2026-10-04 measurements make it stronger — but not as much as an
   earlier draft of this paragraph claimed.** The supply is **5 V,
   measured** (12 V was never a reading, only the top of a datasheet
   range), so the arithmetic below is the ~12 mA case rather than the
   ~82 mA one. That makes the analyser's survival more likely; **D1 is
   still unchecked.**

   **The number the hypothesis most needs has NOT checked out.** The 3.8 V
   minimum is still a search summary of an unretrieved datasheet, and one
   source gives 2.8 V — under which 3.3 V is in spec and the mechanism
   disappears. An earlier draft of this paragraph said "every number it
   needs has now checked out", fifty lines below the warning in this same
   entry that forbade exactly that sentence.

   **A second probe arrangement now fits, and it is the more informative
   one.** `#` parks at **0.3 V — LOW**. In `-j4-null.sr`, D0 was steady
   low and D1 steady high, which is equally consistent with **D0 on `#`
   (parked low, never pulsing) and D1 on `+`** as with the `+`/`T` reading
   below. Under that arrangement the sensor produced no pulses across a
   full 2Cup cycle, which is *direct* evidence it produced nothing — not yet that it was
   dead, since "the turbine never turned" and "D0 was not on `#`" both
   survive — rather than the
   mere consistency conceded below. Nobody recorded the probe positions,
   so both remain live.

   If instead the probes sat on `+` and `T`, neither is the signal,
   so neither could ever move. The analyser is **not 5 V tolerant**
   (74HC245PW on a 3.3 V rail behind 100 Ω), so clipped to `+` its input
   clamp conducts and pulls the sensor supply toward 3.3 V — **below the
   datasheet's 3.8 V minimum.** Without an external series resistor the
   analyser's own 100 Ω bounds that current at roughly
   (5 − 3.3 − 0.5)/100 ≈ **12 mA**, which is larger than the sensor's
   entire <8 mA consumption; with a 1 kΩ fitted it is ~1.1 mA and
   probably harmless. **Which of those applied was never recorded**, and
   that is the single thing that would have made this diagnosable.

   The idle levels are *consistent* with this hypothesis but do not
   support it, for the reasons in the hum section above. Competing
   explanations the data cannot exclude: the probes were on nets that
   never pulse, or a conductor was shorted. **Check D1 still reads
   correctly before trusting the analyser.**

   **What 0d can and cannot deliver.** At ~2400 pulses/L each pulse is
   **~0.42 ml**, an espresso shot at ~2 ml/s pulses at **~4.8 Hz**, and
   the rated ceiling is **~16 Hz**. (The 4 % K-factor spread shifts these
   by far less than the retention estimate below, so it changes nothing
   here.) Inlet volume for a 36 g shot is
   *not* 36 ml — it is output plus puck retention plus the solenoid dump,
   realistically 55–75 ml, so very roughly **130–180 pulses**. That is
   enough to be a useful total and still far coarser than the Bookoo at
   ~10 Hz and 0.01 g, which resolves the pour better. **What the flowmeter
   uniquely gives is inlet volume** — water going *in*, against the
   scale's water coming *out*. The difference is exactly what the puck
   absorbs and the solenoid dumps, which no scale can see.

   For a pulse counter the binding constraint is minimum pulse *width*,
   not rate: at ~50 % duty and a 16 Hz ceiling the half-period is ~31 ms,
   so even 1 kHz sampling is ample and 100 kHz is ~6000× the rate.

   Per invariant 3 the K-factor is never baked in: log raw pulses and
   calibrate ml/pulse by weighing water on a Bookoo. The datasheet itself
   says the same — "we recommend to calibrate the number of pulses per
   litre".

   **A standing habit, from three repeats.** Every table in this entry has
   at some point claimed more than the prose beside it: a confidence
   column reading "high" over a cell whose own text described a guess; a
   header saying "If J4's rail is 5 V" when `+` and `#`'s pull-up rail are
   different nets; and an Evidence column asserting "lever B down" when
   the operator had said they identified those two by looking at the plug.
   The prose was right all three times. **Write the table cell last, from
   the prose — not the prose as a hedge on a cell already written.**

   Related: the `+` safety rule has been lost from CLAUDE.md **three
   times**, each time because it lived as a row in the conditioning table
   and the table was rewritten when the divider changed. It is now a
   standalone block beneath that table. Rewrite the divider freely; leave
   that block alone.

   **Carry into the retry:**

   - **Record the as-built tap before powering anything.** Series value,
     whether a shunt is fitted, which conductor each probe is on. Not
     knowing this is why the incident above cannot be diagnosed, and it
     costs one line in a notebook.
   - **One channel, `#` only, 8.2 kΩ series into the analyser's clamp**
     — the 0b arrangement, ~146 µA. That part is safe at any plausible
     rail.
   - **ESP32 divider: 120 K series + 220 K shunt → 2.95 V. SOLVED
     2026-10-04 by measurement.** The node is a **10.04 kΩ pull-up to
     5 V** against a **147.4 kΩ pull-down** — both read unpowered in Ω
     mode at the T-piece window. That predicts an idle-high of
     5 × 147.4/157.44 = **4.681 V** against **4.69 V measured**, so the
     model is right and the source impedance is R_pu ∥ R_pd = **9.4 kΩ**.

     Because the source is 9.4 kΩ, the divider loads it and the unloaded
     ratio is not the answer:

     | Divider | node | pin | nominal | worst case |
     |---|---|---|---|---|
     | 8.2 K + 15 K | 3.33 V | **2.15 V** | −321 mV | **FAILS** |
     | 150 K + 220 K | 4.57 V | 2.71 V | +239 mV | **−45 mV, FAILS** |
     | **120 K + 220 K** | 4.56 V | **2.95 V** | +472 mV | **+177 mV** |

     Worst case is a full corner: the machine's 5 V sagging 5 %, the
     ESP32's 3.3 V rail 5 % high (VIH 2.60 V), **1 % tolerance on the
     divider pair, and ±1 % meter error on the measured 10.04 kΩ and
     147.4 kΩ**. An earlier version of this table omitted the last two and
     reported −20 mV and +201 mV — conclusions unchanged, margins
     flattering.

     **Two traps in that table.** The 8.2 K + 15 K an earlier draft
     specified is 321 mV *under* VIH — it would have counted nothing,
     forever, with nothing visibly broken. And **150 K / 220 K is in stock
     and still wrong**: fine nominally, fails on tolerance stacking, which
     is precisely why 220 K / 220 K was rejected for 0c. Do not substitute
     it because it is in the drawer.

     The chosen divider disturbs the node by **126 mV (2.7 %)**, leaving
     the machine's own reading unambiguously high.

     **Low side**, which the corner above does not cover: the pin sits at
     **0.19 V** against an ESP32-S3 VIL of 0.25·VDD = **0.784 V** at a
     5 %-low rail — **+590 mV**. Even at the datasheet's *maximum*
     saturation of 0.7 V rather than the measured 0.3 V, the pin is
     0.45 V, still **+331 mV** clear.

     **Drift on the board's own resistors does not threaten it either.**
     They were measured cold and unpowered and sit near a boiler, but they
     move together in the ratio and 340 kΩ ≫ 10 kΩ, so the margin is
     nearly flat: **+177 mV at ±1 %, +160 at ±5 %, +136 at ±10 %,
     +111 mV at ±15 %.** A known gap, closed rather than flagged.

     **The 4.681 V prediction matching 4.69 V to 0.2 % is luckier than the
     instrument.** It confirms the *model*, not the precision of the two
     resistances — which is why the corner still carries ±1 % meter error
     on them. **A 120 kΩ is not in stock** — the E12 kit on the
     shopping list covers it.

     The earlier derivation here assumed a push-pull source and quoted
     5 × 15/23.2 = 3.23 V. That was the wrong model, and the right one was
     already in this file for J5: "3.23 V if push-pull, **2.69 V if
     open-drain**". It is kept in the history because the measurement that
     refuted it only happened because the error was caught first.

     **The 1 kΩ remains wrong for this connector** — CLAUDE.md's
     "conditioned to 3.3 V logic" is the *board* side between J4 and PA8.
   - **Ground to `T` directly** — the yellow conductor on the 2S lead,
     confirmed by continuity beep to J5 pin 1 (no value recorded). J4 is
     self-sufficient for ground and
     no longer needs to borrow J5's `VSS`.
   - **`+` (green) is read-only, high-impedance meter only** — no clamp,
     and **nothing that sources or sinks current**, which includes
     powering the ESP32 from it. That is the specific rule the hypothesis
     supports. See step 3 above; an earlier version of this bullet said
     "put nothing on `+`", which the measurement retracts.
   - **Prefer back-probing to interposing**, on general grounds — a
     seated harness has nothing to fail to reconnect. Note this is a
     preference, not a conclusion from the incident: the evidence is
     tap-present → fault, tap-absent → no fault, which does not
     distinguish *opening* the connector from *loading* a net. Under the
     leading hypothesis, back-probing `+` would fail identically.

   **0e — temperature** (if 0b cracked it), per docs/phase-0-plan.md.

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
