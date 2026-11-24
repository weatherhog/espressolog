# espressolog board — PCB (sense + drive)

One board to replace the hand-wired perfboard. The devkit sits in sockets,
both the sense dividers and the drive side are on it, and **the T-pieces are
on it too** (decided 2026-11-24). The perfboard keeps logging on the machine
until this board has passed its bench test. Decided 2026-11-24.

**The source is `gen_schematic.py`, not the drawing.** The schematic is
label-based: every pin has a short stub and a net label, so the connectivity is
exactly the `part(...)` calls in the script. Edit the script and regenerate.
Do not edit the `.kicad_sch` by hand.

```bash
python3 gen_schematic.py --check
```

That writes `espressolog-board.kicad_sch` and the symbol library, then runs ERC
and exports the netlist from KiCad. It fails if any net differs from the script.
That proves the drawing matches the script. **It does not prove the script
matches the docs**: that needs a review against `docs/board-and-perfboard.md`
("Sense-side layout", "Drive-side layout") and `firmware/src/main.cpp`.

Needs KiCad 10 (`brew install --cask kicad`). Outputs go to `build/`, which is
not committed.

## The T-pieces are on the board

Each machine connector has two headers on the top edge:
- **IN** (J5, J4, J3, J2): side-entry, opening at the edge. The machine's
  **original harness plug** mates here.
- **OUT** (J25, J24, J23, J22): vertical, right behind its IN. A
  ready-made XH cable runs from here to the machine board's own header.
  **It must map pin 1 to pin 1.** Don't buy by the direction word: in
  同向 / 反向 ("same" / "reverse") listings, the pin-1-to-pin-1 cable is
  probably the one whose latches face OPPOSITE sides when laid flat. Two
  identical plugs on an uncrossed cable, both latches up, are mirrored
  (final review S1). Metering step 1 below decides, whatever the listing
  says.

**IN pin k is joined to OUT pin k by one straight track, for every line.** A
plug mates any XH header of its size the same way, so the machine sees its
own harness, conductor for conductor. The taps branch off the OUT pins. IN
and OUT of one connector are electrically interchangeable.

**A plug in the WRONG connector's header is not ruled out (unverified).**
The four have different pin counts (2, 3, 4, 5), but XH plugs are not
width-keyed against a larger housing, so a smaller plug may seat offset in a
bigger header. Example: the J4 flowmeter plug in J2 IN at pins 1–3 puts the
flowmeter's `+` on pos 1 and its `T` on raw 2Cup, which is a held press. Try
each plug in each wrong header at the 1:1 print step (below).

**Operating consequence: the machine's switches, display and flowmeter all
run through this board. Without it, the machine runs only with the original
plugs back on its own headers.**

**The NTC plug stays on the machine's own J3 header for now** (pre-order
review S2). The J3 pass-through exists on the board, but an open or
resistive joint in it reads as a cold boiler, and with the tap unfitted it
delivers nothing. Before it is used:
- design and fit the tap;
- confirm J3 is a 2-pin XH by mating a real part;
- find out, machine cold, what the machine does when J3 is pulled;
- add the J3 rows to the metering below.

**The taps load the machine even when the ESP32 is off, and they cannot be
unplugged separately** (S12). With the devkit fitted but unpowered, its GPIO
protection diodes clamp the tap nodes, and the J5 bus high level is
calculated to drop from 4.20 V to about 3.5–3.8 V. That is at the floor the
display needs (`docs/phase-0-status.md`). **Measure it once**: machine on,
USB out, J25 pin 2 and pin 3 against TP1. Record the result here. If the
display misreads in that state, the cure is to pull the devkit or go back to
the original plugs.

**Pin map, by JST pin number** (pad 1 is the right-hand pin; the silk at the
top edge names each column, which is the same pin on IN and OUT):

| Conn | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 | Source |
|---|---|---|---|---|---|---|
| J5 / J25 | `Disp-P4` (P4) | `ESD4` clock (CK) | `ESD1` data (DA) | `VSS` (0V) | | pad 1 read off the plug (2026-11-24); the rest positional, `docs/machine-display-bus.md` |
| J4 / J24 | `#` | **`+`** (middle, green) | `T` | | | read off the machine-side plug, Lucas, 2026-11-24 (`J4_T_PIN = "3"` in `gen_schematic.py`) |
| J3 / J23 | NTC P1 | NTC P2 | | | | roles unmeasured; the tap is DNP |
| J2 / J22 | pos 1 +5 V | Steam | 2Cup | 1Cup | Water | pad 1 read off the plug (2026-11-24); the rest positional (`CLAUDE.md`, J2 table) |

- **J4 `+` and J5 `Disp-P4` cross the board as isolated pass-through traces.**
  They have no pad but their own header pins, no test point, no via, no pour
  nearby, and keep 2 mm from everything. Nothing connects to them (the J4
  `+` rule in `CLAUDE.md`).
- **The machine's ground (J5 `VSS`, J4 `T`) is our ground.** It passes
  straight through IN→OUT, and each ties to the devkit GND (J12 pin 1) by its
  own track. The J2 dividers' only reference is this ground.

## What the board carries

| Part | Note |
|---|---|
| J11 / J12 | devkit signal column (A) and opposite column (K), 1×22 female headers 25.4 mm apart, pin 1 (3V3) at the top. **Nothing on 5Vin.** Outline on the silkscreen |
| J2–J5 / J22–J25 | IN / OUT, above |
| R1–R16, C1–C3 | sense series legs and shunts. C1–C3 are DNP footprints for the candidate shunt caps (470 pF / 470 pF / 10 nF; **never 10 nF on ESD1/ESD4**) |
| R60–R63, C20–C22, U8 | **J3 NTC tap, ALL DNP** until J3 is measured and the tap has its own design pass. The machine's PID regulates on this NTC, so any load shifts the brew temperature, and a fault that reads cold keeps the heater on. **Fit ONE 10 M** (R60 from pin 1 or R61 from pin 2, whichever is the divider node), because both together put 20 MΩ across the NTC. The rest: R62 an optional shunt (only if the node is 5 V referenced), C20 10 nF, U8 MCP6002 unity follower (half B tied off), R63 1 K + C21 100 nF into GPIO1, C22 decoupling. Wash the flux off: leakage matters at 10 MΩ |
| U1, U2 | drive '221s, in sockets. **Crossed (pre-order review S1): U1 drives photoMOS 1 of each chain (U4, U6), U2 drives photoMOS 2 of each (U5, U7)**, so a fault in one package closes at most one device per chain. The perfboard doc has U1 = 1Cup, U2 = 2Cup. GPIOs, net names and firmware are the same either way |
| U3 | the PROPOSED gate '221. **Socket fitted, chip out, and R36 out with it** (R36 is marked DNP) |
| U4–U7 | AQY212EH in a DIP-8 footprint, with the chip at the pin-1 end of an uncut DIL08PZ (or soldered straight in). Pads 3–6 are not connected |
| Q1–Q4 + R40–R51 | LED-driver cells: 330 Ω, BC547B, 100 K, 8.2 K |
| JP1 (a1), JP2 (a2) | in series with the photoMOS outputs. **OPEN through Phase 0** (invariant 1) |
| JP3 (b) | LED supply. **OPEN until the bench test** |
| JP4 | selftest GPIO8–GPIO6. **Never fitted in service** |
| TP1 / TP2 | GND and 3V3 test loops for the scope and analyser |

**GPIO changes against the perfboard:** U1 pin 13's readback (1Cup
photoMOS 1) is on **GPIO11** (signal column position 17), and **GPIO1**
(opposite column position 4) is the NTC's ADC input (`30beb28`).

## Not settled. Check before ordering

- **J4: `+` is the MIDDLE pin; which outer pin is `T` is `J4_T_PIN`.** The
  first T-piece commit (`ee2795e`) took `docs/phase-0-status.md`'s "`T`
  (middle)" at its word and tied the machine's `+` to our GND. That doc was
  wrong, and Lucas read the plug. `T` is tied to our GND, so its side came
  from the plug, not a guess. If it ever has to change, it is one setting:
  R3 and the `T` ground anchor move with it.
- **Plug orientation: CONFIRMED 2026-11-24 for J4, J5 and J2.** Lucas read
  each machine-side plug from its wire end with the latches up (the latch
  faces up on a side-entry header, also confirmed), which mirrors left and
  right against the board frame:
  - J4: `+` (green) in the middle, `#` (black) on pad 1, `T` (yellow) on
    pad 3;
  - J5: `Disp-P4` on pad 1;
  - J2: the +5 V common on pad 1.

  The pre-power check below still confirms all three on the built board.
- **Capacitor lead pitch, not measured.** The footprints assume 5.0 mm for the
  1 µF MKT and the 100 nF / 470 pF / 10 nF discs. Measure the parts you have.
- **BC547 footprint:** `TO-92_Inline_Wide`, with legs 2.54 mm apart like the
  perfboard's (the standard 1.27 mm one puts the collector–base pair close
  together). Pad 1 is C, and the review checked this against KiCad's stock
  BC547. A 2N3904 would go in reversed.
- **One GND net.** The perfboard kept separate drive (K29) and sense (K30)
  ground ties. Signal-column GND (A22, beside 5Vin) is deliberately left
  unused, as on the perfboard.
- **AQY212EH in an uncut DIP-8 footprint** where the board doc says DIP-4
  sockets: same netlist, and a chip pushed in at the wrong end lands on dead pads.
- **Box headroom above the top edge.** The OUT headers are vertical. A plug
  plus its cable bend needs ~20–25 mm above the board.
- **The open design items stay open**: the readback value (1 K proposed), the
  gate RC (470 K candidate), and whether the gate stays. They are on ordinary
  footprints, so values are chosen at soldering time. Dropping the gate is a
  bodge wire, GPIO38 to the arm net, not a solder jumper. The minimum-gap stage
  on U3's spare channel is not designed, so U3 channel 2 is tied off.

## Before first fit: meter it

Board unpowered, devkit out, DNP parts unfitted, the NTC plug left on the
machine.

0. **The jumpers are open.** Each of JP1, JP2, JP3 and JP4 reads open across
   its two posts. J22 pin 1 reads open to U4 pad 8 and to U6 pad 8. The posts
   are 0.84 mm apart and hand-soldered, and a bridged JP1 or JP2 is invisible
   from the connectors. Invariant 1 rests on this.

1. **Each OUT cable, on this board.** Plug its two ends into that
   connector's own IN and OUT headers (no harness, no machine).
   - **Correct cable:** pin 1 ↔ pin 1. Each line only loops back to itself,
     so no two different pins are joined.
   - **Mirrored cable:** pin k ↔ pin n+1−k, so pin 1 is shorted to pin n.

   No cavity numbering is needed. Meter on the IN header's solder side:
   - J5: pin 1 – pin 4 must read open (mirrored: `Disp-P4` on GND), and
     pin 2 – pin 3 must read ~49 kΩ (mirrored: ~0);
   - J4: pin 1 – pin 3 must read ~320 kΩ (mirrored: `#` on GND, the `E01`
     class);
   - J3: pin 1 – pin 2 must read open;
   - J2: pin 1 – pin 5 must read open, and pin 2 – pin 4 ~740 kΩ.

   **Any ~0 Ω reading means a mirrored cable, which must not be used.**

   This step finds a mirrored cable and nothing else: an unseated cable or a
   broken conductor reads the same as a good one. So, **for every conductor**:
   one end in the OUT header, probe each contact of the free plug to the
   same pin of the IN header, ~0 Ω.
2. **Board pass-through.** IN pin k reads ~0 Ω to OUT pin k, and **not
   ~0 Ω** to any other pin of either header. The readings between different
   pins are the board's own dividers, not opens:
   - J5: pin 2–3 ≈ 49 kΩ, pin 2 or 3 to pin 4 ≈ 24.6 kΩ, pin 1 open to all;
   - J4: pin 1–3 ≈ 320 kΩ, pin 2 open to all;
   - J2: any two of pins 2–5 ≈ 740 kΩ, pin 1 open to all;
   - J3: open.

   A ~0 Ω reading is a solder bridge.
3. **The isolated lines.** J24 pin 2 (`+`, the middle one) and J25 pin 1
   (`Disp-P4`) read open to every other pin, to TP1 (GND) and to TP2 (3V3).
   J23 pins 1 and 2 read open to everything.
4. **Ground.** J25 pin 4 and the J24 `T` pin read ~0 Ω to TP1, and **J24
   pin 2 does NOT** (that reading is the `ee2795e` short). No J2 pin reads
   ~0 to TP1.
5. **Taps by value**, header pin to devkit socket position:
   - 8.2 kΩ: J25 pin 3 → A5 and J25 pin 2 → A4;
   - 100 kΩ: the J24 `#` pin → A6;
   - 150 kΩ: J22 pins 5, 4, 3, 2 → A10, A7, A8, A9 (the 100 K and 150 K bags
     look alike).
   - Then the shunts, socket position to TP1: A4 and A5 16.4 kΩ, A6–A10
     220 kΩ.
6. **At the machine**, machine off and unplugged from the mains, OUT cables
   to the machine board, harness plugs NOT on the IN headers:
   - TP1 reads ~0 Ω to J5's spoked pad (`VSS`) and to J4's `T`;
   - J24 pin 2 reads ~0 Ω to the machine's J4 `+` and open to TP1;
   - the J24 `#` pin reads ~0 Ω to `#`;
   - J25 pin 1 reads ~0 Ω to J5's square pad (`Disp-P4`);
   - J22 pin 1 reads ~0 Ω to the machine's J2 common, the pin in every
     lever's continuity pair.

   This confirms the J5, J4 and J2 orientation on the real machine.

   **If the machine board's pins can't be probed** (back-probing J4 gave no
   reading before), there is a J4 check that needs no access to them, derived
   from the 2026-10-04 readings and not yet tried in this form: with the J24
   cable on the unpowered machine and no harness plug, **J24 pin 1 – pin 2
   reads ≈ 10 kΩ** (the machine's pull-up from `#` to `+`), and TP1 – J24
   pin 2 does not. A mirrored J4 cable swaps those two readings.
7. Only then do the harness plugs go on the IN headers (not the NTC's).

**`selftest`:** "unplug J4 first" now means the **J24 OUT cable**. Pulling
only the harness plug leaves GPIO8 on the machine's `#` node through 100 K.

## Building the board

Placement, routing and finishing are scripts too. Each runs with KiCad's
Python (`KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3`):

```bash
python3 gen_schematic.py --check   # schematic, ERC, netlist round-trip
$KPY gen_pcb.py                    # placement + the machine nets as locked tracks
$KPY route.py                      # Freerouting the rest, headless (Java 21+, jar in ~/.local/share/freerouting)
$KPY route.py --keep               # second pass: keeps the routes, completes what is still open
$KPY finish.py                     # GND pours (both layers), keep-outs, labels, refs
```

`route.py` can be re-run on a routed board. It strips everything except the
locked tracks in a separate process, then routes. **One pass does not always
finish**: with the tight rules the router can leave a connection open, and
the result varies from run to run. `route.py --keep` routes again on top of
what is there. Repeat it until DRC after `finish.py` shows 0 unconnected.
While routing, the router is kept out of the J5/J4 header block and 1 mm off
every board edge; if the re-lay step fails, `route.py` deletes
`build/routed.kicad_pcb` so that `finish.py` cannot finish a stale board. `finish.py` starts from
`build/routed.kicad_pcb`, so run `route.py` first. `finish.py` stops if J4's
pinout or the two isolated pass-throughs are not what was confirmed.

Then run DRC and export with `kicad-cli`. The current board: **DRC 0 violations,
0 unconnected**. The `fab/` folder holds the Gerber + drill zip (for
JLCPCB/PCBWay: 2 layers, 1.6 mm FR-4, 100 × 100 mm), the BOM and
`espressolog-board-1to1-print.pdf`.

**The machine nets are hand-laid, not autorouted** (`MACHINE_TRACKS` in
`gen_pcb.py`, since the 2026-11-24 layout review). That covers every
pass-through, the fans to the taps, pos 1, the drive chains, the returns and
the two ground anchors. Freerouting treated the old netclass as "keep 0.6 mm
away" and nothing more. It ran the raw 1Cup/2Cup returns past LED anode pads,
so one short plus (b) fitted was a held press that no (a) jumper blocks. It
also ran the (a2) feed past U5's raw-1Cup pin and along raw 2Cup, so one
short with (a2) fitted got around both series photoMOS. The layout now:
- **Everything machine-side stays on the output side of the photoMOS.**
  - Pos 1 runs along the top edge to (a1)/(a2).
  - A1_LEG (F.Cu) drops in a lane at x 88.5, above the stack.
  - A2_LEG (F.Cu) runs in a lane at x 96.4.
  - The MID links jog round the empty socket pads.
  - The returns run on B.Cu: 1Cup in lane x 94.7, 2Cup in lane x 98.6.
    Each comes in from the right at its own resistor row. 2Cup's row is above
    1Cup's, so the two lanes never cross.
  - J2's 150 K lie flat in staggered rows. Water's raw pad sits 3 mm left of
    its pin and 2Cup's 1.5 mm right of its pin, and the Steam fan runs down
    its own lane at x 82.6. Below the header, no lever's copper comes within
    2 mm of another lever's.
- **Legs and returns are on opposite layers where they cross** (A1_LEG over
  both returns, A2_LEG over the 2Cup return at one point, the Steam fan over
  both returns). A2_LEG also runs 1.8 mm beside its own return's lane for
  36 mm, through the board. No clearance rule sees opposite layers; this list
  is the check.
- **Two nets of ours are hand-laid too** (`OUR_FIXED_TRACKS`):
  - **The +3V3 trunk** leaves J11 under the devkit and ends on R20. The router
    is kept out of the J5/J4 header block (`ROUTE_KEEPOUT_ON`), so nothing of
    ours runs between the IN and OUT rows.
  - **LED_RAIL** is a trunk left of the cells with a stub per cell. It reaches
    JP3 pin 2 from below, 1.5 mm from JP3 pin 1.

**Rules on the board** (`espressolog-board.kicad_dru` with the netclasses):
- **`Machine` nets keep 2 mm from everything of ours** and 1 mm from each
  other. That covers every `*_RAW` net (incl. J4 `+`, J5 `Disp-P4`, the J3
  NTC), pos 1 and the drive chains. The exceptions:
  - a track leaving an XH header or jumper pin may pass that part's other
    pins at 1.2 mm (the machine's own pitch);
  - the hand-laid (locked) ground tracks may pass their connector's machine
    pins at 1.2 mm (J4/J24 and J5/J25 only). The router's GND gets no
    exception;
  - pads of one connector, jumper or AQY may sit at their own pitch;
  - the empty half of each photoMOS socket (U4–U7 pads 3–6) needs 0.6 mm.
    Every other pad with no net counts as live: devkit pins, the '221s' `Q̄`
    outputs, the mounting holes.
- **The 1Cup and 2Cup chains keep 2.5 mm apart** (`1CUP_RAW`/`MID_1CUP`/
  `A1_LEG` against `2CUP_RAW`/`MID_2CUP`/`A2_LEG`). The exemptions are the
  J2/J22 pins and the tracks touching the "J2 fan zone" rule area (finish.py,
  y < 23.0): the first mm leaving the 2.5 mm header pitch. The fans are cut
  at the zone edge so the exemption ends there. This is what would catch a
  repeat of the first review's finding 2.
- **A leg keeps 2.5 mm from its own return, and pos 1 from the lever lines**
  (track to track). A short there bypasses both photoMOS, or the jumper too.
- **Drive-side nets keep 0.45 mm from everything** (netclass `Drive`:
  LED_RAIL, the LED, base, `Q`, trigger, timing and arm nets). With that,
  nothing can be routed between two pads at 2.54 mm pitch, such as a TO-92's
  legs. At 0.6 mm the router left connections open.
- **KiCad 10 accepts only some condition syntax.** `A.NetClass == '…'` matches
  nothing, so use `hasNetclass()`. `Parent.Reference` doesn't exist, so use
  `memberOfFootprint()`. One condition KiCad can't parse disables the whole
  file and DRC falls back to the netclasses, which is how these rules were
  inert in the first draft. After editing the file, check that a DRC report
  names the custom rules.
- Netclass track widths: signals and `Drive` 0.3 mm, `Power` (+3V3) and
  `Ground` 0.5 mm, `Machine` 0.4 mm (its clearance is the 2 mm above). The
  netclass patterns start with `*`, so they still match if KiCad renames the
  nets to `/NAME`.
- Freerouting narrows a few signal tracks, down to 0.15 mm (the board minimum;
  JLCPCB makes 0.127).
- **No ground pour** in:
  - the machine corner (J2, its 150 K, pos 1, (a1)/(a2), the returns);
  - the photoMOS output column and its lanes;
  - the J5/J4/J3 pass-through block, down to the J3 tap's raw pads;
  - under the antenna;
  - within 2 mm of the devkit's 5Vin (J11 pins 20–22);
  - within 4 mm of mounting holes H1, H3 and H4 (H2 is in the machine corner).

  GND is routed, and poured on both layers everywhere else.
- **Silk:** all of it is ≥ 1.0 mm high with a 0.15 mm stroke (JLCPCB's
  minimum). Labels:
  - pin names at the top edge;
  - `a1` / `a2` `OPEN in Phase 0`, `(b) OPEN until bench`;
  - `J3: PID sensor, tap DNP until measured`;
  - `SELFTEST: never fit`, `U3 + R36 OUT while gate PROPOSED`;
  - the devkit outline, plus `USB end … ANTENNA end`;
  - socket positions 1/5/10/15/20 on J11 and J12, and `5V` at J11 pin 21;
  - `DISPLAY`, `FLOW`, `NTC`, `LEVERS` on the left of each IN/OUT pair;
  - `fit ONE` between R61 and R60;
  - `GND` / `3V3` on the test loops, `<=470p` by C1/C2, `AQY` at each DIP-8's
    pin-1 end.

  JP1's reference is on the fab layer only (the `a1` label names it). R60 and
  R27 are labelled by hand. The references of C15, C20, C22, J2 and J4 sit on
  their own outlines, which leaves 8 silk warnings in DRC.

**Every machine-copper gap under 2 mm**, measured on the routed board. All of
them are inside one part, or a part's own fan:

| Pair | Gap | Why |
|---|---|---|
| neighbouring pins of an XH header (incl. J5 `ESD1`–`VSS`, J4 `T`/`#`–`+`) | 0.80 mm | the 2.5 mm XH pitch: the machine's own connectors |
| pos 1 – A1_LEG / A2_LEG | 0.84 mm | the (a1)/(a2) jumper pins |
| each AQY's two outputs, and the MID/return pad beside it | 0.94 mm | across ONE photoMOS of a chain: the second one still holds |
| tracks leaving a header or jumper pin, beside its next pin | 1.45–1.49 mm | pass-throughs, and the fans inside the J2 fan zone |
| MID / leg tracks beside the next pad of the same chain | 1.54 mm | within one photoMOS, as above |

What the measurement found outside those:
- **Nothing crosses chains.** No 2Cup copper is within 2.5 mm of the 1Cup
  chain, or the reverse, apart from the J2 header and its fan zone. This is
  DRC-enforced (above).
- **No machine net comes within 2 mm of our copper** (GND, +3V3, LED_RAIL,
  an LED pad, a tap node), apart from the connectors' own ground pins.
- **No pour comes within 2 mm of a machine net.**
- **The returns run 3.2 mm apart on B.Cu** where they turn toward J2.
- **Nothing of ours runs between the IN and OUT header rows of J5 or J4.**
  At J3 the `NTC_ADC` track does (B.Cu, 2.03 mm from the J23 pads). The NTC
  plug is not on this board for now.

**Bridge-prone bare pad pairs to inspect under the loupe** (besides the
'221 sockets and BC547 legs in `docs/board-and-perfboard.md`):
- **R60.1 – R61.1** (`NTC_P1_RAW` – `NTC_P2_RAW`, bare while DNP, 1.9 mm).
  A bridge shorts the PID's NTC, so it reads hot and the heater goes off.
  That is the fail-safe direction.
- **R7.1 (`STEAM_RAW`) – R40.2 (`LED_A1`), 2.13 mm.** With (b) fitted, a
  bridge puts 3V3 through 330 Ω on the Steam line, which is a phantom steam
  press and bypasses the (a) jumpers. That is the first review's blocker-1
  class, so inspect it.
- **C16.1 / C17.1 (+3V3) – U1.9 / U2.9 (`A̅`, 1.8 mm).** A bridge holds `A̅`
  at its idle level. Safe.
- **R20.1 / R21.1 (trigger) – R34.2 / R35.2 (readback), 1.9 mm.** A bridge
  joins a trigger to another channel's Q through 1 K. Benign.
- **R27.1 – R28.1 and R29.1 – R30.1 (the two timing nodes of one '221),
  1.9 mm.** Since the crossing, these belong to different chains.
- **R4.2 (`WATER` tap) – C15.2 (GND), 1.0 mm.** A bridge holds the Water
  reading low; it does not reach the machine past the 150 K.

R36–R25 (gate readback – `ARM`) and JP4–R26 (selftest – GND) are now
≥ 2.8 mm.

## For the 1:1 print and bring-up (not Gerber changes)

From the final and pre-order reviews:
- **Wrong-header plugs:** try every harness plug and OUT cable in every
  wrong header. A smaller XH plug probably seats offset in a larger header.
  The pairs are named on the silk; if plugs do seat, consider widening the
  gaps between IN headers in a later revision. The worst pairings give a held
  lever press (the flow or NTC plug in J2 IN).
- **Plug clearance:** mate a plug on each OUT header with its IN header
  fitted (~1.2 mm between bodies), and check adjacent OUT plugs (~2 mm).
- **The `2CUP_RAW` lane runs 1.2 mm from the right edge, with A2_LEG above
  it on the other face:** keep conductive standoffs, rails and box walls off
  that edge, on both faces. Use nylon hardware at H2, which is ~2 mm (with a
  wide washer) from the pos 1 jumper pin.
- **BC547 orientation:** on this board the flat faces the TOP edge, as the
  silk shows. The perfboard doc says bottom.
- **Devkit revision:** this board fits the measured clone (RGB LED on
  GPIO48). A DevKitC-1 v1.1 has its LED on GPIO38, which here is `GATE_EN`.
- **DNP parts carry no mark on the silk** and the render shows them fitted:
  C1–C3, C20–C22, R36, R60–R63, U8. U3's socket is fitted, its chip is not.
- **Antenna:** the J5 `VSS` ground anchor and R1 sit under the module end, ~10
  mm below it. If it matters, move them.
- **Mounting holes against the box standoffs:** unchecked.
- **Not in the BOM** (it lists only the schematic parts): DIL16 sockets ×3,
  DIL08 sockets ×4, jumper shunts ×3, the four OUT cables, the devkit and its
  1×22 female headers ×2.
- **Firmware:** this board reads the 1Cup photoMOS 1 `Q` on GPIO11. Flash
  the branch firmware (`30beb28`) before any `drive` bench run; firmware from
  `main` reads it on GPIO1, which here is the floating NTC_ADC net.

## Before ordering

1. **A layout review in a fresh context**, against the machine-zone rules in
   `docs/board-and-perfboard.md` and this file.
2. **Print `fab/espressolog-board-1to1-print.pdf` at 100 %** (check that
   the 100 mm outline measures 100 mm). Then press in the real parts:
   - the devkit (25.4 mm between columns);
   - a side-entry and a vertical XH header of each size, with the original
     harness plugs;
   - a BC547, an AQY212EH, a DIL08PZ and a DIL16 socket;
   - the 1 µF MKT and the 100 nF discs.
3. Upload `fab/espressolog-board-gerbers.zip`.
