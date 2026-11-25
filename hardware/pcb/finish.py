#!/usr/bin/env python3
"""Finish the routed board: ground pour, keep-outs, labels, readable refs.

  gen_pcb.py -> route.py -> finish.py      (run with KiCad's Python)

Repeatable: it starts from route.py's copy in build/, before any of this.
"""
import os
import shutil
import subprocess
import pcbnew
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_schematic as S          # noqa: E402  (the netlist)
import gen_pcb as G                # noqa: E402  (the hand-laid copper)

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, "espressolog-board.kicad_pcb")
OX, OY, W, H = 100.0, 50.0, 100.0, 100.0
F = pcbnew.FromMM


def P(x, y):
    return pcbnew.VECTOR2I(F(OX + x), F(OY + y))


# Start from route.py's copy: no zones, no labels, so this is repeatable.
ROUTED = os.path.join(HERE, "build", "routed.kicad_pcb")
if not os.path.exists(ROUTED):
    sys.exit("no build/routed.kicad_pcb: route.py did not finish (it removes "
             "a stale one at its start). Run route.py again.")
board = pcbnew.LoadBoard(ROUTED)


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
MACHINE_AREA = [(66, 0), (W, 0), (W, 86), (87.5, 86), (87.5, 35.5),
                (66, 35.5)]
# The J5/J4/J3 pass-throughs (IN and OUT headers): J4 + and J5 Disp-P4 run
# here, and nothing of ours may pour up to them.
# (to y 26: the J3 tap's raw pads R60.1/R61.1 at y 23.4 sit inside it)
HEADER_AREA = [(7.5, 0.5), (48.0, 0.5), (48.0, 26.0), (7.5, 26.0)]
# Devkit 5Vin (J11.21) sits beside its GND (J11.22): keep the pour >= 2 mm
# from J11.20-22 (CLAUDE.md "5Vin ... Leave the gap"; final review S3).
FIVEV_AREA = [(3.0, 77.3), (9.0, 77.3), (9.0, 88.3), (3.0, 88.3)]
# No pour within 4 mm of a mounting hole: a screw head or metal standoff
# must not sit on masked ground copper (pre-order review S11). H2 is in the
# machine corner, which has no pour anyway.
def _octagon(cx, cy, r):
    k = r * 0.4142
    return [(cx - k, cy - r), (cx + k, cy - r), (cx + r, cy - k), (cx + r, cy + k),
            (cx + k, cy + r), (cx - k, cy + r), (cx - r, cy + k), (cx - r, cy - k)]
HOLE_AREAS = [(f"mounting hole {n}: no pour", _octagon(x, y, 4.0))
              for n, (x, y) in [("H1", (3.5, 3.5)), ("H3", (3.5, 96.5)),
                                ("H4", (96.5, 96.5))]]
# Under the ESP32 module's antenna (the devkit's module end, at the top).
ANTENNA_AREA = [(4.5, 24.5), (33.0, 24.5), (33.0, 34.5), (4.5, 34.5)]
for name, pts in [("machine corner: no pour", MACHINE_AREA),
                  ("pass-through headers: no pour", HEADER_AREA),
                  ("antenna: no pour", ANTENNA_AREA),
                  ("devkit 5Vin: no pour", FIVEV_AREA),
                  # named for espressolog-board.kicad_dru: the first mm of
                  # track leaving J2/J22's 2.5 mm pitch, exempt from the
                  # 1Cup/2Cup 2.5 mm rule (it is in the machine corner, so
                  # no pour here either)
                  ("J2 fan zone", [(66.0, 14.0), (84.0, 14.0), (84.0, 23.0),
                                   (66.0, 23.0)])] + HOLE_AREAS:
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
# GND is routed (route.py), and both pours carry it too, joined through the
# THT GND pads.
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


def free(bb, ignore=(), why=False):
    """True if bb is on the board and clear of every obstacle except those
    in ignore (a reference may sit inside its own part's outline)."""
    hits = [o for i, o in enumerate(obstacles) if i not in ignore and bb.Intersects(o)]
    if why:
        for o in hits:
            c = o.GetCenter()
            print(f"   hits obstacle at ({pcbnew.ToMM(c.x) - OX:.1f}, {pcbnew.ToMM(c.y) - OY:.1f})")
        if not edge.Contains(bb):
            print("   outside the board margin")
    return edge.Contains(bb) and not hits


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
            free(bb, why=True)
        obstacles.append(inflate(bb, 0.2))
    return t


# ------------------------------------------------------------- labels
# Silk is >= 1.0 mm high with a 0.15 mm stroke (JLCPCB's minimum).
text("a1", 88.0, 6.0)
text("a2", 92.5, 6.0)
text("OPEN in", 90.3, 15.6)
text("Phase 0", 90.3, 17.1)
text("J3 = PID sensor", 60.0, 1.8)
text("tap: all DNP", 60.0, 3.2)
text("(b) OPEN until bench", 77.0, 93.0)
text("SELFTEST: never fit", 26.0, 93.0)
text("U3 + R36 OUT while gate PROPOSED", 57.0, 97.9)
text("espressolog v2", 85.0, 97.9)     # v1 was the perfboard
# rotated 90 it reads bottom to top: USB end at the bottom, antenna at pin 1
text("USB end    ESP32 devkit    ANTENNA end", 2.0, 58.5, angle=90)

# Each IN/OUT pair named on the board, on the LEFT of the pair, once beside
# the IN header and once beside the OUT header, so that a plug or OUT cable
# moved later goes back where it belongs (pre-order review S10; fix-pass
# check NIT 11). The paper test of 2026-11-25 showed the harness plugs do
# not seat in a larger header; the OUT cables are all-new and unkeyed by size
# only.
for name, x in [("DISPLAY", 7.2), ("FLOW", 22.75), ("NTC", 37.7),
                ("LEVERS", 68.7)]:
    text(name, x, 11.8, 1.2, angle=90)
    text(name, x, 19.2, 1.0, angle=90)

# J3 tap: the build rule on the board itself (final review S4); doubles as
# R61's and R60's reference, left and right of it
text("fit ONE", 42.25, 25.75)
text("R60", 51.8, 23.9)                 # R60's own reference has no room
# the other tap parts the placer found no room for
text("R64", 51.7, 21.95)
text("R68", 49.0, 4.6)
text("R63", 55.0, 7.2, angle=90)
text("R66", 66.7, 13.9, angle=90)
text("R67", 66.7, 19.2, angle=90)
text("R27", 61.6, 26.7)                 # placed by hand: the placer lost it
# devkit socket positions (the metering names A4..A10), and 5Vin
for k in (1, 5, 10, 15, 20):
    y = 32.0 + (k - 1) * 2.54
    text(str(k), 8.6, y)
    text(str(k), 28.9, y)
text("5V", 8.9, 82.8)
# test loops: what they are
text("GND", 60.0, 95.6)
text("3V3", 66.0, 95.6)
# C1/C2 sit on the display bus: never 10 nF there (CLAUDE.md, 0e)
text("<=470p", 24.0, 68.25)
# the AQY212EH goes at the pin-1 end of each uncut DIP-8
for y in (37.0, 50.0, 63.0, 76.0):
    text("AQY", 87.3, y + 1.27, 1.0)

# Pin names at the top edge, in front of each IN header. The OUT header
# right behind it carries the same pin in the same column (straight
# through), so these name both. Not collision-checked against each other:
# the 2.5 mm pitch leaves them closer than the label margin.
PIN_NAMES = [(18.5, "P4"), (16.0, "CK"), (13.5, "DA"), (11.0, "0V"),
             (29.0, "+"), (31.5, "#"), (26.5, "T"),
             (44.0, "1"), (41.5, "2"),
             (82.5, "+5"), (80.0, "ST"), (77.5, "2C"), (75.0, "1C"),
             (72.5, "W")]
for x, name in PIN_NAMES:
    t = pcbnew.PCB_TEXT(board)
    t.SetText(name)
    t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(F(1.0), F(1.0)))
    t.SetTextThickness(F(0.15))
    t.SetPosition(P(x, 1.55))
    board.Add(t)
    obstacles.append(inflate(t.GetBoundingBox(), 0.1))

# devkit outline on the SILKSCREEN (review finding 8): which way round the
# devkit goes, and that the sense shunts sit under it
# (three sides: the module end at y 27 would run over the sense legs' pads)
for (x1, y1), (x2, y2) in [((4.0, 28.0), (4.0, 90.5)), ((4.0, 90.5), (33.4, 90.5)),
                           ((33.4, 90.5), (33.4, 28.0))]:
    dev = pcbnew.PCB_SHAPE(board)
    dev.SetShape(pcbnew.SHAPE_T_SEGMENT)
    dev.SetLayer(pcbnew.F_SilkS)
    dev.SetStart(P(x1, y1))
    dev.SetEnd(P(x2, y2))
    dev.SetWidth(F(0.15))
    board.Add(dev)

# ---------------------------------------------- reference designators
SIZE = 1.0
moved, fab = 0, []
for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
    ref = fp.Reference()
    if fp.GetReference().startswith("H"):
        ref.SetVisible(False)
        continue
    ref.SetTextSize(pcbnew.VECTOR2I(F(SIZE), F(SIZE)))
    ref.SetTextThickness(F(0.15))
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

# ---------------------------------------------------------------- checks
# Not `assert`: python -O strips those. Any failure stops before the save.
def check(ok, msg):
    if not ok:
        sys.exit("finish.py CHECK FAILED: " + msg)

_pads = {(f.GetReference(), p.GetNumber()): p.GetNetname()
         for f in board.GetFootprints() for p in f.Pads()}
# 1. The board is the netlist: every pad of every part on the net
#    gen_schematic.py gives it, so a stale routed board cannot be finished
#    (fix-pass check, SHOULD 5).
_want = {(ref, num): (net or "") for ref, _s, _v, _fp, pins, _d in S.P
         for num, net in pins.items()}
_bad = sorted(k for k in _want if _pads.get(k, "?").lstrip("/") != _want[k])
check(not _bad, f"pad nets differ from gen_schematic.py: {_bad[:8]}")
# 2. The confirmed pinouts, on the board itself (pre-order review S9): J4
#    T (pad 3) on GND, # (pad 1) on its tap, + (pad 2) isolated; J5 pad 4
#    VSS, pad 1 Disp-P4 isolated; J2 pad 1 the +5 V common.
for _j in ("J4", "J24"):
    check((_pads[(_j, "1")], _pads[(_j, "2")], _pads[(_j, "3")])
          == ("FLOW_RAW", "FLOW_PLUS_RAW", "GND"), f"{_j}: wrong pinout")
for _j in ("J5", "J25"):
    check((_pads[(_j, "1")], _pads[(_j, "2")], _pads[(_j, "3")],
           _pads[(_j, "4")]) == ("DISP_P4_RAW", "ESD4_RAW", "ESD1_RAW", "GND"),
          f"{_j}: wrong pinout")
for _j in ("J2", "J22"):
    check([_pads[(_j, str(k))] for k in range(1, 6)]
          == ["POS1_5V", "STEAM_RAW", "2CUP_RAW", "1CUP_RAW", "WATER_RAW"],
          f"{_j}: wrong pinout")
for _net in ("FLOW_PLUS_RAW", "DISP_P4_RAW"):
    check(sum(1 for v in _pads.values() if v == _net) == 2, _net + ": pads")
    check(sum(1 for t in board.GetTracks() if t.GetNetname() == _net) == 1,
          _net + ": more than its one pass-through track")
# 3. The hand-laid copper is exactly gen_pcb.py's: every segment and via on
#    the board and locked, and no other track on a machine net.
_xy = {(f.GetReference(), p.GetNumber()): G.pad_xy(f, p.GetNumber())
       for f in board.GetFootprints() for p in f.Pads()}
def _key(net, lay, a, b):
    a, b = (round(a[0], 2), round(a[1], 2)), (round(b[0], 2), round(b[1], 2))
    return (net, lay, min(a, b), max(a, b))
_laid, _vias = set(), set()
for _net, _lay, _pts in G.MACHINE_TRACKS():
    _p = [_xy[q] if isinstance(q[0], str) else q for q in _pts]
    if _lay == G.V_:
        _vias |= {(_net, round(x, 2), round(y, 2)) for x, y in _p}
        continue
    _laid |= {_key(_net, _lay, a, b) for a, b in zip(_p, _p[1:])}
_on, _onv = set(), set()
for _t in board.GetTracks():
    _s, _e = _t.GetStart(), _t.GetEnd()
    _a = (pcbnew.ToMM(_s.x) - OX, pcbnew.ToMM(_s.y) - OY)
    _b = (pcbnew.ToMM(_e.x) - OX, pcbnew.ToMM(_e.y) - OY)
    _n = _t.GetNetname().lstrip("/")
    if _t.Type() == pcbnew.PCB_VIA_T:
        if _t.IsLocked():
            _onv.add((_n, round(_a[0], 2), round(_a[1], 2)))
        check(_n not in G.MACHINE_NETS, f"a via on machine net {_n}")
        continue
    _k = _key(_n, "F" if _t.GetLayer() == pcbnew.F_Cu else "B", _a, _b)
    if _t.IsLocked():
        _on.add(_k)
    check(_n not in G.MACHINE_NETS or (_t.IsLocked() and _k in _laid),
          f"a track on machine net {_n} that gen_pcb.py did not lay: {_k}")
check(_laid <= _on, f"hand-laid segments missing or unlocked: {sorted(_laid - _on)[:6]}")
check(_on <= _laid, f"locked segments gen_pcb.py does not lay: {sorted(_on - _laid)[:6]}")
check(_vias == _onv, f"locked vias differ: {sorted(_vias ^ _onv)}")
# 3b. Nothing of ours crosses a machine track on the OTHER layer (no DRC
#     rule sees opposite layers; route.py keeps the router out, this proves
#     it). The hand-laid ground anchors count as ours.
def _crosses(a, b):
    def o(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    return (o(a[0], a[1], b[0]) * o(a[0], a[1], b[1]) < 0
            and o(b[0], b[1], a[0]) * o(b[0], b[1], a[1]) < 0)
_segs = []
for _t in board.GetTracks():
    if _t.Type() == pcbnew.PCB_VIA_T:
        continue
    _s, _e = _t.GetStart(), _t.GetEnd()
    _segs.append((_t.GetNetname().lstrip("/"), _t.GetLayer(),
                  ((pcbnew.ToMM(_s.x) - OX, pcbnew.ToMM(_s.y) - OY),
                   (pcbnew.ToMM(_e.x) - OX, pcbnew.ToMM(_e.y) - OY)),
                  pcbnew.ToMM(_t.GetWidth())))
def _pt(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L))
    return ((p[0] - a[0] - t * dx) ** 2 + (p[1] - a[1] - t * dy) ** 2) ** 0.5
def _dist(a, b):
    if _crosses(a, b):
        return 0.0
    return min(_pt(a[0], *b), _pt(a[1], *b), _pt(b[0], *a), _pt(b[1], *a))
# projected edge-to-edge gap through the board: collinear, parallel and
# T-junction runs count, not only proper crossings
_under = sorted({(m[0], o[0], round(o[2][0][0], 1), round(o[2][0][1], 1))
                 for m in _segs if m[0] in G.MACHINE_NETS
                 for o in _segs if o[0] not in G.MACHINE_NETS
                 and o[1] != m[1]
                 and _dist(m[2], o[2]) - m[3] / 2 - o[3] / 2 < 1.0})
check(not _under, "our copper under, or within 1 mm through the board of, a "
      f"machine track (machine net, ours, start of our segment): {_under[:8]}")

# The routed board must be the placement gen_pcb.py last wrote (route.py
# saves the same placement to PCB): a stale build/routed.kicad_pcb after a
# gen_pcb.py edit would otherwise be finished over the new placement.
if os.path.exists(PCB):
    _now = {f.GetReference(): (f.GetPosition().x, f.GetPosition().y,
                               round(f.GetOrientationDegrees(), 1))
            for f in pcbnew.LoadBoard(PCB).GetFootprints()}
    _rt = {f.GetReference(): (f.GetPosition().x, f.GetPosition().y,
                              round(f.GetOrientationDegrees(), 1))
           for f in board.GetFootprints()}
    _moved = sorted(r for r in set(_now) | set(_rt) if _now.get(r) != _rt.get(r))
    check(not _moved, f"build/routed.kicad_pcb is not the placement in {os.path.basename(PCB)}: {_moved[:8]}. Run route.py.")

pcbnew.ZONE_FILLER(board).Fill(board.Zones())
# 4. Nothing left open (route.py exits 0 with connections unrouted).
board.BuildConnectivity()
_open = board.GetConnectivity().GetUnconnectedCount(False)
check(_open == 0, f"{_open} open connection(s): run `route.py --keep` again")
# 5. Every machine net is in netclass Machine and nothing else is: the
#    2 mm rules hang on the name patterns in the .kicad_pro, which nobody
#    regenerates (pre-push review SHOULD 10).
_ncs = {n.GetNetname().lstrip("/"): n.GetNetClassName()
        for n in board.GetNetsByNetcode().values() if n.GetNetname()}
_notm = sorted(n for n in G.MACHINE_NETS if "Machine" not in _ncs.get(n, ""))
_extra = sorted(n for n, c in _ncs.items()
                if "Machine" in c and n not in G.MACHINE_NETS)
check(not _notm and not _extra, "netclass Machine does not match MACHINE_NETS "
      f"(missing {_notm}, extra {_extra}): fix the patterns in the .kicad_pro")
# 6. DRC with the project's rules, on a copy, BEFORE the board is saved: an
#    error-level violation (the clearance rules) stops here.
_tmp = os.path.join(HERE, "build", "finished.kicad_pcb")
board.Save(_tmp)
for _ext in (".kicad_pro", ".kicad_dru"):
    shutil.copy(PCB.replace(".kicad_pcb", _ext), _tmp.replace(".kicad_pcb", _ext))
_rpt = os.path.join(HERE, "build", "drc.rpt")
if os.path.exists(_rpt):
    os.remove(_rpt)
_drc = subprocess.run([S.KICAD_CLI, "pcb", "drc", "--severity-error",
                       "--all-track-errors", "--exit-code-violations",
                       "-o", _rpt, _tmp], capture_output=True, text=True)
check(_drc.returncode == 0 and os.path.exists(_rpt),
      f"DRC errors (or kicad-cli failed): see build/drc.rpt\n{_drc.stdout[-400:]}")
board.Save(PCB)
print("checks passed: netlist, pinouts, locked copper, nothing under machine "
      "tracks, placement, connections, netclass, DRC")
print(f"refs placed on silk: {moved}; fab only (no room): {', '.join(fab) or 'none'}")
