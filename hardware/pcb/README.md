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
$KPY gen_pcb.py                    # placement (no routing), courtyard check
$KPY route.py                      # Freerouting, headless (Java 21+, jar in ~/.local/share/freerouting)
$KPY finish.py                     # GND pour, keep-outs, labels, refs
```

Then run DRC and export with `kicad-cli`. The current board: **DRC 0 violations,
0 unconnected**. The `fab/` folder holds the Gerber + drill zip (for
JLCPCB/PCBWay: 2 layers, 1.6 mm FR-4, 100 × 100 mm), the BOM and
`espressolog-board-1to1-print.pdf`.

**Rules on the board:**
- Netclasses: signals 0.3 mm. `Power` (+3V3, GND, LED_RAIL) 0.5 mm.
  `Machine` (pos 1, every raw line, the drive chains) 0.4 mm with **0.6 mm
  clearance**.
- Freerouting narrows a few signal tracks to ~0.18 mm between pins, so the
  board minimum is 0.15 mm (JLCPCB makes 0.127).
- **No ground pour in the machine corner** (J2, its 150 K, pos 1, (a1)/(a2))
  **or down the photoMOS output column**, and none under the antenna.
- Labels on the silkscreen: `a1` / `a2` `OPEN in Phase 0`, `(b) OPEN until
  bench`, `SELFTEST: never fit`, `U3 + R36 OUT while gate PROPOSED`.

**Closest gaps between machine-net copper**, measured on the routed board:

| Pair | Gap | Why |
|---|---|---|
| pos 1 – Steam, 1Cup raw – 2Cup raw | 0.79, 0.69 mm | adjacent J2 pins, 2.5 mm pitch — the machine's own connector |
| pos 1 – A1_LEG / A2_LEG | 0.84 mm | the (a1)/(a2) jumper pins |
| **A2_LEG – MID_1CUP** | **0.61 mm** | the (a2) feed runs down past the 1Cup chain to U6. A bridge there would only make the 1Cup line depend on U5 alone, and only once (a2) is fitted. Worth a re-route if the review asks for it |
| pos 1 – GND | 23 mm | no ground near pos 1 |

## Before ordering

1. **A layout review in a fresh context**, against the machine-zone rules in
   `docs/board-and-perfboard.md`.
2. **Print `fab/espressolog-board-1to1-print.pdf` at 100 %** (check that
   the 100 mm outline measures 100 mm). Then press in the real parts: the
   devkit (25.4 mm between columns), the side-entry XH headers with the
   **existing branch plugs**, a BC547, an AQY212EH, a DIL08PZ and a DIL16
   socket, the 1 µF MKT and the 100 nF discs.
3. Upload `fab/espressolog-board-gerbers.zip`.
