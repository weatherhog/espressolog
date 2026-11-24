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


def relay_machine(board, pads):
    for t in list(board.GetTracks()):
        if t.GetNetname() in G.MACHINE_NETS:
            board.Remove(t)
    nets = {n: board.FindNet(n) for n in G.MACHINE_NETS}
    G.lay_machine_tracks(board, lambda r, n: pads[(r, n)], nets)


def relay_only():
    """In a fresh process: KiCad's SWIG wrappers come back untyped after a
    Specctra import, so the re-lay runs separately."""
    board = pcbnew.LoadBoard(PCB)
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
    for t in list(board.GetTracks()):  # keep only the fixed machine tracks
        if not t.IsLocked():
            board.Remove(t)
    for z in list(board.Zones()):      # finish.py's pours and keep-outs
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

cmd = [JAVA, "-jar", JAR, "-de", dsn, "-do", ses, "-mp", "40"]
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
