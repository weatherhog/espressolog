#!/usr/bin/env python3
"""
Decode the Ascaso Dream PID display bus (milestone 0b, cracked 2026-09-28).

Usage:
    sigrok-cli -i capture.sr -O csv | tools/decode-display-bus.py [samplerate]

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
                idle. Two lengths occur: 133 bits and 67 bits. Only the
                133-bit frames carry the digits.
  Digits      : three 7-segment fields, MSB = segment a, order abcdefg.
                  bits[106:113]  hundreds / leading  (blank when idle)
                  bits[115:122]  tens
                  bits[124:131]  units

WHAT THE DISPLAY SHOWS

  Idle    : boiler temperature in C, e.g. " 95".
  Brewing : the machine takes the display over for a shot timer in tenths
            of a second, counting 001 -> 169 for a 16.9 s pull. So the
            temperature is NOT readable during a shot -- but the timer is,
            and it gives true pump-on/pump-off boundaries for free, which
            is what 0c switch sensing was going to be for.
"""
import sys

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
        clk, dat = line.split(',')[:2]
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
    """Three 7-segment fields -> the string on the display ('?' = unknown glyph)."""
    return ''.join(SEG.get(bits[o:o + 7], '?') for o in DIGIT_OFFSETS)


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
