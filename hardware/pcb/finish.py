#!/usr/bin/env python3
"""Finish the routed board: ground pour, keep-outs, labels, readable refs.

  gen_pcb.py -> route.py -> finish.py      (run with KiCad's Python)

Repeatable: it starts from route.py's copy in build/, before any of this.
"""
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, "espressolog-board.kicad_pcb")
OX, OY, W, H = 100.0, 50.0, 100.0, 100.0
F = pcbnew.FromMM


def P(x, y):
    return pcbnew.VECTOR2I(F(OX + x), F(OY + y))


# Start from route.py's copy: no zones, no labels, so this is repeatable.
board = pcbnew.LoadBoard(os.path.join(HERE, "build", "routed.kicad_pcb"))


def polygon(zone, pts):
    o = zone.Outline()
    o.NewOutline()
    for x, y in pts:
        o.Append(F(OX + x), F(OY + y))


# ------------------------------------------------------------ keep-outs
# No copper POUR in the machine corner (J2, its 150 K, pos 1, (a1)/(a2), the
# returns' channels) or down the photoMOS output column and its lanes: those
# nets stay as tracks only, so the machine-voltage area reads as its own zone.
# Elsewhere the netclass (2 mm, espressolog-board.kicad_dru) keeps the pour
# away from the J4/J5 raw lines.
MACHINE_AREA = [(60, 0), (W, 0), (W, 86), (87.5, 86), (87.5, 34.5),
                (60, 34.5)]
# Under the ESP32 module's antenna (the devkit's module end, at the top).
ANTENNA_AREA = [(4.5, 16.5), (33.0, 16.5), (33.0, 26.5), (4.5, 26.5)]
for name, pts in [("machine corner: no pour", MACHINE_AREA),
                  ("antenna: no pour", ANTENNA_AREA)]:
    k = pcbnew.ZONE(board)
    k.SetIsRuleArea(True)
    k.SetZoneName(name)
    k.SetDoNotAllowZoneFills(True)
    k.SetDoNotAllowTracks(False)
    k.SetDoNotAllowVias(False)
    k.SetDoNotAllowPads(False)
    k.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    k.SetLayerSet(ls)
    polygon(k, pts)
    board.Add(k)

# ------------------------------------------------------------- GND pours
# GND is not routed (route.py leaves it out): both pours carry it, joined
# through the THT GND pads.
gnd = board.FindNet("GND")
for layer, name in [(pcbnew.B_Cu, "GND pour B"), (pcbnew.F_Cu, "GND pour F")]:
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(gnd)
    z.SetZoneName(name)
    z.SetLocalClearance(F(0.4))
    z.SetMinThickness(F(0.3))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(F(0.5))
    z.SetThermalReliefSpokeWidth(F(0.5))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    polygon(z, [(0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)])
    board.Add(z)

# ------------------------------------------------------------ obstacles
def inflate(bb, mm):
    bb = pcbnew.BOX2I(bb.GetPosition(), bb.GetSize())
    bb.Inflate(F(mm))
    return bb


obstacles = []          # bounding boxes text must avoid
own_silk = {}           # ref -> indexes of its own silk outline in obstacles
for fp in board.GetFootprints():
    for pad in fp.Pads():
        obstacles.append(inflate(pad.GetBoundingBox(), 0.25))
    for g in fp.GraphicalItems():
        if g.GetLayer() == pcbnew.F_SilkS and not isinstance(g, pcbnew.PCB_TEXT):
            own_silk.setdefault(fp.GetReference(), set()).add(len(obstacles))
            obstacles.append(inflate(g.GetBoundingBox(), 0.15))
edge = pcbnew.BOX2I(P(0.8, 0.8), pcbnew.VECTOR2I(F(W - 1.6), F(H - 1.6)))


def free(bb, ignore=()):
    """True if bb is on the board and clear of every obstacle except those
    in ignore (a reference may sit inside its own part's outline)."""
    return edge.Contains(bb) and not any(
        bb.Intersects(o) for i, o in enumerate(obstacles) if i not in ignore)


def text(s, x, y, size=1.0, angle=0, layer=pcbnew.F_SilkS):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(F(size), F(size)))
    t.SetTextThickness(F(size * 0.15))
    t.SetPosition(P(x, y))
    t.SetTextAngleDegrees(angle)
    board.Add(t)
    bb = t.GetBoundingBox()
    if layer == pcbnew.F_SilkS:
        if not free(bb):
            print(f"LABEL COLLIDES: {s!r} at ({x}, {y})")
        obstacles.append(inflate(bb, 0.2))
    return t


# ------------------------------------------------------------- labels
text("a1", 85.0, 5.0)
text("a2", 90.0, 5.0)
text("OPEN in Phase 0", 87.5, 13.6, 0.8)
text("MACHINE", 60.0, 2.6, 1.0)
text("J5 display", 13.0, 1.8, 0.8)
text("J4 flow", 28.5, 1.8, 0.8)
text("J2 switches", 73.0, 1.8, 0.8)
text("(b) OPEN until bench", 79.0, 93.0, 0.8)
text("SELFTEST: never fit", 27.5, 94.6, 0.8)
text("U3 + R36 OUT while gate PROPOSED", 34.0, 99.0 - 0.9, 0.8)
text("espressolog sense+drive v1 2026-11", 75.0, 98.2, 0.8)
text("devkit: module/antenna end", 19.0, 15.6, 0.8, layer=pcbnew.Dwgs_User)

# devkit outline on the drawings layer: where it sits over the sense shunts
dev = pcbnew.PCB_SHAPE(board)
dev.SetShape(pcbnew.SHAPE_T_RECT)
dev.SetLayer(pcbnew.Dwgs_User)
dev.SetStart(P(4.7, 19.0))
dev.SetEnd(P(32.7, 82.5))
dev.SetWidth(F(0.15))
board.Add(dev)

# ---------------------------------------------- reference designators
SIZE = 0.8
moved, fab = 0, []
for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
    ref = fp.Reference()
    if fp.GetReference().startswith("H"):
        ref.SetVisible(False)
        continue
    ref.SetTextSize(pcbnew.VECTOR2I(F(SIZE), F(SIZE)))
    ref.SetTextThickness(F(0.12))
    ref.SetLayer(pcbnew.F_SilkS)
    bb = fp.GetBoundingBox(False)
    cx, cy = bb.GetCenter().x, bb.GetCenter().y
    l, r, t_, b_ = bb.GetLeft(), bb.GetRight(), bb.GetTop(), bb.GetBottom()
    g = F(0.7)
    cands = [(cx, t_ - g, 0), (cx, b_ + g, 0), (l - g * 2, cy, 90),
             (r + g * 2, cy, 90), (l - F(2.0), cy, 0), (r + F(2.0), cy, 0),
             (cx, cy, 0), (cx, cy, 90)]
    for k in (1.6, 2.6):                     # then further out
        cands += [(cx, t_ - F(k), 0), (cx, b_ + F(k), 0),
                  (l - F(k + 1.5), cy, 0), (r + F(k + 1.5), cy, 0),
                  (l - F(k), t_, 0), (r + F(k), t_, 0),
                  (l - F(k), b_, 0), (r + F(k), b_, 0)]
    for x, y, a in cands:
        ref.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))
        ref.SetTextAngleDegrees(a)
        tb = ref.GetBoundingBox()
        own = own_silk.get(fp.GetReference(), ()) if (x, y) == (cx, cy) else ()
        if free(tb, own):
            obstacles.append(inflate(tb, 0.15))
            moved += 1
            break
    else:
        ref.SetLayer(pcbnew.F_Fab)       # no room on silk: fab layer only
        ref.SetPosition(bb.GetCenter())
        fab.append(fp.GetReference())

pcbnew.ZONE_FILLER(board).Fill(board.Zones())
board.Save(PCB)
print(f"refs placed on silk: {moved}; fab only (no room): {', '.join(fab) or 'none'}")
