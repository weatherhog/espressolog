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

Machine-corner topology (all lanes on the OUTPUT side of the photoMOS):
  F.Cu  pos 1 (top), the J2/J4/J5 fans to their series resistors, A1_LEG,
        A2_LEG (outer lane x 97.5), the two MID links on the output column
  B.Cu  the raw returns: 1CUP_RAW in lane x 94.4, 2CUP_RAW in lane x 98.6,
        each up its own channel between the J2 resistor columns
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
at("J11", 6.0, 24.0)
at("J12", 31.4, 24.0)

# --- harness headers on the top edge, opening at the edge (rotated 180)
at("J5", 18.5, 12.0, 180)          # pads 18.5 (1, empty) .. 11 (4, VSS)
at("J4", 31.5, 12.0, 180)          # pads 31.5 (1, T) .. 26.5 (3, #)
at("J2", 78.0, 12.0, 180)          # pads 78 (1, pos 1) .. 68 (5, Water)

# --- sense series legs: raw pad (pad 1) on the right, under its header pin,
# and no two raw pads neighbours
at("R2", 17.5, 17.0, 180)          # ESD4: raw 17.5,17  -> sense 7.34,17
at("R1", 13.5, 20.5, 180)          # ESD1: raw 13.5,20.5 -> sense 3.34,20.5
at("R3", 26.5, 23.0, 180)          # FLOW: raw 26.5,23  -> sense 16.34,23
# --- sense shunts and candidate caps, flat, under the devkit
for i, ref in enumerate(["R8", "R9", "R10", "R11", "R12", "R13", "R14",
                         "R15", "R16"]):
    at(ref, 13.0, 27.0 + i * 3.5)
for i, ref in enumerate(["C1", "C2", "C3"]):
    at(ref, 14.0, 58.5 + i * 3.5)
at("JP4", 27.5, 89.0)              # selftest: beside the USB end, not in line

# --- MACHINE CORNER. J2's 150 K stand as columns, raw pad (pad 1) at the top,
# 6.5 mm apart so each return climbs its own channel between two columns.
J2R_Y = 18.5
J2R_X = {"R4": 63.5, "R5": 69.5, "R6": 76.0, "R7": 82.5}   # W, 1C, 2C, S
for ref, x in J2R_X.items():
    at(ref, x, J2R_Y, 90)                     # checked below: pad 2 is BELOW
at("JP1", 85.0, 8.0)               # (a1): pin 1 = pos 1, pin 2 = 1Cup leg
at("JP2", 90.0, 8.0)               # (a2)

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

for u, c in {"U1": dict(y=28.0, pullA1="R20", pullA2="R21", rc1="R27",
                        rc2="R28", c1="C10", c2="C11", rb1="R32", rb2="R33"),
             "U2": dict(y=53.0, pullA1="R22", pullA2="R23", rc1="R29",
                        rc2="R30", c1="C12", c2="C13", rb1="R34",
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
at("C15", 44.5, 24.0)                      # U1 decoupling, above pin 16
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
upright("R36", COL2_X, G_Y + 6.0, "down")  # gate readback (DNP)

# --- jumper (b) and the bench test loops, bottom right
at("JP3", 88.0, 88.0)
at("TP1", 60.0, 93.0)              # GND
at("TP2", 66.0, 93.0)              # 3V3

HOLES = [(3.5, 3.5), (96.5, 3.5), (3.5, 96.5), (96.5, 96.5)]

# Nets that carry machine voltage or sit on a machine line: netclass Machine.
MACHINE_NETS = {"POS1_5V", "A1_LEG", "A2_LEG", "MID_1CUP", "MID_2CUP",
                "1CUP_RAW", "2CUP_RAW", "STEAM_RAW", "WATER_RAW",
                "ESD1_RAW", "ESD4_RAW", "FLOW_RAW"}


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
    """Pad 1 at (x, y1); pad 2 directly above ("up") or below ("down")."""
    for rot in (0, 90, 180, 270):
        f.SetOrientationDegrees(rot)
        f.SetPosition(mm(x, y1))
        px, py = pad_xy(f, "2")
        if abs(px - x) < 0.01 and ((py < y1) if d == "up" else (py > y1)):
            return
    raise SystemExit(f"cannot orient {f.GetReference()} {d}")


# -------------------------------------------------- fixed machine tracks
# Each entry: (net, layer, [points]). A point is (x, y) or ("ref", "pad").
F_, B_ = "F", "B"
def MACHINE_TRACKS():
    return [
        # pos 1: J2 pin 1 -> (a1) pin 1 -> (a2) pin 1, along the top
        ("POS1_5V", F_, [("J2", "1"), (78.0, 10.0), (80.0, 8.0), ("JP1", "1"),
                         ("JP2", "1")]),
        # J2 fans to the raw pads of their 150 K
        ("WATER_RAW", F_, [("J2", "5"), (68.0, 13.5), (63.5, 18.0), ("R4", "1")]),
        ("1CUP_RAW", F_, [("J2", "4"), (70.5, 15.0), (69.5, 16.0), ("R5", "1")]),
        ("2CUP_RAW", F_, [("J2", "3"), (73.0, 15.0), (76.0, 18.0), ("R6", "1")]),
        # Steam drops 2 mm before turning, so it clears pos 1's pin by 2 mm
        ("STEAM_RAW", F_, [("J2", "2"), (75.5, 14.0), (80.0, 18.5), ("R7", "1")]),
        # J5 / J4 fans to their series legs
        ("ESD4_RAW", F_, [("J5", "2"), (16.0, 14.5), (17.5, 16.0), ("R2", "1")]),
        ("ESD1_RAW", F_, [("J5", "3"), ("R1", "1")]),
        ("FLOW_RAW", F_, [("J4", "3"), ("R3", "1")]),
        # drive legs behind the open jumpers, on the output side
        ("A1_LEG", F_, [("JP1", "2"), (85.0, 13.0), (88.5, 16.5), (88.5, 34.4),
                        ("U4", "8")]),
        ("A2_LEG", F_, [("JP2", "2"), (92.0, 12.54), (97.5, 12.54),
                        (97.5, CELL_Y[3]), ("U6", "8")]),
        # the MID links jog right round the empty socket pads 5/6 between them
        ("MID_1CUP", F_, [("U4", "7"), (OUT_X + 2.28, CELL_Y[1] + 4.82),
                          (OUT_X + 2.28, CELL_Y[2] - 2.28), ("U5", "8")]),
        ("MID_2CUP", F_, [("U6", "7"), (OUT_X + 2.28, CELL_Y[3] + 4.82),
                          (OUT_X + 2.28, CELL_Y[4] - 2.28), ("U7", "8")]),
        # the raw returns, B.Cu, outside the stack, each up its own channel
        ("1CUP_RAW", B_, [("U5", "7"), (94.4, CELL_Y[2] + 2.54), (94.4, 32.5),
                          (72.75, 32.5), (72.75, J2R_Y), ("R5", "1")]),
        ("2CUP_RAW", B_, [("U7", "7"), (98.6, CELL_Y[4] + 2.54), (98.6, 29.5),
                          (79.25, 29.5), (79.25, J2R_Y), ("R6", "1")]),
    ]

MACHINE_WIDTH = 0.4


def lay_machine_tracks(board, padxy, nets):
    """padxy(ref, pad) -> (x, y). Taken as a function because a board that
    has had items removed hands back untyped SWIG footprints."""
    for net, layer, pts in MACHINE_TRACKS():
        xy = [padxy(p[0], p[1]) if isinstance(p[0], str) else p for p in pts]
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

    # the J2 resistors: pad 2 (sense) must be BELOW pad 1 (raw)
    for ref, x in J2R_X.items():
        (x1, y1), (x2, y2) = pad_xy(fps[ref], "1"), pad_xy(fps[ref], "2")
        if not (abs(x2 - x1) < 0.01 and y2 > y1):
            fps[ref].SetOrientationDegrees(270)
            fps[ref].SetPosition(mm(x, J2R_Y))

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
