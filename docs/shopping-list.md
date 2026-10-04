# Shopping list

Supersedes the parts tables in `docs/phase-0-plan.md`, which were written
before the connectors were identified and before the multimeter broke.

Everything here is **Phase 0: read-only**. Hard invariant 1 — nothing on
this list can drive a machine signal, and that is deliberate. The parts
that *can* actuate are at the bottom, under "Deliberately not yet".

---

## 1. Already ordered — don't buy twice

| Item | For |
|---|---|
| **Mixed JST-XH balance extension pack, 2S/3S/4S/5S/6S** (GTIWUNG, €6.99) | the T-pieces — covers J4 (2S), **J5 (3S)**, J2 (4S) |
| JST XH connectors (`XHP-4/5/3`, `SXH-001T-P0.6`, `B*B-XH-A`) | now optional, see §2 |
| Silicone hookup wire, 26 AWG | all harnesses |
| 150 kΩ resistors | 0c switch dividers |
| Multimeter | replacing the broken one |

**One of each, not five of each — so there is exactly one 3S.** The J5
T-piece is a strip-a-window-and-solder job, and if it goes wrong there is
no second 4-pin lead in the bag.

**The 5S is the practice piece, the 6S is the box connector.** Nothing on
this machine is 6- or 7-pin, so those two have no connector job — but they
have one job each, and the pack holds one of them. Practise the
strip-and-solder on the 5S; the 6S gets cut in half to make the
cable-to-box plug pair (`docs/phase-0-status.md`). Only then touch the 3S.

**Wire is 22 AWG silicone** (confirmed in the listing spec), so there is
no PVC-to-silicone transition to engineer — the lead itself is rated for
the environment. The branch wires still solder onto it, in 26 AWG
silicone, but that is one joint in one material rather than a splice
between two.

**That listing calls the connector "JST EH" in its spec table while its own
title says "JST-XH". The spec row was boilerplate error — **confirmed XH
on arrival, 2026-10-01.** The product is defined by what it mates, and
hobby LiPo balance ports are XH in all but a few outliers. Recorded
because XH/EH is the exact pair this project already lost two
evenings to: same pitch, same pin span, same width to 0.3 mm.

**Seat it before soldering.** This is what turns the paragraph above into
a fact, and it is the last check before the part is modified. Machine off
and unplugged:

1. Does the lead's **male** end seat and latch in J5 on the board?
2. Does the machine's **own harness plug** seat and latch in the lead's
   **female** end?

Both must work, because the T sits between them. If either will not seat,
stop rather than force — an EH in an XH shroud feels wrong before it feels
stuck. Depth (5.7 mm XH vs 3.8 mm EH) is the fallback if you want to know
before trying.

---

## 2. The crimp problem, and why it no longer applies

Kept because it is the trap anyone rebuilding this would walk into, and
because the fallback route still needs it.

**JST XH crimp contacts need a crimp tool.** `SXH-001T-P0.6` is a tiny
open-barrel contact with two separate crimp wings — wire and insulation —
and pliers do not produce a contact that seats in the housing. This is
what would have stalled the build.

**The balance leads sidestep it entirely** — they arrive terminated at
both ends, so nothing needs crimping. The route below is only relevant if
a lead turns out not to mate, or if you want the tap ends themselves
connectorised.

**Pre-crimped housing kit.** Multi-pin XH kits sold for LiPo balance leads
(2–10 pin housings, ~90 pre-crimped wires, tweezers, no tool required —
you push the contact into the housing) cost about €10. Look for one whose
wire is **silicone**, not PVC: 22 AWG silicone is the top of the
`SXH-001T-P0.6` range (AWG 28–22) and ~200 °C.

Two things to check on any such listing:

- **"XH 2,54 mm" is the usual misnomer** — XH is 2.5 mm. A listing that
  mentions LiPo balance leads is very likely the right part whatever the
  title says, because balance connectors are nearly always XH — but the
  male ends on
  those leads are aftermarket clones rather than genuine JST, so fit is
  worth checking rather than assuming. Anything genuinely specified as
  Molex/Dupont 2.54 is not equivalent and will not mate.
- **The kit is the female side only.** XH has no cable-mount male part at
  all; see the balance-lead note below.

A crimper (Engineer PA-09, IWISS SN-01BM; €25–40) is then optional, and
only worth it if you expect to keep making harnesses. SN-28B is the cheap
one and is mediocre on XH.

### The male end of the T: balance extension leads

**XH is a wire-to-board series only.** The catalogue has `B*B-XH-A` (top
entry) and `S*B-XH-A` (side entry), both through-hole PCB parts, plus
`XHP-*` housings for the cable side. There is **no cable-mount male
housing**, so a male XH cable end cannot be built from genuine JST parts.
A pre-crimped kit therefore gives you the female half only, and an in-line
T needs both:

```
board J5 (B4B-XH-A) <-[XHP-4]- your T -[male XH]-> machine's harness plug
                                   |
                                   +-- taps out to the ESP32
```

The RC world solved this: **LiPo balance extension leads** are male-XH to
female-XH on cable, wired 1:1 — precisely the pass-through half of the T,
pre-made, often in 22 AWG silicone. A balance connector for an *n*S pack
has *n*+1 pins, which maps onto this machine exactly:

| Connector | Pins | Buy | Milestone |
|---|---|---|---|
| **J5 display** | 4 | **3S** balance extension | **0e — critical path** |
| J2 `BOTONE` switches | 5 | **4S** balance extension | 0c |
| J4 flowmeter | 3 | **2S** balance extension | 0d |

**Count the pins, not the S.** The cell count is one less than the pin
count, so the obvious-looking 2S is a 3-pin and will not fit J5. The mixed
2S–6S pack beats singles — all three connectors for about €7, and the
5S/6S become practice pieces. (Bought; see section 1.)

Build: plug in line, strip a window mid-cable, solder branch wires to the
conductors you want, adhesive-lined heat-shrink over it. Unplugging
returns the machine to stock.

Note the window is **not** empty: the two 8.2 kΩ series resistors live in
it, soldered inline, because series resistance has to be at the source to
protect the machine from a fault further down the cable (reasoning in
`docs/phase-0-status.md`). So allow for two resistor bodies, generous
heat-shrink over them, and a cable tie for strain relief — this is the
one part of the build that is inside the machine and not reworkable.

**This makes the pre-crimped kit optional.** Nothing in that build needs a
housing or a contact. Buy the kit only to make the tap ends themselves
detachable, or as spares — otherwise a balance extension, solder and
heat-shrink is the entire T-piece.

**Verify continuity pin-by-pin before it touches the board.** Some balance
extensions are deliberately wired **mirrored** for particular adapters, and
this cannot be established from a product photo — the wires twist freely
and the colours are the manufacturer's choice, not a standard. First job
for the new multimeter:

1. Lay the lead flat on the bench with **both latches facing up**. Do not
   turn either end over — "looking into the mating face" flips left and
   right, which is how a correct lead reads as mirrored and a mirrored one
   passes. Pin 1 is then the same side of both housings; call it the left.
2. Meter on continuity. Left-most of one end → left-most of the other:
   must beep.
3. That same pin against the other end's remaining three: must **not** beep.
4. Repeat for each position. You want a 1:1 map and nothing else.

Any cross means a mirrored lead. In line at J5 that lands the board's
ground on `Disp-P4` — the bidirectional line, and the one connection on
that connector that can actuate the machine. Four beeps rules it out.

**Colour is not evidence on this machine.** The J5 harness is
red/black/red/black because it was cut from two 2-wire reels, and **ground
lands on a red one**. This already cost an evening once.

**Fallbacks** if the extensions cannot be sourced: solder directly to a
`B4B-XH-A`'s pins and encase the back in adhesive-lined heat-shrink (the
joints then take all the strain), or mount the header on a scrap of
perfboard (robust, but a rigid lump).

**Do not substitute 2.54 mm Dupont male pins.** They insert — 0.12 mm of
accumulated error across four pins — but the `XHP-4` latch has nothing to
grab, and this machine has a vibratory pump.

### If the pigtails turn out to be PVC

Only relevant if you end up with a PVC kit rather than a silicone one.
PVC pigtails are UL1007/UL1571, rated **80 °C**; silicone is ~200 °C. That
decides where each goes rather than which to buy:

- **PVC for the T-piece only** — the 100–150 mm that lives inside the
  loom. Ascaso's own display harness on that exact path is PVC, so ambient
  there is below 80 °C by the manufacturer's own reckoning, and the tap
  runs beside it in the same space.
- **Silicone for the run out of the machine** to the ESP32 box. That is
  the leg whose routing is your decision and whose length is real.
- **Splice with adhesive-lined heat-shrink**, joints staggered a few cm
  apart so the bundle has no single fat lump that will not sit in the loom.

Either way, route the outbound leg **with the existing loom**, which is
already routed around the heat — not over the top of the boiler to reach
the back panel.

**Do not substitute mains/building wire.** H07V-K and similar are wrong on
three counts, two of them counterintuitive: the smallest size the
designation covers is 1.5 mm², against a 0.08–0.33 mm² crimp range, so it
does not fit the contact or the housing; its PVC is rated **70 °C**, which
is *worse* than the pigtails' 80 °C (450/750 V is a voltage rating, not a
thermal one); and "K" means fine-stranded for fixed installation, not
flex-rated for a vibratory pump. If buying Lapp, the right part is
**Ölflex HEAT 180 SiF in 0.25 mm²** — silicone, 180 °C, inside the crimp
range.

**Ambient at J5 is inferred, not measured.** If you want it settled, buy
the multimeter with a **K-type thermocouple** (most €30 ones include one),
tape the bead to the J5 harness, run to steam, soak 20 minutes, read it.

---

## 3. The J5 T-piece (0e — critical path)

The four stub wires soldered to J5 for the 0b capture **were removed**, so
nothing can be captured or logged from the display bus until a tap exists
again. An in-line T beats re-soldering stubs: reversible, and it cannot
leave the machine unable to run.

| Item | Qty | ~€ |
|---|---|---|
| **3S** balance extension lead (**4**-pin, male↔female) | 1 | — |
| — optional — pre-crimped XH kit, for detachable tap ends | 1 | 10 |
| — fallback — `B4B-XH-A` header + perfboard | 5 | 3 |
| — fallback — Ascaso display harness `I.4312` (spare) | 1 | 10–20 |

**Bought**, as part of the mixed pack in section 1 — one 3S, no spare.
Practise on the 5S first; the 6S becomes the cable-to-box connector.

A 3S balance extension, solder and heat-shrink is the whole T. Ascaso's own spare
harness, **part `I.4312`** (mainboard is `I.3957`, matching the board
silkscreen), is the last fallback if anything about the generic connector
turns out not to mate — cut it and both ends are guaranteed by
construction. Worth knowing the part number exists; not worth buying first.

Wire the T straight through and branch **all four** conductors: `ESD1`,
`ESD4` and `Disp-P4` each through an 8.2 kΩ, ground directly. Ground gets
no resistor — it is the reference both shunts return through. An 8.2 kΩ
there lifts the ESP32 ground **~1.9 V** (1.85 V open-drain, 2.07 V
push-pull) and cross-couples the channels, which breaks the divider
outright.

`Disp-P4` is branched but **terminated at nothing** — a labelled test
point, pending the button test. Sensing it through a divider is safe;
**driving** it is the one connection on J5 that could actuate the machine,
and its pull-up strength is unrecorded, so it gets no load until measured.
Full reasoning in `docs/phase-0-status.md`.

**`ESD4` (pin 3) is the clock, `ESD1` (pin 2) the data** — settled
2026-10-03, see CLAUDE.md.

---

## 4. Resistors

A generic E12 kit covers most of it, but the four values that matter are
worth buying as dedicated bags so you are not down to your last two at
11 pm. **1 % metal film, 1/4 W.**

| Value | Qty | Where |
|---|---|---|
| 8.2 kΩ | 10 | 0e T-piece, series leg (**×3** — `ESD1`, `ESD4`, `Disp-P4`); 0b analyser tap (×3). **Bought: 25.** |
| 15 kΩ | 10 | 0e divider, ground leg, at the box (**×2** — `Disp-P4` stays unterminated). **In stock.** |
| 150 kΩ | 10 | 0c switch divider, series leg — **already ordered** |
| 220 kΩ | 10 | 0c switch divider, ground leg (**×2** — 1Cup, 2Cup). **In stock.** |
| 1 kΩ | 10 | 0d flowmeter, **at PA8/PA9 only** — the board side, after its own conditioning. **Never at J4.** **In stock.** |
| 8.2 kΩ (J4) | — | **J4 ANALYSER tap only: 8.2 kΩ, one channel, `#` only, no shunt.** The ESP32 tap at J4 is 120 K + 220 K — see the 0d entry in `docs/phase-0-status.md`. Covered by the 8.2 kΩ bag above. |
| **120 kΩ** | 10 | **0d J4 divider, series leg** — the only part now blocking 0d. Covered by the E12 kit. |
| E12 assortment kit, 1/4 W | 1 | everything else, **including the 120 kΩ above** |

Three of these are load-bearing — 0c is in CLAUDE.md, 0e and 0d are
worked out in `docs/phase-0-status.md`. Repeated here because getting any
of them wrong is expensive, and one of them already was — the 0d tap made
the machine fault and stop, which was loud rather than silent, but only
because the machine caught it:

- **0c must be 150 K / 220 K, never 220 K / 220 K.** 220/220 gives 2.50 V
  against an ESP32-S3 VIH of 2.475 V. Supply tolerance alone eats that
  margin and a pressed button reads as not pressed.
- **0d's J4 ANALYSER tap is 8.2 K series, one channel, `#` only, ground
  to `T` direct.** The **ESP32 divider at J4 is 120 K + 220 K → 2.95 V**,
  solved 2026-10-04 from a measured 10.04 kΩ pull-up and 147.4 kΩ
  pull-down. **Do NOT substitute 150 K / 220 K** — it is in stock, it
  looks fine at 2.71 V, and it fails on tolerance stacking exactly as
  220 K / 220 K does for 0c. CLAUDE.md's conditioning table gives 1 K for the
  *board* side at PA8/PA9 and 120 K + 220 K at the connector; the `+`
  rule sits in its own block beneath it.
- **`+` is read-only, high-impedance meter only.** No input clamp — no
  analyser, no GPIO, no divider — **and nothing that sources or sinks
  current: never power the ESP32 from it.** It is the machine's sensor
  rail off a 3 W IRM-03-5. A 20 MΩ DMM is fine, and is how its 5.0 V was
  read on 2026-10-04. Clamping `+` is what the leading
  hypothesis for the `E01` would make dangerous, and it has not been ruled
  out. The mechanism is **not** established; see the 0d entry before
  treating this as settled history.
- **0e must be 8.2 K / 15 K, and do not enable an internal pull-down.**
  The internal 45 K across the 15 K leg drops the open-drain case to
  2.33 V and the pin stops reading high.

---

## 5. Build and enclosure

| Item | ~€ | Notes |
|---|---|---|
| PTFE or fibreglass sleeving, 2–4 mm, 200 °C+ | 8 | Silicone wire is good to ~200 °C on its own, so this is for abrasion and for anything routed near the boiler or group. Ordinary PVC sleeving and standard heat-shrink are not. |
| Heat-shrink assortment, **adhesive-lined** | 6 | Strain relief at the T-piece branches, and over the stripped window. Adhesive-lined or nothing — plain heat-shrink is not insulation on its own. |
| Perfboard + pin headers | 6 | The dividers want to live on a board, not in mid-air |
| ABS project box, ~120 x 80 x 40 mm | 8 | ESP32 mounts **outside** the machine. Size it for **three** harnesses — 0c adds J2 (5 wires, 2 more dividers) and 0d adds J4, and they all land here. A box that fits only the display tap is a box bought twice. |
| Cable gland or grommet | 3 | Where the harness leaves the case |
| Nylon cable ties / adhesive tie mounts | 4 | Keep the tap away from the boiler and off moving parts |

**Power the ESP32 from its own isolated supply** (any ordinary USB phone
charger). The machine's 5 V comes from an isolated IRM-03-5 module, so
sharing ground is safe — but only because both sides are isolated. Do not
power the ESP32 from the machine.

---

## 6. Tools

| Item | ~€ | Notes |
|---|---|---|
| **Multimeter** | 25–40 | **Already ordered.** Continuity beep and auto-range are what matter; a **K-type thermocouple** would also settle the ambient-at-J5 question empirically. Its absence cost two full evenings of guessing at connectors from photographs. |
| Digital calipers | 15 | The *only* measurement that separates JST XH from EH is depth — 5.7 vs 3.8 mm. Identifying this by eye failed three times. |
| Headband magnifier | 15 | |
| Flux pen + 0.5 mm solder | 12 | |
| Temperature-controlled iron, fine tip | 40–60 | Only if the current one is not adjustable |

Already owned and working: SeenGreat SG-NANO-DLA-A logic analyser,
test-hook grabber clips, ESP32-S3-DevKitC-1 N16R8, 2 × Bookoo Themis.

**The analyser is not 5 V tolerant** despite the listing's "0–5.5 V" —
74HC245PW on a 3.3 V rail behind 100 Ω. The series resistors above are
not optional.

---

## 7. Deliberately not yet — Phase 1

74HC123 or NE555, optocoupler, RC parts for the hardware one-shot.

These are the parts that let the firmware *trigger* the machine while
being physically incapable of *holding* the line — which matters because
holding >2 s silently reprograms that direction's stored timer.

Don't order them yet. 0f is measured (σ = 0.21 g/s, 12.2 %) and overshoot
says stop-at-weight is viable, but the trigger path still depends on 0c
being built and trusted first. Parts sitting in a drawer argue with the
data.

---

## Rough total

Connectors are **bought** — the mixed balance-lead pack was €6.99 and
should cover J5, J2 and J4, pending the seat check in section 1.

Still outstanding: adhesive-lined heat-shrink, PTFE sleeving, perfboard,
project box and cable gland — **~€30**, plus an E12 assortment kit
(€8–15) which is the only resistor item left. **The load-bearing
resistors are no longer on the critical path**: 8.2 K, 15 K, 220 K and
1 K are in stock and 150 K is ordered. **0d is now blocked on the 120 kΩ** in that kit — so
"nothing blocks a build" is no longer true, and the kit moved from
nice-to-have to the one outstanding part that gates a milestone. Add calipers (~€15) if you want the connector
question settled by measurement rather than re-derived from photographs.
The pre-crimped kit (~€10) and the Ascaso `I.4312` harness (~€15) only if
a balance lead disappoints.
