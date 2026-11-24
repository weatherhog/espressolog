#!/usr/bin/env python3
"""Autoroute everything except the machine nets and GND. Run with KiCad's Python.

  gen_pcb.py (placement + fixed machine tracks) -> route.py -> finish.py

- The machine nets are laid by gen_pcb.py as locked tracks; Freerouting sees
  them as fixed obstacles and keeps the netclass clearance (2 mm) from them.
  After the session is imported they are re-laid from gen_pcb.py, so what is
  on the board is exactly what gen_pcb.py says, whatever the router did.
- GND is routed like any other net, and finish.py adds pours on both
  layers. Leaving GND to the pours alone stranded two pads (J12 pin 1 under
  the antenna keep-out, U1 pin 8 boxed in by tracks). The 2 mm from machine
  nets comes from espressolog-board.kicad_dru and the `Machine` netclass.

Needs Java 21+ and the Freerouting jar (FREEROUTING_JAR).
"""
import os, re, shutil, subprocess, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_pcb as G                # noqa: E402

PCB = os.path.join(HERE, "espressolog-board.kicad_pcb")
BUILD = os.path.join(HERE, "build")
JAR = os.environ.get("FREEROUTING_JAR", os.path.expanduser(
    "~/.local/share/freerouting/freerouting-2.5.0.jar"))
JAVA = os.environ.get("JAVA", "/opt/homebrew/opt/openjdk/bin/java")
UNROUTED_NETS = set()             # nets dropped from the DSN, if any


# While routing only: keep the router's tracks (ours) out of the pass-through
# header block, where J4 + and J5 Disp-P4 run. A rule area there would also
# forbid the fixed machine tracks in KiCad's DRC, so --relay removes it again.
# Both are done before any Remove() in their process (SWIG wrappers go
# untyped after one).
ROUTE_KEEPOUT = "route.py: our tracks out of the header block"
# OFF: with the J5/J4 block closed, 3V3 from J11 pin 1 had only a ~1.6 mm
# corridor and Freerouting stopped completing the board (tried 2026-11-24).
# The 2 mm netclass rule still applies there. Some routings run a +3V3 track
# on B.Cu between the J5 IN and OUT pins (2.03 mm at b3d9441's successor);
# check with the review's gap script after any re-route.
ROUTE_KEEPOUT_ON = False


def add_route_keepout(board):
    k = pcbnew.ZONE(board)
    k.SetIsRuleArea(True)
    k.SetZoneName(ROUTE_KEEPOUT)
    k.SetDoNotAllowTracks(True)
    k.SetDoNotAllowVias(True)
    k.SetDoNotAllowZoneFills(True)
    k.SetDoNotAllowPads(False)
    k.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    k.SetLayerSet(ls)
    o = k.Outline()
    o.NewOutline()
    for x, y in [(7.5, 0.5), (36.0, 0.5), (36.0, 21.8), (7.5, 21.8)]:
        o.Append(pcbnew.FromMM(G.OX + x), pcbnew.FromMM(G.OY + y))
    board.Add(k)


def relay_machine(board, pads):
    """Remove the fixed copper and lay it again from gen_pcb.py: every track
    of a machine net, and the GND pass-throughs (matched by their exact
    segments, since the router's own GND tracks stay)."""
    def key(x1, y1, x2, y2):
        a, b = (round(x1, 2), round(y1, 2)), (round(x2, 2), round(y2, 2))
        return (min(a, b), max(a, b))
    fixed_gnd, fixed_vias = set(), set()
    for net, layer, pts in G.MACHINE_TRACKS():
        if layer == G.V_:
            fixed_vias |= {(round(x, 2), round(y, 2)) for x, y in pts}
            continue
        if net in G.MACHINE_NETS:
            continue
        xy = [pads[p] if isinstance(p[0], str) else p for p in pts]
        for (x1, y1), (x2, y2) in zip(xy, xy[1:]):
            fixed_gnd.add(key(x1, y1, x2, y2))
    for t in list(board.GetTracks()):
        n = t.GetNetname()
        s, e = t.GetStart(), t.GetEnd()
        seg = key(pcbnew.ToMM(s.x) - G.OX, pcbnew.ToMM(s.y) - G.OY,
                  pcbnew.ToMM(e.x) - G.OX, pcbnew.ToMM(e.y) - G.OY)
        at = (round(pcbnew.ToMM(s.x) - G.OX, 2), round(pcbnew.ToMM(s.y) - G.OY, 2))
        if (n in G.MACHINE_NETS
                or (t.Type() == pcbnew.PCB_TRACE_T and seg in fixed_gnd)
                or (t.Type() == pcbnew.PCB_VIA_T and at in fixed_vias)):
            board.Remove(t)
    names = {net for net, _l, _p in G.MACHINE_TRACKS()}
    nets = {n: board.FindNet(n) for n in names}
    G.lay_machine_tracks(board, lambda r, n: pads[(r, n)], nets)


def relay_only():
    """In a fresh process: KiCad's SWIG wrappers come back untyped after a
    Specctra import, so the re-lay runs separately."""
    board = pcbnew.LoadBoard(PCB)
    for z in list(board.Zones()):       # route.py's own keep-out, see above
        if z.GetZoneName() == ROUTE_KEEPOUT:
            board.Remove(z)
    pads = {(f.GetReference(), p.GetNumber()): G.pad_xy(f, p.GetNumber())
            for f in board.GetFootprints() for p in f.Pads()}
    relay_machine(board, pads)          # exactly gen_pcb.py's machine copper
    board.Save(PCB)
    routed = os.path.join(BUILD, "routed.kicad_pcb")
    board.Save(routed)                  # finish.py starts from this copy
    for ext in (".kicad_pro", ".kicad_dru"):
        shutil.copy(PCB.replace(".kicad_pcb", ext), routed.replace(".kicad_pcb", ext))
    print("routed ->", os.path.basename(PCB))


if "--relay" in sys.argv:
    relay_only()
    sys.exit(0)

os.makedirs(BUILD, exist_ok=True)
UNROUTED = os.path.join(BUILD, "unrouted.kicad_pcb")
if "--strip" in sys.argv:
    # Its own process: after Remove() KiCad's SWIG wrappers can come back
    # untyped, which broke a second run on an already-routed board.
    board = pcbnew.LoadBoard(PCB)
    old_zones = list(board.Zones())    # finish.py's pours and keep-outs,
    if ROUTE_KEEPOUT_ON:               # taken before anything changes
        add_route_keepout(board)
    for t in list(board.GetTracks()):  # keep only the fixed machine tracks
        if not t.IsLocked():
            board.Remove(t)
    for z in old_zones:
        board.Remove(z)
    board.Save(UNROUTED)
    sys.exit(0)
if subprocess.run([sys.executable, __file__, "--strip"]).returncode:
    sys.exit("strip failed")
board = pcbnew.LoadBoard(UNROUTED)
FPS = {(f.GetReference(), p.GetNumber()): G.pad_xy(f, p.GetNumber())
       for f in board.GetFootprints() for p in f.Pads()}
relay_machine(board, FPS)


dsn, ses = os.path.join(BUILD, "board.dsn"), os.path.join(BUILD, "board.ses")
if not pcbnew.ExportSpecctraDSN(board, dsn):
    sys.exit("DSN export failed")
text = open(dsn).read()
for net in UNROUTED_NETS:              # drop the net, keep its pins as obstacles
    text = re.sub(r"\(net %s\s*\(pins[^)]*\)\s*\)" % re.escape(net), "", text)
    text = re.sub(r"(\(class [^\n]*?) %s( |\n|\))" % re.escape(net), r"\1\2", text)
open(dsn, "w").write(text)

cmd = [JAVA, "-jar", JAR, "-de", dsn, "-do", ses] + os.environ.get(
    "FREEROUTING_ARGS", "-mp 40").split()
print(" ".join(cmd))
r = subprocess.run(cmd, capture_output=True, text=True)
summary = [l for l in r.stdout.splitlines() + r.stderr.splitlines()
           if "Auto-routing stage completed" in l or "ERROR" in l]
print("\n".join(s[s.find("]") + 2:] for s in summary) or r.stdout[-2000:])
if not os.path.exists(ses):
    sys.exit("Freerouting produced no session file")
if not pcbnew.ImportSpecctraSES(board, ses):
    sys.exit("SES import failed")
board.Save(PCB)
sys.exit(subprocess.run([sys.executable, __file__, "--relay"]).returncode)
