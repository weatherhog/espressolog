#!/usr/bin/env python3
"""Autoroute the placed board with Freerouting. Run with KiCad's Python.

  gen_pcb.py (placement) -> route.py -> finish.py (pours, labels, DRC)

Freerouting runs headless on a Specctra DSN export and its session (SES) is
imported back. Needs Java 21+ and the Freerouting jar (FREEROUTING_JAR).
"""
import os, subprocess, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, "espressolog-board.kicad_pcb")
BUILD = os.path.join(HERE, "build")
JAR = os.environ.get("FREEROUTING_JAR", os.path.expanduser(
    "~/.local/share/freerouting/freerouting-2.5.0.jar"))
JAVA = os.environ.get("JAVA", "/opt/homebrew/opt/openjdk/bin/java")

os.makedirs(BUILD, exist_ok=True)
board = pcbnew.LoadBoard(PCB)
for t in list(board.GetTracks()):      # start from a clean, unrouted board
    board.Remove(t)
for z in list(board.Zones()):          # finish.py's pours and keep-outs
    board.Remove(z)
dsn, ses = os.path.join(BUILD, "board.dsn"), os.path.join(BUILD, "board.ses")
if not pcbnew.ExportSpecctraDSN(board, dsn):
    sys.exit("DSN export failed")
cmd = [JAVA, "-jar", JAR, "-de", dsn, "-do", ses, "-mp", "40",
       "--gui.enabled=false"]
print(" ".join(cmd))
r = subprocess.run(cmd, capture_output=True, text=True)
print(r.stdout[-3000:], r.stderr[-3000:])
if not os.path.exists(ses):
    sys.exit("Freerouting produced no session file")
if not pcbnew.ImportSpecctraSES(board, ses):
    sys.exit("SES import failed")
board.Save(PCB)
# finish.py starts from this copy, so it never has to remove its own drawings
import shutil
ROUTED = os.path.join(BUILD, "routed.kicad_pcb")
board.Save(ROUTED)
shutil.copy(PCB.replace(".kicad_pcb", ".kicad_pro"), ROUTED.replace(".kicad_pcb", ".kicad_pro"))
print("routed ->", os.path.basename(PCB))
