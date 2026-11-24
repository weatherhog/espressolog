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
  **same-direction** ready-made XH cable runs from here to the machine
  board's own header.

**IN pin k is joined to OUT pin k by one straight track, for every line.** A
plug mates any XH header of its size the same way, so the machine sees its
own harness, conductor for conductor. The taps branch off the OUT pins. The
four connectors have different pin counts (2, 3, 4, 5), so no plug fits
another connector's header, and IN and OUT of one connector are electrically
interchangeable.

**Operating consequence: the machine's switches, display, flowmeter and NTC
all run through this board. Without it, the machine runs only with the
original plugs back on its own headers.**

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
| U1, U2 | drive '221s, in sockets |
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

Board unpowered, devkit out, every jumper open, DNP parts unfitted.

1. **Each OUT cable on its own.** Lay it flat with both latches up and don't
   turn either end over. Cavity N must read ~0 Ω to cavity N at the other end
   and open to every other cavity. **A reversed cable fails this, and must
   not be used**: on J5 it puts our ground on `Disp-P4`, on J4 it puts our
   ground on `+` or `#`.
2. **Board pass-through.** IN pin k reads ~0 Ω to OUT pin k, and open to
   every other pin of both headers. This catches a solder bridge.
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
   - J25 pin 1 reads ~0 Ω to J5's square pad (`Disp-P4`).

   This confirms the J5 orientation and the J4 pin order on the real
   machine.
7. Only then do the harness plugs go on the IN headers.

## Building the board

Placement, routing and finishing are scripts too. Each runs with KiCad's
Python (`KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3`):

```bash
python3 gen_schematic.py --check   # schematic, ERC, netlist round-trip
$KPY gen_pcb.py                    # placement + the machine nets as locked tracks
$KPY route.py                      # Freerouting the rest, headless (Java 21+, jar in ~/.local/share/freerouting)
$KPY finish.py                     # GND pours (both layers), keep-outs, labels, refs
```

`route.py` can be re-run on a routed board. It strips everything except the
locked machine tracks in a separate process, then routes.

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
  - A2_LEG (F.Cu) runs in the outer lane at x 97.5.
  - The MID links jog round the empty socket pads.
  - The returns run on B.Cu: 1Cup in lane x 94.4, 2Cup in lane x 98.6.
    Each comes in from the right at its own resistor row: J2's 150 K lie
    flat in staggered rows, and 2Cup's row is above 1Cup's so the two lanes
    never cross.
- **The returns cross the A legs and the J2 fans only on the opposite layer.**

**Rules on the board** (`espressolog-board.kicad_dru` with the netclasses):
- **`Machine` nets keep 2 mm from everything of ours** and 1 mm from each
  other. That covers every `*_RAW` net (incl. J4 `+`, J5 `Disp-P4`, the J3
  NTC), pos 1 and the drive chains. The exceptions:
  - a track leaving an XH header or jumper pin may pass that part's other
    pins at 1.2 mm (the machine's own pitch);
  - the ground pass-through may pass its connector's machine pins at 1.2 mm;
  - pads of one connector, jumper or AQY may sit at their own pitch;
  - pads with no net (the empty half of each DIP-8) need 0.6 mm.
- **KiCad 10 accepts only some condition syntax.** `A.NetClass == '…'` matches
  nothing, so use `hasNetclass()`. `Parent.Reference` doesn't exist, so use
  `memberOfFootprint()`. One condition KiCad can't parse disables the whole
  file and DRC falls back to the netclasses, which is how these rules were
  inert in the first draft. After editing the file, check that a DRC report
  names the custom rules.
- Netclasses: signals 0.3 mm. `Power` (+3V3, LED_RAIL) and `Ground` 0.5 mm.
  `Machine` 0.4 mm.
- Freerouting narrows a few signal tracks to ~0.18 mm between pins, so the
  board minimum is 0.15 mm (JLCPCB makes 0.127).
- **No ground pour** in:
  - the machine corner (J2, its 150 K, pos 1, (a1)/(a2), the returns);
  - the photoMOS output column and its lanes;
  - the J5/J4/J3 pass-through block;
  - under the antenna.

  GND is routed, and poured on both layers everywhere else.
- **Silk:** all of it is ≥ 1.0 mm high with a 0.15 mm stroke (JLCPCB's
  minimum). Labels:
  - pin names at the top edge;
  - `a1` / `a2` `OPEN in Phase 0`, `(b) OPEN until bench`;
  - `J3: PID sensor, tap DNP until measured`;
  - `SELFTEST: never fit`, `U3 + R36 OUT while gate PROPOSED`;
  - the devkit outline, plus `USB end … ANTENNA end`.

  JP1's and R60's references only fit on the fab layer.

**Every machine-copper gap under 2 mm**, measured on the routed board. All of
them are inside one part, or a part's own fan:

| Pair | Gap | Why |
|---|---|---|
| neighbouring pins of an XH header (incl. J5 `ESD1`–`VSS`, J4 `T`/`#`–`+`) | 0.80 mm | the 2.5 mm XH pitch: the machine's own connectors |
| pos 1 – A1_LEG / A2_LEG | 0.84 mm | the (a1)/(a2) jumper pins |
| each AQY's two outputs, and the MID/return pad beside it | 0.94 mm | across ONE photoMOS of a chain: the second one still holds |
| tracks leaving a header or jumper pin, beside its next pin | 1.45–1.49 mm | pass-throughs and fans |
| MID / leg tracks beside the next pad of the same chain | 1.54 mm | within one photoMOS, as above |

What the measurement found outside those:
- **Nothing crosses chains.** No 2Cup copper is within 2.5 mm of the 1Cup
  chain, or the reverse, apart from the J2 header.
- **No machine net comes within 2 mm of our copper** (GND, +3V3, LED_RAIL,
  an LED pad, a tap node), apart from the connectors' own ground pins.
- **No pour comes within 2 mm of a machine net.**
- **The returns run 2.6 mm apart on B.Cu** where they turn toward J2.

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
