# espressolog board — PCB (sense + drive)

One board to replace the hand-wired perfboard. The devkit sits in sockets, the
J2/J4/J5 side-entry XH headers are on the board, and both the sense dividers and
the drive side are on it. The perfboard keeps logging on the machine until this
board has passed its bench test. Decided 2026-11-24.

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

## What the board carries

| Part | Note |
|---|---|
| J11 / J12 | devkit signal column (A) and opposite column (K), 1×22 female headers 25.4 mm apart. **Nothing on 5Vin.** |
| J5 / J4 / J2 | side-entry XH, opening at the top edge. Hole order left to right is the perfboard's, so the existing branch plugs mate unchanged. **J4 `+` and J5 `Disp-P4` reach nothing.** |
| R1–R16, C1–C3 | sense series legs and shunts. C1–C3 are DNP footprints for the candidate shunt caps (470 pF / 470 pF / 10 nF; **never 10 nF on ESD1/ESD4**). |
| U1, U2 | drive '221s, in sockets |
| U3 | the PROPOSED gate '221. **Socket fitted, chip out, and R36 out with it** (R36 is marked DNP) |
| U4–U7 | AQY212EH in a DIP-8 footprint, with the chip at the pin-1 end of an uncut DIL08PZ (or soldered straight in). Pads 3–6 are not connected. |
| Q1–Q4 + R40–R51 | LED-driver cells: 330 Ω, BC547B, 100 K, 8.2 K |
| JP1 (a1), JP2 (a2) | in series with the photoMOS outputs. **OPEN through Phase 0** (invariant 1) |
| JP3 (b) | LED supply. **OPEN until the bench test** |
| JP4 | selftest GPIO8–GPIO6. **Never fitted in service** |

## Not settled. Check before ordering

- **Capacitor lead pitch, not measured.** The footprints assume 5.0 mm for the
  1 µF MKT and the 100 nF / 470 pF discs. Measure the parts you have.
- **BC547 footprint:** `TO-92_Inline_Wide`, with legs 2.54 mm apart like the
  perfboard's (the standard 1.27 mm one puts the collector–base pair close
  together). Pad 1 is C, and the review checked this against KiCad's stock
  BC547. A 2N3904 would go in reversed.
- **One GND net.** The perfboard kept separate drive (K29) and sense (K30)
  ground ties. Here that becomes a layout decision, made at placement. Signal-column
  GND (A22, beside 5Vin) is deliberately left unused, as on the perfboard.
- **AQY212EH in an uncut DIP-8 footprint** where the board doc says DIP-4
  sockets: same netlist, and a chip pushed in at the wrong end lands on dead pads.
- **XH pad 1 side.** The footprint opens toward +y, so the headers are placed
  rotated 180° and pad 1 becomes the right-hand pin. Before ordering, lay the
  existing branch plugs on a 1:1 print.
- **The open design items stay open**: the readback value (1 K proposed), the
  gate RC (470 K candidate), and whether the gate stays. They are on ordinary
  footprints, so values are chosen at soldering time. Dropping the gate is a
  bodge wire, GPIO38 to the arm net, not a solder jumper. The minimum-gap stage
  on U3's spare channel is not designed, so U3 channel 2 is tied off.

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
`gen_pcb.py`, since the 2026-11-24 layout review). Freerouting treated the
old netclass as "keep 0.6 mm away" and nothing more. It ran the raw 1Cup/2Cup
returns past LED anode pads, so one short plus (b) fitted was a held press
that no (a) jumper blocks. It also ran the (a2) feed past U5's raw-1Cup pin
and along raw 2Cup, so one short with (a2) fitted got around both series
photoMOS. The layout now:
- **Everything machine-side stays on the output side of the photoMOS.**
  - Pos 1 runs along the top edge.
  - A1_LEG (F.Cu) drops in a lane at x 88.5, above the stack.
  - A2_LEG (F.Cu) runs in the outer lane at x 97.5.
  - The MID links jog round the empty socket pads.
  - The returns run on B.Cu: 1Cup in lane x 94.4, 2Cup in lane x 98.6. Each
    climbs its own channel between the J2 resistor columns (R4–R7 stand as
    columns, 6.5 mm apart).
- **The returns cross the A legs only on the opposite layer.**

**Rules on the board** (`espressolog-board.kicad_dru` with the netclasses):
- **`Machine` nets keep 2 mm from everything of ours** and 1 mm from each
  other. The exceptions:
  - a fan track leaving a J2/J4/J5/JP1/JP2 pin may pass that part's other
    pins at 1.2 mm;
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
- **No ground pour in the machine corner** (J2, its 150 K, pos 1,
  (a1)/(a2), the returns' channels) **or down the photoMOS output column and
  its lanes**, and none under the antenna. GND is routed and also poured on
  both layers.
- Labels on the silkscreen: `a1` / `a2` `OPEN in Phase 0`, `(b) OPEN until
  bench`, `SELFTEST: never fit`, `U3 + R36 OUT while gate PROPOSED`.

**Every machine-copper gap under 2 mm**, measured on the routed board. All of
them are inside one part, or a part's own fan:

| Pair | Gap | Why |
|---|---|---|
| neighbouring J2 / J5 / J4 pins (incl. J5 `ESD1`–`VSS`) | 0.80 mm | the 2.5 mm XH pitch: the machine's own connectors |
| pos 1 – A1_LEG / A2_LEG | 0.84 mm | the (a1)/(a2) jumper pins |
| each AQY's two outputs, and the MID/return pad beside it | 0.94 mm | across ONE photoMOS of a chain: the second one still holds |
| fan tracks beside their connector's next pin | 1.45–1.49 mm | J2/J5/JP fans leaving a pin |
| MID / leg tracks beside the next pad of the same chain | 1.54 mm | within one photoMOS, as above |

Nothing crosses chains: no 2Cup copper is within 2.5 mm of the 1Cup chain
or the reverse, apart from the J2 header pins. No machine net is within 2 mm
of GND, +3V3, LED_RAIL or an LED pad, apart from J5's own `ESD1`–`VSS` pins.
The two returns run 2.6 mm apart on B.Cu for ~15 mm where they turn toward
J2.

## Before ordering

1. **A layout review in a fresh context**, against the machine-zone rules in
   `docs/board-and-perfboard.md`.
2. **Print `fab/espressolog-board-1to1-print.pdf` at 100 %** (check that
   the 100 mm outline measures 100 mm). Then press in the real parts: the
   devkit (25.4 mm between columns), the side-entry XH headers with the
   **existing branch plugs**, a BC547, an AQY212EH, a DIL08PZ and a DIL16
   socket, the 1 µF MKT and the 100 nF discs.
3. Upload `fab/espressolog-board-gerbers.zip`.
