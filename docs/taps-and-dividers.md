# Taps and dividers — the arithmetic

> **Normative on:** the corner arithmetic for every resistive tap this
> project fits to a machine signal — J2 (switch lines), J4 (flowmeter `#`),
> J5 (display bus). The source-impedance measurements each one is sized
> against live here too, because a corner table separated from the
> measurement it rests on is how two files start disagreeing.
>
> **CLAUDE.md carries** the three fitted values, the margin each one lands
> at, the prohibitions (`Disp-P4` never loaded; never 220 K / 220 K at J2;
> never 150 K at J4), and the J4 `+` read-only rule — which is NOT here and
> is not to be copied here. Those are the live conclusions; this file is
> what justifies them.
>
> **Changing a number here means grepping CLAUDE.md for it in the same
> commit.** `tools/check-facts.py` enforces that for the tagged ones and
> runs in CI; it does not cover the untagged ones, so grep anyway.

---

## The three fitted values, in one place

| Tap | Divider (series, shunt) | Lands at | Corner |
|---|---|---|---|
| **J2** — four direction lines | **150 K / 220 K** | 2.89 V measured | **+203 mV** |
| **J4** — flowmeter `#` | **100 K + 220 K** | 3.13 V | **+348 mV** |
| **J5** — `ESD1`, `ESD4` | **8.2 K / 16.4 K** | — | **+18 mV** |
| **J5** — `Disp-P4` | 8.2 K, **no shunt, ever** | — | n/a |

<!-- fact:divider-j2 150 K / 220 K -->
<!-- fact:divider-j4 100 K + 220 K -->
<!-- fact:divider-j5-shunt 16.4 K -->

*(J2's pair is written with a slash and J4's with a plus because that is how
each is written everywhere else in this repo. They mean the same thing.)*

**The J5 row is the one that is specified rather than built.** 8.2 K / 15 K is
what is physically in the machine today and it fails its corner; see below.

**The J4 `+` conductor has no row because it gets no tap of any kind.** Its
read-only rule lives at the top of CLAUDE.md, beside the invariants, and has
been lost three times by being written down near a divider table. Do not give
it a row here.

---

## J5 — the display bus tap

**`Disp-P4` IS THE LINE THAT MUST NOT BE LOADED, AND THIS PARAGRAPH USED TO
SAY THE OPPOSITE.** Until 2026-10-12 it read: *"This is the one J5 line where
a careless connection could actuate the machine… The 8.2 K / 15 K divider is
high-impedance and receive-only, so it is safe as specified."* Two defects, and together they made this the
only paragraph in CLAUDE.md that **invited** an invariant-1 violation rather
than warning against one. (It is not the only invariant-1 hazard documented
here — `selftest` driving GPIO8 and the Phase-1 photoMOS on J2 position 1 are
both hazards, but both are flagged as such.)

- **The antecedent was gone.** "This line" had drifted ~80 lines from the
  `Disp-P4` discussion it referred to, landing after a passage about display
  blinking. A reader met it with no referent and attached it to J5 generally.
- **The conclusion is backwards for `Disp-P4` specifically.** It is
  **bidirectional** and reaches the MCU through R32 = 100 K. `docs/phase-0-status.md`
  records that **a 23.2 K divider could drag it under 1 V if that pull-up is
  weak**, feeding a false level into the machine continuously.

  **Grade that mechanism honestly**: the "MCU sees a button held down" wording
  it was first written with is wrong, because this same file establishes
  (2026-10-03, 344 s of live bus) that `Disp-P4` is **not** the button line —
  the buttons are bits 49/58 of the 67-bit frames, and `Disp-P4` moves only in
  the ~204 ms after rails-up. What is actually true is narrower and still
  sufficient: **its pull-up strength is unrecorded, so the consequence of
  loading it is unknown.** That is reason enough to fit nothing. `docs/shopping-list.md` specifies `Disp-P4` as branched through
  8.2 K and **terminated at nothing**, and that is the built configuration.

**So: the 8.2 K / 15 K divider is correct on `ESD1` and `ESD4`** — correct as
against the 8.2 K / 8.2 K that was mistakenly fitted, which is the error this
passage was written about. **It is NOT the right value for the perfboard:**
2026-10-19 ran its tolerance corner for the first time and the as-built 15 K
comes out at **−70 to −87 mV** (the spread is the unmetered 4.7 K pull-up's
tolerance), worse than the −45 mV that got 150 K / 220 K rejected at J4.

<!-- fact:divider-j5-shunt 16.4 K -->
**Fit 16.4 K** — two 8.2 K in series, 25 in stock, **+18 mV** at the
pessimistic corner. That is the value this repo specified before any of this.

**Two drafts of this line got it wrong in opposite directions**, both recorded
in `docs/phase-0-status.md`. One ordered **23.2 K**, which puts the pin 600 mV
over VDD and the clamp into continuous conduction. The other said to **fit
nothing** until the drive type is metered, because no value cleared both the
logic-high floor and the absolute-max ceiling — but that treated VDD + 0.3 as
a hard wall, and the **8.2 K series bounds clamp current**, which this repo
argues itself for J4 and for the ESP-unpowered case. The ceiling binds
softly; 16.4 K clears it.

**The drive type is still untested and still worth testing** — if J5 is
open-drain the ceiling stops mattering and 18–22 K open up — but it does not
block the build. **Meter it unpowered, in Ω mode — J5 pin 2 or 3 against
J2 position 1**, machine off and both connectors unplugged. The reference
matters: **J5 itself has no +5 V** (it is ground plus three signals), so
"ohm against 5 V" without naming J2 pos 1 returns a false null that looks
exactly like push-pull. A DC reading on the live line is no good either —
`ESD1`/`ESD4` switch continuously, so a meter gives a duty-cycle mean, not a
level. The built board decodes and is not an emergency; the perfboard should
not copy it either way. **And the 15 K
shunt must NOT be fitted to `Disp-P4`.** Branch it through its 8.2 K for
symmetry if you like, leave the far end open, and never hang anything on it
that can source or sink current.

### The J5 filter capacitor — a candidate, not a requirement

**A capacitor here is NOT currently justified, and a 2026-10-09 edit said it
was.** That edit attributed 6086 dropped display edges to pump coupling and
specified 10 nF. Both parts were wrong:

- **10 nF would have stopped the bus decoding.** Against a 6.9 kΩ source it
  is a 69 µs time constant facing 36 µs clock highs
  *(6.9 kΩ is the as-built 15 K arithmetic; at the specified 16.4 K it is
  7.2 kΩ and 72 µs. Worse, not better — the conclusion is unaffected, but
  do not reuse 6.9 kΩ when sizing the 470 pF.)* — the clock never
  settles, and it would have presented as a wiring fault rather than a filter
  error. Corrected to 470 pF the same day.
- **The drops are NOT coupling, and the actual cause is unidentified.** What
  is established: they arrive as **one burst at pump start** (+6086) then
  +164 across 30 s of pumping, and shot 1 gave a comparable figure over a
  37.6 s run — **constant, not scaling with pump-on time**, which coupling
  would. 6086 edges is 1.88 s of bus at this repo's own 3244 edges/s, and the
  display decoded the timer cleanly 0.5 → 33.7 s throughout, which a swamped
  bus cannot do.

  **But "serial polling" does not follow, and an earlier version asserted
  it.** A 3 s poll would drop *periodically*, not in one burst — and the idle
  rows of the shot-2 measurement table (now in `docs/flowmeter-j4.md`, under
  *Measured*) were polled at the same cadence with the drop counter
  **frozen**. What the evidence supports is "a one-off stall around
  pour start"; candidates are the spool/BLE work at that moment or the serial
  traffic, and they are not separated. **`worst poll gap` is the
  discriminator and this repo already prints it** — record it on the next
  capture.

**`docs/phase-0-status.md` already carried ~470 pF for J5 before any of
this**, as a *contingent second remedy* after raising the shunt to 16.4 K.
Prefer that passage. Re-measure with the serial console detached before
fitting anything here.

---

## J2 — the switch tap

**The 5 V rail is now MEASURED (2026-10-04), so the
divider is sized against a reading rather than the schematic** — which is
the step that was skipped at J4. Unlike J4's open-collector line, the
switch is driven through R45's **100 Ω**, so a 370 K divider loads it by
about a millivolt and the unloaded arithmetic is valid here.

<!-- fact:divider-j2 150 K / 220 K -->
**150 K / 220 K** divider on PB5 and PB6 into GPIO
(5 V → 2.97 V, ~0.5 V over VIH; 13.5 µA load, dropping **1.35 mV** across
R45).

**MEASURED AT THE JUNCTION 2026-10-19: 2.89 V with a lever held, 0 V at
rest.** Against an ESP32-S3 VIH of **2.475 V** that is **+415 mV of margin**.
This closes an item the bench list carried as open — *"we proved the firmware
reads `1`, never wrote down the margin"*.

*(A draft wrote "+580 mV against a VIH of 2.31 V". **2.31 V is 0.7 × VDD, the
74HC figure** — this project uses **2.475 V** nominal and **2.60 V** at a
5 %-high rail, and CLAUDE.md quotes 2.475 V again when rejecting
220 K / 220 K. **A first version of this parenthetical then claimed the error "was caught and
fixed in `docs/phase-0-status.md` in the commit that introduced it". It was
not** — that file carried it in three further places, and saying otherwise
closed the loop on a defect that was still open. A review found all three.
Applying a correction in one file and announcing it as complete is strictly
worse than not applying it, because it stops anyone looking.)*

**The 80 mV below the predicted 2.97 V is NOT fully accounted for, and this
line used to say "nothing to chase".** The two causes it offered do not add
up to 80 mV:

- **a 4.86 V rail** would do it — but CLAUDE.md records that rail as
  **measured at 5.0 V** (2026-10-04), so invoking 4.86 V contradicts a
  measurement this repo already has;
- **1 % resistor tolerance** moves the junction by at most **~24 mV**.

Together they leave **~59 mV unexplained** unless the rail really is lower
than recorded. **Still nothing to act on** — the margin is +415 mV and a
60 mV discrepancy threatens nothing — but it is an open discrepancy between
two of this repo's own numbers, not a closed question. Re-read the rail at
the junction next time J2 is live.

**And a logic analyser on the junction costs nothing measurable at room
temperature.** Same reading, 2.89 V, probe on and probe off — the difference
is below meter resolution, so under ~10 mV. A review had flagged this as
worth up to **89 mV**, from a worst-case HC input leakage of 1 µA into the
junction's 89 kΩ source impedance.

**Grade the closure honestly: ONE reading on ONE line at ONE temperature does
not retire a datasheet worst case.** The 1 µA figure is a max over the
part's full temperature range; typical at 25 °C is an order lower, which is
what was measured. The bench is not a boiler housing. What this establishes
is that the analyser is fine to use on a J2 junction *on the bench*, which is
all it is ever used for — not that the 89 mV bound is wrong.

**Note it is specific to J2's high source impedance** — J5's junction is
**5.47 kΩ** at the specified 16.4 K shunt (5.30 kΩ at the as-built 15 K) and
could never have been affected at either value.

**0d rejects 150 K / 220 K and 0c keeps it — that is not a contradiction.**
0c's corner is **+203 mV** (rail −5 %, VIH +5 %, 1 % resistors) because the
switch is driven through **100 Ω**, so the divider barely loads it and the
unloaded ratio holds. J4's source is **9.4 kΩ** — two orders of magnitude
stiffer — which is what collapses the same pair to −45 mV there. **Do not
"correct" 0c to J4's 100 K / 220 K** on the strength of the J4 warning; the
pair is fine here and the source impedance is the whole difference.

---

## J4 — the flowmeter tap

**J4 `#` NODE — SOLVED 2026-10-04.** Measured unpowered in Ω mode at the
T-piece window, then cross-checked against the powered reading:

| | |
|---|---|
| Pull-up, `#`→`+` | **10.04 kΩ** to 5 V |
| Pull-down, `#`→`T` | **147.4 kΩ** |
| Source impedance | R_pu ∥ R_pd = **9.4 kΩ** |
| Predicted idle-high | 5 × 147.4/157.44 = **4.681 V** |
| **Measured idle-high** | **4.69 V** — the model is right |

`#` swings **0.3 V → 4.69 V**, both measured. The duty-cycle inference that
an earlier draft used to argue "0–5 V" is no longer load-bearing.

**The ESP32 divider is 100 K series + 220 K shunt → 3.13 V.** Working into
a 9.4 kΩ source, the divider loads the node, so the unloaded ratio is not
the answer:

**This was specified as 120 K until 2026-10-08**, when the bag turned out to
hold 100 K and 150 K. 100 K is not a fallback — it has roughly **double** the
logic-high margin, because shrinking the series leg stiffens the divider
against that 9.4 kΩ source. The arithmetic below is the reason the
substitution is safe, and it is the reason 150 K is still not.

| Divider | pin | nominal margin | worst case | over-rail corner |
|---|---|---|---|---|
| 8.2 K + 15 K | **2.15 V** | −321 mV | **FAILS** | — |
| 150 K + 220 K | 2.71 V | +239 mV | **−45 mV, FAILS** | — |
| 120 K + 220 K | 2.95 V | +472 mV | +177 mV | −13 mV |
| **100 K + 220 K** | **3.13 V** | **+651 mV** | **+348 mV** | **+174 mV** |

Worst case is a full corner: the machine's 5 V sagging 5 %, the ESP32's
3.3 V rail 5 % high (VIH 2.60 V), **1 % tolerance on the divider pair, and
±1 % meter error on the measured 10.04 kΩ and 147.4 kΩ**. An earlier
version of this table omitted the last two and reported −20 mV and
+201 mV — conclusions unchanged, margins flattering.

**150 K / 220 K is the trap here**: it is in stock, it looks fine
nominally, and it fails on tolerance stacking — exactly the reason
220 K / 220 K was rejected for 0c. Do not substitute it.

The chosen divider disturbs the node by **134 mV (2.9 %)**, which leaves the
machine's own reading unambiguously high.
<!-- fact:j4-node-disturbance 134 mV|2.9 % -->

**The one number 100 K is worse on: the over-rail corner.** With the machine
rail 5 % high, the ESP rail 5 % low and tolerances aligned, the pin sits
**174 mV above VDD**, where 120 K stays 13 mV below it. That is inside the
ESP32-S3's **VDD + 0.3 V absolute maximum** with 126 mV to spare, and a clamp
diode does not conduct meaningfully below ~0.4 V, so it is benign — recorded
because it is the only axis on which the substitution loses, not because it
is a problem. (With the ESP powered OFF and the machine on, the pin drives
~24 µA into the dead rail through the clamp. 120 K gives ~23 µA; this is not
a difference between them, and neither actuates anything.)

**A parts claim in CLAUDE.md was wrong for a day.** "The 120 kΩ is in stock
(2026-10-07)" was recorded from a verbal report and corrected on 2026-10-08
when the bag was opened. In stock should mean counted.

**Low side, which the high-side corner above does not cover:** the pin
sits at **0.19 V** against an ESP32-S3 VIL of 0.25·VDD = **0.784 V** at a
5 %-low rail, so **+590 mV**. Even at the datasheet's *maximum* saturation
of 0.7 V rather than the measured 0.3 V, the pin is 0.45 V — still
**+331 mV** clear.

**Drift on the board's own 10.04 kΩ and 147.4 kΩ does not threaten this.**
They were measured cold and unpowered and live next to a boiler, but they
move together in the ratio and 340 kΩ ≫ 10 kΩ, so the margin is nearly
flat: **+177 mV at ±1 %, +160 at ±5 %, +136 at ±10 %, +111 mV at ±15 %.**

**The 4.681 V prediction matching 4.69 V to 0.2 % is luckier than the
instrument.** It confirms the *model* — a resistive divider off the 5 V
rail — not the precision of the two resistances, which is why the corner
above still carries ±1 % meter error on them.
