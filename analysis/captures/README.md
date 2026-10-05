# Machine captures

Mostly display bus (J5). One J4 flowmeter capture, which is a **null
result and the record of an invariant-1 violation** — see the last section.

Raw sigrok captures from the Ascaso Dream PID control board — J5 for the
display bus, J4 for the one flowmeter attempt. These
are the evidence the 0b/0e decode rests on. Note that **no code reads
these files**: `firmware/test/host/display_test.cpp` uses bit literals
transcribed from them, so the coupling is by hand and nothing breaks if a
file changes.

## Two things that will catch you out

**The sample rate differs per file, and the decoder defaults to 2 MHz.**
Pass it explicitly. Get it wrong and framing collapses: nothing decodes at
all, or the readings are right and every timestamp is silently scaled.

**The channel order is reversed between the two sessions.** The decoder
takes CSV column 0 as the clock, so the 2026-10-03 files need their first
two columns swapped and the 2026-09-28 files must **not** be swapped.

| File | Rate | Clock is | Swap? |
|---|---|---|---|
| `2026-09-28-display-bus-95c-static.sr` | **2 MHz** | D0 | no |
| `2026-09-28-display-bus-flush-timer.sr` | 1 MHz | D0 | no |
| `2026-10-03-*` (all seven) | 1 MHz | D1 | **yes** |

Decoding a 2026-10-03 file:

    sigrok-cli -i <file> -O csv \
      | python3 -c "import sys
    for l in sys.stdin:
        s=l.strip()
        if s and s[0] in '01':
            p=s.split(','); print(f'{p[1]},{p[0]},{p[2]}')" \
      | python3 tools/decode-display-bus.py 1e6

## Channel mapping

**2026-09-28** — four wire stubs soldered directly to the J5 pads, 8.2 K in
series per channel and 1.5 K in the ground lead. Which probe sat on which
pad was not written down, which is why it took until 2026-10-03 to
establish which signal is the clock. The stubs were removed the same day.
The order is now recoverable after the fact: these files decode **only**
with D0 as clock, so **D0 = `ESD4`, D1 = `ESD1`** — the reverse of the
later session.

**2026-10-03** — through the in-line T-piece, with the mapping recorded
this time:

| Channel | J5 pin | Net | Conditioning |
|---|---|---|---|
| D0 | 2 | `ESD1` — data | 8.2 K series |
| D1 | 3 | `ESD4` — **clock** | 8.2 K series |
| D2 | 4 | `Disp-P4` | 8.2 K series, never terminated |
| GND | 1 | `VSS` | direct |

Conditioning, which matters for any cross-file comparison:

| Capture | `ESD1`/`ESD4` |
|---|---|
| `-95c-poweron.sr`, `-buttons.sr` | 8.2 K series only, into the analyser's input clamp — the 0b arrangement |
| `-buttons-separated.sr`, `-setup-menu.sr`, `-flush-timer.sr`, `-1cup-clean.sr`, `-cold-boot.sr` | full 8.2 K / 15 K divider |

`Disp-P4` is 8.2 K series with no shunt in all of them. The bare-clamp
arrangement is fine for the analyser; it is **not** acceptable for the
ESP32 — see `docs/phase-0-status.md`.

**The shunts changed nothing measurable here.** Two claims were made and
both were wrong, so they are recorded rather than quietly deleted.

First: "0.6 % undecodable without shunts against 0 % with". Those frames
were `PrG`, which the decoder could not name at the time — and the
*with*-shunt capture had more of them. Digit-frame corruption is **zero**
in every capture.

Second: "the divider fixed the 66-bit splits". The evidence for that was a
confound. What actually dominates is **how hard the heater is working** —
0.03 % in `-95c-static.sr` (steady idle) against 8.26 % in
`-95c-poweron.sr` (warming from cold), and within a single capture, one
hardware configuration, `-flush-timer.sr`:

| Window | Splits |
|---|---|
| Idle, before the brew | 1.19 % (1 of 84) |
| During the brew | 3.62 % (20 of 552) |
| Settling | 5.19 % (7 of 135) |
| Idle after, boiler recovering | **6.55 % (54 of 825)** |

**Whether the shunts also help is NOT settled, and the honest reading
leans the other way.** The cross-file idle comparison that was used to
dismiss them — 1.10 % unshunted against 1.19 % shunted — rests on *one*
split in 84 frames over 3.3 s, whose confidence interval spans the whole
table. The better-powered shunted sample, `-buttons-separated.sr` at
**0 of 1058 over 40 s**, was dropped; against `-buttons.sr`'s 9 of 817 it
is significant in the shunts' favour. Both captures are idle at
temperature, so that is the comparison to trust until someone runs a
controlled pair.

Either way the shunts earn their place by keeping the ESP32 inside its
absolute maximum rating, which needs no help from this argument.

The shunts earn their place by keeping the ESP32 inside its absolute
maximum rating, which is reason enough. They are not a frame-integrity
fix.

**Every split used to lose a digit reading**: both decoders discarded any
frame that was not 133 bits. Firmware 0.8.0 parses the 67/66 pair properly
and no longer does; `tools/decode-display-bus.py` still has
`MIN_FRAME_BITS = 120` and still does, so the two now disagree. Up to ~8 % of readings
during hard heating. The fix is to parse the 67/66 pair properly instead
of relying on a 1 ms gap threshold — see `docs/phase-0-status.md`.

## 2026-10-05 — warm-up, steam, and the version boot

**The clock is on D1 — and it has been on D1 for both T-piece sessions.**
Recorded this time:

| Channel | Net | Conditioning |
|---|---|---|
| D0 | `ESD1` — data | 8.2 K series |
| D1 | `ESD4` — **clock** | 8.2 K series |
| D2 | `Disp-P4` (boot capture only) | 8.2 K series, never terminated |
| GND | `VSS` | direct |

So these need the **same first-two-column swap** as the 2026-10-03 files.

| Session | tap | clock lands on |
|---|---|---|
| 2026-09-28 | soldered stubs | D0 |
| 2026-10-03 | T-piece | **D1** |
| 2026-10-05 | T-piece | **D1** |

The intent on 2026-10-05 was to put the clock on D0 and it landed on D1
again. **The mapping is stable, and differs only from intent** — which
points at CLAUDE.md's "black adjacent to red is pin 3" rule being
systematically wrong rather than at a one-off mis-plug, though a repeated
habit of connecting it the same way is not excluded.

**It does not affect decoding** — the clock is identifiable from its
two-valued low period regardless — but it **must** be settled before the
ESP32 is wired, because the firmware hard-codes GPIO4 as clock and the
conductor rule is what tells you which wire to land there.

**`tools/decode-display-bus.py` had a two-column bug until 2026-10-05.**
`line.split(',')` without `.strip()` left the trailing newline on the data
field whenever it was the *last* column — which it is in any two-channel
capture. Every glyph lookup then failed. Three-column captures hid it for
months because the data column sat in the middle. If you decode an old
two-channel capture with an old checkout, this is why it returns garbage.

| File | Rate | Holds |
|---|---|---|
| `2026-10-05-display-bus-warmup.sr` | 1 MHz | 114 s of a genuine warm-up. Climbs ` 46`→` 88` one degree at a time (48 change-events, 2270 digit frames), then **jumps to ` 95` with no 89–94**. Settles the ` 88` question. Shows the display reports the *actual* boiler **below ~88 °C** — it jitters ±1 °C there — but then holds ` 95` for 1134 consecutive frames without a single change, which is **not** how a PID boiler reads; above 88 °C actual and setpoint are indistinguishable. No blanks — warm-up does not blink. |
| `2026-10-05-display-bus-steam.sr` | 1 MHz | 113 s spanning a steam cycle. Blinks `165` to 49.487 s, then ` 95` from 54.647 s — a **5.160 s** value-to-value gap (5.770 s blank-onset to blank-onset; quote whichever, but say which). 102 blank onsets → 101 intervals; discarding the capture-start artefact leaves 100, of which **99 fall in 1.021–1.101 s** and one is the 5.770 s regime change. This is the measurement showing `BLINK_WINDOW_US = 600 ms` sits inside a 414–1020 ms guard band — 186 ms below a setpoint edit, 420 ms above steam. |
| `2026-10-05-display-bus-boot-version.sr` | 1 MHz | 90 s, D0/D1/D2, machine switched on at 56.34 s with a **hot** boiler. Reproduces the init burst (33 blips + one 564-bit frame), framing at +4.1 s, and **`Disp-P4`'s late edge at +204.3 ms** against the first capture's 204.5 ms. Also contains the ~3 s silent window in which the display shows `02.4` — the firmware version — that **nothing on the bus carries**. |

## The files

| File | What it holds |
|---|---|
| `2026-09-28-display-bus-95c-static.sr` | Steady idle at 95 °C. Rendered ` 95` in all 2267 frames — the capture that proved the digit decode. |
| `2026-09-28-display-bus-flush-timer.sr` | A flush. The machine takes the display over for a shot timer in tenths of a second, `001`→`169`: 168 displayed steps = 16.8 s against 16.8915 s measured, **+92 ms** (not "against a displayed 16.9 s", which compares to the final value instead of the interval — see CLAUDE.md). |
| `2026-10-03-display-bus-95c-poweron.sr` | **Misnamed — this is mid-warm-up, not a boot.** 8 s showing ` 88` for the first 1.7 s then ` 95`, with no 89…94 in between. **What the ` 88` is remains unexplained**; three stories have been told about it (a lamp test, a "power-on state", the boiler passing 88 °C) and none survives the file itself. Kept as the example of why not to explain a reading you have not deliberately captured. |
| `2026-10-03-display-bus-cold-boot.sr` | 90 s from genuinely **off**, through the rails coming up, to a steady ` 95`. 25 `Disp-P4` transitions spread over 204.5 ms from the rails coming up, none afterwards — see CLAUDE.md, where 24 of them fit rail settling and the 25th does not. (`2026-09-28-display-bus-flush-timer.sr` also carries 4, mid-flush.) Also shows ~4.1 s of irregular non-frame activity (32 single-bit blips, one 564-bit burst) before normal framing starts, then blank → `  0` → ` 95`. |
| `2026-10-03-display-bus-buttons.sr` | 30 s, six button events: left short at 2.06 s, right short at 5.69 s, left held 2.8 s, right held 2.65 s, then repeated left presses from 18.25 s. Contains the 95 → 120 setpoint ramp and its return, and the ~2.44 Hz adjust-mode blink. |
| `2026-10-03-display-bus-flush-timer.sr` | 60 s containing a **2Cup** cycle, stopped by hand at 21.0 s. Timer `001` at 3.326 s to `210` at 24.332 s — 20.9 s of displayed steps across 21.006 s, i.e. **+106 ms — which is *larger* than the display's own 100 ms tick, not within it** (see CLAUDE.md; an earlier draft here claimed otherwise) — then ` 95` at 29.451 s. The fixture for shot-timer parsing, and the only capture whose split rate can be read against machine state within one file. |
| `2026-10-03-display-bus-1cup-clean.sr` | 60 s containing a **1Cup** clean cycle, `001` at 1.638 s to `105` at 12.068 s. Its purpose is the comparison: 1Cup and 2Cup each yield exactly one 67-bit payload and it is byte-identical between them, so **nothing on this bus distinguishes the two switch directions**. |
| `2026-10-03-display-bus-setup-menu.sr` | 60 s walking the whole `SET UP` menu: `5Et`/`UP ` alternating, then `Ud`, `Pr`, `Cr`, `0FF`, `U` — the manual's five parameters in the manual's order. Source of the letter glyphs, and the only capture containing the **both-buttons-pressed** payload. Also shows the setpoint being corrected 92 → 95 from 46.7 s. |
| `2026-10-03-display-bus-buttons-separated.sr` | 40 s through the finished divider. Five short **right** presses alone (5.2–12.5 s) change nothing, because programming mode was never entered; then five short **left** presses (18.8–25.9 s), each rendering **`PrG`**. The cleanest button fixture — one bit at a time, 6 s between the blocks. |

The button sequences are the operator's own account, confirmed against the
traces. Re-recording them identically is not possible, so treat these files
as irreplaceable.

**The display is not digits-only.** `PrG` renders as three 7-segment
letters; `tools/decode-display-bus.py` knows P, r and G and prints any
other unknown glyph as its raw `<abcdefg>` pattern rather than `?`, because
five identical `???` readings look exactly like noise and sent the first
analysis chasing a frame-corruption problem that did not exist.

## J4 — flowmeter (0d), failed attempt

| File | Rate | Channels |
|---|---|---|
| `2026-10-03-flowmeter-j4-idle.sr` | **100 kHz** | D0, D1 on J4 — 2 s idle reference |
| `2026-10-03-flowmeter-j4-null.sr` | **100 kHz** | D0, D1 on J4 — **net mapping NOT established** |

90 s. **Zero edges on both channels**: D0 steady low, D1 steady high, start
to finish. `2026-10-03-flowmeter-j4-idle.sr` is a 2 s idle capture from the
same session with the same levels and also 0 edges.

The file contains **no transitions at all on the probed channels**, so
nothing in it timestamps the 2Cup cycle or the fault. That the cycle
happened inside this window rests on the operator's account, not on the
file.

The machine stopped with **`E01`** (Ascaso's dose-control fault). Disconnecting J4 and
power-cycling cleared it, so **the tap caused the fault**. The mechanism is
not established — the leading hypothesis is that the flow sensor lost its
supply, which would make J4 pin 3 the supply, but that is untested.

Keep this file. It is the evidence for the 0d entry in
`docs/phase-0-status.md`.

**It is also the counter-example to its own first reading.** An earlier
draft argued the absence of 50 Hz hum ruled out floating probes. The `.sr`
format stores all eight hardware channels even when two are enabled, and
reading them refutes that outright:

| Capture | unconnected channels |
|---|---|
| this file | D3 **0 edges, steady low**; D2 and D4–D7 ~50 Hz hum |
| `2026-10-03-display-bus-flush-timer.sr` | D3 **0 edges, steady low**; D2 0 edges, steady high; D4–D7 hum |

Both of this capture's signatures are reproduced by channels connected to
nothing. On a digital capture "zero edges" and "no hum" are also the same
measurement, not two.

Note this file was taken with **all eight channels live**, against the
procedure in `tools/decode-display-bus.py` ("enable ONLY the channels in
use"). Five of the six unused channels are full of hum; that is expected,
not a defect in the capture.

**Do not read "0 edges" as "the flowmeter does not pulse".** The capture
cannot distinguish a dead sensor, wrong nets, a shorted conductor, and
probes on nothing.
