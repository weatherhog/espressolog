# Phase 0 — build plan

Machine: Ascaso Dream PID (STM32F030R8T6 control board)
Grinder: Eureka Mignon Single Dose
Scales: 2 × Bookoo Themis (BLE)

**Goal of Phase 0:** log everything, actuate nothing. Ends with a few
hundred shots in a database and a measured answer to the only question
that decides whether Phase 3 is worth building — how much your shots
vary when you change nothing.

---

## Guiding rules

1. **Nothing in Phase 0 drives a machine signal.** Every *sense* tap is
   high-impedance or passive. If a wire comes loose the machine behaves
   exactly as it does today. From the perfboard onward the actuation parts
   are **present but jumpered open** — see hard invariant 1 in CLAUDE.md,
   amended 2026-10-12, and the actuation section below. This rule used to
   read "nothing touches an output", which the same file's own topology
   section has contradicted since 2026-10-08.
2. **Electronics live outside the machine.** A small box behind or beside
   it, with a thin cable in. Inside is hot, damp and full of triacs.
3. **Photograph before touching.** DIP switch positions, connector
   orientations, wire colours, harness routing. You will want these.
4. **Each milestone is independently useful.** Stop at any point and you
   still have something working.
5. **Prefer reversible.** A Y-harness that plugs between the existing
   connector and the board beats soldering to pads.

---

## Milestones

### 0a — Scale logging (no machine contact at all)

The whole software stack, with zero hardware risk. This is most of the
work and none of the danger.

- ESP32 connects to a Bookoo as BLE central, samples at 10 Hz
- Detect shot start/end from weight rate of change
- Buffer the curve in RAM, write to flash on completion
- Publish to a server, write to SQLite using `schema.sql`
- Serve the web UI

**Second scale.** You have two. Pair both: one under the cup, one for
the grinder. Then `dose_in_g` and `dose_ground_g` log themselves and
the capture screen only ever asks you about taste. This is the single
biggest reason you'll still be logging in three months.

**Done when:** you log every shot for a week without thinking about it.

### 0b — Display bus reverse engineering (parallel, listen-only)

Independent investigation, no integration yet. Logic analyser on the
display connector **J5**, not on MCU pins — same signals, far easier to
probe, and it doesn't matter that we haven't identified the peripheral.

Two candidate lines, each with a 100 R series resistor and a 4.7 K
pull-up to +5 V, routed through the ESDA6V1SC6 array (U26) as nets
`ESD1` / `ESD4`. Either hardware I²C on PB8/PB9 or a bit-banged
two-wire link on PB7/PB8.

Capture while the machine heats up and while pulling a shot. You're
looking for a byte that tracks the displayed temperature.

**Done when:** you can predict the number on the display from the bus
capture. If the protocol turns out to be opaque, abandon it — cost was
€25 and an evening.

### 0c — Switch sensing

Real shot boundaries and flush events from the machine itself.

- **All four direction lines are tapped**, not two: there are TWO levers,
  A (1Cup up / 2Cup down) and B (Hot Water up / Steam down), sharing J2
  conductor 1 as their common. CLAUDE.md has the measured J2 pinout — note
  **2Cup sits before 1Cup** on the harness.
- Idle low, **high when pressed** (+5 V → R45 100 R → switch common),
  measured 2026-10-04.
- **The stored durations are not constants.** This machine's 1Cup is ~10.5 s,
  not the 15 s default, because it was reprogrammed by hand; 2Cup's stored
  value is unknown. An earlier version of these bullets quoted 15 s and 60 s
  as properties of the model. They are user-modifiable state.

Sense with a **resistor divider straight into an ESP32 GPIO**. Not an
optocoupler: an opto LED wants milliamps, and a milliamp through the
1 K series network is a 1 V drop that could break the board's own
threshold.

**Use 150 K / 220 K** — 5 V → 2.97 V at 13.5 µA. An earlier version of
this line said 100 K / 200 K, which is wrong twice over: it gives
5 × 200/300 = **3.33 V, above the ESP32's own 3.3 V rail**, which leaves
**zero headroom** and relies on both rails being exact. (30 mV over the
rail does not forward-bias a clamp diode — that needs ~0.3–0.7 V — so do
not repeat that mechanism; the objection is the missing margin.) CLAUDE.md has carried
150 K / 220 K for some time and this file was never updated — the same
stale-contradiction defect that put the pre-incident wiring in the
shopping list. CLAUDE.md is the source of truth.

Do **not** use 220 K / 220 K either: 2.50 V against an ESP32-S3 VIH of
2.475 V, and supply tolerance alone eats that margin.

Confirm the idle/pressed polarity with the multimeter before connecting
anything.

Share ground with the board's VSS. That's safe here — the 5 V rail
comes from an IRM-03-5, which is an isolated module — provided the
ESP32 also runs from an isolated supply (any USB phone charger).

**DONE 2026-10-08 — firmware and tap both.** All four lines were fitted,
exercised against the real machine and logged correctly; lever B's conductor
mapping went from plug inspection to functional in the same session. See
`docs/phase-0-status.md`. Four
polled, debounced lines, each press emitted with its backdated start and
duration, and a hold past 2 s flagged as a timer reprogram — which is the
machine rewriting its own configuration and the reason this machine's 1Cup
is ~10.5 s rather than 15 s with nothing recording when that happened.

**Note what the level does NOT mean.** Both levers are momentary with spring
return, and the board latches, so the line is high for the *press*, not for
the brew. Anything reading "shot in progress" off these pins is wrong.

**Done when:** flushes and shots appear in `machine_event` with correct
durations, and `first_drip_ms` becomes a real measurement. **Neither is true
yet** — the firmware reports presses on serial and over the live socket, and
nothing persists them. That is the remaining 0c work and it is deliberate:
persistence is not what gates the perfboard.

### 0d — Flowmeter

Inlet-side flow. Combined with outlet weight this is a direct read on
puck resistance — the nearest thing to a pressure profile without
plumbing in a transducer.

**Both paragraphs that used to be here were wrong, and were still here
after CLAUDE.md had corrected them** — the same stale-contradiction defect
flagged under 0c above. CLAUDE.md is the source of truth. For the record,
so nobody re-derives them:

- *"Tap the conditioned side (PA8/PA9), not the raw sensor at J4."* The tap
  is **at J4**, on the raw open-collector output, through a **100 K + 220 K**
  divider derived against the measured 9.4 kΩ node (120 K until 2026-10-08;
  100 K has about double the margin and is what is in stock). 1 K series is correct
  only on the *board* side of the board's own conditioning; at J4 it would
  feed a 4.69 V sensor output into a 3.3 V GPIO.
- *"Two nets — possibly quadrature, possibly two-meter support. Log both."*
  **J4 carries ONE signal** (ground + 5 V + `#`), measured 2026-10-04. The
  question is closed: neither.

**Read `docs/phase-0-status.md` before touching J4.** The 2026-10-03 tap
made the machine fault with `E01` mid-cycle — a hard invariant-1 violation.

**DONE 2026-10-08 — firmware and tap both.** The J4 tap was refitted with
the 100 K + 220 K divider, energised, and **produced no `E01`** across one
session. Pulses reached the database. That is **n=1** — the 2026-10-03 risk
is not retired on one clean run, and the status doc's wording ("energised,
and the machine has not noticed") is the grade to quote.
What is still open is whether the counts are water or ringing — see the
filter sweep in `docs/phase-0-status.md`. Falling
edges on GPIO6 into a ring, a 2 ms glitch floor, and a count windowed to the
shot so a hot-water draw cannot be attributed to a brew (the window opens at
pour confirmation — CLAUDE.md has the two gaps that leaves). Off by
default behind `flow on`; `selftest` drives a known pulse train from GPIO8
so the counting path is provable on the bench without the machine.

**Calibrate empirically** — run water into a jug on the Bookoo, count pulses,
divide. **The datasheet is in hand (2026-10-09): 2382 pulses/L for
`#932-9525-B`, Ø1.00 mm, 0°** — our exact part and orientation.

**Do not calibrate from in-circuit counts until CLAUDE.md's 0d entry is
resolved.** Two shots logged ~5× the datasheet, a published reading that the
counts were pump noise has been withdrawn, and whether they track water is
open. The flush figures below are superseded by that.

The in-circuit flushes said 5900–6500 and two shots said ~5× the datasheet.
**Neither is usable**: what the in-circuit counts measure is unresolved, a
published reading that they were pump noise has been withdrawn, and CLAUDE.md
0d carries the live account and the experiment that separates it. Store raw
pulses and the calibration separately
(`flowmeter_calibration`), never the converted millilitres.

### 0e — Integrate temperature

Only if 0b succeeded. **The deliverable changed once 0b revealed what the
display does during a brew**, and this entry used to describe something
that cannot be done from this source.

The machine takes the display over for its shot timer the moment the pump
runs, so there is **no boiler temperature to read mid-shot**:
`shot_sample.temp_dc` stays NULL for the pour by design, and
`008_machine_signals.sql` says so. The deliverable is therefore the
**pre-pour reading in `shot.boiler_temp_start_c`**, which is a per-shot
column rather than a per-sample one.

**Path DONE 2026-10-07** (shot 109). **What the number means is OPEN** —
see `docs/phase-0-status.md`: at the setpoint value the display cannot be
distinguished from the setpoint itself.

### 0f — Sit still and look at the data

**The most important milestone and the one that gets skipped.**

Pull 30 shots changing nothing. Same bean, same grind, same dose, same
prep. Then compute the standard deviation of shot time and mean flow
rate.

That number is your noise floor. It is the honest ceiling on what any
closed loop can achieve, and it sets the deadband in Phase 3. If it
turns out that your puck prep varies more than a 0.2 notch grind change
does, you have learned something worth more than the whole rest of the
project — and you'll know to spend your effort on prep consistency
rather than on a stepper motor.

---

## Parts

Prices approximate, EUR.

### Order now — milestone 0a

| Item | Notes | ~€ |
|---|---|---|
| ESP32-S3 dev board | Seeed XIAO ESP32S3 (tiny) or ESP32-S3-DevKitC-1 (easier to probe) | 8–15 |
| USB-C cable + charger | probably already have | — |

That's it. 0a needs no soldering and no contact with the machine.

> **Superseded.** The parts tables below were written before the connectors
> were identified and before the multimeter broke. Use
> [`shopping-list.md`](shopping-list.md) for anything you are about to buy;
> these are kept for the reasoning behind each choice.

### Order with it — milestone 0b

| Item | Notes | ~€ |
|---|---|---|
| 8-channel logic analyser | FX2LP (CY7C68013A) clone, works with sigrok/PulseView | 10 |
| Micro test-hook grabber clips, ×10 | **do not skip** — you cannot hand-hold probes on a connector | 12 |

A €10 clone is genuinely fine for a 100 kHz two-wire bus. Don't buy a
Saleae for this.

### Order later — milestones 0c/0d

Identify the connector types on J2, J4 and J5 first (pitch and pin
count) so you can buy matching pigtails and build reversible Y-harnesses
instead of soldering to the board.

| Item | Notes | ~€ |
|---|---|---|
| ~~Resistor kit, E12 1/4 W~~ | **Not required** — see `shopping-list.md`: it covers no named value, and cheap kits are 5 % where every divider here wants 1 %. All of them are in stock. | — |
| 30 AWG silicone hookup wire, 4 colours | flexible, survives vibration | 8 |
| JST pigtails, assorted pitch | **after** identifying connectors | 8 |
| Perfboard + pin headers | 2.54 mm, confirmed against the board | 6 |
| Heat-shrink assortment | | 5 |
| Small ABS project box | mounts outside the machine | 6 |
| 74HC221 (dual), photoMOS ×2 **or ×4**, one-shot RC, disable jumpers | **moved into this order 2026-10-12** — the build decision populates them behind open jumpers rather than leaving footprints. Counts unresolved, see `shopping-list.md` §7 | ~8 |

### Tools, if you don't have them

| Item | ~€ |
|---|---|
| Temperature-controlled soldering iron, fine tip | 40–60 |
| Flux pen + 0.5 mm solder | 12 |
| Headband magnifier | 15 |
| Multimeter | (you have one) |

### Phase 1 — switch actuation, PROVISIONED ON THE PERFBOARD

**Decided 2026-10-08.** The operator chose to design actuation into the
perfboard rather than retrofit it later, after the arguments below were put
and reaffirmed. Retrofitting into a finished, glanded, cable-tied box is the
cost that decided it.

**The invariant 1 decision is MADE — 2026-10-12. Invariant 1 was AMENDED,
not exempted.** This section was pending that decision since 2026-10-08.

The old wording — *"No writes to any machine signal. Every tap is
high-impedance or passive. Read-only."* — did two jobs at once. **"Never
actuates" is the goal; "high-impedance or passive" is one implementation of
it.** The perfboard forces them apart, because a board can carry drive
hardware and still be physically incapable of driving. A carve-out would have
weakened the goal to rescue the mechanism. The amendment keeps the goal and
replaces the mechanism with a stricter one:

> Drive hardware may be **present** on the board only while an **open jumper
> in series with its OUTPUT** makes conduction physically impossible — not
> merely unpowered, not merely un-commanded. A disable that only cuts a
> control input does not satisfy this, because it does not survive the
> controlled part failing short.

Read the full amended text in CLAUDE.md. Two things follow for this board:

- **the disable moves from the LED supply to the drive leg** — see below;
- **"the board is still Phase 0" becomes checkable by looking at it**, rather
  than by trusting the firmware not to fire.

**The chosen build, of three that were put:** populate the actuation parts and
**bench-test them off-machine**, then leave the output jumpers open.

**Build the dummy to match the real circuit, not the discredited one.**
5 V → 100 Ω → photoMOS → ground — the obvious rig — is exactly the
ground-referenced topology this document rejects below, and it exercises the
part at ~500× the real current while testing the wrong thing. Use
**5 V → 100 Ω → photoMOS → 50 kΩ → ground with a voltmeter on the node**: that
reproduces the press loop, confirms the node actually reaches a logic high
against the board's pull-down, and exercises the sense divider's readback at
the same time. The alternative of laying out footprints
and fitting nothing carries less residual risk, and was rejected because it
seals an **unverified** layout into a box — and unverified things being wrong
is where this project's actual defects have come from, not component failure.
Phase 1 then costs two jumpers instead of a soldering iron and a re-crimped
harness.

#### Noise: a capacitor across every shunt — but NOT the same value

**Measured SYMPTOM, 2026-10-09 — the remedy below is untested.** With the pump running, the J4 tap
counted **~22 Hz against ~128 Hz rejected**, both ring buffers overflowed,
and the display bus dropped 6086 edges at pump start. Across 13 idle samples
either side, both taps were perfectly silent.

**Do not read "hash" into those counts.** What they measure is unresolved —
CLAUDE.md 0d carries the live account; a published reading that they were
pump noise has been withdrawn.

| tap | source Z | signal | fit | cutoff |
|---|---|---|---|---|
| J4 `#` | 73.1 kΩ | ≤16 Hz | **10 nF** × 1 | 218 Hz |
| J5 `ESD1`/`ESD4` | 6.9 kΩ | **9.9 kHz clock** | **470 pF** × 2 | 49 kHz |

**Do not use 10 nF on J5.** The first version of this section did, by copying
the J4 value across. Against a 6.9 kΩ source that is a 69 µs time constant
facing 36 µs high pulses — the clock never settles and the display bus stops
decoding, which would present as a wiring fault rather than a filter error.

**Neither value is a settled requirement.** An earlier version of this
section called them "a measured requirement"; what was measured is the
symptom, and no capacitor has been fitted. The J4 case has an unexamined
failure mode (a ~1.1 ms edge through the noise band can *raise* the glitch
count) and the J5 case rests on drops that look like serial polling rather
than coupling. **Leave the footprints, decide after the re-measurement**
described in `docs/phase-0-status.md`.

#### Topology, per actuated line

```
  J2 pos 1 — +5 V switch common (through R45, 100 R, on the control board)
      |             The switch common. Nothing else connects here.
      |
      o             JUMPER (a) — OPEN THROUGHOUT PHASE 0, and on THIS side of
                    the photoMOS: an open here is still an open when the part
      o             fails SHORT, and it leaves the live machine conductor
      |             dead-ending on a bare pad rather than resting on a
  [photoMOS]        semiconductor terminal. Invariant 1 requires this.
      |
      |             DRIVE. Closes common to the direction line.
      |             Its LED supply carries jumper (b) — see the disables
      |             section. Both are wanted; they are not nested.
      +-------------------------------------  J2 pos N, direction line
      |
  [150 K]           SENSE. Unchanged from 0c.
      |
      +--- GPIO
      |
  [220 K]
      |
     GND
```

**Read that carefully: the photoMOS goes between pos 1 and pos N, and
NOTHING on the drive path touches ground.** An earlier box-drawing version of
this diagram put the common on the ground rail — built literally it would
have held the direction line high permanently (pump running, stored timer
reprogrammed inside 2 s) and drawn ~50 mA continuously through R45 off a 3 W
supply. The prose was right and the drawing was not.

**Current budget.** With the photoMOS closed, the only path is +5 V → R45
(100 R) → direction line → **the board's own pull-down**, so the current is
**bounded** at ≤50 mA / 250 mW against the IRM-03-5's 3 W, and bounded by the
machine's resistor rather than by anything of ours.

**That bound is NOT the operating point, and this document read it as one.**
50 mA requires the direction line at 0 V — a dead short to ground, which is
the discredited topology above, not a press. A real press pulls the line
**high**. `docs/phase-0-status.md` records the pull-down as **not determined,
anything from ~50 kΩ upward**, so a press draws **≤ ~100 µA** — the same order
as the 13.5 µA sense dividers, not milliamps. Finish the measurement if the
number ever matters: `R_pulldown = 100 × (5 − V)/V` needs a millivolt reading
across the closed switch.

A **photoMOS relay** (AQY212, TLP222A) across common↔direction is a genuine
contact replacement: bidirectional, no saturation offset, optically isolated.
It does what the lever does. Its on-resistance is irrelevant here — a few ohms
against a loop of ≥50 kΩ, not against R45's 100 Ω as first written.

**CLAUDE.md's "not an optocoupler" warning does not apply here.** That is
about *sensing* — milliamps through the sense network would break the board's
own threshold. On the drive path current is the point.

#### The one-shot: 74HC221, not the 74HC123

Holding a direction line past ~2 s **reprograms that direction's stored
timer**, permanently and silently. So the pulse width must be bounded by
hardware, not by firmware intent.

**The '123 is retriggerable and does not do that.** A trigger oscillating
faster than ~8 Hz restarts the pulse before it ends and holds the output high
indefinitely — a boot loop or a floating pin is enough. The '221 is
non-retriggerable: one bounded pulse per trigger, whatever the input does
next. ~120 ms via its RC.

The NE555's mirror-image trap (a trigger held low holds the output high)
needs an RC+diode differentiator to avoid. The '221 does not.

#### Scope: actuate two lines, sense four

Brew-by-weight needs **1Cup and 2Cup** only — the board latches, so one pulse
starts and a second stops. Steam and Hot Water gain nothing and double both
the parts and the failure surface. **Sense all four, drive two.**

One dual 74HC221 and two photoMOS — **or four, if the series-redundancy
mitigation above is taken. That is unresolved and gates the parts order**;
`docs/shopping-list.md` carries the same open question.

#### The failure neither mechanism bounds

**A photoMOS fails short** — that is the characteristic MOSFET failure mode.
The one-shot bounds the *trigger*; the hardware disable cuts the *LED*.
Neither bounds a shorted output, which holds common to the direction line
indefinitely: the stored timer is reprogrammed within 2 s and the pump does
not stop. There is no fail-safe-open on this path — CLAUDE.md already records
that opening the switch line cannot stop the machine.

**IN PHASE 0 THIS FAILURE IS NOW CLOSED, by moving the disable.** The
hardware disable below was specified on the **photoMOS LED supply**, which
breaks the *control* path only — precisely the path a shorted output does not
use. Put an **open jumper in series with the photoMOS OUTPUT** instead and a
shorted part is still an open circuit. Same component, one position along,
and it is what amended invariant 1 requires. Keep the LED-supply disable too
if you like; it is not what makes the claim true.

**In Phase 1, with the jumpers fitted, the failure is real and unmitigated.**
Nothing below bounds it, and one suggestion that stood here for four days
does not work:

- ~~a **series fuse or PTC** on the drive leg, so a shorted output becomes an
  open one~~ — **WRONG, and recorded rather than deleted.** Three independent
  reasons, the first of which corrects a figure this document got wrong:

  **The press current is ~100 µA, not 50 mA.** A legitimate press and a
  permanent short draw **identical** current, because they are the same
  circuit — that much holds whatever the magnitude. But the magnitude matters
  for this mitigation, and the first draft of this bullet put it at ~50 mA.
  That figure belongs to the **discredited ground-rail topology** above, not
  to normal operation. A real press pulls the direction line **high**
  (CLAUDE.md, measured); the loop is 5 V → R45 (100 Ω) → the board's own
  pull-down, which `docs/phase-0-status.md` records as **not determined,
  anything from ~50 kΩ upward**. So ≤ ~100 µA. If 50 mA flowed, the whole 5 V
  would drop across R45 and the line would sit at 0 V — the press would not
  register at all.

  **At ~100 µA nothing in the fuse or PPTC catalogue responds**, which kills
  the mitigation harder than the original argument did. Even granting the
  wrong current, no current-threshold device separates a press from a short;
  it would have to discriminate on **time**, and 120 ms to 2 s is a **16:1
  window** — wide, not narrow as first written. Small PPTCs at several times
  hold current do trip in 0.1–0.5 s. What actually defeats a thermal device
  here is that its trip point is a steep function of ambient, and this board
  lives next to a boiler.

  **A PPTC self-resets**, so even a perfect one opens, cools, re-closes and
  presses again — an unbounded press *train* instead of one permanent press.
  Worse than the failure it mitigates, and that rules the device class out on
  its own.

  **And a fuse was never going to deliver what it promised.** One opening at,
  say, 1 s has already permitted a press long enough to start a cycle, and
  CLAUDE.md records that opening the switch line cannot stop the machine. It
  prevents the *reprogramming*, not a spurious shot.
- **the readback that already exists, and is free.** The 150 K/220 K **sense
  divider sits on the same direction line**. If the line reads high when
  firmware commanded nothing, a device has failed — an undetected accumulating
  fault becomes a detected one. **Redundancy without detection is not a safety
  chain**, it only buys the probability that two failures pile up unnoticed
  over the machine's life. This costs nothing and is not yet in the firmware.
- **two photoMOS in series** per driven line, both off the same LED supply.
  Cheap — four parts instead of two — but weaker than it first appears, and
  the first draft of this bullet overstated it as "both must fail short":
  - **Two *partial* failures suffice.** An AQY212/TLP222A output is two
    MOSFETs, sources common, body diodes opposed — that is what makes it
    bidirectional. The drive here is **DC of fixed polarity** (pos 1 = +5 V),
    so one shorted FET leaves the other's body diode forward-biased and the
    device conducts at ~0.6 V. Two such half-failures give ~1.2 V of drop and
    still leave ~3.8 V at the direction line, above the STM32's VIH.
    *(Inferred from the standard photoMOS output topology; not checked
    against the AQY212 datasheet in hand.)*
  - **Common cause.** Two identical parts from the same reel, in series, off
    the same LED supply, in the same thermal environment, seeing the same
    surge history on the same node. A surge on J2 — the plausible cause of an
    output short — presents across the pair. "Standard safety-chain move"
    normally implies *diverse* redundancy; this is its weakest form.
  - **UNDECIDED: two photoMOS or four. Resolve before ordering** —
    `docs/shopping-list.md` and the scope section below both still say two.

    **An earlier draft of this bullet contradicted itself** — it said four
    "contradicts the Phase 1 costs two jumpers rationale" and then, in the
    same sentence, that fitting *two* is what bakes in the retrofit. The
    second clause is the right one. **Four does not contradict the build
    rationale; it is what the rationale implies.** Fitting the parts now
    exists so Phase 1 is not a soldering job, and building two and later
    wanting four is exactly that soldering job. There is no cost on that
    axis — only the price of two more parts.
- a **watchdog relay** in the LED supply that firmware must keep re-arming,
  so a crash de-energises the drive path rather than leaving it as it was.
  Note this shares the fuse's blind spot — it is on the control path, so it
  does not bound a shorted output either.
- at minimum, **record it as a known unmitigated failure** rather than let
  "two things make it provably safe" stand as the whole story.

#### Two things that bound the firmware-side failures

- **TWO hardware disables, not one.** An intermediate draft replaced the
  LED-supply jumper with a drive-leg jumper; they are not nested and both are
  wanted.

  **(a) In the drive leg** — a jumper or DIP switch in series with each
  photoMOS output, on the **pos-1 side** of the part. This is what makes "the
  board is still Phase 0" a fact about the hardware, and amended invariant 1
  requires this placement specifically. It is what the LED-supply disable
  cannot do: survive the output failing short.

  **(b) In the LED supply** — which the drive-leg jumper cannot replace,
  because with the output open and the LEDs live, **firmware is free to
  energise them throughout Phase 0 and nothing ever reveals it.** The fault
  then surfaces at the instant the Phase-1 jumper is fitted: line driven,
  timer reprogrammed within 2 s. The LED disable makes "firmware has
  demonstrably never commanded a fire" true by construction, which is exactly
  the evidence wanted *before* fitting (a). It also puts the two opens in
  **different electrical domains** — machine 5 V against our 3.3 V logic — so
  one contaminated pad, flux residue or swarf cannot close both. **Remove it
  last.**

  Note (a) is not a firmware-side bound at all, despite this list's heading:
  it bounds everything while open and nothing once fitted. (b) and the arm
  gate are the firmware-side ones.
- **An arm gate.** AND the one-shot output with a second GPIO that firmware
  must hold high, so one stuck or floating trigger pin cannot fire anything.

#### What this changes in the harness

**J2 position 1 must come to the board.** Today it deliberately does not — it
is the supply and is never tapped. The drive path needs it as the switching
source, so the J2 T-piece goes from four conductors to five, and that
connector carries the switch common. **Not "switching current rather than
microamps"**, as this said until 2026-10-12 — the press loop is ~100 µA, the
same order as the sense dividers.

**SETTLED 2026-10-12: position 1 rides in the Phase 0 harness** (see CLAUDE.md
invariant 1 as amended). **That decision creates a failure mode that did not
previously exist, and the jumper does NOT cover it.** The output jumper makes
the *photoMOS path* safe; it does nothing about the conductor. Position 1 is
+5 V through R45, crimped into a 5-way JST, run down a cable through a gland,
and landed on a perfboard 2.54 mm from four direction-line conductors. A
whisker at the crimp, chafe in the gland, a solder bridge or swarf in the box
bridges it to 1Cup — the machine starts a cycle, and a solid bridge reprograms
the stored timer within 2 s.

So **net Phase-0 actuation risk goes UP** with this decision, not down, and an
earlier sentence in CLAUDE.md claimed the jumper made it safe. Mitigate by
construction, not by text:

- put the jumper on the **pos-1 side** of the photoMOS rather than the pos-N
  side. It satisfies "in series with the output" identically and blocks a
  shorted part identically, but leaves the live machine conductor dead-ending
  on a bare jumper pad instead of resting permanently on a semiconductor
  terminal. Strictly less exposure, zero cost;
- keep pos 1 **physically segregated** in the harness and on the board — its
  own conductor run, maximum spacing from the four direction lines, and no
  shared via column;
- the sense divider on each direction line is already a **detector** for this:
  a line reading high with nothing commanded means a bridge or a failed part.

#### The argument against, recorded rather than dropped

0f measured **σ = 0.21 g/s on mean flow, 12.2 %**, and the project's own
reading is that a controller resolving less than ~12 % is chasing noise. That
figure bundles puck prep with the machine, and **0f has not been re-run with
machine-sourced timing**. The original plan deliberately deferred these parts
so 0f could inform the decision.

That argument is unresolved, not refuted. Provisioning the board does not
commit to populating it, and the go/no-go on *fitting and arming* the drive
section should still wait for a 0f re-run.

---

## Suggested architecture

```
Bookoo #1 ──BLE──┐
Bookoo #2 ──BLE──┤
                 ├── ESP32-S3 ──MQTT──► server ──► SQLite
switch sense ────┤    (10 Hz)          (homelab)      │
flowmeter ───────┤                                    │
display bus ─────┘                          web app ──┘
                                                 │
                                              tablet
```

**Why the tablet only talks to the server:** Web Bluetooth doesn't exist
on iPadOS in any browser, and a tablet that falls asleep mid-shot must
never be able to lose a shot. The ESP32 owns the radio and the timing;
the tablet is a screen.

**Why MQTT:** 10 Hz of samples is nothing, you get fan-out for free, and
when Phase 1 arrives the command path is already there.

**Spool locally.** ESP32 writes each completed shot to flash and retries
the upload. Never lose a shot because the server was rebooting.

### Firmware

PlatformIO + Arduino framework. Not ESPHome — the YAML gets painful the
moment Phase 3's control logic shows up.

- `NimBLE-Arduino` for BLE
- `esp-arduino-ble-scales` — Bookoo Themis is listed as tested, so the
  protocol work is already done. Bookoo also publish their protocol at
  `github.com/BooKooCode/OpenSource` if you need to go deeper.

### Worth reading first

**GaggiMate** (`jniebuhr/gaggimate`) — ESP32, BLE scales, shot logging,
web UI, brew-by-weight. Read how it structures the scale abstraction
and the shot lifecycle before writing your own. You may decide to fork
it rather than start clean.

**Gaggiuino** — for the pressure/flow profiling side, relevant much later.

---

## First evening

1. Flash the ESP32, connect to one Bookoo, print weight to serial at 10 Hz.
2. Pull a shot with the ESP32 watching and the scale on the drip tray.
3. Paste the numbers into a plot.

If you can see the curve, everything else is software.
