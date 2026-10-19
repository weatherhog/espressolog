# The display bus — J5

> **Normative on:** the J5 display bus — connector identification, the
> clock/data assignment, `Disp-P4`, the 67/66 frame structure, the glyph
> table, the bit offsets, the blink rates, the power-on sequence, and what
> the displayed temperature actually means.
>
> **CLAUDE.md carries** the live conclusions in its `Display bus` stub: where
> to tap and with what, the pin table, clock-on-GPIO4/data-on-GPIO5 and its
> silent failure mode, never drive or load `Disp-P4`, parse the 67/66 pair
> rather than 133 bits, bit 114, the buttons, the two blink rates and the
> 600 ms threshold between them, and the narrow meaning of
> `boiler_temp_start_c`. Those change what you build; the captures,
> arithmetic and retractions below are what they rest on.
>
> **Changing a number here means grepping CLAUDE.md for it in the same
> commit.** `firmware/src/display.cpp` and `tools/decode-display-bus.py` are
> the implementations and win over both.

---

## Provenance — which machine, in which state

**The display shows `02.4` for ~3 s at power-on** (read 2026-10-05). It is
**probably a firmware version — that is an interpretation, not something
observed**; all anyone has seen is the string. Which firmware, display
controller or main MCU, is also unknown.

Recorded anyway because every decode in this file came from **this machine
in this state**: the glyph table, the bit offsets, the timer flag at 114,
the 67/66 structure. If a future capture disagrees, check whether `02.4`
still shows before assuming the protocol changed.

---

## The decode

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

**The late edge does not fit that story, and it reproduced on a second
boot.** The quantity that reproduces is **its delay from the preceding
edge**, not the rails-to-last-edge span:

| Boot | delay from previous edge |
|---|---|
| `-cold-boot.sr` | **201.273 ms** |
| `-boot-version.sr` (2026-10-05, hot) | **201.3 ms** |

Agreement to ~30 µs on a 201 ms interval. **Quote that, not the 204 ms
span**, because the span bundles in the settling burst, which does *not*
reproduce: the cold boot had **24** intervals inside the first 3.2 ms, the
2026-10-05 boot had **2**. The surrounding noise varied by an order of
magnitude while the late edge landed within 30 µs — which is itself the
argument that it is a real event rather than more settling.

**n = 2.** Observed on both boots so far; that is not "every boot". Still
no known cause, nothing has been seen to respond to it, and one edge is
still not a protocol. Note also `-flush-timer.sr` carries 4 `Disp-P4` edges
mid-flush, so it is not purely a boot phenomenon.

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

**THE DISPLAY BLINKS FOR TWO DIFFERENT REASONS, AT DIFFERENT RATES.** Only
one of them is a setpoint edit, and the firmware distinguishes them by
period alone — so the period is load-bearing:

| State | blank-to-blank | `display.cpp` |
|---|---|---|
| **Setpoint being adjusted** | **~410 ms** (2.44 Hz, 0.204–0.210 s per half-cycle) | latches `setpoint_touched` ✓ |
| **Steam, and cooling afterwards** | **1.021–1.101 s** (measured over 100 intervals) | does **not** latch ✓ |
| `BLINK_WINDOW_US` | **600 ms**, sitting between them | |

The guard band is **414 → 1020 ms**, and 600 ms sits inside it with
**186 ms below and 420 ms above**. So steaming does not produce a false
`setpoint_touched`, and a setpoint edit still latches. **The threshold was
chosen before anyone knew the steam blink existed**; this is the
measurement that justifies it rather than luck.
`2026-10-05-display-bus-steam.sr`, 102 blank onsets.

**Use the interval, never the blank duration.** Steam blanks average longer
(0.550 s against a setpoint edit's 0.204 s), which looks like a cleaner
discriminator — but steam's **shortest blank is 0.156 s, shorter than the
setpoint blank**, so duration would misfire. The interval does not. This
choice is deliberate.

One interval of the 102 is **5.77 s** — the gap between the `165` and ` 95`
blink regimes. Harmless (it is also > 600 ms) but it is why the quoted
1.021–1.101 s range covers 99 intervals, not 100.

While adjusting, the display blanks **entirely** (all three fields zero)
and comes back. The steam blink does the same thing more slowly, which is
why period is the only discriminator available.

**What blinking means is "showing a target it has not reached":** during a
steam cycle the display blinks **`165`**, and after steam is released it
blinks **`95`** while the boiler cools back down. Contrast the warm-up from
cold, which shows the *actual* temperature climbing and does **not** blink
at all. That asymmetry is unexplained.

**The J5 conditioning is NOT described here.** The divider values and the
`Disp-P4` prohibition are live conclusions and stay in CLAUDE.md; the corner
arithmetic is in `docs/taps-and-dividers.md`. This file is about what the bus
says, not about how to get it into a GPIO safely.

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
no brew. **The decimal point is not in the frame format — 2026-10-05.** The strong
evidence is a counted negative: across the three new captures, the gap bits
**113 / 122 / 123 / 131 / 132 are set in ZERO of 2784 digit frames** (577
boot + 2207 warm-up 133-bit frames; the steam capture agrees). A decimal
point would almost certainly live in one of those, one bit past each
seven-segment field. It never does.

Separately, the display **can** render a point: at power-on it shows
`02.4`. That appears during a window in which the bus sends nothing (see
the power-on table), so the controller can clearly drive the display
without being told frame-by-frame — **`02.4` is therefore most likely
controller-generated and never visible to a frame parser.**

Two honest limits on that. Nobody timed `02.4` against the capture, so
"shown during the silent window" is inference from its ~3 s duration, not
observation. And a search of the boot bitstream for the `0`/`2`/`4` segment
patterns (both polarities, spacings to 20 bits) found nothing — but that is
one strategy against an init burst nobody has decoded, and it would miss
any other encoding.

Consequence either way: the operator's `E.01` reading was punctuation,
consistent with the manual printing `E01`.

**One loose end, recorded rather than smoothed over:** bit 132 is set in
the 564-bit init burst. It is zero in every 133-bit frame. This file says
113/122/123/131/132 are always zero, which holds for digit frames; the init
burst is not one.

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
| +194 µs | `Disp-P4` starts settling, taking ~250 µs — see the `Disp-P4` section **above** (rail settling, R32 = 100 K against the 4.7 K pull-ups). *(This said "see below" and the explanation is ~325 lines above it.)* |
| 0 to ~+1.1 s | **irregular bus activity, not frames**: 32–33 single-bit blips, a 48-bit frame, and one 564-bit burst. Plausibly display-controller init. Anything parsing frames here gets garbage. |
| **~+1.1 s to +4.1 s** | **THE BUS IS SILENT — no frames at all for ~3 s.** Confirmed 2026-10-05: ~800 of the clock edges counted in this window are the 564-bit burst's own tail, and after it the line idles 98.8 % high. |
| | **This is when the display shows `02.4`, the firmware version, with a decimal point** — and nothing on the bus carries it. See the decimal-point note above. |
| +4.1 s | normal 67/66 framing begins |
| +4.12 s | blank |
| +4.63 s | **`  0`** |
| +4.69 s | the first real reading |

**`  0` is a boot transient, not a boiler reading**, and it is present on
both boots — cold (+4.1/+4.6/+4.7) and hot (+4.116/+4.627/+4.694,
2026-10-05). An earlier draft of this table claimed the hot boot skipped it
and told readers to drop the step; that was wrong, and wrong because
`tools/decode-display-bus.py` discards 66-bit frames (`MIN_FRAME_BITS =
120`) and both the blank and the `  0` arrive in 66-bit frames. **If you are
checking boot behaviour, do not use that tool.**

What boiler state *does* change is what comes next: a cold machine climbs
from the forties, a hot one is already at its reading.

**There is no lamp test and no ` 88` phase at boot** — that much is
measured. `-95c-poweron.sr` was taken mid-warm-up, not at power-on, so
whatever it shows is not a boot state.

**The ` 88` is SOLVED — 2026-10-05**, by capturing a warm-up deliberately,
which this file previously said was the only thing that would settle it.
`analysis/captures/2026-10-05-display-bus-warmup.sr`:

    0.040s [ 46]   ...one degree at a time...   52.489s [ 87]
                                                54.631s [ 88]
                                                57.165s [ 95]   <- jumps

The boiler climbs **one degree at a time from 46 to 88, then jumps straight
to 95 with no 89–94** — 48 change-events across 2270 digit frames.

**The ±1 °C jitter elsewhere in the same ramp makes the skip more striking,
not less**: the display demonstrably flickers between adjacent values at
47/48 and 54/55, so 89–94 is not a display that cannot show those numbers. That is exactly the pattern in `-95c-poweron.sr`.

So of the three stories told about it, the third — "the boiler genuinely
passing 88 °C" — was right, and the objection that killed it was the error:
"no 89–94 in between is not how a warming boiler reads" assumed a behaviour
this machine does not have. **It really does skip 89–94.** Why is still
unknown; the display may switch from reporting the boiler to reporting the
setpoint once it decides it is ready.

**The ` 95` at +4.7 s in `-cold-boot.sr` is explained the same way**: that
machine was already hot. No boiler reaches 95 °C in 4.7 s, and that
discrepancy sat unremarked in this file.

**Idle: the display shows a temperature in °C — but WHICH temperature
depends on the range, and the range that matters is the ambiguous one.**

**Below ~88 °C it is demonstrably the actual boiler**: during a warm-up it
climbs one degree at a time and **jitters ±1 °C** (` 47`→` 48`→` 47`→` 48`
at 1.9–2.0 s; ` 54`→` 55`→` 54`→` 55` at 8.5 s), which is what a live
readout looks like.

**At ` 95` it is indistinguishable from the setpoint.** In the same capture
it sits at ` 95` for **1134 consecutive frames — 57 s without a single
change**. A PID-controlled boiler does not hold ±0 °C for a minute. And
during the post-steam cooldown the display blinks ` 95` while the boiler is
demonstrably hotter than that.

**This matters for `boiler_temp_start_dc`** (spool v2, migration 008),
which is sampled immediately before a shot — i.e. always in the ambiguous
range. An earlier draft of this paragraph claimed the warm-up "confirmed"
the column measures the real boiler and so rescued 008's rationale. **It
does not**: it confirms the behaviour only in the range the column is never
sampled in.

What the column *does* reliably carry: a steady ` 95` means at setpoint; a
blink or a different value means not. That is still worth storing. Settling
whether it is ever a true boiler reading above 88 °C needs a capture with
the boiler deliberately pulled off setpoint and allowed to recover. During a brew the
machine takes the display over for a **shot timer in tenths of a second**
(counts `001`→`169` across 16.8915 s — 168 steps, i.e. 16.8 s of
displayed time; see the agreement table below, and do **not** restate it
as "a 16.9 s pull", which compares against the final value instead of the
interval), so temperature is *not* readable
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
