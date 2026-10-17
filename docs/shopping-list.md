# Shopping list

Supersedes the parts tables in `docs/phase-0-plan.md`, which were written
before the connectors were identified and before the multimeter broke.

Everything here is **Phase 0: read-only** in what it can *do*. Hard
invariant 1, as amended 2026-10-12, turns on whether a signal can be driven,
not on what is soldered to the board.

**That changes this list.** The 2026-10-08 build decision populates and
bench-tests the actuation parts **now**, behind open jumpers, rather than
leaving footprints. So the §"Deliberately not yet" block at the bottom is
**no longer deliberate-not-yet** — those parts move into the Phase 0 order,
at **8 × photoMOS (4 in service, 4 spare) and 3 × 2-pin header**, decided
2026-10-12, with the LED drive costed in §7 for the first time.

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

**The 5S and 6S are both practice pieces.** Nothing on this machine is 6-
or 7-pin. An earlier version made the 6S the shared cable-to-box connector;
that is **superseded** — the box now takes **one keyed connector per
harness** (J5 4-pin, J2 5-pin, J4 3-pin), which cannot be cross-plugged
because the pin counts differ, and which the 6S's seven conductors could not
have carried anyway against the ten now needed. See
`docs/phase-0-status.md`. Practise the strip-and-solder on the 5S and 6S;
only then touch the 3S.

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
5S/6S are practice pieces. (Bought; see section 1.)

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
Practise on the 5S and 6S first; neither has a connector job (see section 1).

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
point. **Sensing it through a divider is NOT safe either**, which this
paragraph said until 2026-10-12: its pull-up strength is unrecorded and it
reaches the MCU through R32 = 100 K, so a 23.2 K divider can drag it under
1 V — and **what that does is UNKNOWN**, which is the honest reason to fit
nothing. (This said "the MCU then reads a button held down permanently" until
2026-10-12. CLAUDE.md retracts that wording: this repo's own 2026-10-03
result, 344 s of live bus, establishes that `Disp-P4` is **not** the button
line — the buttons are bits 49/58 of the 67-bit frames. The conclusion is
unchanged; only the mechanism was wrong.) Driving it is
worse again. It gets **no load at all**, which is what is built. Full
reasoning in `docs/phase-0-status.md`; CLAUDE.md carries the same correction.

**`ESD4` (pin 3) is the clock, `ESD1` (pin 2) the data** — settled
2026-10-03, see CLAUDE.md.

---

## 4. Resistors

Buy the load-bearing values as dedicated bags rather than relying on an
assortment kit — **1 % metal film, 1/4 W**. Cheap E12 kits are 5 %, and
while both dividers survive that (J4 +271 mV, 0c +110 mV), the margins are
tighter than they need to be for the sake of a few euro.

| Value | Qty | Where |
|---|---|---|
| 8.2 kΩ | 10 | 0e T-piece, series leg (**×3** — `ESD1`, `ESD4`, `Disp-P4`); 0b analyser tap (×3). **Bought: 25.** |
| 15 kΩ | 10 | 0e divider, ground leg, at the box (**×2** — `Disp-P4` stays unterminated). **In stock.** |
| 150 kΩ | 10 | 0c switch divider, series leg (**×4** — all four J2 lines). **In stock 2026-10-07.** |
| 220 kΩ | 10 | Ground leg: **×4 at J2** (1Cup, 2Cup, Steam, Water) **+ ×1 at J4** = **5**. **In stock.** |
| 10 nF ceramic | — | **CANDIDATE, do not buy yet.** ×1 across the J4 220 K shunt, 218 Hz. Nothing has been fitted and there is no before/after; it may raise the glitch count by slowing the falling edge to ~1.1 ms through the noise band. Decide after the re-measurement in `phase-0-status.md`. |
| 470 pF ceramic | — | **CANDIDATE, weaker case than J4's.** ×2 across the J5 15 K shunts, **never 10 nF** (69 µs against 36 µs clock highs stops the bus decoding). But the J5 drops look like serial polling, not coupling. `phase-0-status.md` already carried this value as a contingent remedy after raising the shunt to 16.4 K. |
| 1 kΩ | 10 | 0d flowmeter, **at PA8/PA9 only** — the board side, after its own conditioning. **Never at J4.** **In stock.** |
| 8.2 kΩ (J4) | — | **J4 ANALYSER tap only: 8.2 kΩ, one channel, `#` only, no shunt.** The ESP32 tap at J4 is **100 K + 220 K** — see the 0d entry in `docs/phase-0-status.md`. Covered by the 8.2 kΩ bag above. |
| **330 Ω (or 390 Ω)** | 20 | **PhotoMOS LED drive, ×4 — NOT IN STOCK, added 2026-10-12.** AQY212EH is guaranteed to operate at 3.0 mA and wants 5–10 mA; 330 Ω from 3.3 V gives ~6 mA. **Do not substitute the in-stock 1 kΩ** — it gives ~2 mA, above typical and below guaranteed, which is the one range that works on the bench and fails in service. 680 Ω if driving from 5 V instead. |
| **100 kΩ** | 10 | **0d J4 divider, series leg (×1)** — the chosen value since 2026-10-08. Also the **base-emitter resistors (×4)** for the LED drivers, added 2026-10-12: with the '221 pulled from its socket — a deliberate third disable — four bases would otherwise float with the rail up. **In stock.** |
| 120 kΩ | 0 | **NOT in stock.** Recorded as in stock on 2026-10-07 from a verbal report; the bag held 100 K and 150 K. Superseded by 100 kΩ, which has ~double the margin — not needed. |
| E12 assortment kit, 1/4 W | 0 | **Not required.** It covers no named requirement, and cheap kits are 5 % where the rows above are 1 %. Both dividers survive 5 % (J4 +271 mV, 0c +110 mV — the J4 figure was +96 mV while 120 K was specified), and 100 kΩ is already in stock. |

Three of these are load-bearing — 0c is in CLAUDE.md, 0e and 0d are
worked out in `docs/phase-0-status.md`. Repeated here because getting any
of them wrong is expensive, and one of them already was — the 0d tap made
the machine fault and stop, which was loud rather than silent, but only
because the machine caught it:

- **0c must be 150 K / 220 K, never 220 K / 220 K.** 220/220 gives 2.50 V
  against an ESP32-S3 VIH of 2.475 V. Supply tolerance alone eats that
  margin and a pressed button reads as not pressed.
- **0d's J4 ANALYSER tap is 8.2 K series, one channel, `#` only, ground
  to `T` direct.** The **ESP32 divider at J4 is 100 K + 220 K → 3.13 V**,
  solved 2026-10-04 from a measured 10.04 kΩ pull-up and 147.4 kΩ
  pull-down. **Do NOT substitute 150 K / 220 K** — it is in stock, it
  looks fine at 2.71 V, and it fails on tolerance stacking exactly as
  220 K / 220 K does for 0c. CLAUDE.md's conditioning table gives 1 K for the
  *board* side at PA8/PA9 and 100 K + 220 K at the connector; the `+`
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
| Breadboard, 830-point | 6 | For the **J5 bench test before the perfboard**. **Corrected 2026-10-12:** a DevKitC-1 straddles a standard board with its headers in columns **A and I** — 1.0 in row-to-row — so **one row (J) stays free**, not none as this line said. The advice is unchanged and was followed: put only the resistor junctions on the breadboard and jumper across to the devkit's headers. Note that is exactly why the ESP was NOT seated during the bench sessions, which a later commit wrongly cited as proof of the pin pitch. |
| **Jumper wires, male-female AND male-male** | 6 | The easily-forgotten part. M-F reaches the devkit header pins from the breadboard. |
| **XH board headers: 3, 4 and 5 pin** | 3 | **×5 each, decided 2026-10-12.** The perfboard gets one socket per harness: **3-pin J4** (`B3B-XH-A`), **4-pin J5** (`B4B-XH-A`), **5-pin J2** (`B5B-XH-A`). **The pin counts self-key** — nothing can be cross-plugged, so do not standardise them to one size for tidiness. Side-entry `S`-series if the printed box has a side gland; decide before designing the box. **Gender:** the board takes the **male** headers and the cable end takes the **female** housing from the pre-crimped kit — XH has no cable-mount male part at all, which is why the machine end still needs balance extension leads. |
| Perfboard **7 × 9 cm** ×2 + **FEMALE** pin headers ×5 | 6 | The dividers want to live on a board, not in mid-air. **Female** so the ESP32 sockets in and comes out — soldering the devkit down means it cannot be swapped or easily reflashed. **Size settled 2026-10-12: 7 × 9, not 5 × 7.** The devkit's footprint is 11 columns × 22 rows (pin rows 10 pitches apart, 22 pins long) and a 5 × 7 board is only ~27 rows, so the devkit alone would take 22 of them. Count the holes on what arrives; centimetre labels vary. **Buy 2 boards** — the dividers can live in the 9 free columns *under* the socketed devkit, which is tight soldering with the actuation block on the same board, and a second board is cheaper than discovering that with copper committed. **Buy 5 header strips**: 1×40 is the standard item, you cut each to 22 (losing the position you cut through), you need two, and female strips do not snap cleanly like male ones so the first one is practice. |
| Second mixed balance-lead pack | 7 | **Insurance.** The 4S is the only lead that fits J2 and there is no spare if a strip-and-solder goes wrong. The T-pieces are the most laborious part of the build and the pack is seven euro. |
| ~~ABS project box, ~120 x 80 x 40 mm~~ **PRINTED, not bought** | 0 | ESP32 mounts **outside** the machine. **The 120 × 80 size is WRONG and would not have fitted** — it was chosen when the perfboard was assumed 5 × 7; the board is now **70 × 90 mm**, and a 120 × 80 external box has ~112 × 72 of interior. Operator is printing one instead: **~110 × 100 × 40 internal**, **but decide
the one-board-or-two question BEFORE printing** — this list says buy two
boards because the dividers under a socketed devkit are tight soldering
alongside the actuation block. Two 70 × 90 boards side by side need ≥180 mm,
and stacking them over a socketed devkit will not fit in 40 mm of height. The
box is the item with lead time, so it should not be the thing that forces the
layout. Sized as sized for the board plus connector height plus wire bend radius, and for **three** harnesses (J5, J2's five wires and four dividers, J4). **PETG or ASA, not PLA** — it lives beside an espresso machine and PLA softens around 60 °C. |
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
| ~~Digital calipers~~ **owned** | 0 | The *only* measurement that separates JST XH from EH is depth — 5.7 vs 3.8 mm. Identifying this by eye failed three times. |
| Headband magnifier | 15 | |
| Flux pen + 0.5 mm solder | 12 | |
| Temperature-controlled iron, fine tip | 40–60 | Only if the current one is not adjustable |

Already owned and working: SeenGreat SG-NANO-DLA-A logic analyser,
test-hook grabber clips, ESP32-S3-DevKitC-1 N16R8, 2 × Bookoo Themis.

**The analyser is not 5 V tolerant** despite the listing's "0–5.5 V" —
74HC245PW on a 3.3 V rail behind 100 Ω. The series resistors above are
not optional.

---

## 7. No longer "deliberately not yet" — Phase 1 parts, fitted in Phase 0

**This heading said "Deliberately not yet" until 2026-10-12.** The 2026-10-08
build decision populates and bench-tests these **now**, behind open jumpers,
rather than leaving footprints — so they belong in the Phase 0 order. See
`docs/phase-0-plan.md` and hard invariant 1 as amended.

**`docs/phase-0-plan.md` is normative on the counts and placement, and THIS
list is normative on part numbers and prices.** Split that way because the two
files briefly each declared the other authoritative for the same parts, which
leaves a disagreement with no resolution rule — and they had already drifted
apart on the transistor, the drive-current claim and the box.

### What to order — part numbers confirmed 2026-10-12

| Part | Qty | Why this one |
|---|---|---|
| **AQY212EH, DIP-4** | **8** | 4 in service, 4 spare. **DATASHEET GRADE since 2026-10-17** — see "Datasheet read" below for what was read and what it settles. DIP-4 through-hole, SPST-NO (1 Form A) **AC/DC**, 60 V peak AC/DC, 0.55 A continuous, 5,000 V AC I/O isolation, all confirmed against Panasonic's own tables. **Bare `AQY212EH` is the through-hole part; `…EHA` is the surface-mount terminal version, tube-packed, `…EHAX`/`…EHAZ` are the same SMD part on tape-and-reel (picked from the 1/2-pin or 3/4-pin side)** — so buy the bare suffix, and a listing that says `EHAX` is SMD whatever its photo shows. |
| **`CD74HC221E`, PDIP-16** | 3 | **Verified 2026-10-12** against TI and Farnell: dual monostable, PDIP-16, 2–6 V, non-retriggerable ("once triggered, the outputs are independent of further trigger inputs"), active lifecycle, 5.2 mA output drive. `SN74HC221N` is scarce — TI is retiring PDIP. **A draft also listed `MC74HC221AN`, `TC74HC221AP` and `HD74HC221P` as equivalents; none could be verified**, and the nearest real hit, `MM74HC221AN`, is obsolete. Those were recollection. The `N`/`E`/`P` suffixes are through-hole; `D`/`PW`/`NS` are SMD. |
| **BC547 or 2N3904, TO-92** | 10 | **LED drivers, one per photoMOS.** **NOT the 2N7000**, which a draft of this row specified: its V_GS(th) is 0.8–3.0 V, so a worst-case part driven from 3.3 V logic has **0.2 V of overdrive** and R_DS(on) is only characterised at V_GS = 4.5/10 V. That is the same works-on-the-bench-fails-in-service trap as the 1 kΩ below, in the part bought to avoid it. A bipolar has no threshold lottery — 0.7 V of V_BE, cleared with enormous margin. **Base resistor 8.2 kΩ and base-emitter 100 kΩ, both in stock.** |
| **330 Ω or 390 Ω, 1 %** | 20 | **The one resistor value not in stock.** See the LED arithmetic below. |
| **2-pin male header** | 3 | the disables. **Buy no shunts** — open is the specified state and a loose shunt in the box is a hazard. Cut from a 1×40 male strip. |
| DIP-16 socket | 3 | wire the socket, verify, *then* insert the '221 — 16 hand-wired joints with no heat near the part. Pulling the IC is also a third disable, stronger than an open jumper. |
| Machined **round-pin** SIP socket strip 1×40 | 2 | **DIP-4 sockets are not a stocked item.** Cut 2-way pieces, two per photoMOS, 0.3 in apart. Soldering the photoMOS directly is acceptable if you cannot get these: a socket contact going intermittent fails **open**, which is the safe direction here. |
| Film capacitors, ~0.47 / 1 / 2.2 µF | few | one-shot RC. **Do not fix the value yet.** `t_W = 0.7 · R_x · C_x` **characterised at V_CC = 4.5 V** — and this build runs the '221 at **3.3 V**, where the coefficient is not guaranteed; Fairchild's version of the same part documents `PW = R·C` with no 0.7 at all, and part-to-part spread is ±5 %. At 150 kΩ from stock, 1 µF gives **105 ms**, not the ~120 ms a draft stated (120 ms needs 1.14 µF, or 171 kΩ at 1 µF). **Measure the pulse on the bench**, and only after the minimum latching press width is known. Minimum R_x ≈ 500 Ω. |

### The LED drive, which this list did not previously cost at all

**AQY212EH needs 5–10 mA recommended, and is only guaranteed to operate at
3.0 mA** (typical 1.2 mA is not a design figure; guaranteed off below 0.4 mA).
Two consequences:

- **1 kΩ is in stock and is the wrong value.** From 3.3 V it gives
  `(3.3 − 1.14 V_F − 0.1 V_ds) / 1 kΩ ≈ 2 mA` *(V_F 1.2 V until 2026-10-17; the datasheet says 1.14 V typ at 5 mA — conclusion unchanged)* — **above typical, below
  guaranteed**, the worst place to sit: it works on the bench and fails on
  some units. **330 Ω gives ~6 mA**; 680 Ω if you drive the LEDs from 5 V.
- **The '221's supply decides its input threshold.** A 74**HC** input needs
  VIH = 0.7 × VCC, so at 5 V that is 3.5 V against an ESP32's ~3.2 V output —
  it does not reliably meet it. Run the '221 at **3.3 V** (VIH 2.31 V) or buy
  **74HCT221** (VIH fixed at 2.0 V). You cannot do both: HCT is only specified
  at 4.5–5.5 V.
- **A transistor per LED, and the honest reason is the test, not the
  current.** A draft said "a 74HC221 at 3.3 V cannot source 3.0 mA, so the
  transistor was needed for the drive current regardless of the test". **That
  is an over-claim** — HC guarantees ±4 mA at 4.5 V and `CD74HC221E` lists
  5.2 mA; at 3.3 V, sinking 6 mA through ~100 Ω of output impedance costs
  ~0.6 V and still leaves ~4.5 mA, half again over the 3.0 mA guarantee. The
  '221 *can* drive this LED directly; it just cannot do it with the margin the
  5–10 mA recommendation wants. **The sufficient reason is the per-device
  test**, which needs each LED on its own path.
- **Base-emitter resistor, 100 kΩ, in stock.** A draft justified the
  equivalent part with "the board unpowered… a bare gate can drift and turn
  the LED on" — **vacuous**: with the board unpowered there is no 3.3 V on the
  LED's 330 Ω feed either, so nothing conducts. **The real case is one this
  build creates**: pulling the '221 from its socket, which this list makes a
  deliberate third disable, leaves four bases floating **with the rail up**.

### Datasheet read — 2026-10-17

**Documents read (both Panasonic's, neither a distributor's paraphrase):**

| | Document | Date / revision | sha256 (first 16) |
|---|---|---|---|
| A | **Panasonic Relay Catalog Part 2: PhotoMOS & Solid State Relays**, `ca_semiconductor1154_en.pdf` from `mediap.industry.panasonic.eu`, 356 pp; AQY21•EH section is printed p.146-149, document id **`ds_x615_en_aqy21eh: 010611J`** | PDF created **2011-10-04** | `8459c7ae3ad528cb` |
| B | **"GU-E PhotoMOS (AQY21•EH)"**, 4-page datasheet, `AQY212EH-Panasonic-datasheet-109895.pdf` from `datasheet.octopart.com` — a **third-party mirror** of a Panasonic datasheet bearing the "Matsushita Electric Works" copyright and a "TESTING" banner | PDF created **2005-03-07**, modified 2005-10-06 | `71c9b77a7bed8328` |

**Neither is a current Panasonic datasheet.** A is Panasonic-hosted but a
2011 catalogue; B is a 2005 mirror. `industrial.panasonic.com` refused
the fetch from this machine, so the current per-part datasheet was **not**
read. A and B **agree on every number compared** (ratings, electrical
table, symbol), which is some comfort that nothing changed 2005→2011 — it
says nothing about 2011→now. A search snippet (not the notice itself, unread) mentions a Panasonic dimensional-tolerance unification for DIP4 starting September 2025;
**that touches package drawings, not electricals**, but it is a reason to
re-fetch before the order if anything dimensional ever matters (the 0.3 in
row spacing does).

**Confirmed (datasheet grade):**

| Claim | Datasheet |
|---|---|
| Package / type | DIP 4-pin, 1 Form A, **AC/DC dual use** (A: "AC/DC dual use", "Peak AC, DC"); 6.4 × 4.78 × 3.2 mm |
| Load | **60 V peak AC/DC, 0.55 A continuous, 1.5 A peak (100 ms, DC)** |
| Isolation | **5,000 V AC** I/O; I/O resistance ≥ 1,000 MΩ at 500 V DC; C_iso 0.8 pF typ. *(A draft appended "reinforced" to the voltage. That is a **certification class** under IEC 60664 / 62368, not an isolation-voltage figure, and nothing read supports it — the documents quote a withstand voltage and nothing about creepage, clearance or class. Not load-bearing here: both sides of this barrier are SELV, so it is a 5 V-to-3.3 V separation and not a mains barrier.)* |
| LED operate current | **1.2 mA typ, 3.0 mA max** at I_L = max, **25 °C** |
| LED turn-off current | **0.4 mA min, 1.1 mA typ** — "guaranteed OFF below 0.4 mA" is right |
| LED forward drop | **1.25 V typ, 1.5 V max at I_F = 50 mA; 1.14 V typ at 5 mA** |
| Recommended I_F | **5-10 mA** |
| On-resistance | **0.85 Ω typ, 2.5 Ω max**, at I_F = 5 mA **and I_L = max (550 mA)**, "within 1 s on time" |
| Turn-on / turn-off | **1 ms typ / 4 ms max; 0.05 ms typ / 1.0 ms max** (I_F = 5 mA, I_L = max) |
| Off-state leakage | **1 µA max** at I_F = 0, V_L = max |
| Abs max LED | I_F 50 mA, V_R 5 V |
| Operating temp | −40 to +85 °C |

**Things the datasheet changes or adds:**

- **LED V_F: the 1.2 V assumption is fine**, slightly pessimistic at 25 °C
  (1.14 V). Typical-sample curve 7 puts V_F at 5 mA between about 1.02 V
  (+85 °C) and 1.22 V (−40 °C). 330 Ω from 3.3 V through a BC547 (V_CE(sat)
  ~0.1 V, an estimate) is **≈ 6.2 mA nominal**; with the rail 5 % low, V_F at
  the 1.5 V (50 mA) ceiling and V_CE 0.2 V it is still **≈ 4.3 mA**. Above 3.0
  mA either way.
- **NEW — the 3.0 mA guarantee is a 25 °C figure.** The electrical table is
  headed "Ambient temperature: 25 °C", and the operate-current-vs-temperature
  plot (curve 5, a typical sample) rises from ~0.6 mA at −40 °C to **~3.1 mA
  at +85 °C**. A box that sits near a boiler can run warm. The 4.3-6 mA design
  still clears that curve, but **the datasheet bounds unit-to-unit spread only
  at 25 °C**, so the margin at 85 °C is unquantified. This is the same
  "works on the bench, fails in service" family as the 1 kΩ trap above, and it
  is a further reason not to economise on the 330 Ω.
- **On-resistance is NOT specified at this circuit's current.** 0.85/2.5 Ω is
  at 550 mA. At ~0.1-1 mA the drop is ~1-2.5 mV *if* the channel stays ohmic,
  which the output I-V plot (8-(2)) supports around the origin but does not
  state as a spec. Immaterial to the 100 Ω R45 loop. *(Plot 8-(2) is labelled
  "Current, mA" with an axis of ±1.5, but a 0.25 Ω part reaching 1.0 at 0.3 V
  is 1.2 A, not mA — the label looks like a datasheet unit error; read it as
  amps. My reading, not Panasonic's statement.)*
- **Turn-on up to 4 ms** is far inside the ~120 ms one-shot pulse and a
  non-issue.
- **Body diodes: NOT specified.** The output schematic is two common-source
  N-MOSFETs; neither document draws or characterises a body diode. See the
  re-graded paragraph in `phase-0-plan.md` (series-redundancy), which this
  read settles at "topology datasheet, body diode inferred" and refines.
- **Not settled here:** TLP222A (the plan's alternate) was not read.

**Correction of record:** this row used to say the figures were "Distributor-
listing grade, 2026-10-12 (Distrelec, Farnell, Conrad agree)", and that a
draft had "used it to upgrade the plan's body-diode analysis to 'rests on the
right structure'". The listing figures were **all right**; what the listings
could not supply was the output topology, and they were never going to. One
listing-site oddity worth knowing: LCSC/JLCPCB print **850 mΩ** for R_DS(on)
and Farnell/Future **2.5 Ω** — both are real, **typical vs maximum**, of the
same 550 mA test.

### Still not costed

The per-device test needs each photoMOS on its **own** one-shot-bounded drive.
A dual '221 gives two channels — enough for one per *line*, not one per
*device*. A second '221 or a different gating scheme is unpriced; the GPIO
budget is not the constraint (see CLAUDE.md's board-read pinout). Buying three
'221s covers it either way.

These are the parts that let the firmware *trigger* the machine while being
physically incapable of *holding* the line — which matters because holding
>2 s silently reprograms that direction's stored timer.

**NOT the 74HC123**, which this list and CLAUDE.md both used to name. The
'123 is **retriggerable**: a trigger oscillating faster than ~8 Hz — a boot
loop, a brown-out, a floating pin — restarts the pulse before it ends and
holds the output high indefinitely, which is the exact failure the one-shot
exists to prevent. The NE555 has the mirror-image trap (a trigger held low
holds the output high) and needs an RC+diode differentiator. The '221 needs
neither.

A **photoMOS** rather than a bipolar optocoupler: it is a true contact
replacement, bidirectional, with no saturation offset, and its on-resistance
is irrelevant against a press loop of ≥5 kΩ (not against R45's 100 Ω, as this
and the plan both said until 2026-10-12). (CLAUDE.md's "not an optocoupler" warning is about
*sensing*, where milliamps would break the board's own threshold. It does
not apply to the drive path.)

**Scope: drive two lines, sense four.** Brew-by-weight needs 1Cup and 2Cup
only — the board latches, so one pulse starts and a second stops. Steam and
Hot Water double the parts and the failure surface for nothing.

**The design is in `docs/phase-0-plan.md`**, including the harness change:
J2 position 1 (+5 V common) now rides in the loom, which it deliberately did
not before 2026-10-12.

**ORDER THESE WITH THE PERFBOARD.** This block said "don't order them yet,
the trigger path still depends on 0c being built and trusted first" — 0c was
built and proven on 2026-10-08, and the 2026-10-12 build decision fits these
parts in Phase 0 behind open jumpers. The reason not to order expired twice
over.

---

## Rough total

Connectors are **bought** — the mixed balance-lead pack was €6.99 and
should cover J5, J2 and J4, pending the seat check in section 1.

Still outstanding: adhesive-lined heat-shrink, PTFE sleeving, perfboard,
cable gland, XH board headers, sockets and the §7 actuation parts —
**~€83 all in**, revised 2026-10-12. **The project box is NOT on this list:
it is printed, €0.** The superseded figure was **~€40** (a draft said €42),
and it predates the box, the second perfboard, the photoMOS **spares** taking
the order from 4 to 8 (the in-service count was 4 before and is 4 now), the XH
board headers, the sockets and the LED-drive parts.

| | ~€ |
|---|---|
| perfboard ×2, female strips ×5, DIP-16 and SIP sockets | 15 |
| XH board headers (€3) + pre-crimped kit (€10) | 13 |
| second balance-lead pack | 7 |
| actuation: `CD74HC221E` ×3, AQY212EH ×8, BC547 ×10, 330 Ω, caps | 25 |
| consumables: PTFE sleeving 8, adhesive heat-shrink 6, 30 AWG silicone wire 8, gland 3, tie mounts 4 | 29 |
| **total** | **~€89** |

*(A draft quoted €9 for the board group and €26 for consumables, giving €80;
the board row had not been re-costed after it grew to two boards and five
strips, and the consumables row omitted the 30 AWG wire, which has no row in
§5 at all and is priced only in `docs/phase-0-plan.md`.)*

**Every resistor this project needs is now in hand (2026-10-08).** 8.2 K,
15 K, 100 K, 150 K, 220 K and 1 K are all in stock; the E12 kit is Qty 0 and
not needed. **The perfboard build IS now gated on parts**, which it was not
before 2026-10-12: it needs a `CD74HC221E`, photoMOS, BC547s, 330 Ω, XH board
headers, sockets and perfboard — none of them bought.

**Note what changed:** 0d was specified as 120 K, which this list claimed was
in stock on 2026-10-07. It was not — the bag held 100 K and 150 K. 0d now
uses **100 K**, which is better here (see CLAUDE.md), and **150 K must not be
substituted at J4** — it fails at −45 mV.

What is left to buy is for the **perfboard stage, not the bench**: perfboard
×2, female header strips, DIP-16 and SIP sockets, XH board headers, the
pre-crimped kit, a second balance-lead pack, cable gland, tie mounts,
adhesive-lined heat-shrink, PTFE sleeving, 30 AWG silicone wire and the §7
actuation parts. **~€89**; the box is printed, not bought. The whole
breadboard bench sequence can be done without any of it. *(Earlier versions
of this sentence and the breakdown above listed different subsets and
different totals; both now match the table in §Rough total.)*

**0d is no longer gated on firmware.** It was, for about an hour: the
counter landed in 0.9.0 (`PulseCounter`, `flowIsr`, `sample_t.inlet_pulses`
written through to SQLite), and this paragraph was rewritten in the very
commit that falsified it — a reader who trusted it would skip the bench
`selftest` and take the first shot's pulse count on faith. What gates 0d now
is the bench run, the J4 refit (read `docs/phase-0-status.md` first: `E01`)
and the calibration weigh-off.

Add calipers (~€15) if you want the connector
question settled by measurement rather than re-derived from photographs.
The pre-crimped kit (~€10, **counted in the total above as a firm line
item** — an earlier closing line offered it as contingent) and the Ascaso `I.4312` harness (~€15) only if
a balance lead disappoints.
