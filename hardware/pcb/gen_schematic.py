#!/usr/bin/env python3
"""Generate the espressolog full-board schematic (sense + drive) for KiCad.

The schematic is LABEL-BASED: every pin gets a short stub and a net label, so
the connectivity is the netlist below and nothing else. That makes this file,
not the drawing, the thing to review against the docs:

  - sense side:  docs/board-and-perfboard.md, "Sense-side layout"
  - drive side:  docs/board-and-perfboard.md, "Drive-side layout" (netlist)
  - pins:        CLAUDE.md GPIO tables / firmware/src/main.cpp

Run:  python3 gen_schematic.py          (writes the .kicad_sch and the lib)
      python3 gen_schematic.py --check  (also ERC + netlist round-trip)
"""
import json, os, re, subprocess, sys, uuid
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = "espressolog-board"
LIB = "espressolog"
KICAD_CLI = os.environ.get(
    "KICAD_CLI", "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli")

# ---------------------------------------------------------------- footprints
FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"
# drive-side resistors (R20 and up) stand upright, as on the perfboard; the
# sense ones lie flat, because the shunts sit under the devkit (10 mm clearance)
FP_R_UP = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical"
# J2's four 150 K stand in a row of columns in the machine corner: 7.62 mm
# pitch keeps each raw pad well clear of the resistor's own sense pad
FP_R_J2 = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"
FP_TP = "TestPoint:TestPoint_Loop_D2.50mm_Drill1.0mm"
# PITCH NOT MEASURED: the bought MKT 1 uF and the X7R 100 nF / NP0 470 pF.
FP_C_FILM = "Capacitor_THT:C_Rect_L7.2mm_W5.5mm_P5.00mm_FKS2_FKP2_MKS2_MKP2"
FP_C_DISC = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
FP_DIP16 = "Package_DIP:DIP-16_W7.62mm_Socket"
FP_DIP8 = "Package_DIP:DIP-8_W7.62mm_Socket"   # AQY212EH in an uncut DIL08PZ
FP_TO92 = "Package_TO_SOT_THT:TO-92_Inline_Wide"  # 2.54 mm legs; pad 1 = C (BC547 C-B-E)
FP_SOCK22 = "Connector_PinSocket_2.54mm:PinSocket_1x22_P2.54mm_Vertical"
FP_HDR2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
FP_XH = "Connector_JST:JST_XH_S{n}B-XH-A_1x0{n}_P2.50mm_Horizontal"
# OUT headers: vertical (top-entry), right behind each side-entry IN header
FP_XH_V = "Connector_JST:JST_XH_B{n}B-XH-A_1x0{n}_P2.50mm_Vertical"
# the J3 NTC tap's raw-side 4.7 M: P5.08 so its raw pad keeps 2 mm from its own far pad
FP_R_TAP = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P5.08mm_Vertical"
FP_DIP8_IC = "Package_DIP:DIP-8_W7.62mm"

# ------------------------------------------------------------------- symbols
# Every pin is passive: ERC then checks only that nothing is left dangling.
# A symbol is (name, ref_prefix, body_rect, [(number, name, x, y, angle)]).
# Angle is the direction the pin line runs FROM its connection point.
def two_pin(name, prefix):
    return (name, prefix, (-1.016, 2.54, 1.016, -2.54),
            [("1", "~", 0, 3.81, 270), ("2", "~", 0, -3.81, 90)])

def conn(name, n, names):
    pins = [(str(k), names[k - 1], -5.08, -(k - 1) * 2.54, 0)
            for k in range(1, n + 1)]
    return (name, "J", (-2.54, 1.27, 7.62, -(n - 1) * 2.54 - 1.27), pins)

HC221_NAMES = {1: "~{1A}", 2: "1B", 3: "~{1R}", 4: "~{1Q}", 5: "2Q",
               6: "2Cx", 7: "2RxCx", 8: "GND", 9: "~{2A}", 10: "2B",
               11: "~{2R}", 12: "~{2Q}", 13: "1Q", 14: "1Cx", 15: "1RxCx",
               16: "VCC"}
def hc221():
    pins = []
    for i in range(8):                       # 1..8 down the left
        pins.append((str(i + 1), HC221_NAMES[i + 1], -10.16, 8.89 - i * 2.54, 0))
    for i in range(8):                       # 16..9 down the right
        n = 16 - i
        pins.append((str(n), HC221_NAMES[n], 10.16, 8.89 - i * 2.54, 180))
    return ("74HC221", "U", (-7.62, 10.16, 7.62, -10.16), pins)

# AQY212EH in a DIP-8 footprint, chip at the pin-1 end of an uncut socket:
# AQY 1 (LED anode) = pad 1, AQY 2 (cathode) = pad 2, AQY 3 = pad 7,
# AQY 4 = pad 8. Pads 3-6 are the socket's empty half and connect to nothing.
AQY = ("AQY212EH_DIP8", "U", (-5.08, 2.54, 5.08, -2.54),
       [("1", "LED+", -7.62, 1.27, 0), ("2", "LED-", -7.62, -1.27, 0),
        ("8", "OUT(4)", 7.62, 1.27, 180), ("7", "OUT(3)", 7.62, -1.27, 180)])
BC547 = ("BC547", "Q", (-1.27, 2.54, 2.54, -2.54),
         [("1", "C", 2.54, 5.08, 270), ("2", "B", -5.08, 0, 0),
          ("3", "E", 2.54, -5.08, 90)])

DEVKIT_A = ["3V3", "3V3", "RST", "IO4", "IO5", "IO6", "IO7", "IO15", "IO16",
            "IO17", "IO18", "IO8", "IO3", "IO46", "IO9", "IO10", "IO11",
            "IO12", "IO13", "IO14", "5Vin", "GND"]
DEVKIT_K = ["GND", "IO43/TX", "IO44/RX", "IO1", "IO2", "IO42", "IO41", "IO40",
            "IO39", "IO38", "IO37", "IO36", "IO35", "IO0", "IO45", "IO48",
            "IO47", "IO21", "IO20", "IO19", "GND", "GND"]

SYMBOLS = {s[0]: s for s in [
    two_pin("R", "R"), two_pin("C", "C"), hc221(), AQY, BC547,
    conn("DevKit_SignalColumn", 22, DEVKIT_A),
    conn("DevKit_OppositeColumn", 22, DEVKIT_K),
    conn("XH_J5", 4, ["Disp-P4", "ESD4", "ESD1", "VSS"]),
    conn("XH_J4", 3, ["outer 1", "+ (middle)", "outer 3"]),
    conn("XH_J3", 2, ["NTC P1", "NTC P2"]),
    conn("XH_J2", 5, ["pos1 +5V", "Steam", "2Cup", "1Cup", "Water"]),
    ("MCP6002", "U", (-5.08, 5.08, 5.08, -5.08),
     [("1", "OUTA", -7.62, 3.81, 0), ("2", "-INA", -7.62, 1.27, 0),
      ("3", "+INA", -7.62, -1.27, 0), ("4", "VSS", -7.62, -3.81, 0),
      ("8", "VDD", 7.62, 3.81, 180), ("7", "OUTB", 7.62, 1.27, 180),
      ("6", "-INB", 7.62, -1.27, 180), ("5", "+INB", 7.62, -3.81, 180)]),
    conn("Jumper2", 2, ["1", "2"]),
    ("TestPoint", "TP", (-1.27, 1.27, 1.27, -1.27), [("1", "~", 0, -3.81, 90)]),
]}

# -------------------------------------------------------------------- parts
# (ref, symbol, value, footprint, {pin: net or None for no-connect}, dnp)
P = []
def part(ref, sym, value, fp, pins, dnp=False):
    P.append((ref, sym, value, fp, pins, dnp))

def R(ref, value, a, b, dnp=False):
    n = int(ref[1:])
    fp = (FP_R_TAP if n in (60, 61) else FP_R_UP if n >= 20
          else FP_R_J2 if 4 <= n <= 7 else FP_R)
    part(ref, "R", value, fp, {"1": a, "2": b}, dnp)

def C(ref, value, a, b, fp, dnp=False):
    part(ref, "C", value, fp, {"1": a, "2": b}, dnp)

# --- harness headers: the T-pieces live on the board (decided 2026-11-24).
# Each machine connector gets an IN header (side-entry, top edge) that the
# ORIGINAL harness plug mates, and an OUT header (vertical, right behind it)
# with a ready-made XH cable to the machine board. IN pin k -> OUT pin k,
# straight through, for EVERY line; the taps branch off the pass-through.
# A plug mates a header the same way whatever its style, so with a
# same-direction cable the machine sees exactly its own harness.
#
# Pin k is the JST pin number (pad 1 = the RIGHT pin, opening at the edge):
#   J5  1 Disp-P4 (the machine's square pad) · 2 ESD4 · 3 ESD1 · 4 VSS
#       -- docs/machine-display-bus.md: VSS is the spoked-pad end
#   J4  2 = + (the MIDDLE conductor, GREEN: Lucas, 2026-11-24, on the
#       T-piece's machine-side plug; docs/phase-0-status.md's "T (middle)"
#       was wrong, and ee2795e tied + to our GND because of it).
#       T (yellow) and # (black) are the outer pins: # on pad 1, T on pad 3
#       (J4_T_PIN below).
#   J3  1 NTC P1 · 2 NTC P2   -- 109 k between them (2026-11-24); which is
#       the divider node is unmeasured; the tap is DNP
#   J2  1 pos 1 · 2 Steam · 3 2Cup · 4 1Cup · 5 Water   -- positional
# J4 + and J5 Disp-P4 cross the board as isolated pass-throughs: nothing else
# connects to them, ever (the J4 + rule in CLAUDE.md).
# Which outer J4 pin is T (yellow): pad 3, the board-left one. Read off the
# plug: 2026-11-24, Lucas, looking at the WIRE-ENTRY end of the T-piece's
# machine-side plug, latches up: green middle, yellow on his RIGHT, black on
# his LEFT. Seen from above the board (opening away) that is yellow LEFT ->
# pad 3 = T, pad 1 = #. The plug's latch faces up on a side-entry header
# (confirmed the same day). J5 (Disp-P4 on pad 1) and J2 (+5 V common on
# pad 1) were read off their plugs the same day and match the map above.
# HARD-CODED, no environment override (pre-order review S9): the other
# value puts # on our ground, and --check would still pass.
J4_T_PIN = "3"
J4_FLOW_PIN = "1"
PIN = {
    "J5": {"1": "DISP_P4_RAW", "2": "ESD4_RAW", "3": "ESD1_RAW", "4": "GND"},
    "J4": {J4_T_PIN: "GND", "2": "FLOW_PLUS_RAW", J4_FLOW_PIN: "FLOW_RAW"},
    "J3": {"1": "NTC_P1_RAW", "2": "NTC_P2_RAW"},
    "J2": {"1": "POS1_5V", "2": "STEAM_RAW", "3": "2CUP_RAW",
           "4": "1CUP_RAW", "5": "WATER_RAW"},
}
OUT_REF = {"J5": "J25", "J4": "J24", "J3": "J23", "J2": "J22"}
for ref, sym, n, what in [("J5", "XH_J5", 4, "J5 display"),
                          ("J4", "XH_J4", 3, "J4 flow"),
                          ("J3", "XH_J3", 2, "J3 NTC (the PID's sensor)"),
                          ("J2", "XH_J2", 5, "J2 switches (no ground)")]:
    part(ref, sym, f"{what} IN - original harness plug",
         FP_XH.format(n=n), PIN[ref])
    part(OUT_REF[ref], sym, f"{what} OUT - cable to the machine board",
         FP_XH_V.format(n=n), PIN[ref])

# --- devkit, two 1x22 female headers 10 pitches (25.4 mm) apart
A = {1: "+3V3", 2: "+3V3", 4: "ESD4", 5: "ESD1", 6: "FLOW", 7: "1CUP",
     8: "2CUP", 9: "STEAM", 10: "WATER", 12: "SELFTEST_IO8", 15: "RB_GATE",
     16: "GATE_TRIG", 17: "RB_1CUP_1"}   # 21 = 5Vin: nothing; 22 = GND left unused, no joint beside 5Vin
K = {1: "GND", 4: "NTC_ADC", 5: "RB_1CUP_2", 6: "TRIG_1CUP_1",
     7: "TRIG_1CUP_2", 8: "TRIG_2CUP_1", 9: "TRIG_2CUP_2", 10: "GATE_EN",
     17: "RB_2CUP_1", 18: "RB_2CUP_2", 21: "GND", 22: "GND"}
part("J11", "DevKit_SignalColumn", "ESP32-S3-DevKitC-1 signal column (A)",
     FP_SOCK22, {str(k): A.get(k) for k in range(1, 23)})
part("J12", "DevKit_OppositeColumn", "ESP32-S3-DevKitC-1 opposite column (K)",
     FP_SOCK22, {str(k): K.get(k) for k in range(1, 23)})

# --- sense: series legs (on the board since 2026-11-03/05) and shunts
R("R1", "8.2k", "ESD1_RAW", "ESD1")
R("R2", "8.2k", "ESD4_RAW", "ESD4")
R("R3", "100k", "FLOW_RAW", "FLOW")
R("R4", "150k", "WATER_RAW", "WATER")
R("R5", "150k", "1CUP_RAW", "1CUP")
R("R6", "150k", "2CUP_RAW", "2CUP")
R("R7", "150k", "STEAM_RAW", "STEAM")
R("R8", "8.2k", "ESD4", "ESD4_SHM")      # 16.4 K shunt = two 8.2 K
R("R9", "8.2k", "ESD4_SHM", "GND")
R("R10", "8.2k", "ESD1", "ESD1_SHM")
R("R11", "8.2k", "ESD1_SHM", "GND")
R("R12", "220k", "FLOW", "GND")
R("R13", "220k", "1CUP", "GND")
R("R14", "220k", "2CUP", "GND")
R("R15", "220k", "STEAM", "GND")
R("R16", "220k", "WATER", "GND")
# Candidate shunt caps, footprints only (docs/phase-0-plan.md, "Noise").
# NEVER 10 nF on ESD1/ESD4.
C("C1", "470p NP0 - DNP, candidate", "ESD4", "GND", FP_C_DISC, dnp=True)
C("C2", "470p NP0 - DNP, candidate", "ESD1", "GND", FP_C_DISC, dnp=True)
C("C3", "10n - DNP, candidate", "FLOW", "GND", FP_C_DISC, dnp=True)
part("JP4", "Jumper2", "selftest GPIO8-GPIO6 - NEVER fitted in service",
     FP_HDR2, {"1": "SELFTEST_IO8", "2": "FLOW"})

# --- drive: the three '221s
# CROSSED (pre-order review S1, 2026-11-24): each '221 package drives ONE
# photoMOS of EACH chain -- U1 the two first devices, U2 the two second ones.
# A package-level fault (an unseated GND pin, latch-up) then closes at most
# one device per chain, which is what the series pair is for. Net names and
# GPIOs are unchanged, so DRIVE_CH[] in main.cpp is untouched; only which
# chip a TRIG / Q net lands on moved (the perfboard doc has U1 = 1Cup).
def drive221(ref, ch1, ch2):
    part(ref, "74HC221", "CD74HC221E", FP_DIP16, {
        "1": f"TRIG_{ch1}", "2": "+3V3", "3": "ARM", "4": None,
        "5": f"Q_{ch2}", "6": f"{ref}_CX2", "7": f"{ref}_RC2", "8": "GND",
        "9": f"TRIG_{ch2}", "10": "+3V3", "11": "ARM", "12": None,
        "13": f"Q_{ch1}", "14": f"{ref}_CX1", "15": f"{ref}_RC1",
        "16": "+3V3"})
drive221("U1", "1CUP_1", "2CUP_1")     # photoMOS 1 of each chain (U4, U6)
drive221("U2", "1CUP_2", "2CUP_2")     # photoMOS 2 of each chain (U5, U7)
part("U3", "74HC221", "CD74HC221E - PROPOSED gate: socket fitted, chip OUT",
     FP_DIP16, {"1": "GATE_TRIG", "2": "+3V3", "3": "GATE_EN", "4": None,
                "5": None, "6": None, "7": None, "8": "GND", "9": "GND",
                "10": "GND", "11": "GND", "12": None, "13": "ARM",
                "14": "U3_CX1", "15": "U3_RC1", "16": "+3V3"})

# pulls: A-bar idles HIGH through EXTERNAL pull-ups, never firmware ones
R("R20", "10k", "TRIG_1CUP_1", "+3V3")
R("R21", "10k", "TRIG_1CUP_2", "+3V3")
R("R22", "10k", "TRIG_2CUP_1", "+3V3")
R("R23", "10k", "TRIG_2CUP_2", "+3V3")
R("R24", "10k", "GATE_TRIG", "+3V3")
R("R25", "10k", "ARM", "GND")            # tied R-bar net pull-down
R("R26", "10k", "GATE_EN", "GND")
# timing: soldered, never socketed
for ref_r, ref_c, chip, ch, val in [("R27", "C10", "U1", 1, "330k"),
                                     ("R28", "C11", "U1", 2, "330k"),
                                     ("R29", "C12", "U2", 1, "330k"),
                                     ("R30", "C13", "U2", 2, "330k"),
                                     ("R31", "C14", "U3", 1, "470k")]:
    R(ref_r, val, f"{chip}_RC{ch}", "+3V3")
    C(ref_c, "1u MKT", f"{chip}_CX{ch}", f"{chip}_RC{ch}", FP_C_FILM)
for ref_c in ["C15", "C16", "C17"]:      # one per '221, at the chip
    C(ref_c, "100n X7R", "+3V3", "GND", FP_C_DISC)
# Q readbacks - value OPEN, 1k proposed
R("R32", "1k (value TBD)", "Q_1CUP_1", "RB_1CUP_1")
R("R33", "1k (value TBD)", "Q_1CUP_2", "RB_1CUP_2")
R("R34", "1k (value TBD)", "Q_2CUP_1", "RB_2CUP_1")
R("R35", "1k (value TBD)", "Q_2CUP_2", "RB_2CUP_2")
R("R36", "1k (value TBD) - fit ONLY with U3", "ARM", "RB_GATE", dnp=True)

# --- LED-driver cells and photoMOS, n = 1..4
CELLS = [("Q_1CUP_1", "U4", "A1_LEG", "MID_1CUP"),
         ("Q_1CUP_2", "U5", "MID_1CUP", "1CUP_RAW"),
         ("Q_2CUP_1", "U6", "A2_LEG", "MID_2CUP"),
         ("Q_2CUP_2", "U7", "MID_2CUP", "2CUP_RAW")]
for n, (q, u, out4, out3) in enumerate(CELLS, start=1):
    R(f"R{39 + n}", "330 1%", "LED_RAIL", f"LED_A{n}")
    part(u, "AQY212EH_DIP8", "AQY212EH", FP_DIP8,
         {"1": f"LED_A{n}", "2": f"LED_K{n}", "8": out4, "7": out3})
    part(f"Q{n}", "BC547", "BC547B", FP_TO92,
         {"1": f"LED_K{n}", "2": f"BASE{n}", "3": "GND"})
    R(f"R{43 + n}", "100k", f"BASE{n}", "GND")
    R(f"R{47 + n}", "8.2k", q, f"BASE{n}")

# --- the three jumpers that keep Phase 0 from actuating: all OPEN
part("JP1", "Jumper2", "(a1) 1Cup drive leg - OPEN in Phase 0", FP_HDR2,
     {"1": "POS1_5V", "2": "A1_LEG"})
part("JP2", "Jumper2", "(a2) 2Cup drive leg - OPEN in Phase 0", FP_HDR2,
     {"1": "POS1_5V", "2": "A2_LEG"})
part("JP3", "Jumper2", "(b) LED supply - OPEN until bench test", FP_HDR2,
     {"1": "+3V3", "2": "LED_RAIL"})

# --- bench test loops, for the analyser and scope ground and supply
part("TP1", "TestPoint", "GND loop", FP_TP, {"1": "GND"})

# --- J3 NTC tap. ALL DNP until J3 is measured and the tap has its own design
# pass: the machine's PID regulates on this NTC, so any load shifts the brew
# temperature, and a fault that reads cold keeps the heater on.
# Measured 2026-11-24 (operator): J3 is a 2-pin XH, the sensor reads 109 k
# between its two wires at room temperature, so a 100 k NTC. Which pin is the
# divider node, and its voltage, are NOT measured yet.
# Two 4.7 M in series, so no single bridged or failed part leaves less than
# 4.7 M on the NTC line: fit ONE of R60/R61 (the pin measured to be the
# divider node; both fitted put 9.4 M across the NTC), plus R64.
# MCP6002 (CMOS, ~1 pA bias): half A is a unity follower. Half B follows A
# through the Rf/Rg footprints -- a wire link in R66 and R67 left out makes
# it a second follower (the parked default); R66/R67 fitted give gain
# 1 + R66/R67 for a node that sits low when hot. Fit ONE output resistor:
# R63 (straight) or R68 (after the gain stage). RC into GPIO1 (ADC1).
R("R60", "4.7M - DNP, fit ONE", "NTC_P1_RAW", "NTC_MID", dnp=True)
R("R61", "4.7M - DNP, fit ONE", "NTC_P2_RAW", "NTC_MID", dnp=True)
R("R64", "4.7M - DNP", "NTC_MID", "NTC_TAP", dnp=True)
R("R62", "shunt - DNP, only if 5 V referenced", "NTC_TAP", "GND", dnp=True)
C("C20", "10n - DNP", "NTC_TAP", "GND", FP_C_DISC, dnp=True)
part("U8", "MCP6002", "MCP6002 - DNP", FP_DIP8_IC,
     {"1": "NTC_OUT", "2": "NTC_OUT", "3": "NTC_TAP", "4": "GND",
      "5": "NTC_OUT", "6": "NTC_FB", "7": "NTC_GOUT", "8": "+3V3"}, dnp=True)
R("R63", "1k - DNP, fit ONE of R63/R68", "NTC_OUT", "NTC_ADC", dnp=True)
R("R66", "Rf - DNP: wire link = unity", "NTC_GOUT", "NTC_FB", dnp=True)
R("R67", "Rg - DNP, gain only", "NTC_FB", "GND", dnp=True)
R("R68", "1k - DNP, fit ONE of R63/R68", "NTC_GOUT", "NTC_ADC", dnp=True)
C("C21", "100n - DNP", "NTC_ADC", "GND", FP_C_DISC, dnp=True)
C("C22", "100n - DNP", "+3V3", "GND", FP_C_DISC, dnp=True)
part("TP2", "TestPoint", "3V3 loop", FP_TP, {"1": "+3V3"})

# ------------------------------------------------------------------ writer
_NS = uuid.UUID("5d2f1b9e-6c1a-4f0e-9a51-e5b0c0ffee01")
_n = [0]
def u(key=None):
    """Deterministic UUIDs, so regenerating gives a stable diff and the PCB
    can link footprints to symbols by uuid. Symbols and the root use a key."""
    if key is None:
        _n[0] += 1
        key = f"item-{_n[0]}"
    return str(uuid.uuid5(_NS, key))

def symbol_uuid(ref):
    return u(f"symbol:{ref}")

ROOT_UUID = u("root")

def fmt(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s

def font(hide=False, justify=None):
    j = f" (justify {justify})" if justify else ""
    h = " hide" if hide else ""
    return f"(effects (font (size 1.27 1.27)){j}{h})"

def lib_symbol(sym, prefixed):
    name, prefix, (x1, y1, x2, y2), pins = sym
    full = f"{LIB}:{name}" if prefixed else name
    out = [f'(symbol "{full}" (pin_names (offset 0.508)) (exclude_from_sim no)'
           f' (in_bom yes) (on_board yes)',
           f'(property "Reference" "{prefix}" (at 0 {fmt(y1 + 1.27)} 0) {font()})',
           f'(property "Value" "{name}" (at 0 {fmt(y2 - 1.27)} 0) {font()})',
           f'(property "Footprint" "" (at 0 0 0) {font(hide=True)})',
           f'(property "Datasheet" "" (at 0 0 0) {font(hide=True)})',
           f'(symbol "{name}_0_1" (rectangle (start {fmt(x1)} {fmt(y1)}) '
           f'(end {fmt(x2)} {fmt(y2)}) (stroke (width 0.254) (type default)) '
           f'(fill (type background))))',
           f'(symbol "{name}_1_1"']
    for num, pname, x, y, ang in pins:
        length = 1.27 if name in ("R", "C") else 2.54
        out.append(f'(pin passive line (at {fmt(x)} {fmt(y)} {ang}) '
                   f'(length {length}) (name "{pname}" {font()}) '
                   f'(number "{num}" {font()}))')
    out.append("))")
    return "\n".join(out)

DIRS = {0: (1, 0), 90: (0, -1), 180: (-1, 0), 270: (0, 1)}  # schematic, y down

def place(ref, sym, value, fp, pins, dnp, sx, sy, root, items):
    name, prefix, (x1, y1, x2, y2), spins = SYMBOLS[sym]
    if name in ("R", "C"):                       # text beside a 2-pin part
        rx, ry, vx, vy = sx + 2.54, sy - 1.27, sx + 2.54, sy + 1.27
    else:                                        # text above anything else
        rx, ry, vx, vy = sx + x1, sy - y1 - 5.08, sx + x1, sy - y1 - 2.54
    pin_uuids = " ".join(f'(pin "{p[0]}" (uuid "{u()}"))' for p in spins)
    items.append(
        f'(symbol (lib_id "{LIB}:{name}") (at {fmt(sx)} {fmt(sy)} 0) (unit 1)'
        f' (exclude_from_sim no) (in_bom yes) (on_board yes)'
        f' (dnp {"yes" if dnp else "no"}) (uuid "{symbol_uuid(ref)}")\n'
        f'(property "Reference" "{ref}" (at {fmt(rx)} {fmt(ry)} 0) {font(justify="left")})\n'
        f'(property "Value" "{value}" (at {fmt(vx)} {fmt(vy)} 0) {font(justify="left")})\n'
        f'(property "Footprint" "{fp}" (at {fmt(sx)} {fmt(sy)} 0) {font(hide=True)})\n'
        f'(property "Datasheet" "" (at {fmt(sx)} {fmt(sy)} 0) {font(hide=True)})\n'
        f'{pin_uuids}\n'
        f'(instances (project "{PROJECT}" (path "/{root}" (reference "{ref}") (unit 1)))))')
    for num, _pname, px, py, ang in spins:
        cx, cy = sx + px, sy - py                    # pin connection point
        net = pins.get(num)
        if net is None:
            items.append(f'(no_connect (at {fmt(cx)} {fmt(cy)}) (uuid "{u()}"))')
            continue
        out_ang = (ang + 180) % 360                  # away from the body
        dx, dy = DIRS[out_ang]
        ex, ey = cx + dx * 2.54, cy + dy * 2.54
        items.append(f'(wire (pts (xy {fmt(cx)} {fmt(cy)}) (xy {fmt(ex)} {fmt(ey)}))'
                     f' (stroke (width 0) (type default)) (uuid "{u()}"))')
        just = "right bottom" if out_ang in (180, 270) else "left bottom"
        items.append(f'(label "{net}" (at {fmt(ex)} {fmt(ey)} {out_ang}) '
                     f'{font(justify=just)} (uuid "{u()}"))')

# Layout on the sheet: blocks, in 1.27 mm steps. Purely for reading.
def layout():
    pos = {}
    g = 1.27
    def row(refs, x0, y0, dx):
        for i, r in enumerate(refs):
            pos[r] = (x0 + i * dx, y0)
    # connectors and devkit, left
    pos["J5"] = (30 * g, 30 * g); pos["J4"] = (30 * g, 45 * g)
    pos["J2"] = (30 * g, 60 * g)
    pos["J25"] = (55 * g, 30 * g); pos["J24"] = (55 * g, 45 * g)
    pos["J22"] = (55 * g, 60 * g)
    pos["J3"] = (30 * g, 250 * g); pos["J23"] = (55 * g, 250 * g)
    row(["R60", "R61", "R64", "R62", "C20", "R63", "C21", "C22"],
        80 * g, 300 * g, 14 * g)
    pos["U8"] = (215 * g, 300 * g)
    row(["R66", "R67", "R68"], 80 * g, 330 * g, 14 * g)
    pos["J11"] = (30 * g, 90 * g); pos["J12"] = (30 * g, 160 * g)
    # sense resistors, two rows
    row(["R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9"], 80 * g, 40 * g, 14 * g)
    row(["R10", "R11", "R12", "R13", "R14", "R15", "R16", "C1", "C2", "C3"],
        80 * g, 75 * g, 14 * g)
    pos["JP4"] = (80 * g, 105 * g)
    # '221s and their parts
    row(["U1", "U2", "U3"], 100 * g, 140 * g, 45 * g)
    row(["R20", "R21", "R22", "R23", "R24", "R25", "R26", "R32", "R33",
         "R34", "R35", "R36"], 80 * g, 185 * g, 14 * g)
    row(["R27", "C10", "R28", "C11", "R29", "C12", "R30", "C13", "R31",
         "C14", "C15", "C16", "C17"], 80 * g, 220 * g, 14 * g)
    # cells and photoMOS, right
    for n in range(1, 5):
        y = (40 + (n - 1) * 45) * g
        pos[f"R{39 + n}"] = (270 * g, y)
        pos[f"U{3 + n}"] = (300 * g, y)
        pos[f"Q{n}"] = (330 * g, y + 15 * g)
        pos[f"R{43 + n}"] = (360 * g, y)
        pos[f"R{47 + n}"] = (375 * g, y)
    row(["JP1", "JP2", "JP3"], 270 * g, 230 * g, 70 * g)
    row(["TP1", "TP2"], 80 * g, 250 * g, 14 * g)
    return pos

NOTES = [
    "espressolog full board - sense + drive. GENERATED by gen_schematic.py: edit the script, not this sheet.",
    "Label-based: connectivity is the labels. Normative: docs/board-and-perfboard.md (Sense-side / Drive-side layout).",
    "Invariant 1: JP1 (a1), JP2 (a2) in series with the photoMOS outputs and JP3 (b) LED supply stay OPEN in Phase 0.",
    "U3 (PROPOSED gate) and R36 (its readback) go in or come out TOGETHER. Phase 0: socket fitted, chip and R36 out.",
    "T-pieces on the board: IN (side-entry, original harness) -> OUT (vertical, cable to the machine), pin k to pin k for every line.",
    "J4 '+' and J5 Disp-P4 are isolated pass-throughs: nothing else connects to them. J2 carries no ground. Nothing on devkit 5Vin.",
    "J4: '+' is the MIDDLE pin (isolated pass-through); T and # are the outer pins, side set by J4_T_PIN. J3 tap: all DNP until J3 is measured.",
]

def write():
    _n[0] = 0
    root = ROOT_UUID
    pos = layout()
    items = []
    for ref, sym, value, fp, pins, dnp in P:
        sx, sy = pos[ref]
        place(ref, sym, value, fp, pins, dnp, sx, sy, root, items)
    for i, t in enumerate(NOTES):
        items.append(f'(text "{t}" (exclude_from_sim no) (at 12.7 {fmt(330 + i * 5.08)} 0) '
                     f'{font(justify="left bottom")} (uuid "{u()}"))')
    libsyms = "\n".join(lib_symbol(s, True) for s in SYMBOLS.values())
    sch = (f'(kicad_sch (version 20231120) (generator "gen_schematic.py") '
           f'(generator_version "1") (uuid "{root}") (paper "A2")\n'
           f'(title_block (title "espressolog board - sense + drive") '
           f'(comment 1 "generated - see hardware/pcb/gen_schematic.py"))\n'
           f'(lib_symbols\n{libsyms}\n)\n' + "\n".join(items) +
           '\n(sheet_instances (path "/" (page "1")))\n)\n')
    with open(os.path.join(HERE, f"{PROJECT}.kicad_sch"), "w") as f:
        f.write(sch)
    with open(os.path.join(HERE, f"{LIB}.kicad_sym"), "w") as f:
        f.write('(kicad_symbol_lib (version 20231120) (generator "gen_schematic.py")\n'
                + "\n".join(lib_symbol(s, False) for s in SYMBOLS.values()) + "\n)\n")
    with open(os.path.join(HERE, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n  (version 7)\n'
                f'  (lib (name "{LIB}")(type "KiCad")(uri "${{KIPRJMOD}}/{LIB}.kicad_sym")(options "")(descr ""))\n)\n')
    pro = os.path.join(HERE, f"{PROJECT}.kicad_pro")
    if not os.path.exists(pro):
        with open(pro, "w") as f:
            json.dump({"meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1}}, f)

# ------------------------------------------------------------------- checks
def intended():
    nets = defaultdict(set)
    for ref, _s, _v, _fp, pins, _d in P:
        for num, net in pins.items():
            if net is not None:
                nets[net].add((ref, num))
    return nets

def check():
    sch = os.path.join(HERE, f"{PROJECT}.kicad_sch")
    out = os.path.join(HERE, "build")
    os.makedirs(out, exist_ok=True)
    erc = os.path.join(out, "erc.rpt")
    subprocess.run([KICAD_CLI, "sch", "erc", "--severity-all", "-o", erc, sch],
                   check=False, capture_output=True)
    m = re.search(r"ERC messages: (\d+)\s+Errors (\d+)\s+Warnings (\d+)",
                  open(erc).read() if os.path.exists(erc) else "")
    print(f"ERC: {m.group(2)} errors, {m.group(3)} warnings" if m else "no ERC report")
    erc_ok = bool(m) and m.group(2) == "0"
    net = os.path.join(out, f"{PROJECT}.net")
    subprocess.run([KICAD_CLI, "sch", "export", "netlist", "-o", net, sch],
                   check=True, capture_output=True)
    text = re.sub(r"\s+", " ", open(net).read())
    text = text[text.index("(nets "):]
    got = defaultdict(set)
    for block in text.split("(net (code ")[1:]:
        name = re.search(r'\(name "([^"]*)"\)', block).group(1).lstrip("/")
        for r, p in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', block):
            got[name].add((r, p))
    want = intended()
    bad = 0
    for name, nodes in want.items():
        if got.get(name) != nodes:
            bad += 1
            print(f"MISMATCH {name}: want {sorted(nodes)} got {sorted(got.get(name, []))}")
    extra = [n for n in got if n not in want and not n.startswith("unconnected")]
    for n in extra:
        bad += 1
        print(f"EXTRA net {n}: {sorted(got[n])}")
    print(f"netlist round-trip: {len(want)} nets intended, {bad} problems")
    return bad == 0 and erc_ok

if __name__ == "__main__":
    write()
    print(f"{len(P)} parts written")
    if "--check" in sys.argv:
        sys.exit(0 if check() else 1)
