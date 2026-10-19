# The flowmeter — J4

> **Normative on:** the Digmesa `#932-9525-B` datasheet in detail, and the
> whole open question of what the J4 counts actually are — the measurements,
> the reading that was published and refuted, recirculation, the OPV, the
> experiments that would separate the surviving readings, the hypotheses
> that are dead, the volume hole, and the filter capacitor.
>
> **CLAUDE.md carries** the live conclusions: the `E01` incident warning, the
> part number and K-factor, that the counts are ~5× the datasheet and
> UNRESOLVED, that pump noise is dead and two water readings are live, that
> the 5× rests on an unmeasured assumption, that 0d-i (weigh the split) comes
> first, that the OPV is 11 bar full-flow and is unrecorded adjustable state,
> and that both experiments need the J4 tap refitted behind the `E01`.
>
> **The J4 `+` read-only rule is NOT here and must never be copied here.** It
> lives at the top of CLAUDE.md beside the invariants, because it has been
> lost three times by being written down next to J4 conditioning material.
> This file is J4 conditioning material.
>
> **Changing a number here means grepping CLAUDE.md for it in the same
> commit.**

---

## The datasheet, in detail

**Two long-standing hedges in this file are now resolved:**

- **"+2.8 V" was never this sensor.** It came from the **9NB nano brass**
  datasheet, a different Digmesa product. +3.8 to +20 VDC is correct.
- **The "2386 / 2476 / 2436 for 0° / 90° / 180° mounting orientation" note
  was wrong about what those are.** They are not mounting positions of one
  part — they are **separate part numbers** (`-B`, `-B90`, `-B180`, `-B270`),
  each with its own measurement curve. Ours has no suffix, so 0°, so 2382.
  Orientation matters enough that Digmesa sells four SKUs for it, which is
  why the confirmed horizontal/contacts-up fitting is worth having recorded.

**The datasheet also confirms the plumbing**, which the operator traced the
same day: *"specially designed for coffee machines that use vibratory pumps…
installed between the water container and the vibratory pump (on the suction
side)."* CLAUDE.md carried "whether that water passes the flowmeter is
unverified" from 2026-10-04; it is now verified twice over.

**Digmesa still say to calibrate**: *"The number of pulses per litre may
differ depending on medium and installation… we recommend to calibrate."*
Invariant 3 already requires that.

---

### WHAT THE J4 COUNTS ARE IS UNRESOLVED — 2026-10-09, one reading refuted

**An earlier version of this section concluded "the J4 tap measures the pump,
not flow". That conclusion is WITHDRAWN.** It was contradicted by the
session's own strongest datum, which the same text filed as an open curiosity
instead of as the refutation. What follows separates what was measured from
what was inferred, because the measurements are sound and the inference was
not.

#### Measured

Two shots and a failed attempt, polled every 3 s, bracketed by idle.

| | pulses | glitches | flow ring | display ring |
|---|---|---|---|---|
| idle before (7 samples, ~20 s of samples) | 967 → 967 | 4988 → 4988 | 0 | 27748 frozen |
| pump running, 33.2 s sampled | 981 → 1717 (**736**) | 5123 → 9371 (**4248**) | 0 → **629** | +6086 at start, then +164 |
| idle after (6 samples) | 1728 → 1728 | 9371 → 9371 | 629 | 33998 frozen |

A further **25 counts** fall in the unsampled gaps at pump start and stop
(967→981 and 1717→1728), so 736 is what two in-shot samples bracket and
**761** is the honest total for the pump window. Not one count and not one
glitch appears in either idle stretch.

| run | yield | counted | glitch:count |
|---|---|---|---|
| shot 1 (machine failed to prime, air) | 36.5 g | 752 | 5.6 |
| shot 2 (primed, clean) | 37.9 g | 736 | 5.8 |
| **aborted — pump ran, NO water drawn** | **0 g** | **11** over 19.3 s | **7.8** |

**The excess over the datasheet is ~5×, not ~8×.** The 8× figure divided by
*yield*, and `docs/phase-0-status.md` states that yield is not inlet: the water
splits between cup, puck retention and the 3-way dump, and the scale sees
perhaps 60 % of it. At that assumption inlet ≈ 63 ml, the datasheet predicts
~150 pulses, and 761/150 ≈ **5.1×**. The 60 % is an assumption, not a
measurement.

#### The reading that was published, and why it is dead

"The counts are pump noise at ~22 Hz." **The aborted run refutes it:**

| | |
|---|---|
| pump ran | **19.3 s** — display timer 2.6 → 19.3, which this file establishes IS the pump-on to pump-off measurement |
| **sampled** window | **16.7 s** — the first sample came 2.6 s after pump-on, and no pre-run idle value was recorded |
| pump-coupling at 22.17 Hz predicts, over the sampled window | 22.17 × 16.7 = **370 counts** |
| observed | **11** |
| | **34× out** |

**Divide by the SAMPLED window, not the pump-on duration.** This table read
16.7 s / 370 / 34× until 2026-10-12, then a sweep "corrected" it to
19.3 s / 428 / 39× — which fixed the label and broke the ratio, because the 11
counts were only ever observed across 16.7 s. Right duration, wrong
denominator; the pre-sweep version had the right denominator with the wrong
name for it. **Counts in the unsampled 2.6 s are unknown, and ~360 would have
to be hiding there to overturn this.**

**The strongest form of this is the GLITCH RATE, which measures coupling at
the pin directly:**

| | glitches/s |
|---|---|
| shot 2, pump + water | 4248 / 33.2 s **sampled** = **128.0** |
| aborted, pump + no water | 86 / 16.7 s **sampled** = **5.15** |
| | **24.9× collapse** |

*(This table divided the aborted run by 19.3 s and shot 2 by its sampled
33.2 s until 2026-10-12 — mismatched denominators, giving 4.5/s and 28.7×.
Consistent windows give 5.15/s and 24.9×. The conclusion is unaffected.)*

Rejected edges are the quantity most directly produced by electrical coupling,
and they fall 29× when the only thing that changed is whether water moved.

**Two weaker supports, graded as such.** The glitch:count *ratio* (7.8 against
5.8) is consistent with unchanged edge quality but carries little information
at **n = 11** — Poisson gives 7.8 ± ~2.5. And "capacitive coupling has no
reason to care whether water flows" is **too strong as stated**: a vibratory
pump that cannot draw water is a different electrical load — stroke, current
waveform and armature travel all change — so coupling amplitude is not
invariant with hydraulic load. 29× is almost certainly too large to explain
that way, but the claim does not hold in general.

#### The water reading is NOT refuted — one earlier objection to it was circular

An earlier version argued the water reading also fails, because 22 Hz ÷ 2382
pulses/L = **9.31 ml/s through the meter** against 1.14 g/s to the cup, which
is "above the sensor's rated maximum". **That objection does not work**, for
three reasons this file supplies itself:

- the rated range is where K is **specified**, not a hydraulic limit, as this
  same entry now states — so exceeding it is not evidence of anything;
- it is **circular**: if the flow really is above the rated range then 2382 is
  not the applicable K, so dividing by 2382 to show it is out of range proves
  nothing;
- and the recirculation subsection below **predicts exactly that figure** —
  under it the meter sees throughput rather than delivered volume.

**THE POSITION IS UNCHANGED AS OF 2026-10-17: one reading refuted, the other
unresolved — and the gauge now leans toward this one.** See the recirculation
subsection: the operator's routine gauge reading is weak evidence *for*
water-plus-recirculation, not against it.

*(A draft dated 2026-10-17 replaced this paragraph with "the position is
NEITHER HOLDS after all", on the grounds that the OPV sits above 9 bar.
**Withdrawn the same day** — that rested on "a relief valve cannot be exceeded
at its own setpoint", which is false. The withdrawal reached the recirculation
subsection and **not this one**, leaving the two contradicting each other
~40 lines apart for one commit: a retraction applied in one place and missed
in another is how every stale line in this file got here.)*

**Note the 11 is a lower bound too.** Shot 2 got a gap correction (+25 counts
in the unsampled windows at pump start and stop); the aborted run has the same
structure — 2.6 s of pump-on before the first sample, and no pre-run idle
value recorded — and is quoted flat. It would take ~360 counts hiding in that
2.6 s to overturn the refutation, so the conclusion survives, but the
asymmetry is real.

#### What the bracket does and does not establish

It separates **brewing from idle**. Nothing finer. The 3-way solenoid is
energised for exactly the pump-run window and the pump triac fires for
exactly that window; neither is separated by this experiment, and nobody
established whether the heater fired during the idle stretches. The counts
are **activity**-correlated. An earlier version asserted "it is the pump, not
the heater, not the solenoid, not ambient" — that was a grade inflation which
deleted this very caveat.

#### Recirculation is STILL LIVE — and the gauge reading makes it MORE likely, not less

It was killed on free-flush evidence — an OPV is closed below setpoint, and
both calibration flushes were free flushes. That reasoning was then applied to
shots, where the 5× evidence actually comes from, and this section re-opened
it on the grounds that *"nobody has read this machine's own 0–16 bar gauge
(`I.4220`) during a pour, and the OPV's relief setpoint is unrecorded"*.

**BOTH HALVES OF THAT WERE WRONG, AND THE OPERATOR HAD THE ANSWER THE WHOLE
TIME.** Asked directly on 2026-10-17: *"Normally the pressure goes up to
roughly 9 bars. I watch it every time I pull a shot."* And on varying the
grind: *"If I pull a coarse shot the pressure will barely rise. If I pull an
ultra fine shot the pressure will rise higher than 9 bar. But not way
higher."*

**Grade it precisely, because it is an unusual kind of evidence.** This is not
one instrumented reading; it is routine observation across many shots. That
makes **n large and the repeatability real**, which is the part that matters
here — and it makes the *numbers* soft: no value was recorded at the time, and
nothing was read simultaneously with the flow counter.

**A FIRST DRAFT OF THIS ENTRY USED THAT OBSERVATION TO DECLARE RECIRCULATION
DEAD, AND THE DEDUCTION WAS BACKWARDS.** It argued: *"an ultra-fine shot
exceeds 9 bar, and a relief valve cannot be exceeded at its own setpoint,
therefore the OPV is above 9 bar, therefore a normal shot runs with it
closed."*

**The premise is false.** A spring relief valve has a *cracking* pressure, and
pressure rises **above** it as flow through the valve increases — accumulation
or pressure override, commonly 10–25 % above cracking at full flow. Being
exceeded under flow is what these valves *do*. The whole chain rested on that
one sentence.

**Read the same observation against an OPV cracking at ~9 bar and it fits
better than pure puck-dependence:**

| grind | observed | OPV cracking at ~9 | no OPV in play |
|---|---|---|---|
| coarse | "barely rises" | puck passes all flow, never reaches 9, valve shut ✓ | low pressure ✓ |
| normal | **"~9 bar, every time"** | puck reaches cracking, valve opens and **clamps** ✓ | 9 bar by coincidence, shot after shot ✗ |
| ultra-fine | "higher than 9, **not way higher**" | nearly all flow through the valve, override to ~10–11 ✓ | rises toward the pump's ~15 bar deadhead ✗ |

**A draft then argued "two things pure puck-dependence cannot explain and a
clamp can" — the same number across varying grind, and no run toward
deadhead. The first half is retracted** (see the dial-in paragraph below: the
operator tunes grind toward ~9 bar, so those variables are not random), and
the ✗ marks in the table's last column overstate it. The second half still
stands and is weak.

So: **this reading is not evidence either way.** The OPV setpoint is bounded
as of 2026-10-19 — see below — and whether a normal shot reaches cracking is
open.

**THE MECHANISM, restored — a draft deleted it while strengthening the
conclusion that depends on it**, which also left the "the recirculation
subsection below predicts exactly that figure" forward reference above
pointing at nothing. The meter is on the suction side (confirmed by the
Digmesa datasheet and by tracing) and the relief leg returns to the tank-side
T **upstream of the meter branch**, so with the OPV open the meter sees
throughput rather than delivered volume. That is what predicts the ~9.3 ml/s
figure the subsection above quotes.

**Grade the plumbing claim honestly**: the inlet side is traced, the rest of
the circuit is not — this file's hydraulics section covers the gauge branch
only and says building a plumbing census is a next-time-in-there job.

#### THE OPV IS 11 BAR — MEASURED 2026-10-19, the first hard number on that valve

Blind basket in the portafilter, pump on, read off the machine's own gauge;
the operator moved the OPV screw to confirm it acts and returned it. **The
method is right**: a blind basket passes no water, so 100 % of pump output
goes through the valve and the gauge reads the valve rather than the puck.

**IT IS THE FULL-FLOW PRESSURE, NOT THE CRACKING PRESSURE, and a draft read
it as the latter.** Maximum flow through a spring valve means maximum
accumulation, so **cracking sits BELOW 11** — spring valves typically show
10–25 % override at full flow, which would put it near **9–10 bar**. That
figure is general spring-valve behaviour, not a measurement of this valve.

**So a ~9 bar shot is NOT cleanly below cracking.** A draft wrote "4 bar below
cracking, OPV closed, recirculation dead for shots" — **withdrawn**. A normal
pull may sit right at the knee. That is the second pressure number this entry
has over-read: first the premise that a relief valve cannot be exceeded at its
setpoint, then this.

**AND IT WAS ALREADY 11 BAR WHEN THE 2026-10-09 SHOTS WERE MEASURED** —
confirmed with the operator. The screw was turned only to verify it acts. So
the flow data is comparable across it and no confound is introduced.

**One gap in that, stated rather than smoothed over: the screw's return to 11
was not re-verified against the gauge.** The reading that established 11 bar
was taken before the adjustment, and "I put it back" is an action, not a
measurement. Re-read it with the blind basket at the next opportunity —
experiment 0d-ii moves the screw twice more and the same applies there.

**THE OPV SETTING IS ADJUSTABLE STATE, AND NOTHING RECORDS IT.** That puts it
in the same class as `grind_epoch` (invariant 5): a number relative to a zero
point that moves. Flow data taken either side of an adjustment is **not
comparable**, and until 2026-10-19 nobody had established that this valve is
adjustable in practice on this machine. If the schema ever grows a column for
machine state, this belongs in it.

**A DRAFT CLAIMED A TWO-COMPONENT ACCOUNT "NOW FITS WHERE NOTHING FITTED
BEFORE" — baseline meter over-count plus relief on shots. WITHDRAWN: THE GAP
IT EXPLAINS MAY NOT EXIST.**

The 5× figure is not measured. It comes from dividing shot counts by an
**assumed** inlet, using this file's own 60 % yield-to-inlet figure — which
this same section already labels *"an assumption, not a measurement"*. Vary
that one number and the gap closes:

| flush over-count (OPV shut) | 5915 / 2382 = **2.483×** |
|---|---|
| apply that SAME factor to shot 2's 761 counts | 761 / 2.483 = **306 true pulses** |
| at the datasheet 2382 pulses/L | **128.7 ml inlet** |
| against a 37.9 g yield | **29.5 % reaches the cup** |

**So a single constant over-count of 2.483×, with the OPV never opening,
reproduces shot 2 exactly — provided ~30 % of inlet reaches the cup instead
of 60 %.** One component, no relief, no gap.

Two readings now fit the same data and **the only parameter separating them is
one this file flags as unmeasured**. A draft took the softer number as firm
because it supported the more interesting story.

**WHICH MAKES THE SPLIT THE MEASUREMENT TO TAKE.** Every "~5×" in this
document inherits that assumption. `inlet = yield + puck retention + 3-way
dump`, and all three are weighable — see experiment 0d-i.

**The free-flush half is untouched by any OPV question** — no basket, no
pressure, nothing relieving, and the meter still read 5915–6508 pulses/L
against a datasheet 2382. **The meter over-counts regardless**, and that has
never depended on the valve.

**AND THE "~9 BAR EVERY TIME" REPEATABILITY HAS A SIMPLER EXPLANATION THAN THE
ONE THIS FILE REACHED FOR.** A draft argued pure puck-dependence "cannot
explain landing on the same number across varying grind, dose and tamp", and
inferred a regulator clamping at 9. **The operator dials the grind to pull a
good shot, and ~9 bar is what that conventionally means.** Grind, dose and
tamp do not vary randomly — they are tuned toward a target. The human is the
feedback loop, and that was walked straight past.

**This is the repo's recurring defect in its purest form.** The file asked for
an experiment ("the cheapest thing that would settle it") that the operator
performs every single morning. Nobody asked. Before adding an experiment to
the list below, check whether it is already being done and merely unrecorded —
this is now the **third** instance, after the GPIO silkscreen check and the
`02.4` power-on string.

#### The experiment that separates them

**0d-i. WEIGH THE SPLIT. Do this one first — it is cheaper than everything
below and every "~5×" in this document depends on it.**

`inlet = yield + puck retention + 3-way dump`. Weigh the loaded portafilter
dry, pull a normal shot catching the cup on the Bookoo, weigh the portafilter
again (wet puck − dry = retention), and catch the drip-tray dump. Three
weighings and a subtraction.

**Until that number exists, the 5× is an assumption wearing a decimal point**,
and a constant 2.483× over-count with a ~30 % split fits the same data with no
relief at all.

**0d-ii. RAISE THE OPV AND PULL ONE NORMAL SHOT.** The operator owns the
adjustment since 2026-10-19, so pressure can be moved without touching the
grind.

It supersedes a coarse-shot version a draft put here, which moved pressure
only through the puck and so changed grind, flow rate and contact time
together. **This is NOT "one variable" either, and a draft said it was**:
raising the OPV raises brew pressure, which moves flow rate and contact time
with it. It changes *fewer* variables, and leaves the grind alone.

Set the OPV to **~14 bar**, pull one otherwise-normal shot, set it back to 11.

**READ THE GAUGE DURING THE SHOT. Without it the experiment can return a false
null and you will not know.** A draft asserted "at 14 the valve is shut
whatever its cracking pressure is" — that does not follow. By this section's
own 10–25 % override, a 14 bar full-flow setting puts cracking at
**11.2–12.7**, and removing a clamp at ~9 may let the puck drive the pull
above that. The gauge is the only thing that tells you which happened:

- **gauge stays well below ~11** → the valve was shut; the comparison is valid
- **gauge pegs near 14** → it relieved anyway; the run is void, not a null
- **gauge sits between ~11 and ~14** → **also void, and a draft left this band
  uncovered** — it is exactly where this section's own cracking estimate
  (11.2-12.7) lies, so the valve may be partly relieving and the run cannot be
  read either way. Lower the OPV or coarsen the grind and repeat.

**Then compare in CONSISTENT UNITS, which a draft did not.** The flushes'
5915–6508 is per litre of *collected water*; a shot's counts per litre of
*yield* is a different quantity, and shot 2 sits at 761 / 0.0379 L =
**20 079 per litre of yield**. Comparing one to the other is meaningless. Do
it in whichever of these the **0d-i** weighing makes available:

| if 0d-i has been done | compare **counts per litre of measured inlet** against the flushes' 5915–6508 |
|---|---|
| if it has not | compare this shot's counts/yield against a **normal-pressure shot's** counts/yield, same grind, same session — a ratio, not an absolute |

Record counts, yield, duration **and the gauge reading**. Deliberate throwaway
shot; at 14 bar it will not taste like anything.

**And re-verify the OPV reading after setting it back**, with the blind basket
— the 11 bar figure is only as good as the last time the gauge was read
against it, and this procedure moves the screw twice.

**BOTH 0d-i AND 0d-ii NEED THE J4 TAP, WHICH MEANS REFITTING THE CONNECTOR BEHIND
THE 2026-10-03 `E01` INCIDENT.** It is currently unplugged (see the bench-rig
entry in `docs/phase-0-status.md`). **Watch for the fault on first contact**,
and note that `+` connects to nothing, ever. This warning sat at the foot of a
retired experiment and lost its antecedent in a restructure; it is the only
safety-relevant line in this section and it covers everything in it.

*(RETIRED THRESHOLDS, from the superseded coarse-shot version. Kept for the
unit trap only, which is the part people get wrong: yield is not inlet, so a
correctly-reading meter gives **2382 / 0.6 ≈ 3970 pulses per litre of
YIELD**, not 2382 — and a confirming result would land at ~1.7× the datasheet
and read as "nothing settled". **Do not use the 3970 as a live threshold**:
the 60 % it rests on is exactly the number experiment 0d-i exists to replace,
and the same arithmetic appears above with that caveat attached.)*

*(A draft of this section promoted the analyser on `#` to first instead. That
was wrong twice over: it contradicted `docs/phase-0-status.md`, which calls
the analyser* **the wrong instrument** *because it produced the `E01` fault on
2026-10-03 and its corrected 8.2 K conditioning has never been re-tested —
400 lines from where the promotion was written, in the same file. And it
justified the series resistor as protecting the analyser, inverting the
documented hazard, which is protecting the machine.)*

The rest are still worth running and none of them decides this on its own:

1. **One shot and one free flush with the serial console DETACHED**, counts
   read before and after, **durations recorded**. Detaching serial removes
   the documented cause of display-ring drops (see the 0e entry) and gives
   the second rate point that a single 22.2 Hz datum cannot provide.
2. **Deliberately re-run the dry case** — pump on, no water drawn. If 11
   counts reproduces, the counts track water and the withdrawn conclusion was
   backwards. **Sample the WHOLE pump-on window this time** — baseline before
   the lever, first sample at the lever — which is what the 2026-10-09 run
   could not do and why its 11 is a lower bound. Over a full 19.3 s run,
   coupling predicts **428**; over a 16.7 s sampled window, **370**.

   **Thresholds, and they are judgement calls rather than derived**: under
   ~50 counts is decisive for water (an order below either prediction); over
   ~300 brings the coupling reading back. Between those, nothing is settled.
   At n ≈ 11 the Poisson spread is ±3.3, so the interesting ambiguity sits far
   below 300 — a result of 40 or 80 would need thinking about, not dismissing.

#### Hypotheses that are DEAD, recorded so nobody re-derives them

An earlier version of this entry deleted this block while the refuted
arguments were still live in `docs/phase-0-status.md` and in the firmware.
That is net information loss, and the opposite of this file's convention.

1. **One count per pump stroke at exactly 50 Hz.** The flush data fitted it
   with no free parameters (1209/50 = 24.2 s, 164/50 = 3.3 s, both implying
   the same ~8 ml/s). **The shots came in at ~22 Hz.** Dead.
2. **The rejected:counted ratio as a signature.** It held at 3.32 and 3.34
   across an 8× volume change, which looked decisive. Both runs were free
   flushes **at the same flow rate**, so duration scaled 8× too and any
   time-proportional source reproduces it. It then moved to 5.8 on the shots.
   It discriminates nothing.
3. **Air in the line.** Shot 1 ran after a priming failure and the datasheet
   names air as a measurement hazard, so this looked decisive. Shot 2 was
   clean and agreed to 6 %.
4. **The counts are pump noise** — see the refutation above. Dead at **34×**
   on consistent windows (370 predicted against 11 observed over the same
   16.7 s), with the glitch rate collapsing 25× alongside it.
*(A draft added "water plus OPV recirculation" here as a fifth dead
hypothesis on 2026-10-17, and it was **withdrawn the same day** — the
deduction that killed it rested on "a relief valve cannot be exceeded at its
own setpoint", which is false. See the recirculation subsection. It is live,
and the gauge reading is weak evidence for it.)*

#### The volume hole, which is the sharpest statement of the problem

| | |
|---|---|
| shot 2, pulses over the pump window | **761** |
| at the datasheet's 2382 pulses/L | **319 ml** |
| yield to the cup | **37.9 g** |
| inlet at this file's own 60 % assumption | **~63 ml** |
| **unaccounted** | **~256 ml over 33 s** |

*(A draft said 280 ml by subtracting only the 37.9 g cup yield while the table
above already credits ~63 ml of inlet. 319 − 63 = **256**, which is also what
this file's own 5.1× figure implies. Conclusion unaffected; the arithmetic was
double-counting.)*

**That is a mugful, and it is a clean dichotomy: either that water moved, or
the meter is not counting volume.** It did not reach the cup.

**Recirculation is the account that says it moved**, and as of 2026-10-17 it
is live — see above. **If it is wrong, the meter is over-counting** and the
mechanism is unidentified, because electrical coupling is dead (hypothesis 4).

*(A draft asserted "nothing else on the traced circuit takes 256 ml". Only the
**inlet** side is traced. This file's own hydraulics section opens by saying
the connector census has no plumbing counterpart and that building one is a
next-time-in-there job, covering the gauge branch only. Do not treat the
circuit as surveyed.)*

**ONE UNEXAMINED CANDIDATE if the meter is over-counting, and it is reasoning
rather than measurement — nobody has looked at the waveform.** The meter is on
the **suction** side of a vibratory pump firing at 50 Hz against ~9 bar. If
that pulls the turbine into **flutter or cavitation** rather than smooth
rotation, the extra edges are *real optical transitions*, not noise — which is
why a filter aimed at electrical glitches would pass them.

**Two objections, both raised the day it was written:**

- **The dry run should have been its best case, and was its worst.** A pump
  running with no water drawn is where cavitation is maximal, and it produced
  **11 counts**. Unless "no water drawn" means nothing moved through the meter
  at all — which the record does not settle — flutter predicts the opposite of
  what was measured.
- **"Counts scale with water" is not in the data.** Shot 1 gave **752 counts
  at 36.5 g**, shot 2 **736 at 37.9 g** — more yield, fewer counts. What the
  data supports is a **wet/dry binary**, nothing finer. A draft claimed scaling
  and there are only two wet points, moving the wrong way.

So it is a weak candidate, recorded so it is not re-derived as an obvious one.

**The discriminator already exists in the experiment list: the analyser on
`#`.** Digmesa specify a **square wave at ~50 % duty**. Flutter or cavitation
shows as irregular widths and a duty cycle that is not 50 %. A clean square
wave at 5× the expected rate means something else again, and would refute
this too.

#### The capacitor is a CANDIDATE, not a measured requirement

**Nothing has been fitted and there is no before/after.** What was measured
is the symptom. The J4 arithmetic is right — 73.1 kΩ source, 10 nF → 218 Hz,
τ 731 µs, rise 1.6 ms against a ~31 ms half-period — but two things were
overstated:

- "pump hash ~150 Hz — crushed" is wrong. A single pole at 218 Hz attenuates
  150 Hz by **1.69 dB**, i.e. ×0.83. Untouched. The argument that might work
  is the 629 ring overflows, which imply bursts far above 12.8 kHz where a
  218 Hz corner genuinely does kill them — but that has a competing
  explanation and needs the before/after anyway.
- An unexamined failure mode: 10 nF across 73 kΩ turns the falling edge into
  a **~1.1 ms ramp** through the band residual noise lives in. ESP32-S3 pad
  hysteresis is small, and a slow edge in noise is a classic multiple-
  interrupt generator. **The capacitor could raise the glitch count.**

It does not threaten invariant 1 — the cap blocks DC so the divider margins
are untouched, the worst transient into the machine node is ~30 µA and an
uncharged cap at connection draws ~43 µA.
