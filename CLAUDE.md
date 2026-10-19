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

   **How high-impedance, honestly:** J4 disturbs its node by **2.9 %**, J2
   by ~1 mV, but **J5 pulls its line from 5.0 V to 4.16 V — a 17 %
   disturbance at 179 µA**. **That J5 figure is CALCULATED, not measured, and
   this clause called it measured until 2026-10-19.** It entered at `7182c56`
   as *"4.7 K pull-ups, so a 23.2 K divider leaves them at 4.16 V"* — the
   "so" is a derivation — and no meter reading of that node exists. The
   179 µA is the same arithmetic again (0.84 V / 4.7 K). J2's ~1 mV is
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

  **And the display DOES detect it — captured 2026-10-05.** Through a steam
  cycle the display blinks **`165`** for 49 s, then after release blinks
  **`95`** while cooling. Unmistakable on the bus, and a *better* signal
  than a switch tap because it says how hot and when it came back, not
  merely that a press happened.

  **So the J2 Steam tap is redundant for detecting steam.** Fitting it is
  still fine — the operator asked for all four functions — but it is not
  required for `boiler_temp_start_dc` integrity, and this entry should not
  claim it is.
- **LEVER B LATCHES — observed 2026-10-19, and it changes the brew guard.**
  A momentary press up starts the pump; it **keeps running after release**,
  and a **second press stops it** — the same latching behaviour as lever A.
  Left alone it runs for *"a couple of minutes"* (operator, approximate) and
  then returns to brewing mode on its own. Steam behaves the same way.

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
actuator that can hold the line will silently reprogram the machine. Phase 1
must use a hardware one-shot so firmware can *trigger* but never *hold*.

**The pulse width's UPPER bound is sound and its LOWER bound is not
established.** ~120 ms is comfortably under the ~2 s reprogram threshold, and
no RC tolerance gets near it. But nothing has established the shortest press
this board actually latches.

**The 345–480 ms figure IS machine presses — confirmed by the operator
2026-10-12, after a review questioned it.** It was recorded on 2026-10-08, by
hand on the levers.

**"Every press ever measured" is no longer true — superseded 2026-10-19**,
which logged ten 1Cup presses at **195–295 ms** and four at **80–90 ms**.

**"All latching" is how a draft put it, and the record does not support that
for all fourteen.** The operator watched the pump answer each of the **six
fast presses** (four of which were logged); the ten longer ones are durations
read off the tap, with no separate note that the machine responded to each.
**The latching evidence is the fast six.** The ten establish the width range
only. Treat 345–480 ms as *one session's* presses, not the machine's
range.

**AT LEAST SIX PLACES CITE IT.** *(This paragraph led with "FOUR" until
2026-10-19 and corrected itself 23 lines later — so a reader skimming the bold
text took away the superseded number. The live count leads now.)* Not the two
a draft listed. That draft named
`brew_guard.cpp:21` and `main.cpp:1109` and presented the list as complete; a
review found `docs/phase-0-plan.md:683` — where it carries an **actuation
safety argument** — and `firmware/test/host/brew_guard_test.cpp:72`.

| | |
|---|---|
| `docs/phase-0-plan.md`, the photoMOS-pair safety argument | **annotated 2026-10-19** |
| `brew_guard_test.cpp`, the `press_long_enough` case | **annotated 2026-10-19** |
| `brew_guard.cpp`, the pump-running-during-press comment | still bare — revisit after the analyser redo |
| `main.cpp`, the ring-headroom comment | still bare — revisit after the analyser redo |

*(Both were cited by line until 2026-10-19. The `main.cpp` one then drifted on
**four consecutive commits**, every time because the commit "fixing" the
citation style edited `main.cpp` in the same breath — and each successive note
recording the drift quoted a final value that was itself already stale, twice.
**No line number appears in this table any more, deliberately.** A line number
in a document is wrong by the time the document is saved; `grep` is not.)*

The `brew_guard.cpp` pump-running comment and the `main.cpp` ring-headroom
comment are about *timing headroom* where the exact figure is not
load-bearing, which is why they are left rather than churned.

**AND THAT TABLE WAS WRONG TOO — IT SAID "FOUR" AND THERE ARE AT LEAST SIX.**
Two more cite 345-480 as **live evidence**, which is worse than citing it as
a timing constant:

| | |
|---|---|
| this file, the **GPIO 7/15 confirmation row** | *"logged as 'correct GPIO mapping, taps of 345-480 ms'"* — **annotated 2026-10-19** |
| `docs/phase-0-status.md`, the **Settled** table | *"taps of 345–480 ms"* — **annotated 2026-10-19** |
| `docs/phase-0-status.md`, the ESP-power note | *"the grounding was identical for the 345-480 ms presses"* — uses it as a **dataset identifier**, which is legitimate: it names which session, and does not claim the range is the machine's |

**So the enumeration offered as complete has now been wrong twice in two
successive commits**, the second time in the very paragraph condemning the
first.

**AND THE FIRST DRAFT OF THIS VERY TABLE CITED `CLAUDE.md:1157`, WHICH WAS
ALREADY 1189 BY THE TIME THE COMMIT LANDED** — the paragraph above it grew,
and the line number drifted while it was being written.

**THEN THE FIX DID IT AGAIN, IN THE CROSS-FILE ROWS.** The replacement table
switched the same-file rows to section names and **left `plan.md:683` and
`main.cpp:1109` as line numbers — both of which that same commit moved**, to
685 and 1115 — and the `main.cpp` one has moved again since, twice, so both
this note's "corrected" values went stale too. **It is the ring-headroom
comment; grep it.** So the lesson was learned in one
half of the table and not the other, in a commit whose subject was incomplete
enumeration.

**One rule, and it is not "cite more carefully":**

- **A prose list of citation sites is the wrong instrument.** It is stale the
  moment anything above it moves, and it will be. `grep -rn '345.480'` cannot
  be stale. **Run that, not this table** — the table is here as narrative, not
  as an index.
- Where a location must be named in prose, **name the section or quote the
  line**, as the rows above now do. Line numbers are for a reader with the
  file open *right now*, not for a document.

**It was questioned for a good reason, and the reason is worth keeping.** The
written bench sequence for that step (`docs/phase-0-status.md`) reads *"jumper
each of GPIO7/15/16/17 to 3V3 and back"* — header touches, which would say
nothing about what this control board latches. **The session used the levers
instead.** So the *procedure* and what was *done* diverge, and anyone reading
the procedure as evidence of what happened will reach the wrong conclusion, as
a 2026-10-12 review did. Fix the procedure or annotate it; do not cite it
against the operator's account.

**MEASURED 2026-10-19: AT LEAST FOUR PRESSES IN THE 80-90 ms BAND LATCHED.**
Six deliberate 1Cup presses, as fast as the operator could make them; **the
pump started or stopped on every one**. The firmware logged four of the six,
at **90, 80, 85 and 80 ms**.

**This is the grade the paragraph above was asking for.** What was on record
before was *press durations observed at the tap* — nothing said the machine
had responded. These are presses **observed to latch**, because the operator
watched the pump answer each one. It is the first latching evidence in the
project, and it is **four times shorter** than the 345-480 ms band.

**THE 80 ms FIGURE IS QUANTISED BY THE POLLER AND A DRAFT USED IT AS IF IT
WERE NOT.** `machine_io.cpp:102` computes `duration_ms = cand_since -
prev_since` — the difference of **two poll timestamps**. Both edges are
rounded up to the next poll, and `worst poll gap` was **42-57 ms** in this
session. So a reported 80 ms means a true width anywhere in roughly
**[23, 137] ms**.

**That draft then claimed "a ~120 ms pulse has 1.5× margin over a
demonstrated-latching 80 ms". It does not follow, and the margin may be
NEGATIVE** — 137 ms is longer than 120 ms. The same commit states, a page
away, that the switch log *"is not an instrument below ~100 ms"*, and then
used an 80 ms reading from it to size a drive path. That is the file's
most-logged error committed against its own warning.

**WHAT THE MEASUREMENT ACTUALLY ESTABLISHES:** presses the poller measured at
80-90 ms latched, so the true threshold is **below ~140 ms** — which is still
a large improvement on 345-480 ms, and still the first latching evidence this
project has. It is **not** enough to choose the RC.

**THE RC REMAINS UNCHOSEN, and the warning that motivated it still stands**:
*a one-shot that is too short produces a drive path that silently does
nothing, and the sense tap would still show the pulse, so it would look like
it worked.* **Redo the measurement with the logic analyser on a J2 junction**
— microsecond edges instead of 50 ms quantisation. That is precisely why the
analyser is in the bench plan, and it was connected on 2026-10-19 and not
used for this.

**Use the 74HC221, NOT the 74HC123 this file used to name.** The '123 is
**retriggerable**: a new trigger edge restarts the pulse, so a GPIO
oscillating faster than ~8 Hz — a boot loop, a brown-out, a floating pin —
retriggers it continuously and holds the output high indefinitely. That is
exactly the failure the one-shot exists to prevent, and the part originally
specified does not prevent it. The '221 is non-retriggerable: the pulse
width is set by its RC per trigger, whatever the input does afterwards.

The NE555 has the mirror-image trap — in the standard monostable a trigger
held **low** holds the output high for as long as it is held — so it needs an
RC+diode differentiator on the trigger before it is safe here. The '221 avoids
the question.

**That reasoning predates the discovery of the second lever and should be
re-checked against it** — the conclusions above are about one direction
line at a time and look unaffected, but they were derived from a
one-switch model and nobody has walked them through a two-lever, shared-
common topology.

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

**`Disp-P4` IS THE LINE THAT MUST NOT BE LOADED, AND THIS PARAGRAPH USED TO
SAY THE OPPOSITE.** Until 2026-10-12 it read: *"This is the one J5 line where
a careless connection could actuate the machine… The 8.2 K / 15 K divider is
high-impedance and receive-only, so it is safe as specified."* Two defects, and together they made this the
only paragraph in this file that **invited** an invariant-1 violation rather
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

### Hydraulics — the pressure-gauge path

**This is the first hydraulic entry in this file.** Everything above it is
electrical: the connector census (J1–J5) has no plumbing counterpart, and
building one is a next-time-in-there job. What follows covers the gauge
branch only, because that is the only part anyone has looked at.

Note also that every tap described above is a resistor on a logic line.
Plumbing is a different risk class, and **whether invariant 1 covers it is
undecided** — the invariant was written about electrical taps, and a tee,
a leak or added dead volume all change what the machine does. Do not read
"every sense tap is high-impedance or passive" as settling anything about
water. **The 2026-10-12 amendment does not change this** — it recast the
invariant around current paths, which is still an electrical notion. Water
remains outside it.

Part numbers below are from **Ascaso's own Dream PID parts diagram**,
which is document-grade and the best mechanical source this project has
had:

| Part | Spanish | What it is |
|---|---|---|
| `I.4220` | MANOMETRO DREAM/STEEL | the gauge, 0–16 bar |
| `I.4238` | SOPORTE MANOMETRO DREAM PID | its bracket |
| **`I.4234`** | **RACOR CONEXION INTER.-MAN. M6-1/8** | thermoblock → manometer fitting |
| `I.3934` | RACOR T 1/8 H-H-M | a 1/8 tee, female-female-male |
| `I...90` | RACOR CODO MACHO 1/8 LATON | brass 1/8 male elbow |
| `I..259` / `I..706` | ELECTROV. 3 VIAS 1/8-1/8 0-15BAR | 3-way solenoid |
| `I.2237` / `I.2238` | CJTO INTERCAMBIADOR INOX | the thermoblock |

**The whole circuit is 1/8.** Even the solenoid is specced `1/8-1/8`.
There are no odd threads in this machine.

**`I.4234` is M6 at one end and G1/8 at the other, oriented thermoblock =
M6, gauge = G1/8.** The three parts of that claim have different grades
and the difference matters:

- The **G1/8 end is MEASURED: 9.55 mm**, operator's calipers, 2026-10-05.
  That sits inside G1/8's tolerance band (min ≈ 9.51 mm) and **below
  M10×1's minimum** (≈ 9.79 mm), so the identification is quantitative
  rather than a judgement call. The threads are visibly parallel, which
  also rules out 1/8 NPT.
- The **M6 end is NOT measured.** It is the catalogue designation plus
  photo proportions. `M6×1.0` in particular is assumed, not established.
- The **orientation** is the operator's account of the part in hand, not
  an independent check.

So the thermoblock presents an **M6 female gauge port**. M6 is also
reported to be the E61 group-head pressure-port thread, which would make
E61 gauge adapters the right category to shop in — but that is **vendor
listing grade** (a sensor maker's compatibility page naming an M6 adapter
for Lelit/Rocket/Profitec/ECM), not a spec, and **the pitch is not stated
anywhere**. Do not buy an adapter on it without checking pitch and seat
form, which is the same class of source that has twice misled this
project.

**An inference that was wrong, recorded so nobody re-derives it:** that
the orientation must be the reverse (thermoblock 1/8, gauge M6), on the
grounds that M6 is the natural size for a compression nut on 2 mm copper
and 1/8 the natural size for a port in a block. The reasoning is plausible
and the conclusion is false. The catalogue name lists the two threads in
the same order as the two words before them, and reading it literally was
right.

**THE GAUGE END CANNOT BE TEED.** Observed 2026-10-05: the gauge's rear
stem is a brass male thread with a **plain steel hex nut clamping it to
the bracket**, and the copper capillary is **soldered at the base of that
thread**. The thread holds the gauge up; pressure arrives through the
capillary. A nutted-to-a-bracket thread is structural, not hydraulic.
Anything that wants pressure has to go to `I.4234`.

**The circuit is rigid stainless tube with compression fittings, not
screwed pipe.** Every joint photographed is a compression nut swaging an
olive onto rigid tube, threaded onto an elbow or tee body. Two
consequences: opening a joint means a fresh olive and possibly a re-cut
tube end, and **there are no spare ports** — every port in the areas
photographed is occupied.

The capillary is **~10 cm** (operator measurement) and ends in a
compression nut with a **separate conical olive**, confirmed by Ascaso's
own product photo of `I.4220`. So the joint is re-makeable and the gauge
assembly comes off intact — keep it, with its olive and the M6 seal, and
the machine goes back to stock.

**Still unknown, all of it cheap to settle next time the machine is open:**
whether the M6 is ×1.0; what seals the M6 at the block (there will be a
washer — note what it is before it is lost, because an M6 *parallel*
thread does not seal on its threads and PTFE tape is not a substitute);
whether `I.4234`'s G1/8 male has a cone seat for the olive, which decides
whether a plain G1/8 female can seal against it; where `I.3934` physically
sits; and the bezel hole diameter.

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

**The board check is DONE — 2026-10-12 — and it was already done on
2026-10-08 without anyone noticing.** This paragraph carried "they have
**not** been checked against the clone DevKitC-1's silkscreen; do that
before the perfboard is drilled" for four days while the bench section 200
lines away recorded the evidence that closed it. Same stale-line defect this
file documents elsewhere.

What closes it is **functional evidence combined with the visual check** —
neither alone. The operator chose each header pin by reading its label, so if
the pin marked `16` were not die GPIO16, the firmware (which names GPIO16
steam) would have been polling an unconnected pad and lever B down could not
have logged `steam`. It logged.

**Do NOT call the functional chain "a higher grade" than the silkscreen read,
as a first draft of this paragraph did.** By this file's own standard — see
the lever A/B discussion above — a silkscreen read is a **one-link**
observation of exactly the claim being made (what is printed beside this
pin). The functional chain is longer: operator picked by label (recalled, not
recorded) → wire landed → firmware name → log line. It exercises more, and
that makes it a *longer* evidential chain, not a better-graded one. The
conclusion is sound because the two **combine**; the grade language was
inflated.

| GPIO | What confirmed it, 2026-10-08/09 |
|---|---|
| 4, 5 | the bus **decoded frames**. A swapped pair gives `frames=0` — this file's own documented signature — so this fixes *which is which*, not merely that both are right |
| 6 | **736 counts** across shot 2, and `selftest` 500/500 at 100 Hz |
| 7, 15 | **lever A**, logged as "correct GPIO mapping" — same full chain as 16/17. *(This row quoted "taps of 345-480 ms" as evidence until 2026-10-19; that is one session's presses, superseded above. The GPIO conclusion is unaffected — it never depended on the width.)* |
| 16, 17 | lever B down logged `steam`, up logged `water` |
| 8 | `selftest` into GPIO6 — **not independent, see below** |

**Two honest limits on that table.**

**Row 8 is not independent evidence.** `selftest` drives one die pin into
another through a physical jumper; if the pins labelled 6 and 8 were swapped,
the jumper still joins die 6 to die 8 and 500/500 still passes. Row 8 is
non-circular only given row 6. The rows are a mutually-supporting system, not
six parallel confirmations.

**Rows 7/15 were briefly "corrected" to jumper touches, and that was wrong.**
A 2026-10-12 review read the written bench procedure — *"jumper each of
GPIO7/15/16/17 to 3V3 and back"* — and inferred the taps were header touches.
I applied that as a correction. **The operator confirms they were lever
presses on the machine.** The procedure records what was planned; the levers
are what happened. Restored, and the divergence is now recorded in the
pulse-width section above so the procedure is not cited this way again.

So GPIO7/15 were exercised the same way as 16/17. **But the record is one
link short of saying so**, and this file spends a paragraph above insisting on
exactly that distinction: lever B has its own row quoting what was logged
(`B up logged water, B down logged steam`), while lever A's evidence is the
summary row "All four switches | correct GPIO mapping" — a conclusion, not an
observation. **CLOSED 2026-10-19 — ten of them, verbatim from the live tap:**

    # switch 1cup 220 ms    # switch 1cup 245 ms
    # switch 1cup 220 ms    # switch 1cup 285 ms
    # switch 1cup 295 ms    # switch 1cup 230 ms
    # switch 1cup 235 ms    # switch 1cup 275 ms
    # switch 1cup 240 ms    # switch 1cup 195 ms

Lever A up, logged as `1cup`, on GPIO7. Row 7/15's evidence is now the same
kind as lever B's — a quoted log line rather than a summary conclusion.

**The visual check's job**: the functional chain alone cannot distinguish
wiring by *label* from wiring by *position*. The operator confirmed the
silkscreen order on 2026-10-12, and since each GPIO exists at exactly one
header position, the two together identify every pin both ways round.

**The reference used for that check was Espressif's published layout, and it
holds up.** A draft here said the layout had been "overturned the same day on
GND placement" and graded it "a weak reference *for this clone*". Wrong on
both counts — the full 44-position enumeration **agrees with the published
table everywhere**, so the comparison is sound and this check is better
supported than that draft claimed.

**The seven are contiguous on one header, which the perfboard layout should
exploit.** Confirmed on the board in hand, 2026-10-12:

**FULL PINOUT — READ OFF THE BOARD 2026-10-12 AND CONFIRMED AGAINST
ESPRESSIFS PUBLISHED v1.1 TABLE.** Both columns enumerated end to end, 22
positions each, position 1 at the 3V3 end (breadboard row 42) and position 22
at the far end (row 63). **The two agree at all 44 positions.** A draft of
this heading said the board read "replaces Espressif's published layout —
use nothing else"; see the retraction below.

| Pos | Signal column | | Opposite column |
|---|---|---|---|
| 1 | **3V3** | | **GND** |
| 2 | 3V3 | | GPIO43 / TX |
| 3 | RST | | GPIO44 / RX |
| **4** | **GPIO4** — `ESD4` clock | | GPIO1 |
| **5** | **GPIO5** — `ESD1` data | | GPIO2 |
| **6** | **GPIO6** — flow `#` | | GPIO42 |
| **7** | **GPIO7** — 1Cup | | GPIO41 |
| **8** | **GPIO15** — 2Cup | | GPIO40 |
| **9** | **GPIO16** — Steam | | GPIO39 |
| **10** | **GPIO17** — Hot Water | | GPIO38 |
| 11 | GPIO18 | | GPIO37 ‡ |
| **12** | **GPIO8** — selftest | | GPIO36 ‡ |
| 13 | GPIO3 † | | GPIO35 ‡ |
| 14 | GPIO46 † | | GPIO0 † |
| 15 | GPIO9 | | GPIO45 † |
| 16 | GPIO10 | | **GPIO48 — onboard RGB LED** ◆ |
| 17 | GPIO11 | | GPIO47 |
| 18 | GPIO12 | | GPIO21 |
| 19 | GPIO13 | | GPIO20 † |
| 20 | GPIO14 | | GPIO19 † |
| 21 | **5Vin** | | **GND** |
| 22 | **GND** | | **GND** |

† strapping or native-USB, avoid. ‡ **GPIO35-37 are octal-PSRAM internal on
this N16R8 module** — physically present on the header and unusable; do not
even probe them.

◆ **THE ONBOARD RGB LED IS ON GPIO48 — SETTLED 2026-10-15 BY A TWO-POINT
TEST. So GPIO38 is FREE.** Espressif put it on **GPIO48 on v1.0 and GPIO38 on
v1.1** and both revisions are sold, so this had to be measured. Open since
2026-10-12.

**The two points, each anchored to a serial reading taken at the same
moment.** Setup: board on USB, **no machine connections**, and — the part a
draft of this paragraph left out — **the yield Bookoo bound and awake**. Row 1
is impossible without it, so "nothing else connected" read literally
reproduces a dark LED in both rows, which is the very trap this entry warns
the old one-second test fell into. Row 2 is produced by switching that scale
off:

| `status` | LED | what `updateLed()` writes |
|---|---|---|
| `yield: state=3` (CONNECTED), `detector=ARMED` | **dim green** | `mode 2` → `neopixelWrite(…, 0, 14, 0)` |
| `yield: state=4` (LOST) | **off** | `mode 0` → `neopixelWrite(…, 0, 0, 0)` |

**ONE POINT WOULD NOT HAVE BEEN ENOUGH, and a draft of this entry published
it as though it were.** That draft argued the *colour* proved the firmware
was driving the LED, because green is specific to ARMED. It does not: a
**green power indicator** beside a dark RGB LED produces the first row
exactly, and `docs/phase-0-plan.md` was about to free GPIO38 on the strength
of it. A power LED cannot produce the second row. Only the pair does.

**How the pin resolves — and a draft got the mechanism wrong while reaching
the right answer.** `RGB_BUILTIN` is **not** 48. The `esp32s3` variant (which
`esp32-s3-devkitc-1.json` selects) defines it as
`SOC_GPIO_PIN_COUNT + PIN_NEOPIXEL` = **49 + 48 = 97**, a sentinel;
`esp32-hal-rgb-led.c:12` subtracts `SOC_GPIO_PIN_COUNT` back off to reach
GPIO48.

**A draft then drew a false consequence from those two true facts**, warning
that *"`digitalWrite(RGB_BUILTIN, …)` addresses pin 97 silently and would not
do what it looks like it does."* **It does what it looks like.**
`cores/esp32/esp32-hal-gpio.c:138` special-cases `pin == RGB_BUILTIN` inside
`__digitalWrite` and routes it to `neopixelWrite(RGB_BUILTIN, c, c, c)` with
`c = RGB_BRIGHTNESS` for any non-zero value — so it lights the LED white.
`__pinMode` carries the same special case at line 95. The sentinel is handled
at every call site that matters. Mechanism right, consequence invented: the
call site was never read, which is this file's most-logged error and was
committed three times in one session.

**This fixes the LED net only. It does NOT make the board "v1.0".** Every
other position in the table above agrees with Espressif's published v1.1
layout, and a clone can mix revisions. Claim the pin, not the revision —
which is also why the table cell above carries ◆ rather than standing as part
of the board read: **an LED net is not silkscreened and is not in the v1.1
table either** (that document says 38), so that one cell comes from this
measurement and from no other source in this file.

**THE ONE-SECOND TEST THIS ENTRY USED TO GIVE WAS WRONG, and would have
produced the opposite answer.** It read: *"if the onboard status LED lights on
the current firmware, it is GPIO48; if it has always been dark, it is
GPIO38."* The second half is false. `updateLed()` sets `mode = 0` whenever the
**yield** scale is not connected, so a board sitting on a bench with no scale
awake is dark on *either* revision. Run as written it concludes GPIO38 and
sends someone to invert a free-GPIO list that was already right. **A dark LED
is informative only alongside `status`** — which is exactly what made the
second row above evidence rather than noise.

**A RETRACTION OF A RETRACTION, recorded because the second error is the one
this file keeps making.** A draft of this entry deleted the old sentence
*"this file and `main.cpp:377` both assert 48 flatly"*, calling it "a line
number cited without being read". **That accusation was false and the original
was correct.** `git show bb3ed26:firmware/src/main.cpp | sed -n '377p'` — bb3ed26
being the commit that wrote the citation — returns exactly
`// output full stop — updateLed() drives the onboard RGB LED on GPIO48, which`.
The line was read, was right, and has since drifted repeatedly — 445, then
472, and moving again within the same day's commits — while still asserting
GPIO48. The draft checked the *current* file
instead of the file at the commit that made the claim — the same
read-a-later-version-and-call-it-history error logged against `pendingCount()`
earlier the same day. **When a line number here looks wrong, check it at the
commit that wrote it before retracting anything.**

**AND DO NOT RE-CITE IT BY LINE, WHICH A DRAFT DID — it said "Cite
`main.cpp:445`" and that was stale within the week.** This paragraph has now
carried three numbers for one comment (377, 445, 472), each correct when
written. It is the `updateLed()` note in the GPIO8 block; `grep` finds it in
one step and cannot go stale.

*(Position 19 carried a "◦" flag until 2026-10-12: the operator's transcription
read "12 12" at positions 18-19 and GPIO13 was inferred. **Espressif's table
gives position 19 = GPIO13**, so the inference is confirmed — by the very
document a draft of this entry told the reader to stop consulting.)*

**Two things this corrects about earlier working notes**, both from the
published layout rather than the board: GPIO19 and GPIO20 are on the
**opposite** column, not at signal-column positions 13-14 (which carry GPIO3
and GPIO46), and the signal column ends **5Vin, GND** rather than running
GPIO14 to the last position. The repo's own table only ever claimed positions
1-12, which are correct — the error was in the fuller recollection used to
reason about free pins.

**GND is on the signal column — and the published layout said so all along.**
A draft of this entry claimed the board read "overturns the published layout"
and called it "a board reading beating a document-grade source". **It did not
beat it; it reproduced it.** Espressif's own v1.1 user guide gives J1-22 = `G`.
The enumeration above agrees with that document at all 44 positions.

**NO TAP DRAWS FROM 3V3.** The position table calls positions 1-2 "the
divider supply" and that is wrong — every divider here is fed from a
**machine** signal and returns to GND (J2: 150 K from the direction line;
J4: 100 K from `#`; J5: 8.2 K at the T-piece). 3V3 carries zero *tap* current.

**The rail is still needed, and a draft of this paragraph said it was not.**
The perfboard carries a populated 74HC221, which needs VCC and GND, and the
photoMOS LED supply runs from our 3.3 V logic. "A rail nothing connects to"
was true of the taps and false of the board.

**ROW-TO-ROW SPACING: 10 pitches — 1.0 in, 25.4 mm. MEASURED 2026-10-12** by
seating the devkit in an 830-point breadboard and reading the columns: one
header in **A**, the other in **I**. A→E is 4 pitches, the 0.3 in channel is
3, F→I is 3. **Column J stays free; nothing is free on the A side.**

That closes the last mechanical unknown. It had been recorded as open on
2026-10-12, deleted the same day by a paragraph rewrite, and covered over by a
claim that nothing mechanical remained — net information loss by rewrite,
restored, and now answered.

**`docs/shopping-list.md` predicted "no rows beside it" and that is REFUTED** —
one row (J) is free. The line is corrected there. It had been used twice to
argue the spacing was already partly constrained.

**The header is 22 pins, spanning breadboard rows 42 to 63** — one pin per
row, 3V3 at row 42. **Call this a consistency check, not a third independent
confirmation**, which a draft did: a header that is not 2.54 mm cannot seat in
the breadboard at all, so the row count is undefined in the counterfactual.
It is the *mating* argument restated from the same seating, on the same day.
Worth having; not a separate observation.

*(A first reading of this gave rows 42–64 and a 23-pin header, which would
have made the board depart from the published 2 × 22 layout. The operator
corrected it to 63 within minutes. Recorded only because the 23-pin reading
briefly reached a commit message.)*

**FOUR GND pins, and the census is now CLOSED by full enumeration** rather
than by a fourth count-by-eye. It had moved twice (three, then four) from
successive closer looks, which is why a review flagged it as a visual scan
standing as the premise of a layout claim. From the table above:

| | Position | Neighbour |
|---|---|---|
| **signal column** | **22** | next to 5Vin at 21 |
| opposite column | 1 | opposite 3V3 |
| opposite column | 21 | next to GPIO19 at 20 |
| opposite column | 22 | the far corner |

**The load-bearing claim SURVIVES.** The signal column runs **3V3 at position
1, GND at position 22, all seven taps between** — so no ground return has to
cross the board. That was in four documents on the strength of a visual scan,
and an intermediate description of the GNDs ("one opposite 3V3, two at the
other end of that row") read as though all of them were on the *opposite*
column, which would have refuted it. The enumeration settles it: there is a
GND at the far end of the taps' own column.

**5Vin is at signal-column position 21.** It is *our* 5 V rail, not the
machine's — do not confuse it with the J4 `+` net, which is read-only. (A
draft called this "not previously recorded anywhere"; it is in Espressif's
published table as J1-21.)

**It sits directly beside the GND at 22, and that is a solder hazard, not a
convenience.** On hand-wired perfboard with a ground bus down that column, one
bridge between 21 and 22 shorts the devkit's USB 5 V to ground. Leave the gap
or route the bus away.

**THE PUBLISHED PINOUT IS CORROBORATED, NOT REPLACED — and a draft of this
entry said the opposite.** It instructed the reader to *"stop citing
Espressif's published pinout for this board… Neither direction is reliable
here."* That rested on two supposed departures and **both were mine**:

| Alleged departure | Actually |
|---|---|
| a 23-pin header (rows 42-64) | a misread of row 63, retracted in the same commit |
| GND on the signal column | **J1-22 is `G` in Espressif's table.** The "departure" was my recollection of the layout, which put GPIO19/20 at J1 positions 13-14 where the real document has 3/46 |

The published layout therefore has a **clean record** against this clone, and
the 2026-10-12 enumeration **strengthens** the pin map by agreeing with it.
The lesson is the inverse of the one that got written down and pushed: **the
unreliable source was my memory of the document, not the document.**

Checked against `docs.espressif.com/projects/esp-dev-kits/` →
`esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html`, 2026-10-12.

The signal column runs **3V3 at position 1, GND at position 22, all seven taps
between**, so **no ground return has to cross the board** — the claim that has
survived every revision above.

**But "between" does not mean "adjacent".** The taps are at positions 4-10 and
that column's GND is at 22, so each divider's ground leg runs 12-18 pitches
(~30-46 mm). The nearest ground to the taps is the *opposite* column's position
1. The no-crossing claim is literally true; do not read it as a short return.

**COUNTING TRAP: breadboard columns are not perfboard holes, and the
difference is the channel.** A to I looks like 9 columns and *is* 9 columns —
but it is **10 pitches**, because the single visual step from E to F is the
0.3 in channel — **worth 3 pitches, not 1** (it spans 3 pitches and contains
2 missing hole positions). Perfboard has
no channel, so the same devkit lands on columns 0 and 10 there:

| | columns you can see | pitches |
|---|---|---|
| breadboard, A to I | 9 | **10** |
| perfboard, continuous | **11 inclusive** | **10** |

Quote the **pitch count**, never the column count, when carrying a dimension
from one board to the other.

**FOOTPRINT ON PERFBOARD: 11 columns × 22 rows**, with 9 free columns running
underneath the devkit between the two pin rows. That underneath space is real
estate — the dividers can live there if the devkit is socketed high enough,
which is the reason `docs/shopping-list.md` specifies **female** headers.

**Size the board from the hole count, not the centimetre label**, and do the
accounting properly — **a draft of this paragraph got it backwards**:

| | holes | devkit pin field | free beside it | free underneath |
|---|---|---|---|---|
| 5 × 7 cm | 19 × 27 = 513 | 11 × 22 | **8 × 27 = 216** | 9 × 22 = 198 |
| 7 × 9 cm | 27 × 35 = 945 | 11 × 22 | **16 × 35 = 560** | 9 × 22 = 198 |

That draft said 5 × 7 "does not fit" because "the devkit eats 22 of the 27
rows, leaving five" — which treats the devkit's **11-column** footprint as if
it spanned the full 19-column width. It does not. And it quoted 7 × 9 as
"16 × 13 clear" — a corner rectangle of 208 holes, **fewer than the 216 a
5 × 7 offers beside the devkit**. By its own metric the draft argued against
its own conclusion.

**7 × 9 is still the right buy**, for soldering room rather than necessity:
560 contiguous holes against 216, with the actuation block, four J2 dividers,
the J4 divider and two J5 resistors to place. But "5 × 7 does not fit" is not
what the numbers say.

**Footprint is the pin field, not the outline.** 11 × 22 holes is 25.4 ×
53.3 mm of *pins*; the PCB overhangs at both ends (module/antenna one end,
USB and buttons the other) by roughly 5 mm each, so the body covers ~25 rows.
Budget rows against the outline.

**Count the holes on the board you actually receive.** Commodity "7 × 9"
protoboards are often ~24 × 34 rather than 27 × 35, and "5 × 7" ones 18 × 24 —
and the hedge should be applied to both, not just to the one being rejected.

**Pitch is 2.54 mm (0.1 in) — and the proof is that the board has been in a
BREADBOARD since 0a.** A caliper reading of **1.9 mm** was taken 2026-10-12
and briefly recorded as possibly the pitch. It is the **gap**: 2.54 mm pitch
less a 0.635 mm square post (0.025 in) is **1.905 mm**. *(An earlier draft
rounded the post to 0.64 first and then called the 1.90 agreement "to the
hundredth" — a precision the inputs do not carry.)*

The arithmetic is not what settles it. **Breadboards are 2.54 mm and the
devkit seats in one — observed 2026-10-12.** A 1.9 mm part cannot mate with a
2.54 mm socket strip, so the pitch is settled by mating, which is this file's
own maxim.

**Mind the provenance, because a pushed commit got it wrong.** That commit
said the devkit "has been seated in a 2.54 mm breadboard **through every bench
session**". It has not: through the bench work the ESP was **off the
breadboard, connected by wires**, and only the divider junctions were in it —
which `docs/shopping-list.md` had already recommended. The evidence is the
2026-10-12 seating and nothing earlier. Conclusion unchanged, evidence
corrected: an assumed history is not an observation, which is the same error
this file logged twice on 2026-10-12 alone.

Recorded because the trap is live for anyone measuring a header: **calipers
laid between two pins read the gap, and the gap on 0.1 in hardware is a
plausible-looking 1.9 mm.** If a span ever does need checking, measure across
**ten pitches** rather than one gap, which multiplies the difference tenfold.
**Measure outer edge of pin 1 to outer edge of pin 11**, not centre to centre:
calipers land on pin faces and cannot find centres, so a centre-to-centre
instruction reproduces the very jaw-placement ambiguity being warned about.

| | 0.1 in pitch | a true 1.9 mm pitch |
|---|---|---|
| centre to centre (not measurable) | 25.40 mm | 19.00 mm |
| **outer edge to outer edge** | **26.04 mm** | **19.64 mm** |

*(10 × pitch + one 0.635 mm post. An earlier draft deleted the outer-edge row,
leaving only the instruction that calipers cannot execute.)*

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

**The 5 V rail is now MEASURED (2026-10-04), so the
divider is sized against a reading rather than the schematic** — which is
the step that was skipped at J4. Unlike J4's open-collector line, the
switch is driven through R45's **100 Ω**, so a 370 K divider loads it by
about a millivolt and the unloaded arithmetic is valid here.

**150 K / 220 K** divider on PB5 and PB6 into GPIO
(5 V → 2.97 V, ~0.5 V over VIH; 13.5 µA load, dropping **1.35 mV** across
R45).

**MEASURED AT THE JUNCTION 2026-10-19: 2.89 V with a lever held, 0 V at
rest.** Against an ESP32-S3 VIH of **2.475 V** that is **+415 mV of margin**.
This closes an item the bench list carried as open — *"we proved the firmware
reads `1`, never wrote down the margin"*.

*(A draft wrote "+580 mV against a VIH of 2.31 V". **2.31 V is 0.7 × VDD, the
74HC figure** — this file uses **2.475 V** nominal and **2.60 V** at a
5 %-high rail, and quotes 2.475 V twelve lines below when rejecting
220 K / 220 K. **A first version of this parenthetical then claimed the error "was caught and
fixed in `docs/phase-0-status.md` in the commit that introduced it". It was
not** — that file carried it in three further places, and saying otherwise
closed the loop on a defect that was still open. A review found all three.
Applying a correction in one file and announcing it as complete is strictly
worse than not applying it, because it stops anyone looking.)*

**The 80 mV below the predicted 2.97 V is NOT fully accounted for, and this
line used to say "nothing to chase".** The two causes it offered do not add
up to 80 mV:

- **a 4.86 V rail** would do it — but this file records that rail as
  **measured at 5.0 V** (2026-10-04), so invoking 4.86 V contradicts its own
  measurement;
- **1 % resistor tolerance** moves the junction by at most **~24 mV**.

Together they leave **~59 mV unexplained** unless the rail really is lower
than recorded. **Still nothing to act on** — the margin is +415 mV and a
60 mV discrepancy threatens nothing — but it is an open discrepancy between
two of this file's own numbers, not a closed question. Re-read the rail at
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
5.3 kΩ and could never have been affected.

**0d rejects 150 K / 220 K and 0c keeps it — that is not a contradiction.**
0c's corner is **+203 mV** (rail −5 %, VIH +5 %, 1 % resistors) because the
switch is driven through **100 Ω**, so the divider barely loads it and the
unloaded ratio holds. J4's source is **9.4 kΩ** — two orders of magnitude
stiffer — which is what collapses the same pair to −45 mV there. **Do not
"correct" 0c to J4's 100 K / 220 K** on the strength of the J4 warning; the
pair is fine here and the source impedance is the whole difference.

**Not an optocoupler**: its mA would drop ~1 V across the 1 K series
network and could break the board's own threshold. Do NOT
use 220 K / 220 K: 2.50 V against an ESP32-S3 VIH of 2.475 V leaves 25 mV
of margin, which supply tolerance alone consumes, and a pressed button
would read as not pressed. The switch harness is **J2** (silkscreen
`BOTONE`), 5 pins: +5 V, Steam, 1Cup, 2Cup, Water. Gives real shot
boundaries, flush events, and a true `first_drip_ms`.

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
| Flow range | **0.033 – 0.40 L/min** (0.55 – 6.67 ml/s) |
| Pressure | **−1 to +0.3 bar** — a suction-side device |
| Supply | **+3.8 to +20 VDC**, <8 mA |
| Signal | open collector NPN, saturation <0.7 V, load max 20 mA, leakage max 10 µA |
| Output | **square wave, ~50 % duty** |
| Accuracy | ±2.0 %, repetition <±0.25 % |
| Temperature | −10 to +65 °C |
| Mounting | **horizontal, contacts facing up** — confirmed as installed, 2026-10-09 |

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

**The one number 100 K is worse on: the over-rail corner.** With the machine
rail 5 % high, the ESP rail 5 % low and tolerances aligned, the pin sits
**174 mV above VDD**, where 120 K stays 13 mV below it. That is inside the
ESP32-S3's **VDD + 0.3 V absolute maximum** with 126 mV to spare, and a clamp
diode does not conduct meaningfully below ~0.4 V, so it is benign — recorded
because it is the only axis on which the substitution loses, not because it
is a problem. (With the ESP powered OFF and the machine on, the pin drives
~24 µA into the dead rail through the clamp. 120 K gives ~23 µA; this is not
a difference between them, and neither actuates anything.)

**A parts claim in this file was wrong for a day.** "The 120 kΩ is in stock
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

---

### J4 `+` — the one hard safety rule, and it does not live in a table

> **`+` is read-only, through a high-impedance meter, and nothing else.**
> Nothing with an input clamp — no analyser, no GPIO, no divider — **and
> nothing that sources or sinks current: never power the ESP32 from it,
> never feed it from another supply, never hang a pull-up on it.** It is
> the machine's own sensor rail off a 3 W IRM-03-5. A 20 MΩ DMM is fine,
> and is how its 5.0 V was read on 2026-10-04.

This block is deliberately **outside** the conditioning table. The rule has
now been lost three times, each time because it was a table row and the
table got rewritten when the divider changed. Rewrite the divider as often
as you like; do not touch this.

**Conditioning, for the other two conductors:**

| Use | Conditioning |
|---|---|
| Analyser at J4 | **8.2 K series alone**, one channel, `#` only, ground to `T` |
| ESP32 at J4 | **100 K + 220 K.** A 10 nF across the shunt is a **candidate, untested** — see the 0d entry; nothing has been fitted and it may raise the glitch count by slowing the edge. It does not threaten invariant 1 (DC blocked, ~30 µA worst transient). **Never 150 K** — it looks fine nominally and fails at −45 mV. |
| At PA8/PA9 instead | **1 K series** — that is the *board* side, after its own conditioning. At J4 it would feed the raw sensor output into a 3.3 V GPIO. |

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
so this is the record of what is fitted. **Fit 16.4 K** (two 8.2 K in
series, 25 in stock), **+18 mV** against the pessimistic corner. A draft
recommended 23.2 K, which puts the clamp into continuous conduction; a second
said to fit nothing, on a hard-wall reading of VDD + 0.3 that the 8.2 K series
resistor makes unnecessary. Both tables: `docs/phase-0-status.md`.

**A capacitor here is NOT currently justified, and a 2026-10-09 edit said it
was.** That edit attributed 6086 dropped display edges to pump coupling and
specified 10 nF. Both parts were wrong:

- **10 nF would have stopped the bus decoding.** Against a 6.9 kΩ source it
  is a 69 µs time constant facing 36 µs clock highs — the clock never
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
