# Espresso shot logger

Instrumentation for an Ascaso Dream PID + Eureka Mignon Single Dose.
Currently **Phase 0: logging only.**

---

## Hard invariants

Violating any of these is a bug, not a design choice.

1. **Phase 0 never actuates anything.** No machine signal is driven, and
   nothing we add joins two machine signals through a path the machine did
   not already have — **except a sense tap that is high-impedance or
   passive**, which is the threshold this rule has always carried and which
   applies to the "driven" clause as much as to the "joins" one. A pin that
   briefly sinks microamps through 150 K is passive in the sense meant
   here; a photoMOS closing a 5 V common onto a direction line is not.

   <!-- fact:j4-node-disturbance 134 mV|2.9 % -->
   **How high-impedance, honestly:** J4 disturbs its node by **134 mV
   (2.9 %)**, J2 by ~1 mV, but **J5 pulls its line from 5.0 V to 4.20 V —
   a 16.0 % disturbance at 171 µA**.

   **That J5 figure is CALCULATED, not measured, and this clause called it
   measured until 2026-10-19.** It entered at `7182c56` as *"4.7 K pull-ups,
   so a 23.2 K divider leaves them at 4.16 V"* — the "so" is a derivation —
   and no meter reading of that node exists. The current is the same
   arithmetic again (0.80 V / 4.7 K).

   **AND IT WAS PINNED TO A SHUNT THIS FILE TELLS YOU NOT TO FIT, until
   2026-10-19.** 4.16 V / 17 % / 179 µA is the **as-built 15 K** (8.2 + 15
   against the 4.7 K pull-up). Two other sections of this file supersede that
   in favour of **16.4 K**, which fails no tolerance corner — so the
   project's top-line invariant was quoting the value the build is meant to
   move away from. Both, for the record:

   | J5 shunt | line sits at | disturbance | current |
   |---|---|---|---|
   | 15 K — as built, superseded | 4.16 V | 16.8 % | 179 µA |
   | **16.4 K — specified** | **4.20 V** | **16.0 %** | **171 µA** |

   Nothing in the clause turns on which: J5 is the large disturbance either
   way, and that is the whole reason it is named here. The headline figure is
   the specified build because that is what a reader is about to solder.
   *(17 % was the 15 K number rounded; 16.8 % is what it actually is.)*

   J2's ~1 mV is
   likewise calculated; **J4's 2.9 % is the only one of the three backed by a
   meter**, and there the same model predicted 4.681 V against a measured
   4.69 V, which is the reason to trust the J5 number while still grading it
   honestly. An earlier draft of this clause said
   "microamps against the machine's own milliamps", which is false for J5.
   The threshold is *does the machine still read what it would have read*,
   not an absolute current.

   **This rule is about our board. The harness is NOT covered and is now
   the larger risk** — see `docs/phase-0-plan.md`. From Phase 1 provisioning
   onward J2 position 1 (+5 V common) rides in the loom; a bridge from it to
   a direction line actuates the machine and no jumper on the board is in
   that path. Plumbing is likewise outside this invariant, for the same
   reason and recorded separately.

   **Drive hardware may be PRESENT on the board**, and from the perfboard
   onward it is. It is permitted only while an **open jumper in series with
   its OUTPUT** leaves it unable to conduct: not merely unpowered, not
   merely un-commanded. **A disable that only cuts a control input does not
   satisfy this**, because it does not survive the controlled part failing
   short, which is the characteristic MOSFET failure mode.

   **"Open jumper" is not "physically impossible", and an earlier draft of
   this clause said it was.** It makes conduction require a deliberate,
   visible mechanical act. A shunt fitted in error, a solder splash, swarf
   or a dropped washer closes it. Far stronger than firmware intent, weaker
   than an absent component — and **checking it means opening the box**,
   which the finished build is glanded and cable-tied shut.

   **How many jumpers, where, and in what order they are fitted is a build
   decision and lives in `docs/phase-0-plan.md`** — do not restate it here.
   This rule says only that at least one open jumper must sit in series with
   every drive output.

   **One pin is bounded operationally, not structurally, and the invariant
   should say so rather than imply otherwise.** `selftest` makes **GPIO8**
   an output. (It is not the only output in the firmware — `updateLed()`
   drives the onboard RGB LED — but it is the only one that can ever reach a
   machine-facing net, which is the claim that matters.) On the perfboard it
   lands on a header with no trace, so a path to the machine exists only
   while the operator has deliberately fitted the selftest jumper to GPIO6 —
   which is why "unplug J4 first" is an **operational** rule. That is the
   weaker kind of guarantee and it is the one clause here not checkable by
   looking at the board.

   *Amended 2026-10-12, re-drafted the same day after two review rounds.*
   The original read "No writes to any machine signal. Every tap is
   high-impedance or passive. Read-only." That sentence did two jobs —
   **"never actuates" is the goal, "high-impedance or passive" is one
   implementation of it** — and the perfboard forces them apart, because a
   board can carry drive hardware and still be unable to drive.

   **The net is not simply "stricter", and a draft claimed it was.** On the
   drive path it is stricter: it names the one placement of the disable that
   survives a component failure. But it also **licenses the
   higher-residual-risk of the two builds considered** — parts fitted behind
   jumpers rather than footprints with nothing in them — trading residual
   risk for a verified layout, because unverified things being wrong is
   where this project's defects have actually come from.

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

## The J4 `+` rule — one hard safety rule, and it does not live in a table

> **`+` is read-only, through a high-impedance meter, and nothing else.**
> Nothing with an input clamp — no analyser, no GPIO, no divider — **and
> nothing that sources or sinks current: never power the ESP32 from it,
> never feed it from another supply, never hang a pull-up on it.** It is
> the machine's own sensor rail off a 3 W IRM-03-5. A 20 MΩ DMM is fine,
> and is how its 5.0 V was read on 2026-10-04.

**Why it is HERE and not with the flowmeter, where it belongs topically.**
This block was inside milestone 0d, ~2300 lines below the invariants, and
`docs/phase-0-status.md` records that **the rule has been lost from
CLAUDE.md three times** — each time because it lived as a row in the J4
conditioning table, and the table was rewritten whenever the divider value
changed. It is deliberately **outside** that table, and now outside that
section. Rewrite the divider as often as you like; do not touch this.

`docs/phase-0-status.md` carries a verbatim backup copy, marked as such.
Duplication is wrong almost everywhere else in this repo; for a rule with
this loss record it is the point.

---

## Hardware facts (verified, do not re-derive)

### Machine: Ascaso Dream PID

**The display shows `02.4` for ~3 s at power-on** (read 2026-10-05). It is
**probably a firmware version — that is an interpretation, not something
observed**; all anyone has seen is the string. Which firmware, display
controller or main MCU, is also unknown.

Recorded anyway because every decode in this file came from **this machine
in this state**: the glyph table, the bit offsets, the timer flag at 114,
the 67/66 structure. If a future capture disagrees, check whether `02.4`
still shows before assuming the protocol changed.


Control board MCU is an **STM32F030R8T6** (LQFP64, 64 KB flash). Read off
the physical chip marking. Note: the community schematic repo
(`techdregs/Ascaso_Dream_PID_Electronics`) bundles the *wrong* datasheet —
`stm32f030f4.pdf` is the 20-pin TSSOP variant. Use the F030x8 datasheet
and RM0360.

Pin map, from the traced schematic:

| Net | Pin | Notes |
|---|---|---|
| 1Cup | PB5 | up direction of the switch. 15 s is the *default*; **this machine is set to ~10.5 s** — the operator reprogrammed it (2026-10-04). See below: these durations are user-modifiable state, not constants. |
| 2Cup | PB6 | down direction. 60 s is the *default*; **this machine is set to 45 s** — operator, 2026-10-19, set deliberately. Recorded as UNKNOWN from 2026-10-03 until then, because that capture was stopped by hand at 21.0 s and says nothing about the stored duration. User-modifiable, like 1Cup. |
| Water | PB4 | **WIRED**, and the conductor mapping is **FUNCTIONAL since 2026-10-08**: with the 0c tap live, lever B **up** drove the line on J2 position 5 high and the firmware logged `water`. Lever B up also runs the pump — observed. Whether that water passes the flowmeter is still **unverified** — see 0d. |
| Steam | PA10 | **WIRED**, and the conductor mapping is **FUNCTIONAL since 2026-10-08**: lever B **down** drove J2 position 2 high and the firmware logged `steam`. Lever B down also drives the heater to 165 °C — observed. |
| DIP1–4 | PB0–PB3 | four config DIP switches (SW1), purpose unknown |
| Flow-P1 / P2 | PA8 / PA9 | flowmeter, conditioned to 3.3 V logic **at the MCU**. **J4 carries only ONE signal** — measured 2026-10-04 (ground + 5 V + one signal), so the two-net reading of this row is wrong for this connector. **Which** of these pins it reaches is untraced. The connector carries the sensor's own open-collector output, **measured 2026-10-04: a 10.04 kΩ pull-up returning to the `+` net (5 V) against a 147.4 kΩ pull-down, idling at 4.69 V.** It is *not* the conditioned 3.3 V logic that exists at PA8/PA9. See 0d for the divider. |
| AC_Sense | PA5 | mains zero-cross, via opto U27 |
| Pump_Ctrl | PA15 | → FODM3053 → BTA204S triac (random-phase) |
| Solenoid_CTRL | PB12 | |
| HTR_CTRL | PB15 | |
| display link | PB7/PB8 or PB8/PB9 | **UNRESOLVED — and nothing below resolves it.** `PB7`/`PB8`/`PB9` appear nowhere else in this file; the J5 section decodes the bus without ever establishing which MCU pins it lands on. The cell said "see below" until 2026-10-19, pointing at nothing. |

**Switch electrical — MEASURED 2026-10-04**, not taken from the schematic:
+5 V → R45 (100 R) → switch common. The common (J2 conductor 1) reads
**5.0 V** against board ground; all four direction lines read **0 V** at
rest. Combined with the continuity result below — a lever closes common to
its direction line — that establishes **pressing pulls the line high,
idle is low**, by measurement rather than inference. 5 V logic, *not*
mains. The 5 V rail comes
from an IRM-03-5 isolated module, so sharing ground with the ESP32 is safe
provided the ESP32 runs from an isolated supply.

**J2 pinout — MEASURED 2026-10-04**, unpowered continuity through the
switches plus a powered DC check. Positions counted on the harness:

| Position | Net | MCU pin | How |
|---|---|---|---|
| **1** | **+5 V, switch common** | — | in **all four** continuity pairs; reads 5.0 V |
| 2 | Steam | PA10 | **functional 2026-10-08** — lever B down, logged as `steam` through the live tap |
| 3 | 2Cup | PB6 | continuity on lever A **down** |
| 4 | 1Cup | PB5 | continuity on lever A **up** |
| 5 | Hot Water | PB4 | **functional 2026-10-08** — lever B up, logged as `water` through the live tap |

Note the order: **2Cup sits before 1Cup**. The list "+5 V, Steam, 1Cup,
2Cup, Water" that this file used to carry is not positional, and reading
it as positional swaps the shot with the clean cycle.

**THERE ARE TWO LEVERS, NOT ONE.** An earlier version of this section
described a single SCI R13-29 and called Steam/Water *"present on board,
likely unwired on a Dream"*. Both wrong:

| Lever | up | down | grade |
|---|---|---|---|
| A | 1Cup | 2Cup | **functional (1-link)** — lever operated, continuity observed |
| B | Hot Water | Steam | **functional (3-link)** — the directions were already observed (Water runs the pump, Steam drives the boiler to 165 °C), and **2026-10-08 closed the conductor→function mapping** by watching each direction drive its own J2 line through the fitted tap. |

All four are wired, and both levers share conductor 1 as their common.
**All four conductor→function assignments are now functional.** 1Cup/2Cup
came from lever-operated continuity on 2026-10-04; Steam/Water was plug
inspection until **2026-10-08**, when the 0c tap went live and each lever
direction was watched driving its own line into its own GPIO.

**It is not quite the same grade as lever A's, and the difference is worth
keeping.** Lever A's mapping is one link: a lever closes, a continuity pair
closes. Lever B's is three — the tap test fixes conductor→GPIO→firmware
name, and the conductor→*function* step still leans on the 2026-10-04
behavioural observations (B up runs the pump, B down drives the boiler to
165 °C), which were observed rather than instrumented, plus the breadboard
lead routing. The conclusion is sound — a swap of 2/5 would have logged
`steam` for B up — but it is a three-link claim, not a one-link one.

**Two consequences of lever B, found 2026-10-04 and not yet designed for:**

- **Steam drives the boiler to ~165 °C**, ~70 °C off setpoint. That lands
  directly on `boiler_temp_start_dc` (spool v2) and silently invalidates an
  0f run, exactly as `PrG` does. The blink signature will not catch it —
  that detects a *user* editing the setpoint, not the machine moving its
  own target.

  **The display DOES detect it** — it blinks `165` through the cycle then
  `95` while cooling, which is a better signal than a switch tap because it
  says how hot and when it came back. **So the J2 Steam tap is redundant for
  detecting steam.** Fit it because the operator asked for all four
  functions, not because `boiler_temp_start_dc` needs it.
- **LEVER B LATCHES — observed 2026-10-19, and it changes the brew guard.**
  A momentary press up starts the pump; it **keeps running after release**,
  and a **second press stops it** — the same latching behaviour as lever A.
  Left alone it runs for *"a couple of minutes"* (operator, approximate) and
  then returns to brewing mode on its own. Steam behaves the same way.

  <!-- fact:brew-guard-ms 75 s -->
  **So `BREW_GUARD_MS = 75 s` can expire while the pump is still running** —
  a draw of "a couple of minutes" outlasts it. Note the mechanism precisely:
  `onRelease()` stores a **backdated** `pressed_at_ms`, so the guard runs from
  the **press**, not the release. A draft said "from release, about 250 ms
  after the press"; that is wrong and it makes the guard marginally shorter,
  not longer.

  **A draft also wrote that 75 s "was sized from 2Cup's unknown stored
  duration, and that guess was low (2Cup is in fact 45 s)". That is
  incoherent** — 45 < 75, so if 2Cup were the basis the guess was *generous*.
  `brew_guard.h` cites 60 s default plus margin. **What makes the guard
  insufficient is the water draw running for minutes, which has nothing to do
  with 2Cup.** Two arguments were run together.

  **AND THERE IS A WORSE FAILURE THAN A SHORT GUARD: THE GUARD CAN FAIL TO
  ARM AT ALL. Found 2026-10-19 by review, unrecorded until then, and it is an
  invariant-2 exposure rather than a sizing complaint.**

  `SwitchBank` confirms a level only after **two consecutive polls agree**,
  `DEBOUNCE_MS` apart (`machine_io.cpp`, the `cand`/`cand_since` path). A
  press seen HIGH on **one** poll and LOW on the next never sets `stable` and
  never emits an `Event`. Both of hot water's guard paths then miss it:

  | path | why it misses |
  |---|---|
  | `busy()` branch 1 — a lever held right now | reads `in.pressed[]`, which is `stable`; never set |
  | `busy()` branch 3 — `pressGuard(HOT_WATER)` | armed by `onRelease()`, called only from the `Event`; never emitted |

  **So `busy()` returns false for the entire draw** — and lever B latches, so
  that draw runs for minutes while flash writes proceed. **That is the
  invariant-2 breach this class exists to prevent**, not a window that is
  merely too short.

  **It is specific to hot water.** 1Cup and 2Cup are covered by branch 2, the
  display's own `Mode::TIMER`; `CLAUDE.md` records that the display does
  **not** run its timer during a lever-B draw, which is exactly why branch 3
  exists and exactly why nothing backs it up.

  **How likely: this is the tail, not the typical case.** 2026-10-19 lost 2 of
  6 deliberately-fast presses (80-90 ms). An ordinary lever-B press spans many
  polls and confirms easily. But the exposure is real, silent, and currently
  unguarded, and it is **made more likely by anything that stretches
  `loop()`** — the same poll-gap excursions recorded in this session.

  **An edge-driven guard is the obvious fix and it is NOT sufficient on its
  own.** The second press is a real stop signal on a line already polled, so
  press N to press N+1 is the exact window — but **the machine self-terminates
  without any second press**, and then `busy()` never clears. That is the
  latched-busy-forever failure `brew_guard.cpp:24-30` already warns about
  (**not `brew_guard.h`** — a draft cited the header; the warning, and the
  account of the draft that latched busy forever, are in the .cpp), and it
  would block flash writes indefinitely: strictly worse than a guard that is
  sometimes short. **Any edge-driven design needs an upper bound as well**,
  and that bound needs the self-terminate period measured — "a couple of
  minutes" is recollection.

- **Hot Water runs the pump**, which **probably** pulses the flowmeter
  outside any shot — a prediction, not an observation. What was seen is
  the pump running and water leaving the steam valve; **whether that path
  crosses the flowmeter has not been traced.** Digmesa describe these
  sensors as sitting between tank and pump, and 0d's whole rationale
  assumes inlet-side placement, on which any pump run pulses it. First
  thing to check once `#` is counted. 0d must scope its counting to shot boundaries or it will
  attribute tap water to a shot.

**The display does NOT run its shot timer during a hot-water draw**
(observed 2026-10-04), so `machine_timer_dl` cannot pick up a tap draw and
`machine_context.h` needs no special case. The flowmeter **does** pulse
during one, so the timer and the flow counter disagree about what counts
as an event — which is why 0d scopes counting to shot boundaries.

**Switch semantics — important.** Each lever is a momentary ON-OFF-ON
(SCI R13-29), spring return to centre. The board *latches*:

- short pulse → start the stored timer, or stop a running shot
- **hold > ~2 s → reprograms that direction's timer to the hold duration**

**So the stored durations are user-modifiable state, not machine
constants.** `analysis/captures/2026-10-03-display-bus-1cup-clean.sr` runs
`001`→`105` — 10.5 s, not the 15 s default — because the operator had
reprogrammed it. Do not treat a captured duration as a property of the
model, and do not "correct" one against the other: record which machine
and when.

Consequences: there is no way to stop the machine by opening the switch
line, so no fail-safe-open design is possible on that path. And any future
actuator that can hold the line will silently reprogram the machine.

**SO PHASE 1 MUST USE A HARDWARE ONE-SHOT, so firmware can *trigger* but
never *hold*.** **Use the 74HC221, NOT the
74HC123**: the '123 is **retriggerable**, so a GPIO oscillating faster than
~8 Hz (a boot loop, a brown-out, a floating pin) holds its output high
indefinitely, which is exactly the failure the one-shot exists to prevent.
The NE555 has the mirror-image trap and needs an RC+diode differentiator
before it is safe here; the '221 avoids the question.

**THE RC IS NOT CHOSEN, and the reason it matters is that getting it wrong
is SILENT**: a one-shot that is too short produces a drive path that does
nothing, while the sense tap still shows the pulse — so it looks like it
worked. ~120 ms is comfortably under the ~2 s reprogram threshold, which
bounds it above. Below, all that is established is that **presses the poller
measured at 80–90 ms latched, so the true threshold is under ~140 ms** — and
that reading is quantised by a 42–57 ms poll gap, so 80 ms means a true width
anywhere in roughly [23, 137] ms. **It is not enough to choose the RC.**
Redo it with the logic analyser on a J2 junction.

**Do not quote 345–480 ms as "the machine's press range".** It is one
session's presses (2026-10-08), it is superseded by 2026-10-19's 195–295 ms
and 80–90 ms, and at least six places in this repo cite it. **`grep -rn
'345.480'` finds them; a prose list of citation sites cannot, and this file
has published a wrong one twice.**

**That whole one-shot argument predates the discovery of the second lever**
and nobody has walked it through a two-lever, shared-common topology. The
conclusions look unaffected; they were derived from a one-switch model.

**`docs/machine-switches-j2.md` is normative on the rest** — the full
pulse-width record and its grading, the latching evidence, the quantisation
trap and the draft that sized a drive path with an 80 ms reading from an
instrument the same commit called unusable below ~100 ms, and the
citation-table saga that is this repo's best worked example of why a line
number in a document is wrong by the time the document is saved.

**Display bus — DECODED 2026-09-28 (0b done), and the decode is 500 lines
of evidence that now lives in `docs/machine-display-bus.md`.** Captures and
the decoder are in the repo: `analysis/captures/2026-09-28-display-bus-*.sr`,
`tools/decode-display-bus.py`. Read the doc before touching this again.

**Where to tap:** **J5**, 4 pins, **JST XH** (`XHP-4` plug,
`SXH-001T-P0.6` contacts, `B4B-XH-A` header). **Every connector on this board
is XH**, including J2 and J4 — and sellers calling them "XH2.54" are selling
the right part under a wrong name, while some print "JST EH" in spec tables
for parts that are plainly XH. **Verify by mating, not by reading.** The one
measurement that separates XH from EH is **depth** (plug 5.7 vs 3.8 mm).

| J5 pin | Net | What it is |
|---|---|---|
| 1 | `VSS` | ground — **the pad with thermal-relief spokes** |
| 2 | `ESD1` | **DATA** → GPIO5 |
| 3 | `ESD4` | **CLOCK** → GPIO4 |
| 4 | `Disp-P4` | bidirectional. **Never drive it, never load it.** Unterminated. |

**Ground is pin 1, the spoked pad — the square-pad-is-pin-1 convention does
NOT hold here. The square pad is pin 4**, the line that must never be driven.
Wire colours say nothing: the harness is red/black/red/black and ground lands
on a red one.

**`Disp-P4` MUST NOT BE LOADED — fit nothing on it, ever.** It is
**bidirectional**, it reaches the MCU through R32 = 100 K, and **its pull-up
strength is unrecorded**, so the consequence of loading it is unknown. That is
reason enough. Branch it through its 8.2 K for symmetry if you like, leave the
far end open, and never hang anything on it that can source or sink current —
in particular **not the shunt**. `docs/shopping-list.md` specifies exactly
that, and it is the built configuration.

<!-- fact:divider-j5-shunt 16.4 K -->
**`ESD1` and `ESD4` are 8.2 K series + 16.4 K shunt.** 16.4 K is two 8.2 K in
series, 25 in stock. The **as-built 15 K is superseded** — it fails its
tolerance corner at −70 to −87 mV, worse than the −45 mV that got 150 K / 220 K
rejected at J4. 16.4 K comes out at **+18 mV**.

**`docs/taps-and-dividers.md` is normative on the arithmetic** — the corner
table, the 23.2 K and fit-nothing drafts that were both wrong in opposite
directions, the untested drive-type question and how to meter it, and why
this paragraph once said the opposite of what it says now. The two values
above are the live conclusion; everything that justifies them is there.

**GPIO4 is the CLOCK and GPIO5 is the DATA, and getting it backwards is
silent** — both lines still idle high, `pins` looks perfect, and `frames=0`.
Zero frames with correct levels is the swapped-pair signature. It happened on
2026-10-09.

**The bus is not I²C** — plain synchronous serial, clock ~9.9 kHz, data
sampled on the **rising** edge. sigrok's `i2c` decoder will happily "decode"
it as endless writes to address 0x00; that output is the symptom of a wrong
guess, not a result.

**It sends a 67-bit frame then a 66-bit frame, about every 41 ms.** The 67
carries the button state, the 66 carries the digits. **A "133-bit frame" is a
merge artefact, not a frame** — the two usually arrive glued together.
**Parse the 67/66 pair directly rather than keying on 133 bits**, because the
pair splits more often the harder the machine is working (1.2 % idle, 8.3 %
heating from cold), so a 133-keyed parser throws away digit readings exactly
when it matters.

**What the bus gives you, and the firmware already reads all of it:**

- **a shot timer in tenths**, which is the machine's own pump-on to pump-off
  measurement — but it does **NOT** say which direction ran, so J2 is still
  required to tell a shot from a clean cycle
- **bit 114 is a TIMER-MODE FLAG**, set in exactly the brew-timer frames and
  clear in every temperature frame, so `100` is never ambiguous
- **the buttons**, as bits 49 (LEFT, enters `PrG` then decreases) and 58
  (RIGHT, increases) of the 67-bit frame — free, same two wires
- **`PrG` means the brew temperature is being changed**, which silently
  invalidates an 0f noise-floor run
- **a steam cycle**, which blinks `165` then `95`. **So the J2 Steam tap is
  redundant for detecting steam** — fit it because the operator asked for all
  four functions, not because `boiler_temp_start_dc` needs it.

**THE DISPLAY BLINKS FOR TWO DIFFERENT REASONS AND THE PERIOD IS
LOAD-BEARING.** A setpoint edit blinks at **~410 ms**; steam and the cooldown
after it blink at **1.021–1.101 s**. `BLINK_WINDOW_US` is **600 ms**, sitting
in a 414→1020 ms guard band with 186 ms below and 420 ms above. **Use the
interval, never the blank duration** — steam's shortest blank is 0.156 s,
shorter than a setpoint blank, so duration would misfire. This is deliberate.

**WHAT `boiler_temp_start_c` MEANS IS NARROWER THAN MIGRATION 008 CLAIMS.**
Below ~88 °C the display tracks the real boiler and jitters ±1 °C. At ` 95` it
is **indistinguishable from the setpoint** — it holds ±0 °C for 57 s, which no
PID boiler does, and it blinks ` 95` post-steam while the boiler is
demonstrably hotter. The column is sampled immediately before a pour, i.e.
**always in the ambiguous range**, so today it means **"at setpoint"**, not a
verified temperature. Settling that needs a capture with the boiler pulled off
setpoint and allowed to recover.

**The display does NOT run its shot timer during a hot-water draw**, so
`machine_timer_dl` cannot pick up a tap draw and `machine_context.h` needs no
special case. The flowmeter **does** pulse during one, which is why 0d scopes
counting to shot boundaries.

**`docs/machine-display-bus.md` is normative on everything else** — the JST
identification by measurement, the `Disp-P4` bidirectional-handshake
hypothesis and the 344 s of live bus that disproved it, the full glyph table,
the exact bit offsets, the power-on sequence, the ` 88` that took three
stories to solve, the 67/66-pair verification, and the timer-agreement
arithmetic. Do not re-derive any of it from the captures; it is all there.

### Hydraulics — the pressure-gauge path

**Moved to `docs/hydraulics.md` (2026-10-19), which is normative on it.**
It is the only hydraulic material in this project and it is wanted by
nobody who is not standing in front of an open machine.

The live conclusions, which are the ones that change what you do:

- **The whole circuit is 1/8**, including the solenoid. There are no odd
  threads in this machine.
- **The thermoblock presents an M6 female gauge port** via `I.4234`
  (M6 at the block, G1/8 at the gauge). The G1/8 end is **measured**
  (9.55 mm); the M6 end is catalogue-plus-photo and `M6×1.0` in particular
  is **assumed, not established**.
- **THE GAUGE END CANNOT BE TEED.** Its rear stem is structural — a hex
  nut clamping it to the bracket — and pressure arrives through a soldered
  capillary. Anything wanting pressure has to go to `I.4234`.
- **The circuit is rigid tube with compression fittings**, so opening a
  joint means a fresh olive, and **there are no spare ports** anywhere
  photographed.
- **WHETHER INVARIANT 1 COVERS PLUMBING IS UNDECIDED.** The invariant was
  written about electrical taps and the 2026-10-12 amendment recast it
  around current paths, which is still an electrical notion. A tee, a leak
  or added dead volume all change what the machine does. Do not read
  "every sense tap is high-impedance or passive" as settling anything
  about water.
- **There is no plumbing census.** The connector census (J1–J5) has no
  hydraulic counterpart; `docs/hydraulics.md` covers the gauge branch
  only, and building the rest is a next-time-in-there job. Several
  arguments in the 0d flowmeter entry lean on this, so do not treat the
  circuit as surveyed.

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
- **GPIO33–37 are unavailable** (octal PSRAM uses them internally). An
  earlier version of this line said 35/36/37 only — Espressif's own
  guidance is that **GPIO33–GPIO37 are connected to SPIIO4–SPIIO7 and
  SPIDQS** whenever octal flash or PSRAM is fitted, i.e. any `R8` or
  higher part. 33 and 34 are not broken out on WROOM-1 modules anyway, but
  touching them in software can lock up the PSRAM, so do not probe them
  either. Configuring any of the five typically crashes the firmware.
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
    scale.{h,cpp}           BLE clients, reconnect
    detector.{h,cpp}        shot state machine
    spool.{h,cpp}           LittleFS ring, upload+retry
    spool_pick.{h,cpp}      which record uploads next
    upload_policy.{h,cpp}   retry / quarantine / delete on each HTTP code
    net.{h,cpp}             WiFi, WebSocket, HTTP POST
    display.{h,cpp}         J5 bus decode, blink discrimination
    machine_io.{h,cpp}      switch poller, debounce, event queue
    machine_context.h       per-shot machine facts for the spool header
    brew_guard.{h,cpp}      invariant 2 — is the pump running?
  test/host/                host-compiled unit tests, no board needed
server/            Go
  cmd/espressolog/
  internal/{api,live,record,store,web}/
                   record = on-flash wire format (v1 + v2 decode)
                   web    = embed.FS for the built PWA
  migrations/001_init.sql   ← from schema.sql
web/               Vite + React PWA
  src/ · prototype/
analysis/          Jupyter notebooks, reads the SQLite file
docs/              overview · phase-0-status · phase-0-plan · shopping-list
```

---

## Phase 0 milestones

**0a — scale logging.** BLE, shot detection, spool, upload, UI. No machine
contact. Most of the work, none of the risk. *This is where to start.*

**0b — display bus.** Logic analyser on J5, offline reverse engineering.
Parallel track, no code yet.

**0c — switch sensing. ALL FOUR direction lines are tapped** — the
operator asked for all functions (2026-10-04), which is sufficient reason
on its own. Four dividers, four GPIOs; the common (position 1) is the supply and is
never tapped **for sensing**.

**Phase 1 changes that.** The actuation design (`docs/phase-0-plan.md`) needs
position 1 at the board as the switching source — a photoMOS closes it to a
direction line, which is what the lever does. That is a drive path, not a
tap. It is **bounded** at ≤50 mA by the machine's own R45 — but that bound is
not the operating point: a press pulls the line high through the board's
pull-down, which is **not determined** (~0.1-1 mA depending on which meter
resolution the 2026-10-04 reading is credited with; `docs/phase-0-plan.md` is
normative on this figure). Note this is **not
the J4 `+` net**, which the standalone read-only rule names and which remains
untouchable; they are different conductors on different connectors that
happen to share the IRM-03-5 rail.

**Position 1 therefore rides in the Phase 0 harness — and that BUYS a new
failure mode rather than merely being made safe.** An earlier draft of this
paragraph said the output jumper "makes that safe". It does not: the jumper
covers the *photoMOS path*, and nothing covers the *conductor*. Position 1 is
+5 V through R45 running down a cable into the box, 2.54 mm from four
direction lines; a whisker, a chafed gland, a solder bridge or swarf starts a
cycle and reprograms the stored timer within 2 s. **That path did not exist
while pos 1 stayed out of the harness.**

Net Phase-0 actuation risk goes **up** with this decision. It is still the
right call — see the build argument in `docs/phase-0-plan.md` — but it is a
trade, and the mitigations are physical (jumper on the **pos-1 side**, pos 1
segregated in the harness, the existing sense divider used as a bridge
detector), not textual. See invariant 1 as amended 2026-10-12.

The supporting arguments, neither settled: **Steam** drives the boiler to
165 °C, which would contaminate `boiler_temp_start_dc` — though the display
may already show that, see above. **Hot Water** runs the pump, which
probably pulses the flowmeter — though the plumbing is untraced. Both are
worth having; neither should be written up as a necessity derived from
behaviour nobody has watched.

**Positions 2 and 5 were plug inspection until 2026-10-08 — now closed.**
The risk was that a swap would make the firmware label a steam event as a
water draw, breaking exactly the two things the four-line tap is for. It was
settled not with the meter but with the tap itself: with all four dividers
fitted and `switches on`, lever B up logged `water` and lever B down logged
`steam`. It exercises more of the chain than the planned continuity check —
conductor, divider, GPIO, firmware name — but it is a *longer* evidential
chain for the identification itself; see the lever table above.

**GPIO budget for the whole board: seven inputs.**

| Signal | GPIO | Note |
|---|---|---|
| `ESD4` clock | **4** | fixed by `main.cpp`; this is J5 **pin 3** |
| `ESD1` data | **5** | fixed by `main.cpp`; J5 **pin 2** |
| Flow `#` | **6** | fixed by `main.cpp` (0.9.0); FALLING-edge interrupt |
| 1Cup | **7** | fixed by `main.cpp` (0.9.0); polled |
| 2Cup | **15** | fixed by `main.cpp` (0.9.0); polled |
| Steam | **16** | fixed by `main.cpp` (0.9.0); polled |
| Hot Water | **17** | fixed by `main.cpp` (0.9.0); polled |

**All seven are now named in firmware 0.9.0, so none of them is a free
choice any more** — this table used to mark five of them "proposed" and they
were still unclaimed. Changing one now means changing `main.cpp`, not just
this row.

**THE BOARD CHECK IS DONE — 2026-10-12.** All seven GPIOs are confirmed
against the clone DevKitC-1 both ways round: **functionally** (the bus
decoded on 4/5, `selftest` counted 500/500 on 6, each lever logged its own
name on 7/15/16/17) **and visually** (the operator read the silkscreen
order). Neither alone would do it — the functional chain cannot tell wiring
by *label* from wiring by *position*, and the silkscreen read cannot tell
you the firmware agrees.

**THE ONBOARD RGB LED IS ON GPIO48, SO GPIO38 IS FREE.** Espressif put it on
48 on v1.0 and 38 on v1.1 and both revisions are sold, so this had to be
measured; it was, 2026-10-15, by a two-point test against `status`. **This
fixes the LED net only — it does not make the board "v1.0"**: every other
position agrees with the published v1.1 layout.

**ESPRESSIF'S PUBLISHED v1.1 PINOUT IS CORRECT FOR THIS CLONE.** The full
44-position board read agrees with it at all 44 positions. A draft here once
told readers to stop citing it; the unreliable source was a recollection of
the document, not the document.

**Layout facts the perfboard turns on:**

- **The seven taps are contiguous at signal-column positions 4–10**, with
  **3V3 at position 1 and a GND at position 22 of that same column**, so no
  ground return has to cross the board. "Between" is not "adjacent" though —
  each divider's ground leg runs 12–18 pitches.
- **5Vin is at signal-column position 21, directly beside that GND at 22.**
  That is a **solder hazard, not a convenience**: one bridge shorts the
  devkit's USB 5 V to ground. Leave the gap or route the bus away.
- **NO TAP DRAWS FROM 3V3.** Every divider is fed from a *machine* signal and
  returns to GND. The rail is still needed — the 74HC221 and the photoMOS LED
  supply run from it.
- **Pitch is 2.54 mm and row-to-row is 10 pitches (25.4 mm)**, measured by
  seating the devkit in a breadboard: one header in column A, the other in I.
- **Perfboard footprint is 11 columns × 22 rows**, with 9 free columns
  underneath the devkit if it is socketed high enough — which is why
  `docs/shopping-list.md` specifies female headers. **Buy 7 × 9 cm** for
  soldering room, and **count the holes on the board you actually receive**:
  commodity "7 × 9" boards are often 24 × 34 rather than 27 × 35.
- **GPIO33–37 are octal-PSRAM internal on this N16R8 module** — present on
  the header, unusable, do not even probe them.

**`docs/board-and-perfboard.md` is normative on all of it** — the 44-position
enumeration in full, the per-GPIO evidence table and the two honest limits on
it, the GPIO48 two-point test and the three drafts that got it wrong, the GND
census, the footprint arithmetic, and the breadboard-column-versus-perfboard-
pitch counting trap that costs a dimension every time it is forgotten.

**GPIO8 is reserved and is the only pin this firmware ever drives ON A
MACHINE-FACING NET** — `updateLed()` drives the onboard RGB LED on GPIO48, so
"the only output" is false and `main.cpp` already says so. The
`selftest` command makes it an output to feed a known pulse train into
GPIO6, proving the ISR/ring/loop path counts exactly, which no real shot can
prove (there is nothing to check the count against). It returns to INPUT the
moment the run ends. **Unplug J4 before running it** — driving a pin that
reaches the machine is precisely what invariant 1 forbids. Keep GPIO8 off
the perfboard's machine side and bring it to a header instead.

GPIO4/5 are **not** free choices — the firmware already names them, and
swapping clock for data decodes nothing. **Nor are the other five any
more** — 0.9.0 names all seven. They were chosen to avoid 0/3/45/46
(strapping; GPIO3 is JTAG source select), 19/20 (native USB), 26–32 (flash)
and **33–37 (octal PSRAM on this module)**, from the constraint list rather
than a pin-by-pin check of the board in hand.

Two things worth knowing about the chosen pins:

- **GPIO15–18 emit a brief (~60 µs) low pulse at power-up**, and three of
  the five switch/flow pins are in that range. Harmless here — through the 150 K
  series leg the pin can sink at most ~33 µA from a line driven through
  R45's 100 Ω — but invariant 1 says every *sense* tap is "high-impedance
  **or passive**", and a pin that briefly drives is not passive. (The
  2026-10-12 amendment keeps that phrase and keeps it applying here; it is
  quoted as current wording.) **The series
  resistor is what makes it so**, which is one more reason it belongs at
  the machine end and is not optional.
- **GPIO11–18 share ADC2, unusable while Wi-Fi is up**, and 15/16/17 are
  in that set. Fine as digital inputs. Both ADC1-capable pins in the budget
  (6, 7) are allocated, and GPIO8 — also ADC1 — is now claimed by `selftest`.
  **GPIO1, 2, 9 and 10 are ADC1 and unallocated**, so an analog read is still
  possible; an earlier version of this line said "there is no ADC1 pin left",
  which was wrong even before GPIO8 was claimed. It matters only if J3's NTC
  is ever wanted, since 0e takes temperature from the display bus instead.

**The 5 V rail is MEASURED (2026-10-04)**, so this divider is sized against
a reading rather than the schematic — the step that was skipped at J4. The
switch is driven through R45's **100 Ω**, so a 370 K divider loads it by about
a millivolt and the unloaded arithmetic is valid here.

<!-- fact:divider-j2 150 K / 220 K -->
**150 K / 220 K** on all four J2 direction lines into GPIO: 5 V → 2.97 V
predicted, **2.89 V measured at the junction** (2026-10-19, lever held; 0 V at
rest), **+415 mV** over the ESP32-S3 VIH of 2.475 V. 13.5 µA, dropping 1.35 mV
across R45.

**Never 220 K / 220 K** — 2.50 V against that same 2.475 V VIH is 25 mV of
margin, which supply tolerance alone eats, and a pressed lever reads as not
pressed. **And do not "correct" this pair to J4's 100 K / 220 K**: 0d rejects
150 K / 220 K and 0c keeps it, and that is not a contradiction — J2's source is
100 Ω, J4's is 9.4 kΩ.

**`docs/taps-and-dividers.md` is normative on the arithmetic** — both corners,
the 80 mV that is still unexplained between the predicted 2.97 V and the
measured 2.89 V, the analyser-leakage question and why one bench reading does
not retire a datasheet worst case, and the VIH figure a draft got wrong.

**Not an optocoupler either**: its mA would drop ~1 V across the 1 K series
network and could break the board's own threshold.

The switch harness is **J2** (silkscreen `BOTONE`), 5 pins: +5 V, Steam,
1Cup, 2Cup, Water. Gives real shot boundaries, flush events, and a true
`first_drip_ms`.

**0d — flowmeter. FIRST ATTEMPT CAUSED AN INVARIANT-1 VIOLATION
(2026-10-03) — read `docs/phase-0-status.md` before touching J4.** A tap on
J4 made the machine fault with `E01` while a 2Cup cycle was running;
disconnecting it and power-cycling cleared the fault. **Nothing is known to have been
damaged — the analyser's D1 input has not been re-checked**, and the
leading hypothesis involves ~12 mA through a clamp meant for signal-level
currents. The machine's behaviour changed because of a Phase-0 tap, which
invariant 1 forbids.

**THE DATASHEET IS IN HAND — 2026-10-09.** `932-952x-Bxxx_GB_20V_V04.pdf`,
Digmesa AG, Ipsach. It names our exact variant: **`#932-9525-B` = FHKSC,
Ø 1.00 mm, 0°**. Three of these were read off the physical sensor the same
day (`DIGMESA IPSACH / 932-9525-B / 14/01/20 / 1.0mm`), so the part is
identified by label and by datasheet, not by inference.

That also closes the worn-label mystery: the marking first read as
"DIMASE PSACH" is **DIGMESA IPSACH**.

| | |
|---|---|
| **K-factor** | **2382 pulses/L**, 0.41 g/pulse |
<!-- fact:digmesa-k-factor 2382 pulses/L -->
| Flow range | **0.033 – 0.40 L/min** (0.55 – 6.67 ml/s) |
| Pressure | **−1 to +0.3 bar** — a suction-side device |
| Supply | **+3.8 to +20 VDC**, <8 mA |
| Signal | open collector NPN, saturation <0.7 V, load max 20 mA, leakage max 10 µA |
| Output | **square wave, ~50 % duty** |
| Accuracy | ±2.0 %, repetition <±0.25 % |
| Temperature | −10 to +65 °C |
| Mounting | **horizontal, contacts facing up** — confirmed as installed, 2026-10-09 |

**Two hedges this file carried for weeks are resolved by it**: the "+2.8 V"
supply figure was a different Digmesa product, and the "2386 / 2476 / 2436
for 0° / 90° / 180°" note was four separate part numbers rather than one
part's mounting positions. Ours has no suffix, so 0°, so 2382.

**The datasheet also confirms the plumbing** — *"installed between the water
container and the vibratory pump (on the suction side)"* — which closes a
"whether that water passes the flowmeter is unverified" that this file
carried from 2026-10-04.

**Digmesa still say to calibrate**, and invariant 3 already requires it: log
raw pulses, never millilitres.

### WHAT THE J4 COUNTS ARE IS UNRESOLVED — and it is 460 lines of argument

**The whole investigation is in `docs/flowmeter-j4.md`, which is normative
on it.** It is the largest open question in the project and the least
useful thing to carry into a session that is not about it. What follows is
only what changes a decision.

**The counts are ~5× the datasheet and nobody knows why.** Shot 2: 736
pulses bracketed, 761 honest total over a 33.2 s pump window, against a
~150 predicted from the datasheet at an assumed inlet. Two readings fit:

| reading | status |
|---|---|
| **pump electrical noise** | **DEAD.** Refuted at 34× — a pump-on run with no water drawn gave **11** counts where coupling predicts 370 over the same sampled window, and the glitch rate collapsed 25× alongside it. |
| **water, plus OPV recirculation** | **LIVE.** The meter is on the suction side and the relief leg returns upstream of it, so with the OPV open the meter sees throughput rather than delivered volume. |
| **water, plus a constant meter over-count** | **LIVE.** A single 2.483× over-count with ~30 % of inlet reaching the cup reproduces shot 2 exactly, with no relief at all. |

**THE 5× IS NOT MEASURED. It is shot counts divided by an ASSUMED inlet**,
using this file's own 60 %-of-inlet-reaches-the-cup figure, which this file
also labels an assumption. Vary that one number and the gap closes. Every
"~5×" in this repo inherits it.

**SO DO EXPERIMENT 0d-i FIRST — WEIGH THE SPLIT.** `inlet = yield + puck
retention + 3-way dump`, three weighings and a subtraction. Until it exists
the 5× is an assumption wearing a decimal point. 0d-ii (raise the OPV, pull
one shot, read the gauge during it) is second. Both are written up in full
in the doc, including the void conditions that make 0d-ii return a false
null if the gauge is not watched.

**The free-flush over-count does not depend on the valve at all** — no
basket, no pressure, nothing relieving, and the meter still read 5915–6508
pulses/L against the datasheet's 2382. **The meter over-counts regardless.**

**THE OPV IS 11 BAR — measured 2026-10-19 with a blind basket, and that is
the FULL-FLOW pressure, not the cracking pressure**, which sits below it
(spring valves typically show 10–25 % override at full flow). So a ~9 bar
shot is **not** cleanly below cracking. It was already 11 bar when the
2026-10-09 shots were measured, so the flow data is comparable across it.

**THE OPV SETTING IS ADJUSTABLE STATE AND NOTHING RECORDS IT.** That puts it
in the same class as `grind_epoch` (invariant 5): a number relative to a
zero point that moves, so **flow data taken either side of an adjustment is
not comparable**. If the schema ever grows a column for machine state, this
belongs in it.

**BOTH EXPERIMENTS NEED THE J4 TAP REFITTED**, which means going back behind
the 2026-10-03 `E01` incident. It is currently unplugged. **Watch for the
fault on first contact, and note that `+` connects to nothing, ever.**

**And before adding any experiment to that list, check whether the operator
already does it every morning.** The repo has asked for "the cheapest thing
that would settle it" three times now — the gauge reading, the GPIO
silkscreen check and the `02.4` power-on string — and all three were already
being done and merely unrecorded.

**J4 pinout — roles MEASURED 2026-10-04**, with a DC pen meter, at the
window in the 2S T-piece lead, nothing attached:

| Marking | Role | Lead | What was actually read |
|---|---|---|---|
| `T` | **ground** | yellow | continuity beep to J5 pin 1 `VSS`; **no resistance value recorded** |
| `+` | **+5 V** | green | 5.0 V, steady through a flush |
| `#` | **signal** | black | 0.3 V parked, **2.5 V averaged during flow** |

The *roles* are measured. The conductor→**marking** mapping is corroborated
but still rests on re-reading the worn label that first read "DIMASE
PSACH"; `T` is anchored independently by the continuity test.

Note: probing the three **board header** pins against the J5 pin 1 pad
first gave **no beep at all**. Unexplained — plausibly the shrouded
`B3B-XH-A` pins or an occupied J5 pad, but that is a hypothesis. **The
T-piece window is where continuity is actually obtainable**, which matters
because the retry plan prefers back-probing.

So J4 carries ground, supply and **one** signal — the Flow-P1/P2 row above
is wrong to claim both reach this connector, and **"quadrature, or two
independent meters?" is closed: neither.** Which MCU pin the signal reaches,
and whether the schematic's second net exists elsewhere or is commoned at
the pin, is **untraced**.

**`#` does NOT reliably idle high.** An open collector at rest is on or off
depending on where the turbine parked; this one parked at 0.3 V, i.e. LOW.
Below 0.7 V is *consistent with* saturation, but a shorted conductor or a
stuck output reads the same — it does not establish health.

**The high level was unknown until 2026-10-04 and is now measured** — see
below. An earlier draft derived it from a duty-cycle figure taken off the
wrong model's datasheet, which left it underdetermined (3.3 V and 5 V both
fitted the single 2.5 V reading). Superseded by direct measurement.

**The J4 `#` node is a 10.04 kΩ pull-up to 5 V against a 147.4 kΩ pull-down**
— both measured unpowered in Ω mode, 2026-10-04. Source impedance **9.4 kΩ**;
`#` swings **0.3 V → 4.69 V**, both measured.

<!-- fact:divider-j4 100 K + 220 K -->
**The ESP32 divider is 100 K + 220 K** (100 K series, 220 K shunt) **→
3.13 V**, fitted since 2026-10-08. **Never 150 K**: it is in the drawer, it looks fine nominally, and
it fails its corner at −45 mV.
<!-- fact:j4-node-disturbance 134 mV|2.9 % -->
The chosen divider disturbs the node by **134 mV (2.9 %)**, which leaves the
machine's own reading unambiguously high — and which is the figure hard
invariant 1 cites as the only one of its three backed by a meter.

**`docs/taps-and-dividers.md` is normative on the arithmetic** — the four-way
divider comparison, the full tolerance corner, the low side, the over-rail
corner, drift on the board's own resistors, and the 120 K that was specified
until the bag was opened.

**The `+` conductor's read-only rule is NOT here.** It lives with the
invariants at the top of this file, deliberately, because it has been lost
from this spot three times — each time because it sat in or beside the
conditioning table and the table was rewritten when the divider changed.

**Conditioning, for the other two conductors:**

| Use | Conditioning |
|---|---|
| Analyser at J4 | **8.2 K series alone**, one channel, `#` only, ground to `T` |
| ESP32 at J4 | **100 K + 220 K.** A 10 nF across the shunt is a **candidate, untested** — see the 0d entry; nothing has been fitted and it may raise the glitch count by slowing the edge. It does not threaten invariant 1 (DC blocked, ~30 µA worst transient). **Never 150 K** — it looks fine nominally and fails at −45 mV. |
| At PA8/PA9 instead | **1 K series** — that is the *board* side, after its own conditioning. At J4 it would feed the raw sensor output into a 3.3 V GPIO. |

<!-- fact:divider-j4 100 K + 220 K -->

---

**`E01` is the dose-control fault**, from Ascaso's `MAN.29-V10` alarm table
("Dose control fault" / "Fallo control volumétrico"); `E02` is the probe.

Calibrate by weighing water on a Bookoo; invariant 3 means the K-factor is
never baked in, and Digmesa's own datasheet recommends calibrating it.

**0e — temperature. PATH DONE 2026-10-07** (shot 109); **what the number
means is OPEN.** The J5 tap is
wired to the ESP32 and `boiler_temp_start_c` / `machine_timer_s` land in
the database off the machine's own display. Conditioning: 8.2 K series at
the T-piece, 15 K shunt at the board, `ESD4`→GPIO4, `ESD1`→GPIO5, `VSS`
ground direct, `Disp-P4` unterminated.

**THE 15 K SHUNT FAILS ITS TOLERANCE CORNER AT −70 TO −87 mV — run
2026-10-19, for the first time.** That is the as-built value and it decodes,
so this is the record of what is fitted.

<!-- fact:divider-j5-shunt 16.4 K -->
**Fit 16.4 K** (two 8.2 K in series, 25 in stock), **+18 mV** against the
pessimistic corner. A draft
recommended 23.2 K, which puts the clamp into continuous conduction; a second
said to fit nothing, on a hard-wall reading of VDD + 0.3 that the 8.2 K series
resistor makes unnecessary. Both tables: `docs/phase-0-status.md`.

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
  would. 6086 edges is 1.88 s of bus at this file's own 3244 edges/s, and the
  display decoded the timer cleanly 0.5 → 33.7 s throughout, which a swamped
  bus cannot do.

  **But "serial polling" does not follow, and an earlier version asserted
  it.** A 3 s poll would drop *periodically*, not in one burst — and the idle
  rows of the table above were polled at the same cadence with the drop
  counter **frozen**. What the evidence supports is "a one-off stall around
  pour start"; candidates are the spool/BLE work at that moment or the serial
  traffic, and they are not separated. **`worst poll gap` is the
  discriminator and this repo already prints it** — record it on the next
  capture.

**`docs/phase-0-status.md` already carried ~470 pF for J5 before any of
this**, as a *contingent second remedy* after raising the shunt to 16.4 K.
Prefer that passage. Re-measure with the serial console detached before
fitting anything here.

**GPIO4 is the CLOCK and GPIO5 is the DATA, and getting it backwards is
silent.** Swapped on 2026-10-09 during a rebuild: both lines still idled
high, `pins` looked perfect, and `frames=0`. Zero frames with correct levels
is the swapped-pair signature.

**What `boiler_temp_start_c` actually means is narrower than migration
008's comment claims.** Below ~88 °C the display tracks the real boiler and
jitters ±1 °C; at 95 it holds ±0 °C for a minute, which no PID boiler does,
and during post-steam cooldown it blinks ` 95` while the boiler is
demonstrably hotter. The column is sampled immediately before a pour —
always in that ambiguous range — so today it means **"at setpoint"**, not a
verified temperature. Settling it needs a capture with the boiler pulled
off setpoint and allowed to recover. See `docs/phase-0-status.md`.

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

- **ARMED** — **stable at ANY value for 1 s.** `ARM_BAND_MG` (0.5 g) is a
  **flatness** band measured against the current reading, not a band around
  zero: the mean over that second becomes the baseline the curve is measured
  against, because a tare is just a baseline the scale applied for you. The
  un-tared cup-swap workflow depends on this.

  **This line said "weight within ±0.5 g of zero (i.e. tared with the cup
  on)" until 2026-10-12**, which is stale against detector 0a.6 — and it was
  load-bearing: it is where the false premise came from that wrongly withdrew
  a real invariant-2 finding in `main.cpp`. An un-tared cup of any mass can be
  ARMED, so a 5 g-threshold weighing episode CAN complete in that state.
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

**`inlet_pulses` needs a flag, not a format version — bit 4,
`SPOOL_FLAG_FLOW_TAP` (0.9.0).** The field has sat in `sample_t` since 0a
holding a placeholder 0, which is exactly why the flash layout did not have
to change when 0d arrived. But that also means **a stored 0 cannot be read
as a measurement**: it is indistinguishable from "nothing was counting".
The flag says the tap was live for that shot; without it the server stores
NULL, the same way `INT16_MIN` means "no reading" for temperature.

The server previously bound a literal `NULL` for *every* record while its
comment claimed it was only doing so for v1 — so counted pulses would have
been dropped at ingest with a green test suite. Fixed 2026-10-07, with a
test that fails against the old behaviour.

**Pulses are windowed, not free-running**, so the number shares the sample's
time base and a hot-water draw pulled beforehand cannot be attributed to the
brew. Backdated replay samples store 0 — their true counts are not
recoverable, the detector's ring holds weights rather than pulses, and 0 is
the one answer that cannot over-attribute.

**The window opens at POUR CONFIRMATION, and that is two gaps, not one.** An
earlier version of this paragraph said "windowed at the shot's own `t=0`",
which is not what the code does:

| gap | size | why |
|---|---|---|
| `t0` → confirmation | ~300 ms (`POUR_HOLD_MS`) | `t0` is backdated to the flow crossing, but `pulse_base` is captured in `beginPour()`, which runs after the hold |
| pump-on → first drip | seconds | the window opens at first drip; every pre-infusion pulse is outside it |

The second dwarfs the first, which is why neither is fixed in code yet —
closing 300 ms while seconds go uncounted is polish on the wrong axis. Both
close together when **0c** supplies the pump-start edge.

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
