# Display-bus captures

Raw sigrok captures from J5 on the Ascaso Dream PID control board. These
are the evidence the 0b/0e decode rests on and the fixtures
`firmware/test/host/display_test.cpp` runs against — real recorded frames,
never synthetic bit patterns.

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
| `2026-10-03-*` (all three) | 1 MHz | D1 | **yes** |

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

`-buttons.sr` and `-95c-poweron.sr` were taken with **no shunt legs** —
each node is 8.2 K into the analyser's input clamp, the same arrangement as
0b. `-buttons-separated.sr` was taken after fitting the 15 K shunts on
`ESD1`/`ESD4`, giving the full 8.2 K / 15 K divider. The bare-clamp
arrangement is fine for the analyser; it is **not** acceptable for the
ESP32 — see `docs/phase-0-status.md`.

**What the shunts measurably changed.** Not the digit decode: every
133-bit frame in all three files renders cleanly, so there is no
corruption to improve on. (An earlier version of this file claimed 0.6 %
undecodable without shunts against 0 % with. That was wrong — those frames
were `PrG`, which the decoder could not name at the time, and the
*with*-shunt capture had more of them.) What does move is **truncated
66-bit frames**, which both decoders drop silently:

| Capture | 66-bit frames |
|---|---|
| `-95c-poweron.sr` (no shunt) | 18 of 218 — **8.26 %** |
| `-buttons.sr` (no shunt) | 9 of 817 — 1.10 % |
| `-buttons-separated.sr` (shunts) | 0 of 1058 — **0.00 %** |

Suggestive of real edge loss that the divider fixes, but their content has
not been examined and the three captures are not otherwise comparable.
Treat it as a lead, not a result.

## The files

| File | What it holds |
|---|---|
| `2026-09-28-display-bus-95c-static.sr` | Steady idle at 95 °C. Rendered ` 95` in all 2267 frames — the capture that proved the digit decode. |
| `2026-09-28-display-bus-flush-timer.sr` | A flush. The machine takes the display over for a shot timer in tenths of a second, `001`→`169` against a displayed 16.9 s. |
| `2026-10-03-display-bus-95c-poweron.sr` | 8 s from power-on. **First 1.7 s read ` 88`** — both digit fields all-segments, leading field blank — then ` 95` at 1.81 s. Counting that as corruption invents a noise problem that is not there. |
| `2026-10-03-display-bus-buttons.sr` | 30 s, six button events: left short at 2.06 s, right short at 5.69 s, left held 2.8 s, right held 2.65 s, then repeated left presses from 18.25 s. Contains the 95 → 120 setpoint ramp and its return, and the ~2.44 Hz adjust-mode blink. |
| `2026-10-03-display-bus-buttons-separated.sr` | 40 s through the finished divider. Five short **right** presses alone (5.2–12.5 s) change nothing, because programming mode was never entered; then five short **left** presses (18.8–25.9 s), each rendering **`PrG`**. The cleanest button fixture — one bit at a time, 6 s between the blocks. |

The button sequences are the operator's own account, confirmed against the
traces. Re-recording them identically is not possible, so treat these files
as irreplaceable.

**The display is not digits-only.** `PrG` renders as three 7-segment
letters; `tools/decode-display-bus.py` knows P, r and G and prints any
other unknown glyph as its raw `<abcdefg>` pattern rather than `?`, because
five identical `???` readings look exactly like noise and sent the first
analysis chasing a frame-corruption problem that did not exist.
