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
| JST XH connectors (`XHP-4/5/3`, `SXH-001T-P0.6`, `B*B-XH-A`) | J5 / J2 / J4 taps — female side only, see §2 |
| Silicone hookup wire, 26 AWG | all harnesses |
| 150 kΩ resistors | 0c switch dividers |
| Multimeter | replacing the broken one |

That order is **not** a complete T-piece: it covers the crimp problem
only if the contacts came pre-crimped, and it has no male cable end at all.
Read section 2 before assuming it is done.

---

## 2. The one that will stall the build

**JST XH crimp contacts need a crimp tool.** `SXH-001T-P0.6` is a tiny
open-barrel contact with two separate crimp wings — wire and insulation —
and pliers do not produce a contact that seats in the housing.

**Solved by a pre-crimped kit.** The multi-pin XH kits sold for LiPo
balance leads (2–10 pin housings, ~90 pre-crimped wires, tweezers, no tool
required — you push the contact into the housing) cover this for about €10.
Look for one whose wire is **silicone**, not PVC: 22 AWG silicone is the
top of the `SXH-001T-P0.6` range (AWG 28–22) and ~200 °C, so the same wire
does the T-piece *and* the run out of the machine with no splice.

Two things to check on any such listing:

- **"XH 2,54 mm" is the usual misnomer** — XH is 2.5 mm. A listing that
  mentions LiPo balance leads is very likely the right part whatever the
  title says, because balance connectors *are* XH — but the male ends on
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
count, so the obvious-looking 2S is a 3-pin and will not fit J5. 22 AWG
silicone, 200 mm, ~€5 for five; sellers carry 2S–6S on the same listing.

Build: plug in line, strip a window mid-cable, solder branch wires to the
conductors you want, adhesive-lined heat-shrink over it. No rigid lump
inside the machine, and unplugging returns the machine to stock.

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
| **3S** LiPo balance extension lead (**4**-pin, male↔female) | 2 | 5 |
| — optional — pre-crimped XH kit, for detachable tap ends | 1 | 10 |
| — fallback — `B4B-XH-A` header + perfboard | 5 | 3 |
| — fallback — Ascaso display harness `I.4312` (spare) | 1 | 10–20 |

A 3S balance extension, solder and heat-shrink is the whole T. Ascaso's own spare
harness, **part `I.4312`** (mainboard is `I.3957`, matching the board
silkscreen), is the last fallback if anything about the generic connector
turns out not to mate — cut it and both ends are guaranteed by
construction. Worth knowing the part number exists; not worth buying first.

Wire the T straight through and branch three taps: `ESD1` (clock), `ESD4`
(data), ground. **Do not branch `Disp-P4` to anything that can source
current** — it is bidirectional, and driving it is the one connection on
J5 that could actuate the machine.

---

## 4. Resistors

A generic E12 kit covers most of it, but the four values that matter are
worth buying as dedicated bags so you are not down to your last two at
11 pm. **1 % metal film, 1/4 W.**

| Value | Qty | Where |
|---|---|---|
| 8.2 kΩ | 10 | 0e display divider, series leg (×2); 0b analyser tap (×3) |
| 15 kΩ | 10 | 0e display divider, ground leg (×2) |
| 150 kΩ | 10 | 0c switch divider, series leg — **already ordered** |
| 220 kΩ | 10 | 0c switch divider, ground leg |
| 1 kΩ | 10 | 0d flowmeter series protection (×2) |
| 1.5 kΩ | 10 | 0b analyser ground lead |
| E12 assortment kit, 1/4 W | 1 | everything else |

Two values are load-bearing — 0c is in CLAUDE.md, 0e is worked out in
`docs/phase-0-status.md`. Repeated here because getting either wrong fails
silently:

- **0c must be 150 K / 220 K, never 220 K / 220 K.** 220/220 gives 2.50 V
  against an ESP32-S3 VIH of 2.475 V. Supply tolerance alone eats that
  margin and a pressed button reads as not pressed.
- **0e must be 8.2 K / 15 K, and do not enable an internal pull-down.**
  The internal 45 K across the 15 K leg drops the open-drain case to
  2.33 V and the pin stops reading high.

---

## 5. Build and enclosure

| Item | ~€ | Notes |
|---|---|---|
| PTFE or fibreglass sleeving, 2–4 mm, 200 °C+ | 8 | Silicone wire is good to ~200 °C on its own, so this is for abrasion and for anything routed near the boiler or group. Ordinary PVC sleeving and standard heat-shrink are not. |
| Heat-shrink assortment, **adhesive-lined** | 6 | Strain relief at the T-piece branches, and the PVC-to-silicone splices. Adhesive-lined or nothing — plain heat-shrink is not insulation on its own. |
| Perfboard + pin headers | 6 | The dividers want to live on a board, not in mid-air |
| Small ABS project box | 6 | ESP32 mounts **outside** the machine |
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
| **Multimeter** | 25–40 | **Already ordered** — if it is not too late, pick one with a **K-type thermocouple**, which settles the PVC question above. Continuity beep and auto-range are the features that matter. Its absence cost two full evenings of guessing at connectors from photographs. |
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

Pre-crimped XH kit, balance extensions, resistors, sleeving, box and
consumables:
**~€55**. Add calipers (~€15) if you want the connector question settled
by measurement rather than re-derived from photographs. The Ascaso
`I.4312` harness (~€15) only if the generic connector disappoints.
