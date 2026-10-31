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

1. **Nothing in Phase 0 drives a machine signal.** If a wire comes loose the
   machine behaves exactly as it does today. From the perfboard onward the
   actuation parts are **present but jumpered open**.
   **`CLAUDE.md` hard invariant 1 is normative; this line is a pointer and
   must not restate its terms.** A paraphrase here ("nothing touches an
   output") contradicted this file's own topology section for four days, and
   a later draft quoted wording CLAUDE.md had already retracted.
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
- Publish to a server, write to SQLite using `server/migrations/`
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
  not the 15 s default, because it was reprogrammed by hand; **2Cup is 45 s**,
  not the 60 s default, set deliberately by the operator and recorded
  2026-10-19 *(this bullet said "unknown" until 2026-10-27; every other site
  had been updated)*. An earlier version of these bullets quoted 15 s and 60 s
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
duration, and a hold past 2 s flagged, on release, as a timer reprogram
(a line already high at first sight is adopted and not flagged) — which is the
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
<!-- fact:digmesa-k-factor 2382 pulses/L -->
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
| ~~30 AWG silicone hookup wire, 4 colours~~ | **SUPERSEDED 2026-10-28 — two wires were bought instead, neither of them this one.** `shopping-list.md` §1 is normative: **24 AWG solid core** for board point-to-point and **26 AWG silicone** for the T-piece branches. The rationale here — "flexible, survives vibration" — was right about the harness and is carried by the 26 AWG; 30 AWG was simply too fine for either job. *(A draft of this cell said "this was the only file that priced it" — false: `shopping-list.md`'s historical consumables row priced it at €8 — a row deleted 2026-10-30 with the rest of that file's price estimate. That claim was copied from a sentence in `shopping-list.md` that was already wrong the same way, and both are corrected 2026-10-29.)* | 0 |
| JST pigtails, assorted pitch | **after** identifying connectors | 8 |
| Perfboard **10 × 10 cm FR-4 ×5** + **female** headers ×5 + sockets | 2.54 mm confirmed by mating. **SUPERSEDES "7 × 9 ×2", 2026-10-28** — 7 × 9 was picked for soldering room against a nominal 27 × 35, and the boards actually sold are 24 × 30. See `docs/board-and-perfboard.md` for the table and `shopping-list.md` §5 for the reasoning; `shopping-list.md` is normative on parts and prices. **The boards alone are €10**; the female strips are €4.59 and the sockets €2.19, both priced separately in §1, so this row understated at €10 until 2026-10-29 | 16.78 |
| Heat-shrink assortment | | 5 |
| ~~Small ABS project box~~ **PRINTED** | the 120 × 80 size was chosen for a 5 × 7 board and would not have fitted the 70 × 90 one — **and 70 × 90 is no longer the board either: it is 10 × 10 since 2026-10-28.** The ~110 × 100 plan dimensions that replaced 120 × 80 are **withdrawn** for the same reason and have no successor — the box is printed last and sized to the finished board; **the 40 mm height alone is retained, as an upper bound**. `shopping-list.md` §5, "The box has no dimensions any more", is normative | 0 |
| **`CD74HC221E`** **×3** *(×3 until 2026-10-29, then ×2 — one channel per photoMOS, four channels, two dual chips; see "The one-shot channels WERE the constraint" below — and ×3 again from 2026-10-30 for the arm-window gate, a different third chip from the one first estimated)*, **AQY212EH DIP-4 ×8** (4 on the board, 4 spare), **BC547/2N3904 ×10**, **330 Ω**, one-shot caps, **3 × 2-pin header** (no shunts), DIP sockets | **moved into this order 2026-10-12** — the build populates them behind open jumpers rather than leaving footprints. `shopping-list.md` is normative on part numbers and prices; **this file is normative on counts and placement** | ~25 |

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

**`CLAUDE.md` holds the invariant text; this file does not restate it.** A
draft of this section quoted it as *"makes conduction physically impossible"*,
which CLAUDE.md retracts — an open jumper makes conduction require a
deliberate mechanical act, and a shunt fitted in error or a solder splash
closes it. Quoting a retracted absolute in the document a builder follows is
the worst place to do it. **Read the rule there, not here.**

Two things follow for this board:

- **a disable on the LED supply alone is not sufficient** — see
  "Two things that bound the firmware-side failures", below, for what is
  actually fitted;
- **"the board is still Phase 0" becomes checkable by looking at it** for
  every clause but one. GPIO8 stays operationally bounded; CLAUDE.md says so.

**The chosen build, of the two that were put:** populate the actuation parts
and **bench-test them off-machine**, then leave the drive-leg jumpers open.
The alternative — lay out footprints, fit nothing — carries **less** residual
risk and was rejected because it seals an **unverified** layout into a box,
and unverified things being wrong is where this project's defects have
actually come from, not component failure. Phase 1 then costs fitting shunts
instead of a soldering iron and a re-crimped harness.

**Build the dummy to match the real circuit.** 5 V → 100 Ω → photoMOS →
ground is the obvious rig and it tests the wrong thing: it proves the contact
closes, which was never in doubt, and says nothing about whether the node
**reaches a logic high against the board's pull-down**, which is the only
electrical question Phase 1 has. Use **5 V → 100 Ω → photoMOS → 50 kΩ →
ground, voltmeter on the node**.

*(A draft also rejected the simple rig for "being the ground-referenced
topology this document discredits" and for running the part at ~500× the real
current. Both were wrong: the discredited topology was a **wiring error**
inside the machine, not a bench load to bench ground, and 50 mA is well inside
an AQY212's rating — more current is a harder test of a contact, not a
defect.)* The alternative of laying out footprints
and fitting nothing carries less residual risk, and was rejected because it
seals an **unverified** layout into a box — and unverified things being wrong
is where this project's actual defects have come from, not component failure.
Phase 1 then costs fitting shunts instead of a soldering iron and a
re-crimped harness.

#### Noise: a capacitor across every shunt — but NOT the same value

**Measured SYMPTOM, 2026-10-09 — the remedy below is untested.** With the pump running, the J4 tap
counted **~22 Hz against ~128 Hz rejected**, both ring buffers overflowed,
and the display bus dropped 6086 edges at pump start. Across 13 idle samples
either side, both taps were perfectly silent.

**Do not read "hash" into those counts.** What they measure is unresolved —
`docs/flowmeter-j4.md` carries the live account (CLAUDE.md 0d has the
conclusion); a published reading that they were
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
count) and the J5 case rests on drops whose cause is **unidentified**.
**Leave the footprints, decide after the re-measurement** described in
`docs/phase-0-status.md`.

*(This said the J5 drops "look like serial polling rather than coupling"
until 2026-10-27. `docs/taps-and-dividers.md` is normative and retracts
that: a 3 s poll would drop periodically, not in one burst, and idle rows
polled at the same cadence had the counter frozen. Coupling is still
excluded; what replaces polling is "a one-off stall around pour start",
with the spool/BLE work and the serial traffic unseparated. `worst poll
gap` is the discriminator.)*

#### Topology, per actuated line

```
  J2 pos 1 — +5 V switch common (through R45, 100 R, on the control board)
      |             Shared by BOTH levers' four direction lines on the
      |             machine side, and now by both our drive legs.
      |
      o             JUMPER (a) — one per driven line, OPEN THROUGHOUT PHASE 0.
                    On this side so the photoMOS sees no voltage in Phase 0.
      o             (A series open blocks the same on either side; that is not
      |             the reason. See "Two things that bound the
      |             firmware-side failures".)
      |
  [photoMOS]        DRIVE, device 1 of 2.
      |
  [photoMOS]        DRIVE, device 2 of 2 — in SERIES. Both must fail for the
      |             line to be driven (but see the note below the diagram).
      |             Each LED needs its OWN path, for the
      |             per-device test; they cannot share one drive.
      |             The LED supply carries jumper (b), shared across lines.
      |
      +-------------------------------------  J2 pos N, direction line
      |
  [150 K]           SENSE. Unchanged from 0c. NOT a fault detector — this is
      |             the line the operator presses, so a high here is almost
      +--- GPIO     always a lever, not a failure.
      |
  [220 K]
      |
     GND
```

**"Both must fail" is the body-diode picture only.** The two failures are not
independent and a gate-to-drain fault is not diode-limited — see the
**"two photoMOS in series per driven line"** bullet under **"The failure
neither mechanism bounds"**, below, and its 2026-10-19 correction.

**Read that carefully: the photoMOS goes between pos 1 and pos N, and
NOTHING on the drive path touches ground.** An earlier box-drawing version of
this diagram put the common on the ground rail — built literally it would
have held the direction line high permanently (pump running, stored timer
reprogrammed inside 2 s) and drawn ~50 mA continuously through R45 off a 3 W
supply. The prose was right and the drawing was not.

**AQY212EH pin functions — DATASHEET grade since 2026-10-29.** Source:
Panasonic *PhotoMOS GE DIP4 1 Form A* product catalogue,
`semi_eng_ge1a_aqy21_e.pdf`, 19 PDF pages — **pp.1–7 are ASCTB126E
202601** (©2026, the per-part catalogue; catalogue page = PDF page − 1),
**pp.8–19 are ASCTB65E 202509** (©2025, general PhotoMOS cautions,
bundled). Every citation in this repo is to the ASCTB126E pages. It is the
current per-part document, which the 2026-10-17 read could not fetch; `docs/shopping-list.md` "Datasheet
read" records it as document C. Until this read the repo held the part's
electricals at datasheet grade and its pinout nowhere.

| DIP-4 pin | function | in this topology |
|---|---|---|
| **1** | **LED anode (+)** — the wiring diagram drives I_F into this pin | the 330 Ω from the jumpered 3V3 LED supply |
| **2** | **LED cathode (−)** | the BC547 collector |
| **3** | output | one end of the drive chain |
| **4** | output | the other end |

Pins 1 and 2 are on the input side of the package, 3 and 4 on the output
side: in the top view 1 sits opposite 4 and 2 opposite 3. **The outputs are
two N-MOSFETs source-to-source and the part is rated "AC/DC dual use", so 3
and 4 are interchangeable** — the chain is pos 1 → jumper (a) → device 1
(3/4) → device 2 (3/4) → pos N, in either orientation. **Only the LED has a
polarity to get wrong.** A reversed LED sees at most 3.3 V against a 5 V
V_R absolute maximum, so it is not damaged — it simply never closes, a
silent failure on the bench and exactly what the per-device test the series
pair exists for is there to show.

**Pin-1 mark — DRAWING grade, not text.** The outline drawing shows a
circular dimple on the body at one corner and the wiring diagram draws a
circle beside pin 1; no sentence in the document says the dimple is pin 1.
Confirm on the part from the printed label, which reads `212EH` (Panasonic
omit the `AQY`), with the pin diagram on the catalogue's first page.

**And the 2026 schematic DRAWS the body diodes** — one per FET, opposed —
which the 2011 catalogue and the 2005 mirror did not. It still characterises
neither, so the "voltage arithmetic" paragraph below stands as an estimate.

**Current budget.** With the photoMOS closed, the only path is +5 V → R45
(100 R) → direction line → **the board's own pull-down**, so the current is
**bounded** at ≤50 mA / 250 mW against the IRM-03-5's 3 W, and bounded by the
machine's resistor rather than by anything of ours.

**That bound is NOT the operating point, and this document read it as one.**
50 mA requires the direction line at 0 V — a dead short to ground, the
discredited topology above, not a press. A real press pulls the line **high**
through the board's own pull-down.

**The press current is ~0.1-1 mA and the range is the honest answer.**
`docs/phase-0-status.md` is normative here and says the pull-down is **not
determined**: ≥~50 kΩ *if* the pen meter resolves 10 mV on its 20 V range,
but **~5 kΩ at 100 mV resolution**. That is 100 µA against ~1 mA — a factor of
ten, and a draft of this paragraph quoted only the favourable branch as if it
were the record. An STM32F030's internal pull-downs are 30-50 kΩ (40 kΩ typ),
which would put 50 kΩ at the *top* of the range, not the floor.

Either way it is **two to three orders below the 50 mA bound** and every
conclusion here survives at both ends. Finish it if the number ever matters:
`R_pulldown = 100 × (5 − V)/V` needs a millivolt reading across the closed
switch.

A **photoMOS relay** (AQY212, TLP222A) across common↔direction is a genuine
contact replacement: bidirectional, no saturation offset, optically isolated.
It does what the lever does. Its on-resistance is irrelevant here — a few ohms
against a loop of ≥5 kΩ, not against R45's 100 Ω as first written. **Its
off-state leakage is at most 1 µA** (Panasonic, at V_L = max; **the table gives
no typical for the AQY212EH** — the 100 pA typical in the family sheet's
feature text is the AQY210EH's. **Plot 9-(2) does show an AQY212EH leakage
curve** at 25 °C: a sample curve on a log axis, read here only to an order of
magnitude, roughly 1-100 pA over 0-60 V), which is what makes an
un-commanded device a non-conductor:
1 µA into 5-50 kΩ is 5-50 mV at the direction line — **at 25 °C only**: the
leakage table is headed "Ambient temperature 25 °C", junction leakage rises
steeply with temperature, the box sits near a boiler, and no document gives
the slope. 5-50 mV is far under any logic-low threshold (the plan quotes no
VIL figure for the machine's STM32F0, only its VIH of ≈1.79 V, **which is
itself unsourced** — it appears only in this plan, in two places, and is not
derived from the usual forms at 3.3 V (0.5·VDD+0.2 = 1.85 V; 0.7·VDD =
2.31 V). The STM32F0 FT-pin form, recalled from memory and NOT verified, is
0.475·VDD+0.2 ≈ 1.77 V — close to 1.79 V, so the figure may well be right;
it is simply unsourced in this repo. Read the STM32F030 datasheet before
leaning on any of it).
*(This sentence said "nanoamps … 1 nA into even 50 kΩ is
50 µV" until 2026-10-20, and a first correction called that "a thousand times
better than the one datasheet figure" and denied any typical exists. Both
overreached: the figure sat between the plotted picoamps and the 1 µA maximum,
so it was consistent with the datasheet's plot, merely about 100× pessimistic
against it; and plot 9-(2) is a datasheet-grade curve the correction missed.
The safe basis for the series-pair argument is still the 1 µA maximum, which
is also the figure that matters when the box is hot, and the argument does
not need better.)*

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

**Two dual 74HC221** for the drive channels, **plus a third for the
arm-window gate** (proposed 2026-10-30, footprint reserved), and **four
photoMOS** — two in series per driven line,
decided 2026-10-12, one '221 channel per photoMOS, decided 2026-10-29; see the
failure analysis above. **Phase 1 needs a separate drive path per photoMOS**
for the per-device test. **Costed since 2026-10-29**: the driver transistors
and their resistors are specified (see below and `docs/shopping-list.md`), the
GPIO budget is **not** the constraint for the levers (the setpoint buttons
are another matter, below), and the one-shot channels are settled —
a dual '221 gives two, one per *line*; one per *device* takes two chips, and
ten are held. *(This read "One dual 74HC221 … what remains unpriced is
one-shot channels … Three '221s are on the order, which covers it" until
2026-10-29 — three was an estimate, never ordered as three; the decision is
under "The one-shot channels WERE the constraint" below.)*

#### A SECOND Phase 1 candidate: the setpoint buttons — scoped 2026-10-23, nothing built

**The question "can the ESP change the brew temperature?" has been asked and
is worth recording before it is re-derived.** Reading them is long settled;
driving them is not, and this entry exists so the next person does not start
from the capture files.

**What is settled.** The buttons are **bits 49 and 58 of the 67-bit frame,
active low** — bit 49 LEFT (enters `PrG`, then decreases), bit 58 RIGHT
(increases). Semantics are from the Ascaso manual, not inference, and all
four payload combinations have been observed. `docs/machine-display-bus.md`
is normative.

**THE FIRMWARE DECODES THE BUTTON BITS AND DOES ESSENTIALLY NOTHING WITH
THEM — a draft of this entry said they are "what sets `SETPOINT_TOUCHED`",
which is false.** `display.cpp` latches `touched` from the **blink period**
and from the **`PrG` banner text**; the button bits land in `btn`, whose only
consumer is a `status` printf. So the decode is free and present, and the
setpoint-edit detection would survive the buttons being ignored entirely.
Worth knowing before building on them: the bits are **proven**, not
load-bearing.

**DRIVE THE CONTACTS, NOT THE BUS.** Injecting a frame bit on `ESD1` would
need two things this project does not have:

| | |
|---|---|
| **which end sources the 67-bit frame** | **UNRECORDED.** The buttons sit on the display panel, so display→main is the natural reading — but that is inference, and decoding a frame says nothing about who sourced it. |
| **the drive type of `ESD1`** | **UNTESTED** — the same open question that governs the J5 shunt. Open-drain on its 4.7 K pull-up means a second device can pull it low; push-pull means fighting a driver. |

A photoMOS across each physical switch needs neither. It is the **J2 lever
pattern unchanged** — same AQY212EH, same one-shot, same open jumper in
series with the output — and the bus then serves as readback rather than as
the drive path.

**THE HOLD HAZARD IS NASTIER IN CONSEQUENCE AND LOOSER IN TIMING, and a
draft of this entry got the second half backwards.**

| line | hold hazard | bound |
|---|---|---|
| J2 levers | reprograms that direction's stored duration | **> ~2 s** |
| **button 23 (RIGHT) alone** | **`PrS` — an IMMEDIATE FACTORY RESET** | **~3 s** |

That draft concluded *"the one-shot is more necessary here, not less… the RC
has to be re-run rather than inherited"*. **It does not follow. 3 s is a
LOOSER bound than 2 s**, so a one-shot sized to stay clear of the levers'
~2 s is automatically clear of the button's ~3 s. The RC **is** inherited on
this axis; the levers are what constrain it.

**What IS true is the consequence, and one design cost.** A lever reprogram
is recoverable by reprogramming; a factory reset is not — so the *stakes* of
a stuck-on output are higher even though the *threshold* is further away. And
a one-shot short enough for the levers **forecloses the `SET UP` menu
entirely**, since entering it needs a ~3 s hold: fine if the scope is the
setpoint, a hard limit if anything ever wants a menu parameter.

*(The same draft called `PrS` a "combination". It is button 23 **alone** —
the combination is the `SET UP` gesture, 23 held while 22 is pressed.)*

**THE ATTRACTIVE PART: this is the only actuation path in the project with
closed-loop verification.** Press, and the display reports the new setpoint
on a bus already decoded. Levers and pump are fire-and-hope; here the ESP can
step, read, and step again until it reaches target, with the documented 3 s
of inactivity as the natural commit. That is worth more than the convenience
of setting a temperature.

**Not scoped, not costed, nothing built.** It needs its own design pass:
which contacts are reachable without modifying the display assembly, and
whether the parts fit the board as laid out. **It does not gate anything in
Phase 0.**

**The device count is FOUR, not two, and a draft said "two more photoMOS".**
The J2 pattern this entry calls "unchanged" is **two in series per driven
contact** — that series pair is the whole answer to a photoMOS failing short.
Two buttons therefore need **four devices, consuming all four spares** on the
current order, plus **four more one-shot channels**, where a dual '221 gives
two. Two '221s are on the board and cover the levers — plus a third for the
proposed arm-window gate, whose spare channel is not a drive channel; the
buttons would need
two more chips, four more trigger GPIOs — **eight pins if each channel gets
a `Q` readback like the levers', against five free after the proposed gate
and readbacks — fewer still if J3's NTC ever wants an ADC1 pin from
GPIO9/10, depending on which pins the layout spends — so the GPIO budget
would then bind** — and the board space
for both, none of
which is allocated — the chips are held (ten bought), the rest is not. *(This
said "Three '221s are on the order and cover the levers" until 2026-10-29 —
three was an estimate, never ordered.)* That is the real cost, and it is why
this is a design pass rather than an increment.

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
and it is what amended invariant 1 requires. The LED-supply disable is **also fitted** — see
"Two things that bound the firmware-side failures", which is normative on
the count — for sequencing
reasons rather than because it makes this claim true.

**In Phase 1, with the jumpers fitted, the failure is real and unmitigated.**
Nothing below bounds it, and one suggestion that stood here for four days
does not work:

- ~~a **series fuse or PTC** on the drive leg, so a shorted output becomes an
  open one~~ — **WRONG, and recorded rather than deleted.** Three independent
  reasons, the first of which corrects a figure this document got wrong:

  **The press current is ~0.1-1 mA, not 50 mA.** A legitimate press and a
  permanent short draw **identical** current, because they are the same
  circuit — that much holds whatever the magnitude. But the magnitude matters
  for this mitigation, and the first draft of this bullet put it at ~50 mA.
  That figure belongs to the **discredited ground-rail topology** above, not
  to normal operation. A real press pulls the direction line **high**
  (CLAUDE.md, measured); the loop is 5 V → R45 (100 Ω) → the board's own
  pull-down, which `docs/phase-0-status.md` records as **not determined,
  anything from ~50 kΩ upward at 10 mV meter resolution and ~5 kΩ at 100 mV**.
  So **~0.1-1 mA**, not 50 mA. If 50 mA flowed, the whole 5 V would drop
  across R45 and the line would sit at 0 V — the press would not register at
  all.

  **At ~0.1-1 mA nothing in the fuse or PPTC catalogue responds**, which kills
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
- ~~**the readback that already exists, and is free** — the sense divider on
  the same direction line flags a high that firmware did not command~~ —
  **WRONG, withdrawn 2026-10-12.** 1Cup and 2Cup are **the lines the operator
  presses with the lever**, every shot. The divider cannot distinguish an
  operator press from a shorted photoMOS or a harness bridge — that is the
  whole reason 0c exists. As a fault detector it false-positives on every
  pour. The only sub-case it could catch is a **sustained** high past ~2 s,
  which the firmware flags as `REPROGRAM` — but narrowly: the flag is raised
  **on release only**, for a hold of ≥2 s that has ended. A line already
  high when the firmware first saw it is adopted as a press it did not
  witness and the alarm is suppressed, and a permanent bridge never
  releases, so only the on-demand `status` line shows it: `high?` for a
  bridge that predates boot or `switches on`, `(Ns!)` only for one that
  formed later — and only if the tap reads high at all, which the sense-tap
  paragraph below calls indeterminate (see `machine_io.cpp` and `main.cpp`).
  It was cited three times as a mitigation and is worth close to nothing.

  What *would* work is a readback **qualified by the one-shot**: a high
  outside the ~120 ms window following a commanded trigger, and not
  coincident with a logged lever event. That is real but it is firmware work,
  not a free property of the existing divider.
- **two photoMOS in series per driven line — DECIDED 2026-10-12, four parts.**
  The pair genuinely requires **two** failures: for it to conduct, *each*
  device needs an internal short in the polarity-relevant FET, and one healthy
  device blocks whatever the other does. **[Body-diode-only: a gate-to-drain
  fault conducts by a different route, and the two failures are not
  independent — see "Net" and the 2026-10-19 correction below.]** Against a single-device failure whose
  consequence is **unrecoverable** — the stored timer is reprogrammed within
  2 s and this file already records that opening the switch line cannot stop
  the machine — two more parts at ~€1.50 each is not a close call. Detection
  alone does not help here: it tells you after the damage.

  **CURRENT CONCLUSION (2026-10-20), stated once so the history below need not
  be read to use it.** The pair is **better than one device** — it needs a
  conduction-causing fault in *each* device, and the healthy one blocks any
  single fault — **by an amount nobody has quantified, and not "squared"**.
  **The decision stands: four parts, with the per-device test.** A device can
  fail conducting by more routes than the body-diode picture shows — for
  example a drain-source short of its +5 V-side FET, a gate-to-drain fault on
  that FET (if the unread gate model holds), a full-device short, or a fault
  in the gate drive that leaves the gate charged; this is a list of
  examples, not a census — and the two
  devices' faults are correlated by surge and thermal history. The pair also
  **hides its own first failure**, which only the per-device test finds. The
  datasheet read neither raises nor lowers the case for four parts. The
  paragraphs below are the working behind this, kept because earlier drafts
  overclaimed in both directions.

  **There is no tension with the build rationale, and an earlier draft claimed
  there was.** Fitting the parts now exists so Phase 1 is not a soldering job.
  Building two and later wanting four *is* that soldering job. Four is what
  the rationale implies; the only cost is the price of two parts.

  Three things the pair does not fix, recorded rather than smoothed over:

  - **Two *partial* failures suffice.** An AQY212/TLP222A output is two
    MOSFETs, sources common, body diodes opposed — that is what makes it
    bidirectional. The drive here is **DC of fixed polarity** (pos 1 = +5 V),
    so one shorted FET leaves the other's body diode forward-biased and the
    device conducts at one diode drop. Two such half-failures leave the line at
    roughly 3.6-4.2 V, far above an STM32F0 VIH of ≈1.79 V *(unsourced — see
    the leakage paragraph above)*. *(Figures and their derivation: the
    "voltage arithmetic" paragraph below — an estimate, not a Panasonic number.
    The MACHINE-side conclusion, that the line stays far above the board's
    own VIH, is unaffected by any value from 0.35 to 0.65 V per diode, a band
    wider than the estimate. It is NOT unaffected for the ESP32 sense tap: see
    the "sense tap" paragraph below.)*
    So the chain needs two
    *half*-failures, not two full shorts — **and a single-FET short is a more
    probable event than a full device short, so the pair buys less than
    squaring a full-short probability.**
    **Checked against Panasonic's datasheet 2026-10-17 — topology
    CONFIRMED, one refinement.** The internal schematic for the AQY21•EH
    family (same drawing in both documents read that day — see
    `shopping-list.md` for filenames, dates and hashes) shows **two N-channel MOSFETs, sources joined
    to a common node, gates joined to one gate node, drains to pins 3 and 4**,
    driven by one LED. So "two MOSFETs, sources common" is **datasheet grade**.
    What the 2026-10-17 read did **not** do is draw or specify the body
    diodes; **document C (ASCTB126E, January 2026) draws them** — one per
    FET, anodes at the common source node, so opposed — and characterises
    neither. So **"body diodes opposed" is drawing grade since 2026-10-29**,
    and the diode-drop figures above remain a silicon-junction estimate, not
    a Panasonic number. *(Read "inferred from the MOSFET symbol, not read
    from a table" until 2026-10-29 — true of the 2011 and 2005 documents
    (A and B in `docs/shopping-list.md`), whose symbol carries only the
    source-to-body arrow.)* (`TLP222A` was not checked; the sentence above
    names it only by analogy.)

    **The refinement: the half-failure is polarity-dependent.** *(A first
    version said the draft "missed" this. The section's own lead bullet — "each
    device needs an internal short in the polarity-relevant FET" — already
    carried the idea, so the section was internally inconsistent, not silent;
    what it lacked was the F_hi / F_lo spelling-out and the consequence for
    the off-state stress below.)* Label the FET on the +5 V side of a device F_hi and
    the other F_lo. If **F_hi** shorts, current runs pos 1 → F_hi (shorted) →
    common node → F_lo's body diode (anode = node, cathode = F_lo's drain, so
    forward) → line: the device conducts at one diode drop, as above. If
    **F_lo** shorts instead, the +5 V side meets F_hi's *reverse*-biased body
    diode and F_hi's channel is off: **the device still blocks.** So only one
    of the two single-FET failure sites per device is dangerous, and the pair
    needs **F_hi shorted in both devices**, not "any FET in each".

    **[SUPERSEDED IN PART by the 2026-10-19 review below: this squaring
    assumes independent, equal failure rates, and the "Redundancy is slightly
    better" paragraph further down explains why neither holds. Read the two
    together; the arithmetic stands, its assumptions do not.]**

    **That is up to a factor of FOUR on the pair, not two, and a draft said
    "up to half".** Halving each device's *dangerous*-failure rate squares
    across two devices in series: (F/2)² = F²/4.

    **"Up to" is doing real work and a draft stated the 4× flat.** It halves
    only the *single-FET-short* population. A **full-device short** — both
    FETs — conducts in either polarity and is not halved at all, so the
    device's total dangerous rate is somewhere between F/2 and F depending on
    the mix, which nobody has data on. 4× is the ceiling, not the result. The "up to half" figure is right for a
    single device and was carried to the pair unchanged — the same
    per-device-to-pair slip the "squaring a full-short probability" sentence
    above warns about. It holds only *if* the two FETs fail short equally
    often, which is an assumption, not data, and the common-cause point below
    bites regardless. Orientation of a device in the chain cannot be used to avoid this:
    swapping its pins only swaps which FET is F_hi. The "common cause" and
    "hides its own first failure" points below are unchanged.

    **The voltage arithmetic, and how it moved.** The sentence above first
    read "~0.6 V per diode, ~1.2 V for two, ~4.2 V at the line", which is
    internally inconsistent (5 − 1.2 = 3.8). A first correction then said 0.6 V
    is a ~1 mA figure and moved the drop DOWN to ~0.35-0.45 V at this loop's
    ~0.1-1 mA. **That direction is doubtful, but not shown wrong:** the
    ideal-diode slope is 59.5 mV per decade of current at n = 1, so 0.6 V at
    1 mA implies ~0.54 V at 0.1 mA — but a real junction has n between 1 and
    2 (60-120 mV per decade), and a larger die (this part's R_on is 0.85 Ω)
    has a higher saturation current and so a LOWER drop at a given current.
    The 0.6 V anchor is itself unsourced. **So the honest statement is an
    estimate of ~0.4-0.65 V per diode, ~0.8-1.3 V for the pair, ~3.6-4.2 V at
    the line**, and **no document read gives the AQY212EH's body-diode
    characteristic** *(the January 2026 catalogue draws the diodes, which the
    earlier two did not; it still puts no number on them — 2026-10-29)*. **The 3.6 V low end includes the drop across R45
    (100 Ω) at 1 mA, ≤0.1 V** — 5 − 1.3 alone is 3.7 V.
    **Diode drop also falls ~2 mV/K
    per junction**, so near a boiler at 60-85 °C each diode could be 70-120 mV
    lower than at 25 °C and the line 0.14-0.24 V HIGHER than the band above —
    the same temperature argument made for the leakage figure. The conclusion
    that the line stays far above the board's VIH holds across all of that.

    **BUT ONE CONSEQUENCE DOES DEPEND ON IT: THE SENSE TAP.** Through the 150
    K / 220 K divider (×0.5946) a line at 3.6-4.2 V puts the pin at ~2.14-2.50
    V. Against that: this repo uses an ESP32-S3 VIH of **2.475 V** nominal and
    **2.60 V** at a 5 %-high rail, and its own bench reading of a lever press
    was **2.89 V against a predicted 2.97 V** (~3 % low) — scaled the same
    way, the top of the band is ~2.43 V. So **at 25 °C the whole band sits
    below 2.60 V and all but its extreme top end below 2.475 V: the tap
    PROBABLY DOES NOT READ HIGH.** **At box temperature that statement does
    not hold, and it is stronger than the evidence.** The temperature
    coefficient above is assumed, not measured here, but if it applies the
    line moves to ~3.7-4.4 V and the pin to ~2.2-2.64 V (~2.16-2.57 V after
    the measured ~3 % shortfall), so nominal VIH of 2.475 V sits INSIDE the
    band. And the verdict depends on the diode estimate this section calls
    unsourced: at 0.35 V per diode (the lower figure an earlier draft used, and
    outside this section's own 0.4-0.65 V estimate) at 25 °C the line is
    ~4.2-4.3 V and the pin ~2.50-2.55 V unscaled, ~2.43-2.48 V after the
    measured shortfall — ON the threshold, not clearly above it; combine it
    with the hot case and the pin is ~2.5-2.6 V scaled, up to ~2.7 V unscaled —
    at or above 2.475 V either way. **So the honest
    statement is: probably not high at 25 °C, INDETERMINATE at box temperature
    and across the diode estimate's own uncertainty.** Between VIL (0.25·VDD =
    0.784 V at a 5 %-low rail, as `docs/taps-and-dividers.md` uses; 0.825 V at
    nominal 3.3 V) and VIH a GPIO read is
    **undefined**, not "LOW" — a first version of this paragraph said "LOW".
    The old ~4.2 V figure gave ~2.50 V at the pin, i.e. exactly on the
    threshold, so this is not a new problem; it was borderline and never
    stated. A lever press reads 2.89 V against under ~2.5 V here, so the two
    levels differ — but this repo's taps are digital inputs and nothing here
    claims the divider detects the failure. See the withdrawn readback bullet
    under "The failure neither mechanism bounds", and note the "already a
    detector" sentence under "What this changes in the harness" is the same
    withdrawn idea.

    **What the old text got wrong, and why it was believable:** it called the
    failure "one shorted FET leaves *the other's* body diode forward-biased"
    as though that held for either FET. It is the standard photoMOS picture
    and the conclusion (two half-failures leave the line far above VIH) holds
    for the case it describes; it just omitted that the other case blocks.

    **"Redundancy is slightly *better* than the draft said" WAS TOO STRONG,
    AND A 2026-10-19 REVIEW SHOWED WHY. It holds only under equal, independent
    failure rates, and both assumptions lean the wrong way:**

    - **F_hi is the stressed FET — in a single device.** With 5 V across one
      device and the line at 0 V, F_hi's channel is off and its body diode
      reverse-biased, so F_hi carries essentially the whole off-state voltage;
      F_lo's diode is forward and F_lo carries about a diode drop at most.
      **In the series pair it is a fraction of the 5 V**, set by the floating
      middle node (leakage at DC, capacitive division for a fast transient)
      until the other device fails. And 5 V is ~8 % of a 60 V part's rating,
      so "its short rate is plausibly *higher* than F_lo's" is **speculation**,
      not a finding — hence "plausibly", and nothing here is quantified.
    - **A surge can stress the same FET in both devices at once — and which
      FET depends on the surge's polarity.** A positive-going transient on the
      +5 V side loads F_hi of both devices; a negative-going one swaps the
      roles and loads F_lo of both, which are the FETs whose short *fails
      safe*. Correlation across the two devices is directionally right for a
      fast transient (the off-capacitances are similar), so the common-cause
      point below is **not** a separate caveat: it is the same two FETs, and
      it erodes the squaring the factor-of-four argument relies on — for the
      polarity of surge that matters.
    - **A drain-source short is not the only way to conduct.** The two gates
      are joined. **The model below assumes the gate drive is source-referenced
      and floating (an isolated photodiode array) — that is INFERENCE: neither
      document draws or describes it, the schematic shows only a light arrow
      onto the joined gates, and real PhotoMOS parts also contain gate-discharge
      circuitry that nothing read shows. The conclusions that follow depend on
      that unread structure.** A gate-to-drain failure of **F_hi** holds
      the shared gate near the +5 V side, about 5 V above the low-side
      terminal, so **both** channels turn on: one failure site per device, no
      body-diode logic involved. A gate-to-drain failure of **F_lo** ties the
      gate to the low-side drain, holds both channels off, and **fails
      safe-open** — so the F_hi-only restriction applies here too, and **the
      polarity refinement is not irrelevant to this mode** *(a first version
      said it "does nothing for it", which is wrong)*. What this mode adds is
      a failure population that is **not a drain-source short and is not
      limited by any body diode**. Nobody has data on its rate against a D-S
      short; it is **not** excluded by anything read.

    **Net — RELATIVE TO THE EARLIER DRAFT'S "slightly better", not to a
    single device:** the polarity refinement is real and removes one failure
    population, but the honest statement is "somewhere between a little better
    and no better than that draft said". **A pair still needs two
    independent-enough failures where one device needs one, so it remains
    better than a single device** — what is in doubt is *how much*, and the
    squaring the factor-of-four paragraph relies on. **The datasheet read
    neither raises nor lowers the case for four parts** — what the pair does
    not give is unchanged.

    **Two statements elsewhere in this section are body-diode-only and do not
    cover the gate-to-drain mode above:** the lead bullet ("each device needs
    an internal short in the polarity-relevant FET") and the diode-drop
    arithmetic. *(The "~1.2 V" in the "what the old text got wrong"
    paragraph was changed to "~0.8 V" in the first review-fix commit without
    an annotation, and that figure was itself wrong; it now carries no drop
    figure and defers to the voltage-arithmetic paragraph earlier in
    this section.)*
  - **Common cause.** Two identical parts from the same reel, in series, in
    the same thermal environment, seeing the same surge history on the same
    node. A surge on J2 — the plausible cause of an output short — presents
    across the pair. **A positive-going surge loads F_hi of both devices;
    a negative-going one loads F_lo of both, whose short fails safe — see the
    2026-10-19 correction above, which makes this the same correlation, not a
    separate caveat.** "Standard safety-chain move" normally implies *diverse*
    redundancy; this is its weakest form.
  - **THE PAIR HIDES ITS OWN FIRST FAILURE, and this is the part that
    constrains the layout.** One device shorts, the healthy one still blocks,
    the line stays low, and **nothing is visible** — including to the sense
    divider, which is why that readback does not cover the pair. You find out
    when the second device goes, by which point the redundancy that was
    supposed to protect you has been absent for an unknown length of time.

    **What catches it is a per-device test**: energise device 1's LED alone —
    the line must stay low, because device 2 blocks. Then device 2 alone. Only
    both together make a press. Three consequences, all real:

    - **The two LEDs in a pair cannot share one drive.** Each needs its own
      path — and since 2026-10-12 that is settled hardware, not just a
      constraint: each photoMOS gets a **BC547 or 2N3904** with an 8.2 kΩ
      base resistor and a 100 kΩ base-emitter resistor, fed through
      **330 Ω, 1 %** for ~6 mA. AQY212EH is guaranteed to operate only at
      **3.0 mA at 25 °C**
      (typical 1.2 mA is not a design figure; `docs/shopping-list.md`, the
      "85 °C note" under "Datasheet read", is normative on what the
      temperature caveat does to these figures).
      <!-- fact:led-drive-resistor 330 Ω, 1 % -->
      <!-- fact:photomos-guarantee-25c 3.0 mA at 25 °C --> **The transistor's reason is
      drive margin.** The '221 can source ~4.5 mA into this LED at 3.3 V —
      over the 3.0 mA guarantee at 25 °C, but **below the 5 mA floor of
      Panasonic's recommended range** (5–30 mA in C, 2026; 5–10 in the
      2011/2005 documents) and only ~1.45× the plotted 85 °C curve, dividing
      its 25 °C current by the 85 °C operate current; the BC547
      puts it at ~6 mA nominal, inside the range, and ≈ 4.3 mA at the 25 °C
      worst-case corner, which is not. `docs/shopping-list.md` ("Datasheet
      read") carries both BC547 figures and criterion B's 85 °C worst case.
      *(This said "the transistor's reason is this test, not the drive
      current" until 2026-10-29 — true while one '221 channel drove both LEDs
      of a pair; with one channel per photoMOS each LED has its own path
      without a transistor. The older retraction stands: a draft's "a 74HC221
      at 3.3 V cannot source that reliably, so the transistor was needed
      regardless" over-claimed — `CD74HC221E` lists 5.2 mA drive and does
      deliver ~4.5 mA here.)*
      **And not a 2N7000** — V_GS(th) 0.8–3.0 V
      leaves a worst-case part 0.2 V of overdrive from 3.3 V logic. 

      **Free GPIOs are not the constraint.** Against the pinout in
      `docs/board-and-perfboard.md`:
      9-14, 18, 21 and 47, four of which go to the gate trigger and the last
      three `Q` readbacks — **38 and 39-42 were on this list until
      2026-10-29 and are now the gate enable and the four triggers, and 1
      and 2 are the first two `Q` readbacks** (CLAUDE.md's drive table) — (33-37 are octal-PSRAM
      internal on this N16R8 module, *"35-37" until 2026-10-30*;
      0, 3, 19, 20, 45, 46 are strapping or native USB; 43/44 are the UART0
      console, which `platformio.ini` names as the fallback flashing port, so
      treat them as spoken for).

      **GPIO38 IS NOT THE LED AND GPIO48 IS SPENT — measured 2026-10-15**
      *("GPIO38 IS FREE" until 2026-10-29: free of the LED, which it still is;
      no longer unallocated, since the drive table spent it on the arm gate
      — and since 2026-10-30 it is the proposed gate *enable*, the gate '221's `R̅`,
      not the drive channels' reset)*,
      closing
      the "38 or 48 depending on board revision" hedge this list carried from
      2026-10-12. The onboard RGB LED is on 48, by a **two-point** test with
      the board on USB, no machine connections and **the yield Bookoo bound
      and awake** (without it both rows are dark and the test says nothing):
      `yield: state=3` with `detector=ARMED` gave dim green, and `state=4`
      (LOST), the scale switched off, gave dark — the two things `updateLed()`
      writes for those states.
      **One point was not enough**: green alone is what a green power
      indicator beside a dark RGB LED would also show, and a draft of this
      line freed GPIO38 on exactly that. `docs/board-and-perfboard.md`
      has the full grading.

      So the draft that listed 38 as free was right by accident, and the
      correction that hedged it was right to hedge. Neither was measurement;
      this is.

      **The one-shot channels WERE the constraint, and the count is DECIDED
      2026-10-29: one channel per photoMOS — FOUR channels, TWO dual
      '221s.** A dual '221 gives two, enough for one per *line*; the
      per-device test this passage argues for needs one per *device*, so two
      chips. Ten `CD74HC221E` were bought (Reichelt, 2026-10-27), so stock is
      not a constraint either. **A third chip carries the proposed arm-window gate
      (2026-10-30)** — one channel, the other spare; see "Arm window".
      *(This read "a second '221 or a different
      gating scheme is unpriced; three are on the order" until 2026-10-29 —
      three was an estimate, and three were never ordered.)*
    - **Each test path must still be one-shot bounded.** If a per-device drive
      is a bare GPIO, the ~120 ms ceiling does not exist and a stuck test pin
      holds the line past 2 s. And if the pair is fired by **two independent**
      RC one-shots in normal operation, the real press is their **overlap** —
      materially shorter than either, against a minimum latching width nobody
      has measured.
    - **The test is not free when it finds something, and "the board latches"
      is not established.** If device 1 has already failed short, energising
      device 2 alone *is* a press.

      **Whether a ~120 ms press starts a cycle is still unknown, but the
      reason has changed and this passage stated the old one until
      2026-10-19.** It read *"the shortest press this board latches has never
      been measured, and every press ever recorded was 345-480 ms"*. Both
      halves are now false: 2026-10-19 logged presses **observed to latch** at
      a poller-measured 80-90 ms, so the threshold is **below ~140 ms** once
      the poller's 42-57 ms quantisation is carried through.

      **That does NOT clear ~120 ms.** The upper end of the quantised band is
      137 ms, which is longer than 120, so the margin may be negative — see
      CLAUDE.md. The question is open on a **tighter** bound than before, not
      on no bound at all.
      So either it runs a cycle (the spurious cycle is the alarm,
      at the price of an unwanted flush) or it does not (and the detection
      story needs the sense line read directly instead). **Measure the minimum
      latching width before relying on either reading.**

    **Off-machine testing commissions the pair; it does not replace this.** A
    draft called it "the better first pass", which reads as a substitute. The
    failure this test exists for **accumulates in service** — the whole point
    is that the pair hides its first failure for an unknown length of time —
    so a one-off bring-up check cannot catch it. Both are wanted: off-machine
    at commissioning, periodic thereafter.
- a **watchdog relay** in the LED supply that firmware must keep re-arming,
  so a crash de-energises the drive path rather than leaving it as it was.
  Note this shares the fuse's blind spot — it is on the control path, so it
  does not bound a shorted output either.
- at minimum, **record it as a known unmitigated failure** rather than let
  "two things make it provably safe" stand as the whole story.

#### Two things that bound the firmware-side failures

- **THREE jumpers — DECIDED 2026-10-12. This is the normative statement of
  the count and placement; CLAUDE.md does not restate it.** An intermediate
  draft replaced the LED-supply jumper with a drive-leg jumper; they are not
  nested and both are wanted.

  | # | Where | Why |
  |---|---|---|
  | 2 | **one per driven line, in the drive leg**, on the **pos-1 side** of that line's photoMOS pair | the only disable that survives an output failing short — invariant 1 requires a series open in every drive output. One per *line* covers both devices of the pair, because a series open is a series open; per-device jumpers buy nothing |
  | 1 | **shared, in the photoMOS LED supply** | sequencing, not evidence — see below |

  **The pos-1-side placement has one good reason and one that does not
  work.** Good: in Phase 0 the photoMOS then sits with **no voltage across
  it**, rather than with the live machine common permanently on one terminal.
  Does not work: a draft said an open there "is still an open when the part
  fails short" and called it "strictly less exposure" — a series open blocks
  identically on either side, so that discriminates nothing, and for the
  *harness-bridge* hazard this placement is mildly **worse**, because it
  extends the live 5 V node onto a bare header pad inside the box.

  **The LED jumper's justification is sequencing, and a draft got it wrong.**
  That draft said it makes "firmware has demonstrably never commanded a fire"
  true. It does not: with the supply open a latent firmware fault is **exactly
  as invisible** as with it closed — the '221 still asserts and nothing
  records it. The real argument is about **the order in which risk is taken
  on**: closing the LED jumper is a deliberate act you can perform
  off-machine, on the bench, watching the '221 output, with J2 unplugged.
  Fitting a drive-leg jumper is done with the machine connected. So the LED
  jumper is where a firmware fault gets **found**, under observation, instead
  of being discovered by the machine. **Fit it first, on the bench; fit the
  drive-leg jumpers last, at the machine.**

  **And between those two: solder the timing RC — DECIDED 2026-10-29.** The
  sweep runs on 2-pin headers, on the bench, with every drive-leg jumper
  open. The chosen `R_ext`/`C_ext` pair is then **soldered before any
  drive-leg jumper is fitted**, and never socketed in service. The reason is
  the failure direction, and it inverts the photoMOS-socket argument in
  `docs/shopping-list.md`: a socketed photoMOS going intermittent fails
  **open** (safe), whereas the pulse ends only when `C_ext` charges through
  `R_ext`, so an intermittent `R_ext` leaves `Q` high — **a HOLD**, the one
  thing the one-shot exists to prevent *(circuit-confirmed 2026-10-29 from
  SCHS166F's logic diagram — see the datasheet facts under "An arm gate"
  below; an open `C_ext` fails the OTHER way, to a pulse too short to latch,
  so the PAIR is soldered, not the resistor alone)*. A socket on the timing resistor would put the project's
  worst-case failure behind a spring contact.

  *(A draft said "remove the LED one last", which inverts this and is also
  unbuildable — the specified state is headers with no shunt fitted, so there
  is nothing to remove. The act is fitting.)*

  **"Two electrical domains" was overstated.** A draft argued that machine
  5 V against our 3.3 V logic means "one contaminated pad cannot close both".
  Closing a jumper is a **mechanical** event; the potential on the pads has
  nothing to do with whether one blob can span them. What actually requires
  two faults is that both a drive-leg jumper **and** the LED jumper must
  close, *and* a photoMOS must then conduct. The property worth designing for
  is **physical separation** — put the jumper fields apart on the board, and
  note that adjacency would create a hazard this section otherwise misses: a
  bridge across both fields shorts machine 5 V onto the 3V3 rail.

  **Use 2-pin headers with NO shunt fitted — not DIP switches.** The
  invariant's virtue is being checkable by eye, and bare pins with nothing on
  them is the most visible open state there is; a DIP position is small and
  easy to misread. **Keep spare shunts out of the box.** Note the check means
  opening a glanded, cable-tied enclosure.

  Note the drive-leg jumpers are not firmware-side bounds, despite this list's
  heading: they bound everything while open and nothing once fitted. The LED
  jumper and the arm gate are the firmware-side ones.

- **An arm gate.** Gate the one-shot with a second GPIO that firmware must
  hold high, so one stuck or floating trigger pin cannot fire anything. *(This
  read "AND the one-shot output with a second GPIO" until 2026-10-29 — the
  external-AND-on-`Q` mechanism the table below rejects, stated as the
  bullet's own headline.)*

  **MECHANISM DECIDED 2026-10-29: the '221's own `R` (reset) input, on
  GPIO38, held high to arm — `B` tied to 3V3 on all four channels.** *(The
  first draft of this decision, the same day, chose `B`; a pre-push review
  read the datasheet and found `R` bounds strictly more for the same zero
  parts — the third row below.)* Three ways to build the sentence above, and
  they are not equivalent:

  | | cost | what it bounds |
  |---|---|---|
  | the '221's `B` input, tied across all four channels, held high to arm — **superseded same day** | no extra part — the '221 needs `B` high to accept a falling `A` | a stuck or floating **trigger** pin, which is exactly the failure the sentence names. **Nothing more: once a channel has fired, `B` is powerless until the RC times out** |
  | an external AND on each `Q` output (**a 74HC08 — a seventh package and socket**) — **declined** *(said "a fifth IC" until 2026-10-29, the single-'221 count)* | one more DIP-14 | the trigger pin, **and** every `Q`-held-high state while disarmed: a '221 whose `Q` is stuck by internal failure, **and the timing-circuit failures** — `R_ext` open or intermittent, `C_ext` leaky, a bad joint on the `RxCx` pin. *(The first draft listed only the chip failure here, which undercounted what was being declined.)* |
  | **the '221's `R` input, tied across all four channels, held high to arm — CHOSEN** | no extra part | everything `B` bounds (`R` low blocks all three trigger edges), **plus every `Q`-high state the timing circuit produces** (`R_ext` open or intermittent, `C_ext` leaky, a bad joint on the `RxCx` pin), **provided firmware disarms unconditionally** — arm → trigger → wait the fire window plus margin → drop `R̅`, every time. `R` low ends a running pulse at once, including the `R_ext`-open HOLD, which the comparator never ends but reset overrides — so the HOLD is bounded to the arm window **with no detector at all**. *(This row cited the J2 sense divider as the detector for "high with nothing commanded" until 2026-10-29; that detector was withdrawn 2026-10-12 — see "the readback that already exists" under "The failure neither mechanism bounds", and the struck bullet under "What this changes in the harness".)* |

  **`R` wins. What it does not bound: a '221 whose `Q` is stuck high by
  internal failure** — that stays on the known-unmitigated list beside the
  shorted photoMOS output and the watchdog's blind spot. The 74HC08 would
  bound that one, by moving the single point onto a chip that can fail the
  same way; a part that relocates a failure is not worth a socket, now that
  `R` takes the timing-circuit failures off the AND's side of the ledger for
  free.

  **PROPOSED 2026-10-30, not decided: drive the tied `R̅` net from the
  `Q` of a third '221 channel, the gate, with GPIO38 enabling the gate.**
  The row above says the HOLD is bounded to the arm window "provided
  firmware disarms unconditionally"; the gate would bound **each window**
  without that proviso, but not the gap between windows, and an open gate
  `R_ext` falls back to firmware. It also costs something the GPIO-driven
  net did not: a gate `Q` stuck high arms all four channels with no
  firmware disarm. See "Arm window" and its OPEN block below. *(Headed
  "AMENDED" and said the gate's RC "ends the window whatever firmware
  does" until review the same day.)*

  **The datasheet facts this rests on — DATASHEET grade, TI SCHS166F
  (CD54HC221 / CD74HC221 / CD74HCT221, Nov 1997, rev Oct 2003), read
  2026-10-29.** Until then the trigger edges were recollection. Truth table,
  per channel:

  | `A̅` | `B` | `R̅` | `Q` |
  |---|---|---|---|
  | H | X | H | L — no pulse |
  | X | L | H | L — no pulse |
  | L | ↑ | H | **one pulse** |
  | ↓ | H | H | **one pulse** |
  | X | X | L | L — **reset; ends a pulse in progress** |
  | L | H | ↑ | **one pulse** (datasheet note 1 — the cells print "Note 3", but the only note on the page is numbered 1: reached only through the A-low / B-high sequence) |

  **TI writes the trigger and reset pins with overbars — `A̅` and `R̅`, active
  low — and the H/L entries above are PIN LEVELS, transcribed as the
  datasheet prints them.** This repo writes `A` and `R` in prose for the
  pins; where it says "`A` idles HIGH" or "`R` held high to arm" it means the
  pin, and high is the inactive state of both. *(The table header read `A` /
  `R` without the bars for part of 2026-10-29, which left a reader checking
  it against the PDF unable to tell pin level from logical sense.)*

  And in prose: *"Once triggered, the outputs are independent of further
  trigger inputs on A̅ and B. The output pulse can be terminated by a LOW
  level on the Reset (R̅) pin."* Also: *"On power up, the IC is reset"*; `B`
  has a Schmitt trigger and `A` does not; unused inputs must be terminated
  high or low; `R_X` minimum typically 500 Ω; `t_W = 0.7 R_X C_X` at
  V_CC = 4.5 V. **Three trigger edges, as recollected — but the `R` edge
  needs `A` low AND `B` high**, which the recollection left out and which is
  what makes `R` safe as the arm line under the rule below.

  PDIP-16 pinout, same source (page 1, top view, and the page-2 functional
  diagram agree): **1** 1A̅ · **2** 1B · **3** 1R̅ · **4** 1Q̄ · **5** 2Q ·
  **6** 2C_X · **7** 2C_XR_X · **8** GND · **9** 2A̅ · **10** 2B · **11** 2R̅ ·
  **12** 2Q̄ · **13** 1Q · **14** 1C_X · **15** 1C_XR_X · **16** V_CC. TI
  labels the reset pin `R̅`; `CLR` is the SN74 name for the same pin.
  **So the true output of channel 1 is pin 13 and of channel 2 is pin 5;
  pins 4 and 12 are the complements.**

  **This list had Q and Q̄ SWAPPED on both channels for part of
  2026-10-29** — "4 1Q · 5 2Q̄ · 12 2Q · 13 1Q̄" — at datasheet grade, in the
  table CLAUDE.md sends a builder to for exactly this. Built from it, each
  BC547 base lands on the complement: the LED is ON while idle and ON while
  DISARMED (`R̅` low forces Q̄ high), and goes off only for the pulse. The
  arm gate would have turned the drive on. Caught by a pre-push review of
  the commit that introduced it, before any wire; recorded because a
  transcription error in a datasheet-grade table is the one kind the grade
  does not warn you about.

  **`A` MUST IDLE HIGH, and it must do so across a reset.** With `B` tied
  high, raising `R` fires a pulse on any channel whose `A` is low at that
  moment; with `A` high the `R` edge is inert. The ESP32-S3 cannot hold `A`
  high by itself: per its datasheet (v2.2, §2 pin table), GPIO38–42 have
  **no pull and no input-enable during reset** (Table 2-1, "At Reset" blank);
  after reset they are inputs, no pull on 38/40/41/42 and a weak pull-up on
  39 only while `EFUSE_DIS_PAD_JTAG` is 0 *(said "input-enabled with no pull
  at and after reset" until 2026-10-29)*.
  Opening the serial port resets the ESP32 without power-cycling the '221,
  so on every flash and every monitor session the '221 sits powered with
  those inputs floating unless something external holds them. **So,
  passive, no firmware involved — DECIDED 2026-10-29:**

  - **10 K pull-up to 3V3 on each `A` line, ×5** *(×4 until the gate,
    2026-10-30 — the gate's `A̅` is the fifth)* — idle high through every
    reset; a GPIO pulling low sinks 0.33 mA. `A` has no Schmitt trigger,
    which is the argument for 10 K over the 100 K also held.
  - **10 K pull-down on the `R` arm line, ×2** *(×1 until the gate,
    2026-10-30)* — one on GPIO38, the gate's `R̅`; one on the four drive
    channels' tied `R̅` net, which the proposed gate's `Q` would drive, so it is low with
    the gate chip pulled. Disarmed at power-on, after a system reset, and
    with the devkit pulled from its headers. **Not necessarily after a
    panic**: a panic restart is a CPU reset that may leave GPIO38 driven
    high, which no pull-down overrides — there it would be the proposed gate's own RC that
    disarms.
  - **`B` tied directly to 3V3, all four channels and the gate** — a Schmitt input that
    must not float; a permanent high.
  - 10 K: **25 held, counted by the operator 2026-10-29** *(read "count not
    taken" for part of that day)*; `docs/shopping-list.md` §4 carries the row.
  - **Firmware ordering, for when drive code exists.** Write each trigger
    HIGH **before** making it an output (`digitalWrite(pin, HIGH)` then
    `pinMode(pin, OUTPUT)`): on arduino-esp32 2.0.17 `pinMode` sets the mode,
    not the level, so the reverse order can drive `A̅` low for an instant —
    which per SCHS166F fires an armed channel or primes a disarmed one for
    the next `R̅` rise. The external 10 K pull-ups do not cover this: a GPIO
    actively driving low overrides them. **Never let `R̅` rise without first
    reading every `A̅` it gates high** — with the proposed gate, the drive
    `R̅` rises when the gate fires, so the four drive `A̅` lines are read
    immediately before the gate trigger, and the gate's own `A̅` before
    raising GPIO38. *(Core behaviour read from
    `esp32-hal-gpio.c`, framework-arduinoespressif32 3.20017.241212,
    2026-10-29: `__pinMode` → `gpio_config()` only, `__digitalWrite` →
    `gpio_set_level()`; the output latch's reset value of 0 is assumed, not
    checked against the ESP32-S3 TRM.)*
  - **Arm window: bounded in HARDWARE by a third '221 channel, the GATE —
    PROPOSED 2026-10-30, NOT DECIDED.** Three fresh reviews in one day each
    found design errors in this and the two bullets below; the open ones
    are listed under "OPEN — the gate, readback and `drivetest`". **What
    the layout acts on is only the footprint**: a third DIP-16 for the gate,
    its RC, its pulls and its two pins. The behaviour is a proposal until
    the bench sweep supplies the numbers it depends on. *(Two drafts the same day bounded it in firmware
    instead — first a permanent 1 s loop watchdog, then a timer ISR plus a
    per-window watchdog, a panic-handler wrap and a shutdown handler. Two
    fresh reviews found the firmware layers either rebooted the board on
    ordinary work or could not give the bound: a non-IRAM timer ISR is held
    off by every flash write, a 5 s watchdog cannot enforce a ~2 s limit,
    and an esptool or monitor reset runs none of the hooks. See "What the
    firmware still does" below for what survived.)*

    Only `R̅` low ends an `R_ext`-open HOLD, so whatever drives the four
    channels' tied `R̅` net decides how long a HOLD can last. **The
    proposal drives that net from the `Q` of a gate channel, not a GPIO:**

    - **The gate is channel 1 of a THIRD `CD74HC221E`** (ten are held).
      Its `A̅` is a new GPIO, the **gate trigger** — 10 K pull-up to 3V3,
      pulse low to fire, the same idle-high rule as the four drive `A̅`
      lines. Its `B` is tied to 3V3. Its `R̅` is **GPIO38**, now the **gate
      enable**, with **a new 10 K pull-down of its own**. Its `Q` drives
      the four drive channels' tied `R̅` net, which **keeps the existing
      10 K pull-down** — two pull-downs, nothing moved — so the net is low
      with the gate chip pulled from its socket (but see the readback
      caveat in OPEN).
    - **Channel 2 of that chip is spare, terminated**: `A̅` high, `B` low,
      `R̅` low, as SCHS166F requires of unused inputs.
    - **Arming is**: read the gate's `A̅` high → raise GPIO38 (inert) →
      read the four drive `A̅` lines high → pulse the gate trigger low → the gate's `Q`
      goes high and arms the four channels → fire the drive triggers →
      **wait until every drive channel's `t_max` sample has been taken,
      plus margin** → drop GPIO38, which ends the gate pulse early.
      *(A draft, 2026-10-30, went straight from "fire" to "drop GPIO38" —
      that resets both drive pulses microseconds after they start, so no
      press ever latches, and it masks an open drive `R_ext` from the
      `t_max` check. The wait is the step the 2026-10-29 sequence had as
      "wait the fire window plus margin".)*
    - **If firmware never drops GPIO38 — a hang, a panic, a core dump to
      flash, any reset at all — the gate's own RC ends the window.** The
      '221 is non-retriggerable, so holding or toggling the gate trigger
      during the pulse does not extend it, and a stuck GPIO38 alone arms
      nothing: raising `R̅` fires only with `A̅` low. **So no single gate
      pulse outlasts its RC, whatever the ESP32 does. That bounds each
      window — NOT the gap between windows.** Firmware that re-fires the
      gate the instant it times out gives `Q` a low gap only as long as its
      own latency, and a drive channel whose `A̅` is low at that moment
      re-fires on the `R̅` rise. A gap of microseconds is shorter than the
      photoMOS turn-off and invisible to the machine's switch polling, so
      back-to-back windows can still read as one press past ~2 s. **Nothing
      enforces a minimum gap yet — OPEN, below.** *(This said "no HOLD
      outlives one gate pulse whatever the ESP32 does" until review the
      same day. That a `Q`-low gap of any firmware latency clears the drive
      channels is itself inferred: no `t_w(R̅)` from SCHS166F is recorded
      here, and none at 3.3 V.)*
    - <!-- fact:arm-window-cap@1 500 ms -->
      **The gate's pulse is at most 500 ms at its worst tolerance corner**,
      and at least every drive channel's `t_max` sample plus margin — not
      merely the fire window, because the drive `t_max` check needs the
      gate still high. Like the drive channels' RC it is chosen at the bench
      sweep and **soldered**, never socketed in service; its values are
      OPEN, below.

    **What the gate does not bound: its own timing failure.** An open or
    intermittent `R_ext` on the GATE holds the `R̅` net high until GPIO38
    drops — back to a firmware bound. That needs two independent faults
    (a gate RC fault and a firmware fault in the same window), the gate's
    `Q` is read back like the others, and `drivetest` times the gate
    before the first press of every boot (below). A `Q` stuck high by
    internal failure in any '221 is still unbounded and stays on the
    known-unmitigated list.

    **What the firmware still does — backstops, not the bound:**
    - **A timer ISR drops GPIO38 at a firmware cap**, so a gate RC fault
      alone is still bounded. The cap must sit **after** the gate's own
      `t_max` sample — at 500 ms exactly it would end an open-`R_ext` gate
      pulse before the check could see it held — and well under ~2 s; its
      value is OPEN, below. It must be attached with
      `timerAttachInterruptFlag(…, ESP_INTR_FLAG_IRAM)` — plain
      `timerAttachInterrupt()` passes flags `0` (`esp32-hal-timer.c`,
      framework 3.20017.241212, read 2026-10-30) and is held off while the
      flash cache is disabled — and its `IRAM_ATTR` handler clears GPIO38
      by writing `GPIO_OUT1_W1TC_REG` bit 6 directly. The same ISR takes
      the `t_min`/`t_max` samples, so it re-arms its own alarm, and that
      must use the ISR-safe `timer_group_set_alarm_value_in_isr()`:
      Arduino's `timerAlarmWrite()` calls `timer_set_alarm_value()`, which
      is not in IRAM (`esp32-hal-timer.c:67-68`, read 2026-10-30).
    - **No flash write while armed** — the arm-window form of invariant 2,
      and the reason the ISR flag matters less than it would otherwise.
    - **`setup()` begins `digitalWrite(38, LOW); pinMode(38, INPUT);`** —
      the latch is written LOW first, so a later `pinMode(38, OUTPUT)`
      cannot drive HIGH from a latch a CPU reset left at 1. That makes the
      "output latch's reset value of 0 is assumed" note above moot.
    - **No task watchdog subscription, no panic-handler wrap, no shutdown
      handler.** They were the firmware bound in the drafts; the gate
      replaces them. *(For reference, the core's defaults on framework
      3.20017.241212, `qio_opi` sdkconfig, read 2026-10-30: the task
      watchdog times out at 5 s, panics and reboots, and watches only
      CPU0's idle task; `loopTask` is not subscribed; the interrupt
      watchdog is 300 ms; every panic writes a core dump to a 64 KB flash
      partition; a panic restart is a CPU reset, `RTC_SW_CPU_RST`. Never
      subscribe `loopTask` to the task watchdog. Not permanently:
      `net.tick()` uploads synchronously with a 3 s connect and 5 s read
      timeout, and `selftest` blocks for up to 30 s. Not per arm window
      either: a 5 s timeout cannot enforce a ~2 s limit, and the ISR that
      ends the window cannot make the unsubscribe call.)*
    The watchdog relay above stays an option, as a layer that does not
    depend on the ESP32 at all.
  - **Every `Q` is read back — the four drive channels and the gate —
    PROPOSED 2026-10-30.** The five input pins are the layout part; the
    series value — **1 K proposed, value OPEN** (see the pulled-'221
    caveat in OPEN) — and the check below are a proposal. The series pair hides a single open `R_ext`: the
    machine sees only the overlap of both pulses, so one channel holding for
    the whole arm window looks like a normal press until a second fault
    turns it into a real hold. So each `Q` goes through a series resistor
    (1 K proposed) into a GPIO
    input — GPIO1, GPIO2 and three of 9-14, 18, 21, 47, chosen at layout.
    **The gate trigger is not GPIO18**: GPIO15–18 emit a ~60 µs low pulse at
    power-up, and a falling `A̅` fires the gate. Harmless while GPIO38's
    pull-down holds the gate in reset, one fault closer after a CPU reset
    that leaves GPIO38 high. A readback input on 18 is fine.
    **A pulse is checked as a WINDOW, not timed by its edges**: the IRAM
    timer samples each `Q` level at **`t_min`** after its trigger, where it
    must be high, and at **`t_max`**, where it must be low. Low at `t_min`
    catches an open `C_ext` (a pulse of tens of ns) and a short `R_ext`;
    high at `t_max` catches an open or high `R_ext`. *(A draft said "a
    `CHANGE` interrupt stamps both edges, the same pattern as the
    flowmeter's GPIO6 interrupt" — `flowIsr` is FALLING-only and stamps
    with `micros()`, and one `CHANGE` handler cannot tell the edges apart
    on a pulse shorter than its own latency, which is exactly the
    open-`C_ext` fault. The window check needs no edge at all.)* A failed
    check drops GPIO38, latches the fault **in RAM** and refuses to arm
    until reboot; the fault reaches flash only when the machine is quiet,
    with the next upload, never mid-shot.
  - **`drivetest` — PROPOSED 2026-10-30, with known defects listed under
    OPEN below.** It fires each channel alone, the gate included — on the
    first actuation request after every boot, and on command, never
    automatically at boot. *(This said the first request made it "attended
    by construction"; a Phase 1 request can come from a Home Assistant
    automation with nobody at the machine, so it is not.)* It checks two
    things on every lone fire:
    - **the `Q` window**, as above — the '221 and its RC;
    - **the direction's J2 sense line stays LOW for the whole fire** — the
      per-device test this exists for ("energise device 1's LED alone — the
      line must stay low", under *two photoMOS in series per driven line* above). The `Q` window
      cannot see a shorted photoMOS; only the sense line can. It is sampled
      **raw** (GPIO7 for 1Cup, GPIO15 for 2Cup) by the same IRAM timer,
      not through `SwitchBank`, whose confirmation needs two consecutive
      polls to agree (`DEBOUNCE_MS` = 20 ms, `machine_io.h`) at a loop that
      runs every 42–57 ms, and still lost 2 of 6 80–90 ms presses on
      2026-10-19. *(This gave "~100 ms of agreement" until review the same
      day; the conclusion stood, the number did not.)*

    **On the first sense-line HIGH, `drivetest` aborts**: drops GPIO38,
    latches the fault in RAM, fires nothing more, and tells the operator
    that the lone fire **was** a press, which direction, and to stop the
    cycle at the lever — and that the suspect is the **partner** of the
    channel just fired, which is the healthy one. *(This said "the shorted
    partner's lone fire" until review, which reads as blaming the device
    that was fired.)* So a shorted partner costs at most
    one unwanted press, made while someone is standing there. **If every
    lone fire passes, no press happened, and the request that triggered
    the test is then carried out.** *(A draft the same day did not carry
    out the triggering request and had the operator "ask again", to avoid
    the test's press toggling the requested one — but it had no way to
    know the test had pressed, so "ask again" only deferred the toggle.)*
    A lone gate fire, with all four drive `A̅` lines high, fires nothing.
    *(Until review on 2026-10-30, `drivetest` checked only the `Q`
    windows, so a shorted partner — the fault the per-device test exists to
    catch — passed it.)*
    - **The pass lives in RAM only.** Every power cycle re-tests, so
      soldering the RC, swapping a '221 or a photoMOS cannot leave a stale
      pass behind, and nothing is written to NVS — at a press or ever.
    - **`drivetest` refuses unless the machine is idle**: the detector IDLE
      (the check `selftest` makes, alongside `flow on`) **and**
      `machineBusy()` false, **and** it refuses outright with the display
      or switch taps off: `machineBusy()` then sees nothing, and the sense
      check below has nothing to read. A latched lever-B draw reads idle on
      the lever lines and does not run the display timer, which is why the
      brew guard is the idle check and the lever lines are not — and **both
      of the brew guard's known gaps apply** (CLAUDE.md, "LEVER B
      LATCHES"): a press too fast to confirm never arms it, and an ordinary
      draw outlasts `BREW_GUARD_MS` (75 s) while the pump still runs. So
      `drivetest` can run during a hot-water draw; with healthy parts that
      is harmless, and the sense check below stops it at the first fault.
    *(Drafts of this bullet, 2026-10-30: one fired every channel at every
    boot and called the shorted-partner press "the per-device test's
    accepted alarm" — an unattended press on every reboot. The next stored
    the pass in NVS keyed on firmware version, so a runtime fault cleared
    it with a flash write mid-shot and a reworked board kept its old pass;
    and its idle check, "display not in timer mode and every lever line
    low", was described as `selftest`'s when `selftest` checks neither,
    and could not see a latched hot-water draw.)* `Q` and the gate are our
    3.3 V logic, not machine nets, so invariant 1 is not touched.
  - **OPEN — the gate, readback and `drivetest`, for the bench sweep
    (recorded 2026-10-30, not settled on paper).** Four drafts of this
    design went wrong in one day, mostly by deciding behaviour before
    anything was measured. The gate's timing waits for the drive channels'
    RC to be chosen:
    - **A minimum gap between gate pulses.** Each window is bounded; the
      gap between windows is not, so runaway firmware can chain them into
      one press past ~2 s. Candidates: a second '221 stage or an RC
      lockout on the gate trigger that refuses a re-fire for, say, a
      second. The spare channel on the gate chip is the obvious part.
    - **The margin ordering**: drive `t_max` sample < gate minimum width;
      gate maximum width ≤ 500 ms < gate `t_max` sample < firmware ISR cap
      < ~2 s. Every value in it is unchosen.
    - **The gate RC values.** The drive sweep's ordered 330 K / 470 K
      against 470 nF–1 µF spans roughly 109–329 ms at `0.7 R C` (the 4.5 V
      formula). A drive choice below the top of that range leaves an ordered
      pair long enough for the gate; **only if the sweep picks a drive pulse
      near the top does the gate need parts not yet bought.** *(This said
      "no ordered pair is known to reach a gate longer than the drive
      pulse" until review the same day — false as written.)*
    - **A gate `R_ext` that fails in service stays hidden until the next
      boot**, because every normal arm ends the gate early and `drivetest`
      runs once per boot. The board runs for weeks, so the first of the
      "two independent faults" can sit latent. Options: re-time the gate,
      alone and unloaded, on a schedule while the machine is idle.
    - **`t_min` is not tied to the latching press width.** The window only
      requires `Q` high at `t_min`; a `C_ext` drifting the pulse below the
      machine's latch threshold (unmeasured, under ~140 ms) still passes —
      the silent too-short failure CLAUDE.md warns about. `t_min` has to
      sit above the measured threshold, which the bench has not measured.

    `drivetest`'s sense check, from the third review (2026-10-30) — each a
    defect in the proposal above, not yet designed out:
    - **A half-failed photoMOS can press while the sense line reads LOW.**
      This file's own half-failure analysis (*two photoMOS in series per
      driven line*) puts the machine line at ~3.6–4.2 V, which the machine
      reads as a press, and the divided ESP32 pin at ~2.14–2.50 V against a
      2.475 V V_IH — graded INDETERMINATE there. A digital read cannot
      settle it; an ADC read or a lower threshold might.
    - **No positive control.** A dead sense path reads LOW, the same as a
      pass — J2 has no ground of its own, so with J5 and J4 unplugged the
      line is stuck low, and an open series leg does the same. The test has
      to prove the line can read HIGH before trusting a LOW.
    - **Discrete samples cannot show "LOW for the whole fire".** A press
      rising after the photoMOS turn-on delay and falling before the next
      sample is missed. It needs continuous sampling across the pulse, or a
      latched edge on the sense GPIO.
    - **It refuses in the normal first-request states.** The detector is
      ARMED whenever a cup is on the scale, and a brew-by-weight stop
      arrives mid-shot with `machineBusy()` true — so the first actuation
      after any reboot fails, and a stop press lets the shot run to the
      stored timer.
    - **Once per boot is not "periodic"**, which the per-device test above
      asks for, and a RAM-only fault latch forgets a failed part on any
      reset — opening the serial monitor is one.
    - **The 1 K readback can drive a `Q` node with its '221 pulled.**
      Pulling a '221 from its socket is a deliberate third disable, and it
      leaves each `Q` node driven only by its readback pin through 1 K. A
      readback pin made an output and driven HIGH (a `pinMode` slip, a
      firmware fault) then feeds a drive channel's BC547 base through its
      8.2 K — a bare-GPIO drive with no one-shot bound — or pulls the gate's
      tied `R̅` net to ~3.0 V against its 10 K pull-down, arming all four.
      With the chip fitted the push-pull `Q` wins, and the open output
      jumpers still block conduction in Phase 0. The series value and the
      tap point are layout decisions: a series resistor well above the base
      and pull-down impedances would close it, if the readback's input
      timing allows. *(Inferred from circuit reasoning, review 2026-10-30;
      the drive-channel half predates the gate.)*
    - **A stuck-high gate `Q` removes the disarm path for every channel.**
      With GPIO38 on the tied `R̅` net, firmware could always disarm. With
      the gate in between, one internal fault in the gate chip holds all
      four channels armed, and dropping GPIO38, the pull-downs, a failed
      `Q` check and `drivetest`'s abort all do nothing. Before the gate, a
      stuck `Q` cost one channel. This weighs on whether to keep the gate
      at all or return GPIO38 to the net directly. **Dropping the gate is
      a small, deliberate rework**: leave its chip out and run one wire
      from GPIO38 to the tied `R̅` net, then change the firmware, whose
      arming sequence and `drivetest` assume the gate. *(A draft on
      2026-10-30 reserved an unfitted link position for this beside the
      gate's DIP-16. Review found it a new bridge hazard — fitted, or
      bridged by solder, with the gate socketed, it ties GPIO38 to the
      gate's push-pull `Q` — and "not rework" untrue for the firmware.
      Withdrawn the same day. Before that, this said "the footprint is
      reserved so that either can be built" with nothing reserved.)*
    - **The GPIO18 exclusion's rationale is weak**: the power-up glitch
      occurs while the '221 is itself in power-on reset, so it cannot meet
      "a CPU reset leaving GPIO38 high". And the "safe" set 9–14, 21, 47
      was not checked against the ESP32-S3 power-up glitch table. Kept as a
      cheap precaution, not as a derived rule.

  CLAUDE.md's GPIO table carries the pins — the four triggers, the gate
  enable and trigger, the five readbacks — and the idle-state rule; this
  section carries the parts. *(Said "the pin, the idle-state rule and the
  three parts" until 2026-10-30, a count from before the gate.)*

  **GPIO39–42 are the JTAG pads and are plain GPIO here — DATASHEET grade,
  ESP32-S3 Series Datasheet v2.2, §2 pin table and §3.4 Table 3-5, read
  2026-10-29.** 39 = MTCK, 40 = MTDO, 41 = MTDI, 42 = MTMS. With
  `EFUSE_DIS_PAD_JTAG`, `EFUSE_DIS_USB_JTAG` and `EFUSE_STRAP_JTAG_SEL` all
  at their default 0, the JTAG source is the USB Serial/JTAG controller and
  **GPIO3 is ignored**; GPIO3 selects the source only once `STRAP_JTAG_SEL`
  is burned, which nothing here does. Nothing drives these pads at boot:
  during reset they are high-impedance with no pull; after reset, inputs,
  with a weak pull-up on 39 only while `EFUSE_DIS_PAD_JTAG` is 0 *(said
  "through reset they are inputs" until 2026-10-29)*. *(CLAUDE.md carried this as a RECOLLECTION
  caveat — "GPIO3 is the source select, so plain GPIO", "a pin that moves at
  boot" — until the datasheet was read the same day: right pin order, right
  conclusion, wrong lever, unsupported hedge.)*

#### What this changes in the harness

**J2 position 1 must come to the board.** Today it deliberately does not — it
is the supply and is never tapped. The drive path needs it as the switching
source, so the J2 T-piece goes from four conductors to five, and that
connector carries the switch common. **Not "switching current rather than
microamps"**, as this said until 2026-10-12 — the press loop is ~0.1-1 mA. Our 370 K
sense tap draws 13.5 µA of that — ~12 % of the line current on the
favourable branch, not a negligible fraction.

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
  **[THE "STRICTLY LESS EXPOSURE" CLAIM IS WITHDRAWN — see "Does not work"
  under "Two things that bound the firmware-side failures": a series open blocks
  identically on either side, and for the harness-bridge hazard this
  placement is mildly WORSE. It is kept so the photoMOS sees no voltage in
  Phase 0 — the one good reason for the placement — not as a bridge
  mitigation. (Sequencing is the argument for the shared LED-supply jumper,
  not for this one.)]**
- keep pos 1 **physically segregated** in the harness and on the board — its
  own conductor run, maximum spacing from the four direction lines, and no
  shared via column;
- the sense divider on each direction line is already a **detector** for this:
  a line reading high with nothing commanded means a bridge or a failed part.
  **[WITHDRAWN 2026-10-12 — see the "readback that already exists" bullet in
  "The failure neither mechanism bounds": the divider cannot tell a lever
  press from a bridge, and in the double half-failure case it may or may not
  read high — indeterminate, per the sense-tap paragraph there.]**

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

## Suggested architecture — SUPERSEDED, kept as the day-one plan

> **This section is the original day-one sketch, and the diagram below is
> the SUPERSEDED one — it is kept, not corrected.** `CLAUDE.md`'s
> *Architecture (decided — don't relitigate)* is normative and differs on
> the transport: **HTTP POST for shot upload plus a WebSocket for live**,
> with MQTT carrying only the Home Assistant summary entities. Go there
> for the current picture; do not read the one below as live.
>
> The section was left unmarked until 2026-10-27, so the plan was still
> advertising a decision the architecture had reversed — and doing it in a
> picture, which is what a newcomer reads. **A first attempt at this fix
> that same day redrew the diagram instead of marking it** — which broke
> this repo's keep-the-superseded-claim rule, and took MQTT out of the
> picture altogether while both `CLAUDE.md`'s normative diagram and the
> prose below keep it for the Home Assistant summary leg. Banner added and
> the original diagram restored after review. The day-one sketch has no HA
> leg because the decision postdates it; that is what superseded looks
> like, and it is not a reason to redraw it.

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
the tablet is a screen. *(This part survives unchanged.)*

**NOT MQTT, and this said "Why MQTT" until 2026-10-27.** `CLAUDE.md`:
*"HTTP POST, not MQTT, for shot upload in Phase 0. Simpler, curl-able, and
the flash spool already handles retries. MQTT arrives in Phase 1 when
there's a command path to carry."* The original argument — fan-out is free
and Phase 1 needs the command path — was not wrong about Phase 1; it was
wrong that Phase 0 should pay for it early. MQTT still carries the Home
Assistant summary entities.

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
