# espressolog board — PCB (sense + drive)

One board to replace the hand-wired perfboard. The devkit sits in sockets,
both the sense dividers and the drive side are on it, and **the T-pieces are
on it too** (decided 2026-11-24). The perfboard keeps logging on the machine
until this board has passed its bench test.

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

**A harness plug in the WRONG connector's header: closed for the harness
plugs, 2026-11-25.** The four have different pin counts (2, 3, 4, 5), and in
the paper test a smaller harness plug did not seat in a larger header. The
OUT cables are new parts and were not tried; the pairs are named on the silk
beside both headers.

**Operating consequence: the machine's switches, display and flowmeter all
run through this board. Without it, the machine runs only with the original
plugs back on its own headers.**

**The NTC plug stays on the machine's own J3 header for now** (pre-order
review S2). The J3 pass-through exists on the board, but an open or
resistive joint in it reads as a cold boiler, and with the tap unfitted it
delivers nothing. J3 is a 2-pin XH (confirmed 2026-11-24). The one planned
use before the tap is fitted is the measuring run in "The J3 NTC tap" below.
Before that run:
- find out, machine cold, what the machine does when J3 is pulled;
- meter the J3 rows as in "Before first fit" (pin 1 – pin 2 open, each pin
  open to TP1).

**The taps load the machine even when the ESP32 is off, and they cannot be
unplugged separately** (S12). With the devkit fitted but unpowered, its GPIO
protection diodes clamp the tap nodes, and the J5 bus high level is
calculated to drop from 4.20 V to about 3.5–3.8 V. That is at the floor the
display needs (`docs/phase-0-status.md`). **Measure it once**: machine on,
USB out, J25 pin 2 and pin 3 against TP1. Record the result here. If the
display misreads in that state, the cure is to pull the devkit or go back to
the original plugs.

**Pin map, by JST pin number** (pad 1 is the right-hand pin; the silk at the
top edge names each column, which is the same pin on IN and OUT).
**For J5 this is NOT `CLAUDE.md`'s numbering:** there, and in the display-bus
doc, pin 1 is `VSS` (the spoked pad) and pin 4 is `Disp-P4`. On this board
pad 1 is `Disp-P4` and pad 4 is `VSS`. Never clip a ground to "J5 pin 1" of
this board; ground is TP1.

| Conn | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 | Source |
|---|---|---|---|---|---|---|
| J5 / J25 | `Disp-P4` (P4) | `ESD4` clock (CK) | `ESD1` data (DA) | `VSS` (0V) | | pad 1 read off the plug (2026-11-24); the rest positional, `docs/machine-display-bus.md` |
| J4 / J24 | `#` | **`+`** (middle, green) | `T` | | | read off the machine-side plug, Lucas, 2026-11-24 (`J4_T_PIN = "3"` in `gen_schematic.py`) |
| J3 / J23 | NTC P1 | NTC P2 | | | | 109 kΩ between the two (2026-11-24); which is the divider node is unmeasured; the tap is DNP |
| J2 / J22 | pos 1 +5 V | Steam | 2Cup | 1Cup | Water | pad 1 read off the plug (2026-11-24); the rest positional (`CLAUDE.md`, J2 table) |

- **J4 `+` and J5 `Disp-P4` cross the board as isolated pass-through traces.**
  They have no pad but their own header pins, no test point, no via, no pour
  nearby, and keep 2 mm from everything of ours. Their own header's
  neighbouring pins sit at the machine's 2.5 mm pitch (0.8 mm gap, as in the
  machine's harness), and the machine's own ground pass-through beside them
  runs at 1.45 mm. Nothing connects to them (the J4 `+` rule in `CLAUDE.md`).
- **The machine's ground (J5 `VSS`, J4 `T`) is our ground.** It passes
  straight through IN→OUT. J4 `T` ties to the devkit GND (J12 pin 1) by a
  hand-laid track, and J5 `VSS` joins that track through a via. The J2
  dividers' only reference is this ground.

## What the board carries

| Part | Note |
|---|---|
| J11 / J12 | devkit signal column (A) and opposite column (K), 1×22 female headers 25.4 mm apart, pin 1 (3V3) at the top. **Nothing on 5Vin.** Outline on the silkscreen |
| J2–J5 / J22–J25 | IN / OUT, above |
| R1–R16, C1–C3 | sense series legs and shunts. C1–C3 are DNP footprints for the candidate shunt caps (470 pF / 470 pF / 10 nF; **never 10 nF on ESD1/ESD4**) |
| R60–R68, C20–C22, U8 | **J3 NTC tap, ALL DNP** until J3 is measured (section "The J3 NTC tap" below). The machine's PID regulates on this NTC, so any load shifts the brew temperature, and a fault that reads cold keeps the heater on. Wash the flux off: leakage matters at 9.4 MΩ |
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
  from the plug, not a guess. `J4_T_PIN` and `J4_FLOW_PIN` are constants now
  and finish.py checks the pads against them: changing one stops the build
  until R3's position and the check are changed with it. That is the point.
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

## The J3 NTC tap (all DNP, values not final)

J3 is the PID's own temperature sensor, so this is the one tap where a
mistake changes what the machine does to the boiler. Everything here is a
footprint; nothing is fitted until the measurements below exist.

**Measured so far (Lucas, 2026-11-24):** J3 is a 2-pin JST XH, and the
sensor reads **109 kΩ between its two wires**, plug off the machine board,
at what was presumably room temperature. That
fits a 100 kΩ NTC (B ≈ 3950 gives 109 kΩ at 23 °C), which would be roughly
6–9 kΩ at 95 °C. The B value, which pin is the divider node, the pull-up
and the node voltage are **not measured**.

**The circuit:**

| Part | Value | What it does |
|---|---|---|
| R60 **or** R61 | 4.7 MΩ, **fit ONE** | from the divider-node pin (R60 = pin 1, R61 = pin 2) to `NTC_MID`. Both fitted put 9.4 MΩ across the NTC |
| R64 | 4.7 MΩ | `NTC_MID` → `NTC_TAP`. Two parts in series, so one bridged or failed part still leaves 4.7 MΩ on the line |
| R62 | open | shunt to GND, only if the node turns out to sit above ~3 V (5 V referenced) |
| C20 | 10 nF | at the op-amp input |
| U8 half A | MCP6002 | unity follower (CMOS input, ~1 pA) |
| U8 half B, R66, R67 | Rf, Rg | follows half A. **Wire link in R66, R67 empty = unity** (the parked default). R66/R67 fitted = gain 1 + R66/R67, for a node that sits low when hot. **Whenever U8 is fitted, R66 is fitted** (link or Rf): without it half B runs open-loop |
| R63 **or** R68 | 1 kΩ, **fit ONE** | R63 from the follower, R68 from the gain stage, into `NTC_ADC` |
| C21, C22 | 100 nF | on `NTC_ADC` (GPIO1, ADC1), and U8's supply |

**What it costs the machine.** In normal running the follower draws
nothing, so the load is leakage only. The worst case is the tap node held
at a rail, and that is also the everyday case of **machine on, logger
off**: U8's input clamps to its dead supply. Then 9.4 MΩ sits across the
node. Against ≤ 9 kΩ at brew temperature that is under 0.1 % of the
reading, about 0.03 °C; against 109 kΩ cold it is ~1.2 %, about 0.3 °C.
**Which way it reads is not known yet:** slightly hot (the direction that
heats less) if the NTC is the leg to ground, slightly cold by the same
amount if it is the leg to the supply. Step 1 below settles it.

**Still to measure, and none of it needs a hand-built T-piece:**
1. **Now, machine off and unplugged:** each pin of the machine board's J3
   header to ground (J5's spoked pad), in Ω. One pin near 0 Ω means that pin
   is ground and the other is the node. (Back-probing J4's header gave no
   reading before; if J3 does the same, skip it, step 2 answers it too.)
2. **With this board, tap unfitted** (after the two checks under "The NTC
   plug stays on the machine" above): J3 through IN → OUT like the other
   three, **with the J5 or J4 OUT cable on the machine too** (TP1 is only
   the machine's ground through one of those). The board is the T-piece.
   Meter J23 pin 1 and pin 2 against TP1 in
   DC volts, cold and at setpoint, and note whether the reading is steady (a
   pulsed excitation would not be). A 10 MΩ meter loads the node as much as
   the tap will.

Those readings choose R60 or R61, whether R62 is needed, and the gain
(largest that keeps the hot-end voltage under ~2.5 V at GPIO1). **Running
J3 through the board is itself the risk this section is about**: an open
pass-through reads cold and the heater stays on. Seat both plugs fully and
watch the display on the first heat-up.

Firmware reads nothing on GPIO1 yet. When it does, it stores raw
millivolts (invariant 3) and the conversion lives in analysis.

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
   - J24 pin 2 reads ~0 Ω to the machine's J4 `+`, and **not ~0 Ω to TP1**.
     It is never "open" there: the machine's own path `+` → 10 k → `#` →
     147 k → `T` (with our 320 k beside it) reads about 110 kΩ, and the
     unpowered 5 V rail can pull that lower. The value to expect is the
     `+`–`T` reading taken on the machine's own J4 plug (below); a reading
     near **10 kΩ** points at a mirrored J4 cable, and anything under
     ~1 kΩ, down to ~0 Ω, means `+` is on or near ground: stop either way;
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
   pin 2 does not. With a mirrored J4 cable both read ≈ 10 kΩ (with J5's
   cable on, `#` is then on `T` through our ground).
   **This only discriminates if the machine's unpowered 5 V rail reads well
   above 10 kΩ, and nobody has measured it.** Meter `+` to `T` once on the
   machine's own J4 plug (machine off, no board attached) before relying on
   it. If that reads near 10 kΩ the two readings look alike and this check
   says nothing.
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
While routing, the router is kept out of the J5/J4 header block, out of the
corridor between the J3 rows, 1 mm off every board edge, and **out from
under every hand-laid machine track** (a strip on the opposite layer: no
clearance rule sees opposite layers, and one re-route on 2026-11-24 took a
drive net between the J2 rows under all five lever pass-throughs).

**A router run is not reproducible, and it can leave connections open.**
`route.py` prints `open connections: N`; run `route.py --keep` until it is 0,
and if two or three passes do not close it, start again from `gen_pcb.py`
(the next fresh run usually does). The pipeline no longer finishes a bad
board quietly:
- `route.py` deletes `build/routed.kicad_pcb` and the old router session
  before it starts, and stops if Freerouting fails;
- `finish.py` starts from `build/routed.kicad_pcb` and **stops before
  saving** unless: every pad is on the net `gen_schematic.py` gives it; the
  J4, J5 and J2 pinouts are the confirmed ones; `+` and `Disp-P4` are two
  pads and one track each; the locked copper is exactly `gen_pcb.py`'s;
  nothing of ours runs under a machine track or within 1 mm of one through
  the board; the routed copy has the placement `gen_pcb.py` last wrote
  (`gen_pcb.py` also deletes the routed copy); no connection is open; the
  machine nets are exactly the nets in netclass `Machine`; and **DRC with
  the project's rules reports no error** (run on a copy in `build/`, report
  in `build/drc.rpt`).

**Never use the GUI's "Update PCB from Schematic":** it renames every net to
`/NAME`. The rule file survives that, `route.py` does not. Regenerate with
`gen_pcb.py`. The tool paths are macOS defaults; override them with
`KICAD_CLI`, `KICAD10_FOOTPRINT_DIR`, `FREEROUTING_JAR`, `JAVA` and
`FREEROUTING_ARGS`.

Then export with `kicad-cli`. The current board: **DRC 0 errors,
0 unconnected** (8 silk warnings). The `fab/` folder holds the Gerber + drill zip (for
JLCPCB/PCBWay: 2 layers, 1.6 mm FR-4, 100 × 100 mm), the BOM and
`espressolog-board-1to1-print.pdf` (black and white: pads with their holes,
the part outlines and labels, the board edge; no tracks or pour. Made with
`kicad-cli pcb export pdf --layers F.Mask,F.Silkscreen,Edge.Cuts
--mode-single --black-and-white`).

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
  36 mm, and 1.3 mm beside the 1Cup return's lane (x 96.4 over x 94.7) for
  22 mm, both through the board. No clearance rule sees opposite layers;
  this list is the check.
- **Two nets of ours are hand-laid too** (`OUR_FIXED_TRACKS`):
  - **The +3V3 trunk** leaves J11 under the devkit and ends on R20. The router
    is kept out of the J5/J4 header block and out of the corridor between
    the J3 rows (`add_route_keepout` in route.py), so nothing of ours runs
    between the IN and OUT rows of J5, J4 or J3.
  - **LED_RAIL** is a trunk left of the cells with a stub per cell. It reaches
    JP3 pin 2 from below, 1.5 mm from JP3 pin 1.

**Rules on the board** (`espressolog-board.kicad_dru` with the netclasses):
- **`Machine` nets keep 2 mm from everything of ours** and 1 mm from each
  other. That covers every `*_RAW` net (incl. J4 `+`, J5 `Disp-P4`, the J3
  NTC), pos 1 and the drive chains. The exceptions:
  - a machine track may pass an XH header's or (a) jumper's pins at 1.2 mm
    (the machine's own pitch). The rule is written for tracks leaving that
    part, but its condition covers ANY machine track beside ANY of those
    pins, so DRC does not hold a foreign machine track to 2 mm there;
  - the hand-laid (locked) ground tracks may pass their connector's machine
    pins at 1.2 mm (J4/J24 and J5/J25 only). The router's GND gets no
    exception;
  - pads of one connector, jumper or AQY may sit at their own pitch;
  - the empty half of each photoMOS socket (U4–U7 pads 3–6) needs 0.6 mm.
    Every other pad with no net counts as live: devkit pins, the '221s' `Q̄`
    outputs.
- **Machine nets keep 3 mm from the edge of a mounting hole** (its own
  `hole_clearance` rule: the holes are unplated, so no copper clearance rule
  sees them).
- **The 1Cup and 2Cup chains keep 2.5 mm apart** (`1CUP_RAW`/`MID_1CUP`/
  `A1_LEG` against `2CUP_RAW`/`MID_2CUP`/`A2_LEG`). The exemptions are the
  J2/J22 pins and the tracks touching the "J2 fan zone" rule area (finish.py,
  y < 23.0): the first mm leaving the 2.5 mm header pitch. The fans are cut
  at the zone edge so the exemption ends there. This is what would catch a
  repeat of the first review's finding 2.
- **A leg keeps 2.5 mm from its own return, and pos 1 from the 1Cup, 2Cup
  and Water lines** (track to track; pads are not covered). A short there
  bypasses both photoMOS, or the jumper too. **Pos 1 keeps 2.0 mm from
  Steam** (its own rule: the J2 pass-throughs themselves sit 2.10 apart).
- **Not enforced, only measured:** lever against lever outside 1Cup/2Cup
  (Steam, Water) is held to 1.0 mm by DRC; the 2 mm below the header is this
  layout, re-measure it after any change to the fans. Net names in the rule
  file are written `'*NAME'`, so the rules survive KiCad renaming nets to
  `/NAME`.
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
- Freerouting may narrow signal tracks, down to 0.15 mm (the board minimum;
  JLCPCB makes 0.127). On the current routing the narrowest is 0.225 mm.
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
  - `J3 = PID sensor`, `tap: all DNP`;
  - `SELFTEST: never fit`, `U3 + R36 OUT while gate PROPOSED`;
  - the devkit outline, plus `USB end … ANTENNA end`;
  - socket positions 1/5/10/15/20 on J11 and J12, and `5V` at J11 pin 21;
  - `DISPLAY`, `FLOW`, `NTC`, `LEVERS` on the left of each IN header and
    again on the left of each OUT header;
  - `fit ONE` between R61 and R60;
  - `GND` / `3V3` on the test loops, `<=470p` by C1/C2, `AQY` at each DIP-8's
    pin-1 end.

  JP1's reference is on the fab layer only (the `a1` label names it). R27
  and the tap's R60, R63, R64, R66, R67 and R68 are labelled by hand. The references of C15, C20, C22, J2 and J4 sit on
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
- **Nothing of ours runs between the IN and OUT header rows** of J5, J4, J3
  or J2, **and nothing of ours crosses under a machine track** on the other
  layer or runs within 1 mm of one through the board. `finish.py` checks
  that on every build.

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
  ties a 10 K-pulled `A̅` to an idle-low Q through 1 K, so `A̅` sits at
  ~0.3 V. The '221 also fires on `R̅` rising with `A̅` low, so that one
  channel would pulse on every arm: one photoMOS for 220 ms, not a press,
  but not nothing.
- **R27.1 – R28.1 and R29.1 – R30.1 (the two timing nodes of one '221),
  1.9 mm.** Since the crossing, these belong to different chains.
- **R4.2 (`WATER` tap) – C15.2 (GND), 1.0 mm.** A bridge holds the Water
  reading low; it does not reach the machine past the 150 K.
- **R6.2 (`2CUP` tap, GPIO15) – R4.1 (`WATER_RAW`), 2.11 mm.** A bridge
  puts the raw Water line (5 V behind 100 Ω) straight onto GPIO15 and joins
  Water to 2Cup through 150 K.
- **The LED_RAIL trunk passes R48.2–R51.2 (the four BASE pads) and R33.1
  (`Q_1CUP_2`) at 0.75 mm**, on the solder side. With (b) fitted a bridge
  lights that cell: one photoMOS closed.
- **JP4 pin 1 (`SELFTEST_IO8`, GPIO8) – the `FLOW` track, 0.26 mm**, on the
  top face beside the pin. A bridge **is the selftest jumper, fitted
  invisibly**: GPIO8 on the `#` sense node. Step 0's "JP4 reads open"
  catches it; look at it again after any rework near JP4.
- **C16.1 (+3V3) – the STEAM sense track, 0.27 mm** (top face, y 50.2). A
  bridge holds GPIO16 high (a phantom steam in the log) and feeds ~22 µA
  through 150 K into the machine's Steam line.
- **The two 2Cup triggers (`TRIG_2CUP_1` – `TRIG_2CUP_2`), 0.6 mm** on the
  solder side near U2. A bridge fires both 2Cup channels from one: with (a2)
  and (b) fitted on the bench that is a 2Cup press.
- **Cross-chain drive pairs at 0.59–0.65 mm:** `TRIG_1CUP_2` – `TRIG_2CUP_2`
  at U2.1; `Q_2CUP_1` beside `TRIG_1CUP_1`, `U1_RC1` and the two `CX1`
  tracks; `Q_1CUP_1` – `U1_RC2`. A bridge fires or disturbs one device of
  the other chain: not a press. (`Q_1CUP_2` – `Q_2CUP_1` is now 0.82 mm.)
- **Readback pairs at 0.25–0.27 mm** (`RB_1CUP_1`/`RB_1CUP_2`/`RB_2CUP_1`):
  a bridge ties two readbacks, which only confuses the bench `drive` trace.
- **`NTC_TAP` – R62.2 (GND), 0.27 mm.** Harmless to the machine (it is
  behind 9.4 MΩ); flux left there is a measurement error once the tap is
  fitted.

These are measured on THIS routing. **After any re-route the router's pairs
move: re-measure them** (the placement pairs above them do not).

R36–R25 (gate readback – `ARM`) and JP4–R26 (selftest – GND) are now
≥ 2.8 mm.

## For the 1:1 print and bring-up (not Gerber changes)

From the final and pre-order reviews:
- **Wrong-header plugs:** closed for the harness plugs by the paper test
  (2026-11-25, below). Still try each OUT cable in each wrong OUT header when
  the cables arrive; the worst pairing is a smaller plug in J22.
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
  C1–C3, C20–C22, R36, R60–R68, U8. U3's socket is fitted, its chip is not.
- **Antenna:** the J5 `VSS` ground anchor, R1 and the hand-laid 3V3 trunk
  (y 33.3) sit under the module end, ~10 mm below it. If it matters, move
  them.
- **Mounting holes against the box standoffs:** unchecked.
- **Not in the BOM** (it lists only the schematic parts): DIL16 sockets ×3,
  DIL08 sockets ×4, jumper shunts ×3, the four OUT cables, the devkit and its
  1×22 female headers ×2.
- **Firmware:** this board reads the 1Cup photoMOS 1 `Q` on GPIO11.
  **Firmware 0.15.2 and later read it there; 0.15.1 and earlier read GPIO1**,
  which on this board is the NTC_ADC net. Check the boot line before any
  `drive` bench run.

## Paper test: DONE 2026-11-25

Lucas pressed the real parts into the 1:1 print.

**Fits as drawn:** the side-entry and vertical XH headers with the harness
plugs; the devkit in its two 22-pin sockets; DIL16 and DIL08 sockets, the
'221 and the AQY212EH; the 1 µF MKT (C10) and the 470 pF disc (C1); flat
and upright resistors; the 2-pin jumper header.

**Fits with the legs bent, footprint kept on purpose:**
- **BC547:** the parts in hand have straight legs (1.27 mm apart); the
  holes are 2.54 mm apart. Splay the two outer legs. The wide footprint
  stays: at 1.27 mm the pads nearly touch, and a bridge on a BC547 can light
  a photoMOS.
- **100 nF disc (C15–C17, later C21/C22):** the parts in hand have about
  2 mm between the legs (operator's reading; 2.5 mm is the usual nominal),
  the holes are 5.0 mm apart. Splay the legs. Not worth a re-route.

**Checked with the plugs:**
- **A smaller harness plug does NOT seat in a larger header**, so the
  wrong-header worry (top of this file) is closed for these plugs.
- **A plug on a vertical OUT header clears the plug in the side-entry header
  in front of it.**

**Not checked: the box.** There is none yet; it gets designed around the
board (mounting holes 3.5 mm in from each corner, 3.2 mm; 20–25 mm headroom
above the board for the vertical plugs; nothing conductive along the right
edge or at H2).

## Ordering

Done: the layout reviews in fresh contexts (five, 2026-11-24) and the paper
test (above). What is left:
1. Upload `fab/espressolog-board-gerbers.zip`: 2 layers, 1.6 mm FR-4,
   100 × 100 mm, defaults otherwise.
2. Parts the perfboard build did not need: vertical XH headers (2, 3, 4 and
   5 pin), a 2-pin side-entry XH header, four XH cables for the OUT side.
