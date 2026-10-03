# Espresso shot logger

Instrumentation for an Ascaso Dream PID + Eureka Mignon Single Dose.
Currently **Phase 0: logging only.**

---

## Hard invariants

Violating any of these is a bug, not a design choice.

1. **Phase 0 never actuates anything.** No writes to any machine signal.
   Every tap is high-impedance or passive. Read-only.
2. **Never write flash during a shot.** An ESP32 flash write blocks for
   tens of ms, which stalls the BLE stack and puts gaps in the curve.
   Buffer in RAM, write once when the pump stops.
3. **Store raw sensor values, never derived ones.** Flowmeter pulses, not
   millilitres. The ml/pulse factor is calibrated and will be revised;
   baking it in makes old shots uninterpretable.
4. **Weights are integer milligrams.** Bookoo reports to 0.01 g, so mg is
   lossless and repeated arithmetic never drifts.
5. **Grind settings are meaningless without their epoch.** A setting is
   relative to a zero point that moves whenever the burrs come out.
   Never compare or fit across `grind_epoch` boundaries.
6. **Never lose a shot.** Spool to flash, upload, delete only on
   confirmed success.

---

## Hardware facts (verified, do not re-derive)

### Machine: Ascaso Dream PID

Control board MCU is an **STM32F030R8T6** (LQFP64, 64 KB flash). Read off
the physical chip marking. Note: the community schematic repo
(`techdregs/Ascaso_Dream_PID_Electronics`) bundles the *wrong* datasheet —
`stm32f030f4.pdf` is the 20-pin TSSOP variant. Use the F030x8 datasheet
and RM0360.

Pin map, from the traced schematic:

| Net | Pin | Notes |
|---|---|---|
| 1Cup | PB5 | up direction of the switch, 15 s clean cycle |
| 2Cup | PB6 | down direction, 60 s shot |
| Water | PB4 | present on board, likely unwired on a Dream |
| Steam | PA10 | as above |
| DIP1–4 | PB0–PB3 | four config DIP switches (SW1), purpose unknown |
| Flow-P1 / P2 | PA8 / PA9 | flowmeter, conditioned to 3.3 V logic |
| AC_Sense | PA5 | mains zero-cross, via opto U27 |
| Pump_Ctrl | PA15 | → FODM3053 → BTA204S triac (random-phase) |
| Solenoid_CTRL | PB12 | |
| HTR_CTRL | PB15 | |
| display link | PB7/PB8 or PB8/PB9 | see below |

**Switch electrical:** +5 V → R45 (100 R) → switch common. Pressing pulls
the line **high**; idle is low. 5 V logic, *not* mains. The 5 V rail comes
from an IRM-03-5 isolated module, so sharing ground with the ESP32 is safe
provided the ESP32 runs from an isolated supply.

**Switch semantics — important.** The switch is an SCI R13-29, momentary
ON-OFF-ON, spring return to centre. The board *latches*:

- short pulse → start the stored timer, or stop a running shot
- **hold > ~2 s → reprograms that direction's timer to the hold duration**

Consequences: there is no way to stop the machine by opening the switch
line, so no fail-safe-open design is possible on that path. And any future
actuator that can hold the line will silently reprogram the machine. Phase 1
must use a hardware one-shot (74HC123 or NE555, ~120 ms) so firmware can
*trigger* but never *hold*.

**Display bus — DECODED 2026-09-28 (0b done).** Captures and the decoder
are in the repo: `analysis/captures/2026-09-28-display-bus-*.sr`,
`tools/decode-display-bus.py`. Read those before touching this again.

Probe at **J5** on the main control board, 4 pins. **The connector is
JST XH** — verified 2026-09-28 against the JST datasheet by measurement,
not by eye:

| Role | Part |
|---|---|
| Plug housing (on the machine harness) | `XHP-4` |
| Crimp contact | `SXH-001T-P0.6` (AWG 28–22) |
| Board header, top entry | `B4B-XH-A` |

Measured 12.3 mm wide across the latches (spec 12.3; EHR-4 is 12.0),
5.5 mm thick (spec 5.7; EH is 3.8 — the decisive one), pin span ~8 mm
(spec 7.5). Friction latch with ribs, which EH does not have.

**Sellers list these as "XH2.54". That is a misnomer — XH is 2.5 mm.**
The listings are the right part; anything actually specified as Molex
2.54 is not equivalent.

Sellers also print **"JST EH"** in spec tables for parts that are plainly
XH — seen 2026-09-30 on a LiPo balance extension whose own title said XH.
What the part is *for* is better evidence than what the listing calls it:
hobby LiPo balance ports are XH in all but a few outliers. **Confirmed
2026-10-01 on the mixed balance-extension pack that arrived: XH, as the
title said and the spec table denied.** Verify by mating, not by reading.

**Every connector on this board is XH**, including J2 (`BOTONE`, 5 pins →
`XHP-5` / `B5B-XH-A`) and J4 (flowmeter, 3 pins → `XHP-3` / `B3B-XH-A`).

**The headers are moulded `H JST`, and that marking does NOT mean EH.**
JST moulds it on XH headers too — it appears in the XH header drawing but
not in any extractable text, so a text search of the datasheet will tell
you the opposite. EH headers carry no such marking. The moulded number is
the circuit count, so `3 H JST` is a `B3B-XH-A`.

If a connector on this machine ever needs re-identifying, the one
measurement that separates XH from EH — they share pitch, pin span and
width to within 0.3 mm — is **depth**: plug 5.7 vs 3.8 mm, header shroud
5.75 vs 3.8 mm. Nothing else discriminates, which is why identifying this
from photographs failed repeatedly.

Its pinout, from the traced schematic in
`techdregs/Ascaso_Dream_PID_Electronics`:

| J5 pin | Net | Notes |
|---|---|---|
| 1 | VSS | ground — the pad with thermal-relief spokes into the copper pour |
| 2 | `ESD1` | signal, via U26. One of the two we decode. |
| 3 | `ESD4` | signal, via U26 and R24. The other one. |
| 4 | `Disp-P4` | third signal — **bidirectional**, see below. Idle high; moves only at power-on, see below. |

**`ESD4` (pin 3) is the CLOCK. `ESD1` (pin 2) is the DATA.** Settled
2026-10-03 off the T-piece, after being unrecorded since 0b. The
discriminator is **the LOW period, not the high**: `ESD4`'s low took only
**two** distinct values across 5 s (65 and 66 µs — 1 MHz quantisation of
the documented 65.5 µs), against **31** for `ESD1`. Distinct *high* widths
separate nothing, because inter-frame gaps give the clock about as many as
the data line has (74 vs 92); an earlier draft of this paragraph offered
that as the proof and it does not work.

The conclusion stands on its own anyway: decoding with the pair the other
way round yields **zero** frames of 120 bits or more — longest run 9 bits
— against 363 clean 133-bit frames the right way round.

Note `tools/decode-display-bus.py` reads **column 0 as the clock**, so a
CSV captured as `D0=ESD1, D1=ESD4` must have its first two columns swapped
before piping in. Capturing `ESD4` on the lower channel number avoids
that.

**There is no +5 V on J5** — it is ground plus three signals. A constant-high
line on a logic analyser looks exactly like a supply rail, which is how it
got mislabelled first time round.

**`Disp-P4` is bidirectional — never drive it.** It reaches the MCU (net
`P16`) through a two-transistor pair: `P16` → R30 (100 R) → Q11
(PDTC143ET) → R31 (2.2 K) → Q10 → R32 (100 K) → the pin. One transistor
lets the MCU pull the line low, the other lets it sense the line being
pulled low from the display end. That topology only exists when both ends
talk on one wire.

**That hypothesis was WRONG — tested 2026-10-03 and disproven.** Across
**344 s of live bus** — idle, every button press, the whole `SET UP` menu,
both brew directions and a complete power cycle — `Disp-P4` produced
transitions in exactly one place: **25 edges in the 204.5 ms after the
supply rails come up**, starting 194 µs after `ESD1`/`ESD4` move. Nothing
anywhere else. Per file: 8/30/40/60/60/60 s with no edges at all, and 86 s
with those 25.

The full interval list, in µs, because a truncated version of it was
previously used to argue a conclusion it does not support:

    1, 187, 1, 34, 1, 18, 1, 13, 2, 1, 9, 1, 23, 1, 32, 1, 94, 1,
    2814, 1, 2, 2, 6, 201273

Twenty-four edges inside the first 3.25 ms look like **rail settling**:
irregular, converging, and `ESD1`/`ESD4` glitch at the same instant but
settle within ~5 µs because they sit on 4.7 K pull-ups while `Disp-P4`
reaches the MCU through **R32 = 100 K**, twenty times the impedance. A
handshake would show a uniform bit period; this does not.

**The 25th edge does not fit that story.** It arrives 201 ms after the
previous one, long after the rails are up, and is unexplained. One edge is
not a protocol either. Both readings are offered; neither is established.

Note also that `2026-09-28-display-bus-flush-timer.sr` carries **4**
`Disp-P4` edges — two 1 µs glitches at 47.959 s and 48.235 s, mid-flush —
so "silent whenever the machine is doing anything" is not quite true
either.

So: not the button line, not the display answering, and silent whenever the
machine is actually doing anything. Branched to a test point, terminated at
nothing.

**The buttons are in the 67-bit frames instead** — the short frames this
file previously wrote off as carrying nothing. Two bits, active low:

| Bit | Button | Effect |
|---|---|---|
| 49 | LEFT | enters programming (`PrG`), then **decreases** |
| 58 | RIGHT | **increases** |

**Four** distinct 67-bit payloads exist: neither bit low, 49 low, 58 low,
and **both low** — the last seen 2026-10-03 during the `SET UP` entry
gesture, which is the only thing that presses both at once. Press and
release timings matched the operator's account across three sessions, so
the bit-to-button mapping is confirmed.

Semantics are from the **Ascaso Dream PID user manual**, not inference —
two earlier drafts of this paragraph guessed and one of them guessed wrong:

- **bit 49 = the LEFT button** (manual's #22). Pressing it enters
  temperature programming: the display renders **`PrG`**. Further presses
  **decrease** the setpoint.
- **bit 58 = the RIGHT button** (#23). **Increases** the setpoint.
- **Three seconds of inactivity** returns the display to normal.
- **23 held while 22 is pressed, ~3 s** opens the main `SET UP` menu — 22
  scrolls parameters, 23 shows the options under each. **Confirmed
  2026-10-03**, including the both-bits-low payload.

This matches the captures: five short right-button presses alone did
nothing, because programming mode had not been entered; five short left
presses each rendered `PrG`; and in the first session, with the mode open,
holding right ran 95 -> 120 in 41 ms steps and holding left brought it
back.

**`PrG` on the display means the brew temperature is being changed** —
which is the event that silently invalidates an 0f noise-floor run, and
the reason any of this is worth logging.

**This costs nothing to log.** Same two wires, same two GPIOs, no divider
on the bidirectional line, no extra pin — the short frames were already
arriving and being discarded. `Disp-P4` can stay an unterminated test
point.

**Setpoint changes are detectable:** while adjusting, the display blanks
**entirely** (all three fields zero) and comes back, at a measured 2.44 Hz
— 0.204–0.210 s per half-cycle. That blink is the other signature of the
same event.

This is the one J5 line where a careless connection could actuate the
machine, which hard invariant 1 forbids. The 8.2 K / 15 K divider is
high-impedance and receive-only, so it is safe as specified — but do not
replace it with anything that can source current.

Two traps, both of which cost time on 2026-09-28:
- **Ground is pin 1, the spoked pad.** The square-pad-is-pin-1 convention
  does NOT hold on this board. **The square pad is pin 4** (`Disp-P4`) —
  established 2026-10-03: spokes at one end of J5, square at the other,
  two plain pads between. So either end identifies the connector, and the
  square one marks the line that must never be driven.

  Wire colours say nothing — the harness is red/black/red/black because it
  was cut from two 2-wire reels, and ground lands on a red one. Its order,
  from the spoked pad: **red (1, VSS), black (2, ESD1), red (3, ESD4),
  black (4, Disp-P4)**. Useful for orienting the harness once unplugged,
  but only in combination with the pads — two of the four are red.

  **A replacement lead has its own colours and they will not match.**
  Derive the mapping instead: hold the lead's male end in the orientation
  it plugs in, see which housing end lands on the spoked pad (that is
  position 1), then meter position-to-conductor. Pin 1 is also the only one
  with continuity to the ground pour, which is an independent check that
  needs no convention at all.

  **The T-piece lead built 2026-10-03** (3S balance extension, three black
  and one red) maps **red → pin 4, `Disp-P4`**. The three blacks follow by
  position counting away from red: furthest from red is pin 1 `VSS`, then
  pin 2 `ESD1`, then pin 3 `ESD4` adjacent to red. Convenient accident
  worth keeping if this is ever rebuilt: **the one visually distinct
  conductor is the one that must never be driven.**
- Both decoded signals idle high on the 4.7 K pull-ups, as the schematic
  predicted.

It is **not I²C.** Plain synchronous serial: clock ~9.9 kHz (high 36 µs,
low 65.5 µs), data sampled on the **rising** edge and stable across a whole
clock period.

**The bus sends two frames, 67 bits then 66 bits, about every 41 ms.** The
67-bit frame carries the button state; the 66-bit frame carries the digits.

**A "133-bit frame" is a merge artefact, not a frame.** The gap between the
pair usually falls under the 1 ms idle threshold both the decoder and
`display.cpp` use to split frames, so they arrive glued together — and 67 +
66 = 133. Verified 2026-10-03: every 133-bit frame's **first 67 bits**
match a standalone 67-bit payload, in all nine captures. The **last 66**
match a standalone 66-bit payload 141/141 in `-95c-poweron.sr`; that half
cannot be checked in `-buttons-separated.sr`, which contains no standalone
66-bit frames at all, so an earlier "604/604" here was meaningless. When the gap happens to
run long the pair splits properly, which is all the stray "66-bit frames"
ever were, and **both decoders silently discard them, losing that digit
reading**. Within the merged frame the digits sit at 106/115/124, which is
39/48/57 of the 66-bit frame.

**How often the pair splits tracks heater activity, not wiring.** Measured
within a single capture (`2026-10-03-display-bus-flush-timer.sr`): 1.18 %
idle before a flush, 3.62 % during it, 5.19 % settling, **6.56 % while the
boiler recovers**, and 8.26 % in a separate capture of a machine heating
from cold. Across files at comparable states the divider makes no
difference — 1.10 % unshunted against 1.18 % shunted, both idle at
temperature. An earlier note here credited the shunts; that was a confound
between captures taken at different boiler states.

So a parser that keys on 133 bits throws away up to ~8 % of digit readings
exactly when the machine is working hardest. **Parse the 67/66 pair
directly rather than relying on a gap threshold.**
Beware: sigrok's `i2c` decoder happily "decodes" this as endless writes to
address 0x00 — that output is the symptom of a wrong guess, not a result.

**The display is not digits-only.** Observed vocabulary, all measured
2026-10-03 (`analysis/captures/2026-10-03-display-bus-setup-menu.sr`):

| Glyph | abcdefg | Seen in |
|---|---|---|
| `0`–`9` | — | temperature, shot timer |
| `P` | 1100111 | `PrG`, `UP` |
| `r` | 0000101 | `PrG`, `Pr`, `Cr` |
| `G` | 1011110 | `PrG` |
| `E` | 1001111 | `SET` |
| `t` | 0001111 | `SET` |
| `U` | 0111110 | `UP`, `Ud`, `U` |
| `d` | 0111101 | `Ud` |
| `C` | 1001110 | `Cr` |
| `F` | 1000111 | `OFF` |
| blank | 0000000 | leading field, blink |

**Two letters are indistinguishable from digits. This is the display's
limitation, not the decoder's:** `S` is the same seven segments as `5`, and
`O` the same as `0`. So the `SET UP` banner arrives as `5Et`/`UP `, and the
stand-by parameter as `0FF`. Only context can tell you which was meant.

Still unobserved: **`n`**, needed for the `ON` value of the timer
parameter. It appears only in a parameter *value*, which means pressing
button 23 inside the menu and changing a setting.

**Bit 114 is a TIMER-MODE FLAG, and it is the most useful thing in the
frame after the digits.** Of the six bits around the digit fields,
113/122/123/131/132 are always zero — but 114 is set in **exactly** the
brew-timer frames and clear in **every** temperature frame:

| Capture | bit 114 set | clear |
|---|---|---|
| `-flush-timer.sr` (2Cup) | 492, all `001`…`210` | 619, all ` 95` |
| `-1cup-clean.sr` | 307, all `001`…`105` | 878, all ` 95` |
| `-buttons.sr` | 0 | 363, incl. the ` 96`…`120` setpoint ramp |
| `-setup-menu.sr` | 0 | 803, incl. `0FF`, `Ud`, `Pr` |
| `-cold-boot.sr` | 0 | 1593 |

So the machine tells you outright whether a three-digit reading is a timer
in tenths or a temperature ≥ 100 °C. `display.cpp` **used** to guess, with
a 1.5 s staleness heuristic; it now reads the bit (firmware 0.8.0). The
test is a pair of real frames that both read `100` — one captured during a
flush, one while the setpoint was ramped past 100 °C.

This was recorded as "no decimal point observed" in an earlier draft —
wrong, because the gap bits had only been checked in captures that contain
no brew. The decimal point question is separate and still open; `0.5`
appears only as a stand-by parameter value and has never been displayed.

**The `SET UP` menu**, entered by holding 23 while pressing 22 for ~3 s,
scrolls with 22 in this fixed order: `Ud` (units C/F), `Pr` (pre-infusion
0–5 s), `Cr` (timer ON/OFF), `OFF` (stand-by OFF/0.5/1/2 h), `U` (offset
between set and displayed temperature). Three seconds idle saves and exits.
**Do not hold 23 alone for three seconds — that is `PrS`, an immediate
factory reset.**

Three 7-segment digit fields, MSB = segment a, order abcdefg:

| Field | Bits in the 133-bit frame |
|---|---|
| hundreds / leading (blank when idle) | `[106:113]` |
| tens | `[115:122]` |
| units | `[124:131]` |

**Power-on sequence**, measured from genuinely off in
`analysis/captures/2026-10-03-display-bus-cold-boot.sr`:

| From rails up | What happens |
|---|---|
| 0 | all three lines leave 0 V; `ESD1`/`ESD4` settle in ~1 µs on their 4.7 K pull-ups |
| +194 µs | `Disp-P4` starts settling, taking ~250 µs — see below |
| 0 to +4.1 s | **irregular bus activity, not frames**: 32 single-bit blips and one 564-bit burst. Plausibly display-controller init. Anything parsing frames here gets garbage. |
| +4.1 s | normal 67/66 framing begins, display **blank** |
| +4.6 s | `  0` |
| +4.7 s | ` 95`, steady |

**There is no lamp test and no ` 88` phase at boot** — that much is
measured. `-95c-poweron.sr` was taken mid-warm-up, not at power-on, so
whatever it shows is not a boot state.

**What the ` 88` in that file actually is remains UNEXPLAINED.** Three
stories have now been told about it — a lamp test, an unexplained
"power-on state", and the boiler genuinely passing 88 °C — and none is
supported. The third was argued from a ` 45` reading in an uncommitted
scratch capture, which is not evidence this repo holds; and it is
contradicted by the file itself, where ` 88` holds for 1.7 s and then
jumps straight to ` 95` with no 89…94 in between, which is not how a
warming boiler reads. Leave it unexplained until someone captures a
warm-up deliberately.

**Idle: the display shows boiler temperature in °C.** During a brew the
machine takes the display over for a **shot timer in tenths of a second**
(counts `001`→`169` for a 16.9 s pull), so temperature is *not* readable
mid-shot. That timer is a gift, but a narrower one than first recorded. It IS the
machine's own pump-on to pump-off measurement — stopping a cycle by hand
stops the timer, so it tracks real pump time rather than counting out a
stored duration (verified 2026-10-03: a 2Cup cycle cut short by hand
stopped at `210`).

**It does NOT tell you which switch direction ran.** 1Cup and 2Cup each
produce exactly one 67-bit payload and it is byte-identical between them;
nothing on J5 distinguishes a clean cycle from a shot. So the display
replaces 0c for *timing* only. Deciding what kind of event it was still
needs J2.

Agreement with real time, from the first frame showing `001` to the first
showing the final value. (**`000` IS displayed** — once per brew, always
as a standalone 66-bit frame, which is why a decoder that only understood
merged frames never saw it. Firmware 0.8.0 does.)

| | displayed steps | measured | error |
|---|---|---|---|
| 2Cup `001`→`210` | 20.9 s | 21.0061 s | **+106 ms** |
| 1Cup `001`→`105` | 10.4 s | 10.4303 s | **+30 ms** |

Both are of the order of the display's own 100 ms tick plus the ~41 ms
frame period, so there is nothing to explain — but **do not quote this as
"within 100 ms"**, because 106 ms is not. Two earlier claims here were
wrong: "6 ms" compared against the final displayed value instead of the
interval, and "within the display's own 100 ms quantisation" was asserted
without checking that 106 > 100.

### Scales: 2 × Bookoo Themis (BLE)

Pair **both**: one under the cup, one for the grinder. That makes
`dose_in_g` and `dose_ground_g` self-logging, which is the difference
between a logger still in use in six months and one abandoned in week
three.

- `esp-arduino-ble-scales` lists Bookoo Themis as tested — use it rather
  than writing the protocol
- Bookoo publish their protocol at `github.com/BooKooCode/OpenSource`
- The scale sleeps after ~15 min idle. Reconnect logic is mandatory.
- Timestamp samples on arrival with `millis()`. Don't trust device clocks.

### Board: ESP32-S3-DevKitC-1 clone, N16R8

16 MB quad flash + 8 MB octal PSRAM. Three consequences:

- PlatformIO needs `memory_type = qio_opi`. Getting this wrong boot-loops.
- **GPIO35/36/37 are unavailable** (octal PSRAM uses them internally).
- **Partition table entries must stay below 0x800000.** The die is genuinely
  16 MB (esptool reads/writes distinct data above 8 MB, no address wrap), but
  any partition beyond 8 MB makes this clone's Macronix chip boot-loop
  silently (RTC_SW_SYS_RST before any output) on arduino-espressif32 2.0.17.
  Bisected empirically 2026-08-27; only the first 8 MB is usable by firmware.

PSRAM is unused by this workload. It's present because that's the cheap
commodity module, not because we need it.

---

## Architecture (decided — don't relitigate)

```
Bookoo ×2 ──BLE──► ESP32-S3 ──WebSocket──► Go service ──► SQLite
switch sense ─────►  (buffer   (live)          │
flowmeter ────────►   in RAM)                  ├─► serves PWA
                            ──HTTP POST──►     └─► MQTT summary ──► HA
                              (shot complete)
```

**The tablet only ever talks to the Go service.** Web Bluetooth does not
exist on iPadOS in any browser, and a sleeping tablet must never be able
to lose a shot. The ESP32 owns the radio and the timing.

**HTTP POST, not MQTT, for shot upload in Phase 0.** Simpler, curl-able,
and the flash spool already handles retries. MQTT arrives in Phase 1 when
there's a command path to carry.

**Home Assistant consumes, never stores.** Publish 4–5 summary entities
over MQTT discovery for glanceable status and notifications. HA's
entity-plus-recorder model cannot represent a shot object with a 350-point
curve and foreign keys to beans and grind epochs; the relational structure
is the whole value.

**Go for the service, Python for the analysis, SQLite as the interface.**
Go: single static binary, frontend via `embed.FS`, `modernc.org/sqlite`
(no CGO). Python/Jupyter reads the same file for variance work, model
fitting and later Bayesian optimisation. Neither needs to know about the
other.

Deployment target: one Debian LXC on Proxmox, 512 MB RAM. No Postgres, no
Redis, no k3s. Single-user database, ~50 MB after several years.

---

## Repo layout

```
firmware/          PlatformIO, Arduino framework
  platformio.ini
  partitions.csv
  src/
    main.cpp
    scale.{h,cpp}      BLE clients, reconnect
    detector.{h,cpp}   shot state machine
    spool.{h,cpp}      LittleFS ring, upload+retry
    net.{h,cpp}        WiFi, WebSocket, HTTP POST
server/            Go
  cmd/espressolog/
  internal/{store,api,live,mqtt}/
  migrations/001_init.sql   ← from schema.sql
web/               Vite + React PWA
  (start from espresso-logger.jsx — chart, ghost trace, capture flow)
analysis/          Jupyter notebooks, reads the SQLite file
docs/
```

---

## Phase 0 milestones

**0a — scale logging.** BLE, shot detection, spool, upload, UI. No machine
contact. Most of the work, none of the risk. *This is where to start.*

**0b — display bus.** Logic analyser on J5, offline reverse engineering.
Parallel track, no code yet.

**0c — switch sensing.** **150 K / 220 K** divider on PB5 and PB6 into GPIO
(5 V → 2.97 V, 13.5 µA load — an optocoupler's mA would drop ~1 V across
the 1 K series network and could break the board's own threshold). Do NOT
use 220 K / 220 K: 2.50 V against an ESP32-S3 VIH of 2.475 V leaves 25 mV
of margin, which supply tolerance alone consumes, and a pressed button
would read as not pressed. The switch harness is **J2** (silkscreen
`BOTONE`), 5 pins: +5 V, Steam, 1Cup, 2Cup, Water. Gives real shot
boundaries, flush events, and a true `first_drip_ms`.

**0d — flowmeter.** Tap PA8/PA9 (already 3.3 V logic) through 1 K series,
count on interrupt. Calibrate by weighing water on a Bookoo. Log both nets
— possibly quadrature, possibly two-meter support.

**0e — temperature**, only if 0b succeeded.

**0f — sit still.** 30 shots changing nothing, then compute the standard
deviation of shot time and mean flow. That number is the noise floor, the
Phase 3 deadband, and the honest go/no-go on closed-loop control. Do not
skip this to get to the fun part.

**MEASURED 2026-09-28** (`analysis/0f-noise-floor.ipynb`, n=46):
**σ = 0.21 g/s on mean flow (12.2 %)**, σ = 3.43 s on shot time (14.2 %).
Stratify before trusting any re-run — one bean, one grind epoch, one
`detector_version`; pooling detector versions inflates σ for no physical
reason. The figure bundles puck prep with the machine, so it is an upper
bound on what control could fix, not a machine spec.

---

## Weighing attribution

The workflow weighs **once**, after grinding — freezer doses are
pre-portioned, so there is no pre-grind weighing. `dose_ground_g` is
measured; `dose_in_g` is usually `bean.portion_target_g`, and
`shot.dose_source` records which.

**Never ask the UI to declare intent before a weighing.** No "arm dose
capture" button. Every required tap before coffee is a tax paid while
holding a portafilter, and it is how logging habits die.

Instead, record every stable reading as an anonymous `weighing` row and
attribute it **retroactively** when a shot closes:

> the last unattributed weighing before this shot started is its dose

Stable event definition:

```
|dw/dt| < 0.05 g/s sustained >= 1.5 s
  AND weight > 5 g
  AND no shot in progress
  AND within a plausibility window (12-24 g for a dose)
```

Re-placing or nudging the cup yields several events — the last wins,
earlier ones stay with `superseded = 1`. Keep them: when a dose looks
wrong you want to see what the scale actually saw.

This works identically with one scale or two, so `role_hint` on a scale
is a hint and never a requirement. A flat battery must degrade the data,
not break the workflow.

**Show the inferred dose on the capture screen with a stepper to correct
it.** Not a required input — a correctable default. That's how you get
zero-friction and correctness at once. User corrections set
`attributed_by = 'user'`.

---

## Shot detection (0a, scale only)

State machine `IDLE → ARMED → POURING → SETTLING → DONE`:

- **ARMED** — weight within ±0.5 g of zero and stable for 1 s (i.e. tared
  with the cup on)
- **POURING** — `dw/dt > 0.3 g/s` sustained 300 ms. Backdate `t=0` to the
  crossing.
- **stop** — `dw/dt < 0.1 g/s` for 1.5 s. `stop_ms` = when it first
  dropped below.
- **SETTLING** — keep sampling 5 s more → `yield_final_g`.
  `settle_offset_g = final − at_stop`.
- **reject** — final yield < 5 g (a drip or a knock, not a shot)
- **fault** — weight drops > 5 g mid-pour (cup removed)

Flow rate: central difference over a 700 ms window. `mean_flow_gps` over
from 5 g out to 80 % of final, skipping the first-drop lag.

`first_drip_ms` is **null in 0a** — without switch sensing there's no
pump-start reference. It becomes real in 0c.

`settle_offset_g` matters more than it looks: today you stop the shot by
hand when the scale reads target, so the overshoot distribution *is* the
calibration data Phase 1 needs. Phase 0 measures it before any wire is cut.

---

## On-flash format

Packed binary, never JSON — same data as JSON is 20 KB instead of 3.5 KB
and slower to write.

```c
typedef struct __attribute__((packed)) {
  uint16_t t_ms;         // offset from shot start
  int32_t  weight_mg;    // signed; tare drift goes negative
  uint16_t inlet_pulses; // cumulative
  int16_t  temp_dc;      // decidegrees C; INT16_MIN = no reading
} sample_t;              // 10 bytes
```

**Header format v2** (2026-10-03) added three machine-sourced facts, the
first data in this system that neither a scale measured nor a human typed:
`boiler_temp_start_dc`, `machine_timer_dl`, and a `SETPOINT_TOUCHED` flag
bit. Header 109 → 113 bytes. **The server decodes v1 and v2 both** — v1
records exist spooled and as fixtures, and losing one to a format bump
would violate invariant 6. A v1 record reports the new fields as "the
machine did not say", which is the same thing a v2 record written with the
display bus off reports, so nothing downstream needs a version check.

**`shot_sample.temp_dc` will be NULL for most of a shot and that is
correct.** The machine takes the display over for its timer the moment the
pump runs, so there is no boiler temperature to read during a brew. The
useful value is the one from just before the pump started, which is why
`boiler_temp_start_c` is a per-shot column rather than something derived
from the samples.

350 samples ≈ 3.5 KB per shot. One file per shot, header + samples.
Config (WiFi, endpoint, flow calibration, current bean, grind epoch) in
NVS, not the filesystem.

---

## Reference

- **Schema:** `server/migrations/001_init.sql` — read it before touching
  the data model. The comments explain why, not what.
- **GaggiMate** (`jniebuhr/gaggimate`) — ESP32, BLE scales, shot logging,
  web UI. Read its scale abstraction and shot lifecycle first.
- **Gaggiuino** — pressure/flow profiling, relevant from Phase 5.
