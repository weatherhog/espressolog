#!/usr/bin/env python3
"""
Decode the Ascaso Dream PID display bus (milestone 0b, cracked 2026-09-28).

Usage:
    sigrok-cli -i capture.sr -O csv | tools/decode-display-bus.py SAMPLERATE

PASS THE SAMPLERATE. It defaults to 2e6, so a 1 MHz capture decodes with
every timestamp at half its true value -- the readings are right and the
times are silently wrong, which is worse than failing.

    ... | tools/decode-display-bus.py 1e6

To capture in the first place (SG-NANO-DLA-A / fx2lafw), with the T-piece
dividers on D0/D1 and Disp-P4 on D2:

    sigrok-cli -d fx2lafw --config samplerate=1m \
               --channels D0,D1,D2 --samples 5M -o capture.sr

Enable ONLY the channels in use. Unused inputs float and pick up 50 Hz
mains hum that looks exactly like signal -- during 0b that got misread as
miswiring. 1 MHz gives ~36 samples across a 36 us clock high, which is
ample; 5M samples is ~5 s, or ~120 frames at 41 ms.

Column 0 of the CSV is taken as the clock, and THE CLOCK IS ESD4 (J5 pin
3); ESD1 (pin 2) is the data. Settled 2026-10-03. Capture ESD4 on the lower
channel number and the CSV needs no rearranging; the 2026-10-03 files in
analysis/captures/ were taken the other way round and need their first two
columns swapped -- see that directory's README.

Reads a two-column CSV (clock, data) on stdin and prints what the machine's
front display was showing, with a timestamp for every change.

PROTOCOL (measured, not guessed -- see docs/phase-0-status.md)

  Probe point : J5 on the main control board, 4 pins.
                pad with thermal-relief spokes into the copper pour = GND
                (the wire colours are meaningless: the harness is
                red/black/red/black because it was cut from two reels)
  Electrical  : 5 V logic, both signal lines idled high by the board's
                4.7 K pull-ups, exactly as the traced schematic predicted.
  Signalling  : plain synchronous serial, NOT I2C. An i2c decoder will
                "successfully" decode it as endless writes to address 0x00,
                which is how you know it is wrong.
                  clock ~9.9 kHz (high 36 us, low 65.5 us)
                  data sampled on the RISING clock edge
                  data is stable across a whole clock period
  Framing     : frames repeat every ~41 ms, separated by >1 ms of clock
                idle. The bus actually sends a 67-bit frame (buttons)
                then a 66-bit frame (digits); a "133-bit frame" is the two
                merged because the gap between them usually falls under
                FRAME_GAP_S. See CLAUDE.md. This decoder only handles the
                merged form, so it drops standalone 66-bit frames and the
                digit reading they carry.
  Digits      : three 7-segment fields, MSB = segment a, order abcdefg.
                  bits[106:113]  hundreds / leading  (blank when idle)
                  bits[115:122]  tens
                  bits[124:131]  units

WHAT THE DISPLAY SHOWS

  Idle    : boiler temperature in C, e.g. " 95".
  Brewing : the machine takes the display over for a shot timer in tenths
            of a second, counting 001 -> 169 across 16.8915 s, i.e. 168 steps = 16.8 s of
  displayed time, +92 ms -- NOT "a 16.9 s pull", which compares against
  the final value instead of the interval. So the
            temperature is NOT readable during a shot -- but the timer is,
            and it gives true pump-on/pump-off boundaries for free. NOT a
            replacement for 0c though: 1Cup and 2Cup emit byte-identical
            67-bit payloads, so nothing here says which direction ran.
"""
import sys

# Letters the machine actually shows. The display is NOT digits-only: a
# short press of button A puts it into programming mode, which renders
# 'PrG' (measured 2026-10-03, analysis/captures/2026-10-03-*-buttons.sr).
# Reporting those as '?' makes a normal display state look like frame
# corruption, which is exactly how it was first misread.
SEG_ALPHA = {
    '1100111': 'P',   # a b e f g
    '0000101': 'r',   # e g
    '1011110': 'G',   # a c d e f
    '1001111': 'E',   # a d e f g
    '0001111': 't',   # d e f g
    '0111110': 'U',   # b c d e f
    '0111101': 'd',   # b c d e g
    '1001110': 'C',   # a d e f
    '1000111': 'F',   # a e f g
}

# TWO LETTERS ARE INDISTINGUISHABLE FROM DIGITS, and this is the display's
# limitation, not the decoder's:
#
#   S == 5   both a c d f g     so 'PrS' (factory reset) renders 'Pr5'
#   O == 0   both a b c d e f   so 'OFF' (stand-by param)  renders '0FF'
#
# Both confirmed on the machine 2026-10-03: the SET UP banner came through
# as '5Et' / 'UP ', and the stand-by parameter as '0FF'. SEG is consulted
# before SEG_ALPHA, so these resolve to the digit; only context can tell
# you otherwise.

SEG = {
    '1111110': '0', '0110000': '1', '1101101': '2', '1111001': '3',
    '0110011': '4', '1011011': '5', '1011111': '6', '1110000': '7',
    '1111111': '8', '1111011': '9', '0000000': ' ', '0000001': '-',
}
DIGIT_OFFSETS = (106, 115, 124)
FRAME_GAP_S = 0.001      # clock idle longer than this starts a new frame
MIN_FRAME_BITS = 120     # short (67-bit) frames carry no digits


def frames_from_csv(stream, samplerate):
    """Yield (timestamp_s, bitstring) for each frame, sampling data on rising clock."""
    prev = None
    t = 0
    edges = []
    for line in stream:
        if not line or line[0] not in '01':
            continue
        # .strip() is load-bearing: with a TWO-column CSV the data field is
        # the last one and carries the trailing newline, which poisons every
        # glyph lookup downstream. Three-column captures hid this for months
        # because the data column was in the middle. Found 2026-10-05.
        clk, dat = line.strip().split(',')[:2]
        if prev is not None and clk != prev and clk == '1':
            edges.append((t / samplerate, dat))
        prev = clk
        t += 1

    cur, last = [], None
    for ts, bit in edges:
        if last is not None and ts - last > FRAME_GAP_S and cur:
            yield cur[0][0], ''.join(b for _, b in cur)
            cur = []
        cur.append((ts, bit))
        last = ts
    if cur:
        yield cur[0][0], ''.join(b for _, b in cur)


def render(bits):
    """Three 7-segment fields -> the string on the display.

    An unknown glyph prints as <abcdefg> rather than '?', because the raw
    segment pattern is what lets you work out what it was. 'PrG' took one
    look at the bits; five identical '???' readings looked like noise.
    """
    out = []
    for o in DIGIT_OFFSETS:
        seg = bits[o:o + 7]
        g = SEG.get(seg) or SEG_ALPHA.get(seg)
        out.append(g if g else f'<{seg}>')
    return ''.join(out)


def main():
    samplerate = float(sys.argv[1]) if len(sys.argv) > 1 else 2e6
    previous = None
    for ts, bits in frames_from_csv(sys.stdin, samplerate):
        if len(bits) < MIN_FRAME_BITS:
            continue
        shown = render(bits)
        if shown != previous:
            print(f"{ts:9.3f}s  [{shown}]")
            previous = shown


if __name__ == '__main__':
    main()
