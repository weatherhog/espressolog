#!/usr/bin/env python3
"""Place the espressolog board: footprints, nets, outline, rules. No routing.

Run with KiCad's own Python (it needs the pcbnew module):

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen_pcb.py

The parts and nets come from gen_schematic.py, so the board cannot drift from
the schematic. Each footprint is linked to its symbol by uuid, so KiCad's
"Update PCB from Schematic" recognises them.

Coordinates below are mm on the board, origin at the top-left corner, y down.
The board is 100 x 100 mm, the size of the perfboard it replaces.

Zones (docs/board-and-perfboard.md, "Drive-side layout", carried over):
  left        devkit sockets; the sense shunts lie flat under the devkit
  top-left    J5, J4 and the three sense series legs
  top-right   MACHINE CORNER: J2, its four 150 K, pos 1, jumpers (a1) (a2)
  right edge  the photoMOS stack, outputs on the right-hand column
  centre      the three '221s with their passives either side
  right-mid   the four LED-driver cells, each beside its photoMOS
  bottom      jumper (b), as far from (a1)/(a2) as the board allows
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

# ref -> (x, y, rotation in degrees). x, y is the footprint's pad-1 origin.
POS = {}
def at(ref, x, y, rot=0):
    POS[ref] = (x, y, rot)

# --- devkit: A column (signal) and K column, 25.4 mm apart, pin 1 at the top
DEV_X_A, DEV_X_K, DEV_Y1 = 6.0, 31.4, 24.0
at("J11", DEV_X_A, DEV_Y1)
at("J12", DEV_X_K, DEV_Y1)

# --- harness headers on the top edge, opening at the edge (rotated 180)
at("J5", 18.5, 12.0, 180)          # pads 18.5 (1, empty) .. 11 (4, VSS)
at("J4", 31.5, 12.0, 180)          # pads 31.5 (1, T) .. 26.5 (3, #)
at("J2", 78.0, 12.0, 180)          # pads 78 (1, pos 1) .. 68 (5, Water)

# --- sense series legs, between the headers and the devkit
at("R2", 9.0, 17.5)                # ESD4
at("R1", 9.0, 21.0)                # ESD1
at("R3", 22.0, 17.5)               # FLOW (#)
# --- sense shunts and candidate caps, flat, under the devkit
for i, ref in enumerate(["R8", "R9", "R10", "R11", "R12", "R13", "R14",
                         "R15", "R16"]):
    at(ref, 13.0, 27.0 + i * 3.5)
for i, ref in enumerate(["C1", "C2", "C3"]):
    at(ref, 14.0, 58.5 + i * 3.5)
at("JP4", 14.0, 88.0)              # selftest, below the devkit's USB end

# --- MACHINE CORNER: J2's 150 K, raw end (pad 1) toward J2, sense end left
for i, ref in enumerate(["R4", "R5", "R6", "R7"]):
    at(ref, 74.0, 16.5 + i * 3.2, 180)
at("JP1", 85.0, 8.0)               # (a1): pin 1 = pos 1, pin 2 = 1Cup leg
at("JP2", 90.0, 8.0)               # (a2)

# --- photoMOS stack, pad 1 (LED anode) facing left, outputs on the right
PM_X = 85.0
CELL_Y = {1: 33.0, 2: 46.0, 3: 59.0, 4: 72.0}
for n, y in CELL_Y.items():
    at(f"U{3 + n}", PM_X, y)
    at(f"R{39 + n}", 78.0, y)                  # 330: rail -> anode
    at(f"Q{n}", 80.5, y + 4.5, 180)            # C at right, E at left
    at(f"R{43 + n}", 71.5, y + 1.0, 270)       # 100 K base-emitter
    at(f"R{47 + n}", 67.5, y + 1.0, 270)       # 8.2 K Q -> base

# --- the three '221s and their passives
CHIP_X = 43.0
LEFT_X, CAP_R_X, COL1_X, COL2_X = 36.5, 55.3, 60.0, 63.5
CHIPS = {
    "U1": dict(y=28.0, pullA1="R20", pullA2="R21", rc1="R27", rc2="R28",
               c1="C10", c2="C11", rb1="R32", rb2="R33", dec="C15"),
    "U2": dict(y=53.0, pullA1="R22", pullA2="R23", rc1="R29", rc2="R30",
               c1="C12", c2="C13", rb1="R34", rb2="R35", dec="C16"),
}
for u, c in CHIPS.items():
    y0 = c["y"]
    at(u, CHIP_X, y0)
    at(c["dec"], 44.5, y0 - 4.0)               # decoupling, by pin 16
    at(c["pullA1"], LEFT_X, y0, 270)           # pin 1 pull-up
    at(c["rb2"], LEFT_X, y0 + 6.0, 270)        # pin 5 readback
    at(c["c2"], LEFT_X + 0.8, y0 + 12.0, 270)  # pins 6/7
    at(c["c1"], CAP_R_X, y0, 270)              # pins 14/15
    at(c["rc1"], COL1_X, y0, 270)
    at(c["rc2"], COL1_X, y0 + 6.0, 270)
    at(c["pullA2"], COL1_X, y0 + 15.5, 270)    # pin 9 pull-up
    at(c["rb1"], COL2_X, y0 + 2.0, 270)        # pin 13 readback
# U3, the PROPOSED gate
G_Y = 78.0
at("U3", CHIP_X, G_Y)
at("C17", 44.5, G_Y - 4.0)
at("R24", LEFT_X, G_Y, 270)                    # gate trigger pull-up
at("R26", LEFT_X, G_Y + 6.0, 270)              # gate enable pull-down
at("C14", CAP_R_X, G_Y, 270)
at("R31", COL1_X, G_Y, 270)
at("R25", COL1_X, G_Y + 6.0, 270)              # ARM pull-down
at("R36", COL2_X, G_Y + 6.0, 270)              # gate readback (DNP)

# --- jumper (b), bottom right, far from (a1)/(a2)
at("JP3", 88.0, 88.0)

HOLES = [(3.5, 3.5), (96.5, 3.5), (3.5, 96.5), (96.5, 96.5)]

# Nets that carry machine voltage or sit on a machine line. They get their own
# netclass with wider clearance and track, so the router keeps them apart.
MACHINE_NETS = {"POS1_5V", "A1_LEG", "A2_LEG", "MID_1CUP", "MID_2CUP",
                "1CUP_RAW", "2CUP_RAW", "STEAM_RAW", "WATER_RAW",
                "ESD1_RAW", "ESD4_RAW", "FLOW_RAW"}


def mm(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))


def build():
    board = pcbnew.BOARD()
    nets = {}
    for ref, sym, value, fp, pins, dnp in S.P:
        for net in pins.values():
            if net and net not in nets:
                ni = pcbnew.NETINFO_ITEM(board, net)
                board.Add(ni)
                nets[net] = ni

    root = S.ROOT_UUID
    for ref, sym, value, fp, pins, dnp in S.P:
        lib, name = fp.split(":")
        f = pcbnew.FootprintLoad(os.path.join(FPDIR, lib + ".pretty"), name)
        if f is None:
            raise SystemExit(f"footprint not found: {fp}")
        f.SetReference(ref)
        f.SetValue(value)
        f.SetFPIDAsString(fp)
        f.SetPath(pcbnew.KIID_PATH(f"/{root}/{S.symbol_uuid(ref)}"))
        if dnp:
            f.SetDNP(True)
        x, y, rot = POS[ref]
        f.SetPosition(mm(x, y))
        f.SetOrientationDegrees(rot)
        board.Add(f)
        for pad in f.Pads():
            net = pins.get(pad.GetNumber())
            if net:
                pad.SetNet(nets[net])

    for i, (x, y) in enumerate(HOLES, start=1):
        h = pcbnew.FootprintLoad(os.path.join(FPDIR, "MountingHole.pretty"),
                                 "MountingHole_3.2mm_M3")
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

    out = os.path.join(HERE, f"{S.PROJECT}.kicad_pcb")
    board.Save(out)
    return board, out


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
            a, b = boxes[i][1], boxes[j][1]
            if a.Intersects(b):
                hits.append((boxes[i][0], boxes[j][0]))
    edge = pcbnew.BOX2I(mm(0, 0), pcbnew.VECTOR2I(pcbnew.FromMM(W), pcbnew.FromMM(H)))
    outside = [r for r, bb in boxes if not edge.Contains(bb)]
    return hits, outside


if __name__ == "__main__":
    board, out = build()
    board = pcbnew.LoadBoard(out)       # reload so courtyards are built
    board.BuildConnectivity()
    hits, outside = overlaps(board)
    UNDER_DEVKIT = {"J11", "J12"}       # parts under the devkit are expected
    real = [h for h in hits if not (set(h) & UNDER_DEVKIT)]
    print(f"placed {len(board.GetFootprints())} footprints -> {os.path.basename(out)}")
    for a, b in real:
        print(f"OVERLAP {a} / {b}")
    for r in outside:
        print(f"OUTSIDE BOARD {r}")
    print(f"{len(real)} courtyard overlaps, {len(outside)} outside the board")
