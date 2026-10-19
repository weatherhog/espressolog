# The switch harness — J2

> **Normative on:** the J2 press-width record, the evidence that this
> control board latches and at what width, the citation-drift saga around
> the 345–480 ms figure, and the Phase-1 one-shot reasoning in full.
>
> **CLAUDE.md carries** the live conclusions: the J2 pinout and lever table,
> that a hold > ~2 s REPROGRAMS the stored duration (so the durations are
> user-modifiable state and no fail-safe-open design is possible), that
> Phase 1 needs a hardware one-shot and it must be a 74HC221 and not a
> '123, that the RC is still unchosen and getting it wrong fails silently,
> that 345–480 ms is one session's presses and `grep` rather than a list is
> how its citation sites are found, and the brew-guard fail-to-ARM exposure
> — which is a live invariant-2 finding and is NOT here.
>
> **Changing a number here means grepping CLAUDE.md for it in the same
> commit.**

---

## The lever table's evidence grades

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

---

## Steam drives the boiler to ~165 °C

**And the display DOES detect it — captured 2026-10-05.** Through a steam
  cycle the display blinks **`165`** for 49 s, then after release blinks
  **`95`** while cooling. Unmistakable on the bus, and a *better* signal
  than a switch tap because it says how hot and when it came back, not
  merely that a press happened.

  **So the J2 Steam tap is redundant for detecting steam.** Fitting it is
  still fine — the operator asked for all four functions — but it is not
  required for `boiler_temp_start_dc` integrity, and this entry should not
  claim it is.

---

## The press width, and what actually latches

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
