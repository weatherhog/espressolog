#!/usr/bin/env python3
"""Place the espressolog board and lay the machine nets as FIXED tracks.

Run with KiCad's own Python (it needs the pcbnew module):

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen_pcb.py

The parts and nets come from gen_schematic.py, so the board cannot drift from
the schematic. Each footprint is linked to its symbol by uuid.

Coordinates are mm on the board, origin at the top-left corner, y down. The
board is 100 x 100 mm, the size of the perfboard it replaces.

WHY THE MACHINE NETS ARE HAND-LAID (layout review, 2026-11-24): an autorouter
treats a netclass as "keep N mm away" and nothing else. The perfboard rules
also say WHICH copper a machine net may run beside -- never the LED (3V3) side
of the photoMOS, never along the other line's chain, never as a pair. Freerouting
broke two of those, each a single-short path around the open (a) jumpers.
So every machine net below is a fixed, locked track; route.py routes only the
rest, and keeps it 2 mm away (netclass `Machine`).

The T-pieces are on the board (2026-11-24): each machine connector has an
IN header (side-entry, top edge, the original harness plug) and an OUT header
(vertical, right behind it, cable to the machine board), joined pin k to pin
k by a straight track. The taps hang off the OUT pins.

Machine-corner topology (all lanes on the OUTPUT side of the photoMOS):
  F.Cu  pass-throughs, pos 1 (top), the fans to the series resistors,
        A1_LEG (lane x 88.5), A2_LEG (lane x 96.4), the two MID links
  B.Cu  the raw returns: 1CUP_RAW in lane x 94.7, 2CUP_RAW in lane x 98.6,
        each in at its own resistor row from the right
"""
import os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_schematic as S          # noqa: E402  (parts, nets, uuids)

FPDIR = os.environ.get(
    "KICAD10_FOOTPRINT_DIR",
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")
OX, OY = 100.0, 50.0               # where the board sits on the page
W, H = 100.0, 100.0

# ref -> (x, y, rotation). x, y is the footprint origin (pad 1).
POS = {}
def at(ref, x, y, rot=0):
    POS[ref] = (x, y, rot)

# --- devkit: A column (signal) and K column, 25.4 mm apart, pin 1 at the top
# (8 mm lower than the first layout, to make room for the OUT headers)
DEV_Y1 = 32.0
at("J11", 6.0, DEV_Y1)
at("J12", 31.4, DEV_Y1)

# --- harness headers: IN side-entry on the top edge (opening at the edge,
# rotated 180, pad 1 on the right); OUT vertical right behind it, pad k
# directly behind IN pad k, so each pass-through is one short straight track.
IN_Y, OUT_Y = 12.0, 18.9
for inn, out, x1 in [("J5", "J25", 18.5), ("J4", "J24", 31.5),
                     ("J3", "J23", 44.0), ("J2", "J22", 82.5)]:
    at(inn, x1, IN_Y, 180)
    at(out, x1, OUT_Y, 180)

# --- sense series legs: raw pad (pad 1) under its OUT pin, sense end to the
# left, rows staggered so no two raw pads are neighbours
at("R2", 16.0, 23.5, 180)          # ESD4: raw 16,23.5   -> sense 5.84
at("R1", 13.5, 26.7, 180)          # ESD1: raw 13.5,26.7 -> sense 3.34
# FLOW: raw pad under J4 pin 1, which is # (S.J4_FLOW_PIN, asserted in finish.py)
at("R3", 31.5, 25.5, 180)          # raw 31.5,25.5 -> sense 21.34
# --- sense shunts and candidate caps, flat, under the devkit
for i, ref in enumerate(["R8", "R9", "R10", "R11", "R12", "R13", "R14",
                         "R15", "R16"]):
    at(ref, 13.0, 35.0 + i * 3.5)
for i, ref in enumerate(["C1", "C2", "C3"]):
    at(ref, 14.0, 66.5 + i * 3.5)
at("JP4", 36.5, 92.0)              # selftest: beside the USB end; 2.8 mm from R26 GND

# --- MACHINE CORNER. J2's 150 K lie flat, raw pad (pad 1) on the right
# two columns away from the next lever's fan, so that no fan passes another
# lever's raw pad closer than 2 mm (pre-order review S5): Water's pad sits
# 3 mm LEFT of its pin, 2Cup's 1.5 mm RIGHT of its pin, and Steam's fan runs
# down a lane at x 82.6 to stay clear of 2Cup's pad.
# 2Cup's row is ABOVE 1Cup's: the 2Cup return comes from the outer lane
# (x 98.6), so it has to turn in above the top of the 1Cup lane (x 94.7).
J2R = {"R4": (69.5, 23.4), "R6": (79.0, 26.6), "R5": (75.0, 30.2),
       "R7": (80.0, 33.4)}                          # W, 2C, 1C, S
for ref, (x, y) in J2R.items():
    at(ref, x, y, 180)
at("JP1", 88.0, 9.0)               # (a1): pin 1 = pos 1, pin 2 = 1Cup leg
at("JP2", 92.5, 9.0)               # (a2), clear of the H2 screw head

# --- photoMOS stack, pad 1 (LED anode) facing left, outputs on the right
PM_X = 83.5
OUT_X = PM_X + 7.62
CELL_Y = {1: 37.0, 2: 50.0, 3: 63.0, 4: 76.0}
for n, y in CELL_Y.items():
    at(f"U{3 + n}", PM_X, y)
    at(f"R{39 + n}", 76.5, y)                  # 330: rail -> anode
    at(f"Q{n}", 79.0, y + 4.5, 180)            # C at right, E at left
    at(f"R{43 + n}", 71.0, y + 1.0, 270)       # 100 K base-emitter
    at(f"R{47 + n}", 67.0, y + 1.0, 270)       # 8.2 K Q -> base

# --- the three '221s and their passives. Upright resistors are placed by
# where pad 1 goes and which way pad 2 points, so the bridge-prone neighbours
# face each other with benign nets (review finding 6).
CHIP_X = 43.0
LEFT_X, CAP_R_X, COL1_X, COL2_X = 36.5, 55.3, 60.0, 63.5
UP = []          # (ref, x, pad1_y, "up"|"down")
def upright(ref, x, y1, d):
    UP.append((ref, x, y1, d))

# (crossed assignment: U1 carries 1Cup PM1 on channel 1 and 2Cup PM1 on
# channel 2, U2 the two PM2s -- gen_schematic.py, drive221)
for u, c in {"U1": dict(y=28.0, pullA1="R20", pullA2="R22", rc1="R27",
                        rc2="R28", c1="C10", c2="C11", rb1="R32", rb2="R34"),
             "U2": dict(y=53.0, pullA1="R21", pullA2="R23", rc1="R29",
                        rc2="R30", c1="C12", c2="C13", rb1="R33",
                        rb2="R35")}.items():
    y0 = c["y"]
    at(u, CHIP_X, y0)
    upright(c["pullA1"], LEFT_X, y0 + 2.54, "up")    # 3V3 on top, A at y0+2.54
    upright(c["rb2"], LEFT_X, y0 + 8.54, "up")       # readback on top, Q below
    at(c["c2"], LEFT_X + 0.8, y0 + 12.0, 270)        # pins 6/7
    at(c["c1"], CAP_R_X, y0, 270)                    # pins 14/15
    upright(c["rc1"], COL1_X, y0 + 2.54, "up")       # 3V3 on top
    upright(c["rc2"], COL1_X, y0 + 6.0, "down")      # RC2 on top, 3V3 below
    upright(c["pullA2"], COL1_X, y0 + 15.5, "down")  # pin 9 pull-up
    upright(c["rb1"], COL2_X, y0 + 4.54, "up")       # readback on top, Q below
at("C15", 54.5, 24.5)                      # U1 decoupling, by pin 16
# --- J3 NTC tap, all DNP. The two raw-side 4.7 M stand upright with their
# raw pad under the OUT pins and their far pad (NTC_MID) pointing AWAY from
# each other; everything from NTC_MID on is behind 4.7 M and is ours.
upright("R60", 44.0, 23.4, "right")        # P1 raw -> mid at 49.08
upright("R61", 40.5, 23.4, "left")         # P2 raw -> mid at 35.42
upright("R64", 48.9, 20.0, "right")        # mid -> tap at 51.44,20.0
upright("R62", 52.0, 6.0, "down")          # tap -> GND shunt, scaling only
upright("C20", 49.5, 17.0, "up")           # 10 n, tap -> GND at the op-amp
at("U8", 54.0, 12.5)                       # MCP6002: pin 3 = +INA at 54,17.58
upright("R63", 57.0, 8.5, "up")            # 1 K, follower out -> NTC_ADC
upright("R68", 48.6, 2.6, "right")         # 1 K, gain out -> NTC_ADC at 51.14
upright("C21", 60.0, 6.0, "right")         # 100 n on NTC_ADC
upright("C22", 60.0, 9.3, "right")         # 100 n VDD decoupling, above pin 8
upright("R66", 64.5, 12.6, "down")         # Rf: OUTB (pin 7) -> -INB at 15.14
upright("R67", 64.5, 17.9, "down")         # Rg: -INB -> GND at 20.44
at("C16", 49.5, 49.0, 180)                 # 3V3 pad by U1 pin 9 (pulled up
at("C17", 49.5, 74.0, 180)                 # anyway), GND pad away from it
# U3, the PROPOSED gate
G_Y = 78.0
at("U3", CHIP_X, G_Y)
upright("R24", LEFT_X, G_Y + 2.54, "up")   # gate trigger pull-up, 3V3 on top
upright("R26", LEFT_X, G_Y + 7.0, "down")  # gate enable pull-down
at("C14", CAP_R_X, G_Y, 270)
upright("R31", COL1_X, G_Y + 2.54, "up")   # 3V3 on top
upright("R25", COL1_X, G_Y + 9.0, "down")  # ARM pull-down, away from 3V3
upright("R36", COL2_X + 1.0, G_Y + 6.0, "down")  # gate readback (DNP), 2.9 mm from ARM

# --- jumper (b) and the bench test loops, bottom right
at("JP3", 88.0, 88.0)
at("TP1", 60.0, 93.0)              # GND
at("TP2", 66.0, 93.0)              # 3V3

HOLES = [(3.5, 3.5), (96.5, 3.5), (3.5, 96.5), (96.5, 96.5)]

# Nets that carry machine voltage or sit on a machine line: netclass Machine.
MACHINE_NETS = {"POS1_5V", "A1_LEG", "A2_LEG", "MID_1CUP", "MID_2CUP",
                "1CUP_RAW", "2CUP_RAW", "STEAM_RAW", "WATER_RAW",
                "ESD1_RAW", "ESD4_RAW", "FLOW_RAW", "FLOW_PLUS_RAW",
                "DISP_P4_RAW", "NTC_P1_RAW", "NTC_P2_RAW"}


def mm(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))


def load(fp):
    lib, name = fp.split(":")
    f = pcbnew.FootprintLoad(os.path.join(FPDIR, lib + ".pretty"), name)
    if f is None:
        raise SystemExit(f"footprint not found: {fp}")
    return f


def pad_xy(f, num):
    p = [p for p in f.Pads() if p.GetNumber() == num][0].GetPosition()
    return pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY


def place_upright(f, x, y1, d):
    """Pad 1 at (x, y1); pad 2 above, below, left or right of it."""
    for rot in (0, 90, 180, 270):
        f.SetOrientationDegrees(rot)
        f.SetPosition(mm(x, y1))
        px, py = pad_xy(f, "2")
        ok = {"up": abs(px - x) < 0.01 and py < y1,
              "down": abs(px - x) < 0.01 and py > y1,
              "left": abs(py - y1) < 0.01 and px < x,
              "right": abs(py - y1) < 0.01 and px > x}[d]
        if ok:
            return
    raise SystemExit(f"cannot orient {f.GetReference()} {d}")


# -------------------------------------------------- fixed machine tracks
# Each entry: (net, layer, [points]). A point is (x, y) or ("ref", "pad").
F_, B_, V_ = "F", "B", "via"
def MACHINE_TRACKS():
    # the T-pieces: IN pin k -> OUT pin k, straight down, for every line --
    # GND too (J5 VSS, J4 T), so the machine's own ground current takes the
    # short way and not our pour
    passthrough = []
    for inn, out in S.OUT_REF.items():
        for k, net in S.PIN[inn].items():
            passthrough.append((net, F_, [(inn, k), (out, k)]))
    return passthrough + [
        # GROUND ANCHORS: the machine's ground (J4 T, J5 VSS) is our dividers'
        # reference and the only ground the J2 taps have. Both tie to the
        # devkit's GND at J12 pin 1, clear of the raw fans; the pour is kept
        # off the pass-through block, so these are tracks.
        ("GND", F_, [("J24", S.J4_T_PIN), (26.5, 28.5), (29.9, 31.9),
                     ("J12", "1")]),
        # J5 VSS: B.Cu down the left and across at y 30 to a via on the T
        # anchor's diagonal (a B.Cu run on to J12 walled off the shunts)
        ("GND", B_, [("J25", "4"), (9.0, 20.9), (9.0, 30.0), (28.0, 30.0)]),
        ("GND", V_, [(28.0, 30.0)]),
        # pos 1: J2 IN pin 1 -> (a1) pin 1 -> (a2) pin 1, along the top
        ("POS1_5V", F_, [("J2", "1"), (82.5, 11.0), (84.5, 9.0), ("JP1", "1"),
                         ("JP2", "1")]),
        # fans from the OUT pins straight down to the raw pads of the taps
        ("ESD4_RAW", F_, [("J25", "2"), ("R2", "1")]),
        ("ESD1_RAW", F_, [("J25", "3"), ("R1", "1")]),
        ("FLOW_RAW", F_, [("J24", S.J4_FLOW_PIN), ("R3", "1")]),
        ("NTC_P1_RAW", F_, [("J23", "1"), ("R60", "1")]),
        ("NTC_P2_RAW", F_, [("J23", "2"), (41.5, 21.0), (40.5, 22.0),
                            ("R61", "1")]),
        # The four lever fans. Inside the "J2 fan zone" (y < 23.0) they sit
        # at the header's own 2.5 mm pitch; each is cut at the zone edge so
        # the DRC exemption ends there. Below it every lever is >= 2 mm from
        # every other lever's copper, and 1Cup >= 2.5 mm from 2Cup.
        ("WATER_RAW", F_, [("J22", "5"), (72.5, 20.4), ("R4", "1")]),
        ("1CUP_RAW", F_, [("J22", "4"), (75.0, 23.3), ("R5", "1")]),
        ("2CUP_RAW", F_, [("J22", "3"), (77.5, 22.0), (79.0, 23.5),
                          ("R6", "1")]),
        ("STEAM_RAW", F_, [("J22", "2"), (80.0, 21.0), (82.6, 23.6),
                           (82.6, 30.8), ("R7", "1")]),
        # drive legs behind the open jumpers, on the output side
        ("A1_LEG", F_, [("JP1", "2"), (88.0, 13.0), (88.5, 13.5), (88.5, 34.4),
                        ("U4", "8")]),
        # x 96.4: 1.8 mm lateral from its own return on the other layer
        # (x 98.6), 2.6 mm from the MID_1CUP jog (pre-order review S8)
        ("A2_LEG", F_, [("JP2", "2"), (94.46, 13.5), (96.4, 13.5),
                        (96.4, CELL_Y[3]), ("U6", "8")]),
        # the MID links jog right round the empty socket pads 5/6 between them
        ("MID_1CUP", F_, [("U4", "7"), (OUT_X + 2.28, CELL_Y[1] + 4.82),
                          (OUT_X + 2.28, CELL_Y[2] - 2.28), ("U5", "8")]),
        ("MID_2CUP", F_, [("U6", "7"), (OUT_X + 2.28, CELL_Y[3] + 4.82),
                          (OUT_X + 2.28, CELL_Y[4] - 2.28), ("U7", "8")]),
        # the raw returns, B.Cu, outside the stack, each in its own lane and
        # in at its own row (crossing the A legs and fans on the other layer)
        ("1CUP_RAW", B_, [("U5", "7"), (94.7, CELL_Y[2] + 2.54),
                          (94.7, J2R["R5"][1]), ("R5", "1")]),
        ("2CUP_RAW", B_, [("U7", "7"), (98.6, CELL_Y[4] + 2.54),
                          (98.6, J2R["R6"][1]), ("R6", "1")]),
    ] + OUR_FIXED_TRACKS()


def OUR_FIXED_TRACKS():
    """Two nets of OURS that the autorouter must not place (pre-order review
    S6, S7). Laid like the machine nets; route.py re-lays them the same way.

    +3V3 trunk: the board's only 3V3 feed used to run between the J5 IN and
    OUT rows, under Disp-P4 and the bus. It now leaves J11 under the devkit
    (B.Cu, y 33.27), rounds the VSS via and J12 pin 1, and ends on R20.2;
    the router takes 3V3 on from there, and is kept out of the header block.

    LED_RAIL: the router threaded it between TO-92 pads and past JP3.1 (the
    3V3 side of jumper (b)). It is now a trunk at x 65.25, left of the cells,
    with one stub per cell 1 mm above the cell's resistor row, and it reaches
    JP3.2 from below, away from JP3.1."""
    rail = [("LED_RAIL", B_, [(65.25, CELL_Y[1] - 1.0), (65.25, 81.5),
                              (75.65, 91.9), (86.64, 91.9), ("JP3", "2")])]
    for n, y in CELL_Y.items():
        rail.append(("LED_RAIL", B_, [(65.25, y - 1.0), (75.5, y - 1.0),
                                      (f"R{39 + n}", "1")]))
    return rail + [
        ("+3V3", B_, [("J11", "1"), ("J11", "2"), (7.27, 33.27),
                      (27.5, 33.27), (29.3, 31.47), (29.3, 30.9),
                      (29.9, 30.3), (34.2, 30.3), ("R20", "2")]),
    ]


MACHINE_WIDTH = 0.4


def lay_machine_tracks(board, padxy, nets):
    """padxy(ref, pad) -> (x, y). Taken as a function because a board that
    has had items removed hands back untyped SWIG footprints."""
    for net, layer, pts in MACHINE_TRACKS():
        xy = [padxy(p[0], p[1]) if isinstance(p[0], str) else p for p in pts]
        if layer == V_:
            for x, y in xy:
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(mm(x, y))
                v.SetWidth(pcbnew.FromMM(0.9))
                v.SetDrill(pcbnew.FromMM(0.45))
                v.SetNet(nets[net])
                v.SetLocked(True)
                board.Add(v)
            continue
        for (x1, y1), (x2, y2) in zip(xy, xy[1:]):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(mm(x1, y1))
            t.SetEnd(mm(x2, y2))
            t.SetWidth(pcbnew.FromMM(MACHINE_WIDTH))
            t.SetLayer(pcbnew.F_Cu if layer == F_ else pcbnew.B_Cu)
            t.SetNet(nets[net])
            t.SetLocked(True)
            board.Add(t)


def build():
    board = pcbnew.BOARD()
    nets = {}
    for ref, sym, value, fp, pins, dnp in S.P:
        for net in pins.values():
            if net and net not in nets:
                ni = pcbnew.NETINFO_ITEM(board, net)
                board.Add(ni)
                nets[net] = ni

    upright_at = {r: (x, y, d) for r, x, y, d in UP}
    fps = {}
    for ref, sym, value, fp, pins, dnp in S.P:
        f = load(fp)
        f.SetReference(ref)
        f.SetValue(value)
        f.SetFPIDAsString(fp)
        f.SetPath(pcbnew.KIID_PATH(f"/{S.ROOT_UUID}/{S.symbol_uuid(ref)}"))
        if dnp:
            f.SetDNP(True)
        if ref in upright_at:
            place_upright(f, *upright_at[ref])
        else:
            x, y, rot = POS[ref]
            f.SetOrientationDegrees(rot)
            f.SetPosition(mm(x, y))
        board.Add(f)
        fps[ref] = f
        for pad in f.Pads():
            net = pins.get(pad.GetNumber())
            if net:
                pad.SetNet(nets[net])

    for i, (x, y) in enumerate(HOLES, start=1):
        h = load("MountingHole:MountingHole_3.2mm_M3")
        h.SetReference(f"H{i}")
        h.SetPosition(mm(x, y))
        board.Add(h)

    corners = [(0, 0), (W, 0), (W, H), (0, H)]
    for (x1, y1), (x2, y2) in zip(corners, corners[1:] + corners[:1]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(pcbnew.FromMM(0.1))
        s.SetStart(mm(x1, y1))
        s.SetEnd(mm(x2, y2))
        board.Add(s)

    lay_machine_tracks(board, lambda r, n: pad_xy(fps[r], n), nets)
    out = os.path.join(HERE, f"{S.PROJECT}.kicad_pcb")
    board.Save(out)
    return out


def overlaps(board):
    """Courtyard bounding boxes that intersect: a cheap pre-DRC check."""
    boxes = []
    for f in board.GetFootprints():
        cy = f.GetCourtyard(pcbnew.F_CrtYd)
        bb = cy.BBox() if cy.OutlineCount() else f.GetBoundingBox(False)
        boxes.append((f.GetReference(), bb))
    hits = []
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            if boxes[i][1].Intersects(boxes[j][1]):
                hits.append((boxes[i][0], boxes[j][0]))
    edge = pcbnew.BOX2I(mm(0, 0), pcbnew.VECTOR2I(pcbnew.FromMM(W), pcbnew.FromMM(H)))
    outside = [r for r, bb in boxes if not edge.Contains(bb)]
    return hits, outside


if __name__ == "__main__":
    # a new placement makes any routed copy stale: finish.py must not find it
    _stale = os.path.join(HERE, "build", "routed.kicad_pcb")
    if os.path.exists(_stale):
        os.remove(_stale)
    out = build()
    board = pcbnew.LoadBoard(out)       # reload so courtyards are built
    hits, outside = overlaps(board)
    real = [h for h in hits if not (set(h) & {"J11", "J12"})]
    print(f"placed {len(board.GetFootprints())} footprints, "
          f"{len(board.GetTracks())} fixed machine track segments")
    for a, b in real:
        print(f"OVERLAP {a} / {b}")
    for r in outside:
        print(f"OUTSIDE BOARD {r}")
    print(f"{len(real)} courtyard overlaps, {len(outside)} outside the board")
