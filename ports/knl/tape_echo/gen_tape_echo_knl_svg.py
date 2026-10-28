#!/usr/bin/env python3
"""The tape_echo kennel plugin's screen (the Claude Design canvas "Tape echo
screen", design B "Schematic", 2026-10-28; driven by tape_echo_knl.cc).

    python3 gen_tape_echo_knl_svg.py tape_echo.svg

The Space Echo's signal path, left to right on the tape row (y 56): the input
arrow, the record head, the tape (white, a wave the screen scales by age and
breathes with the wow), the three play heads (white, lit per pattern) tapping
down to a bus, the sum, the output arrow. The return path runs under it with
green chevrons for the feedback (a count set). A blue dot travels the tape at
the tape speed, and the heads spread away from the record head with the time
(the screen translates them). While shift is held on the time page, blue 16th
ticks over the tape from the record head show the project's beat; locked to a
division, its name reads out top left in blue. White pressed: a bracket round
the heads, the pattern read out top left in white. Hold (green): the input
dims, the return path and the hold glyph (top right) turn green. The blue
input-gain gauge at the far left (a bracket and a dB readout while blue is on
page 2), the orange mix gauge at the far right.

Rules enforced (docs/HANDOVER.md "SVG lessons", hundekraft's svg_check): ids
letters and digits only, at most 2 decimals, no arcs, no text, no paths,
polylines never turn back. Stdlib only.
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'common'))
from knl_glyphs import label, num   # noqa: E402

WHITE, DIM, BLUE, GREEN, ORANGE, BLACK, MID = (
    '#DED7FF', '#38354D', '#638BFF', '#00EE92', '#FF395A', '#010101', '#514959')

TOP, BOTTOM, CY = 28, 124, 76              # the gauges' rows
GAIN_X, MIX_X = 16, 304
GAIN_BAR = 24
READOUT_X, READOUT_Y = 4, 8
Y = 56                                     # the tape row
REC, SUM = 68, 258                         # the record head, the summing node
SPAN0 = 97                                 # head 3's distance from the record head as drawn (time 50 %)
WAVE_AMP, WAVE_STEP, WAVE_PERIOD = 4, 3, 18
BUS_Y, RET_Y = Y + 22, 104
CHEV_X, CHEV_Y = 190, RET_Y
TICKS = 24
HOLD_X, HOLD_Y = 290, 14
DIVISIONS = ('1/16T', '1/16', '1/8T', '1/16.', '1/8', '1/4T', '1/8.', '1/4', '1/2T', '1/4.', '1/2')   # shortest first (tape_echo_knl.cc's order)
PATTERNS = ('3', '2+3', '1+2+3', '1+3')                                       # kTapeEchoPatternHeads' order


def stroke(colour):
    return 'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"' % colour


def attrs(id_, hidden):
    return (' id="%s"' % id_ if id_ else '') + (' display="none"' if hidden else '')


def line(x1, y1, x2, y2, colour, id_=None, hidden=False):
    return '<line%s %s x1="%s" y1="%s" x2="%s" y2="%s"></line>' % (attrs(id_, hidden), stroke(colour), num(x1), num(y1), num(x2), num(y2))


def polyline(points, colour, id_=None, hidden=False):
    return '<polyline%s %s points="%s"></polyline>' % (attrs(id_, hidden), stroke(colour), ' '.join('%s,%s' % (num(x), num(y)) for x, y in points))


def polygon(points, colour, fill, id_=None):
    return '<polygon%s fill="%s" stroke="%s" stroke-width="1.5" stroke-linejoin="round" points="%s"></polygon>' % (
        attrs(id_, False), fill, colour, ' '.join('%s,%s' % (num(x), num(y)) for x, y in points))


def circle(cx, cy, r, colour, fill='none', id_=None):
    return '<circle%s fill="%s" stroke="%s" stroke-width="1.5" cx="%s" cy="%s" r="%s"></circle>' % (
        attrs(id_, False), fill, colour, num(cx), num(cy), num(r))


def rect(x, y, w, h, fill, stroke_colour='none', id_=None, hidden=False):
    return '<rect%s fill="%s" stroke="%s" stroke-width="1.5" x="%s" y="%s" width="%s" height="%s"></rect>' % (
        attrs(id_, hidden), fill, stroke_colour, num(x), num(y), num(w), num(h))


def group(id_, inner, hidden=False):
    return '<g id="%s"%s>%s</g>' % (id_, ' display="none"' if hidden else '', inner)


def gauge(x, bar):
    return line(x, TOP, x, BOTTOM, DIM) + line(x - 4, TOP, x + 4, TOP, DIM) + line(x - 4, BOTTOM, x + 4, BOTTOM, DIM) + bar


def head(x, y, colour, fill, id_=None):
    return polygon([(x - 5, y), (x + 5, y), (x + 3, y + 6), (x - 3, y + 6)], colour, fill, id_)


def lemniscate(cx, cy, a, steps=32):
    pts = []
    for i in range(steps + 1):
        t = 2 * math.pi * i / steps + 0.4
        d = 1 + math.sin(t) ** 2
        pts.append((cx + a * math.cos(t) / d, cy + a * 0.6 * math.sin(t) * math.cos(t) / d))
    return pts


def build():
    e = ['<?xml version="1.0"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">']
    e.append(group('palette', ''.join(line(0, 0, 1, 0, c, i) for i, c in (
        ('palwhite', WHITE), ('paldim', DIM), ('palblue', BLUE), ('palgreen', GREEN), ('palorange', ORANGE),
        ('palblack', BLACK), ('palmid', MID))), hidden=True))
    # Static: the bus, the sum, the output, the foot line, the tape's stub into the sum.
    s = [line(REC + 14, BUS_Y, SUM, BUS_Y, DIM), line(SUM, BUS_Y, SUM, Y + 6, DIM),
         circle(SUM, Y, 6, MID), line(SUM - 3, Y, SUM + 3, Y, MID), line(SUM, Y - 3, SUM, Y + 3, MID),
         line(SUM + 6, Y, 280, Y, MID), polyline([(276, Y - 3), (280, Y), (276, Y + 3)], MID),
         line(SUM - 14, Y, SUM - 6, Y, MID),
         line(44, BOTTOM, 280, BOTTOM, DIM)]
    e.append(group('static', ''.join(s)))
    # The input (dimmed while holding) and the record head.
    e.append(line(40, Y, REC - 8, Y, MID, 'inl'))
    e.append(polyline([(REC - 12, Y - 3), (REC - 8, Y), (REC - 12, Y + 3)], MID, 'ina'))
    e.append(head(REC, Y + 3, MID, BLACK, 'rech'))
    # The tape: a wave at full amplitude (the screen scales it by age about y 56).
    pts = []
    x = REC
    while x <= SUM - 14:
        pts.append((x, Y - WAVE_AMP * math.sin((x - REC) * 2 * math.pi / WAVE_PERIOD)))
        x += WAVE_STEP
    e.append(polyline(pts, WHITE, 'tape'))
    # The beat ticks over the tape (drawn at the record head; the screen places them).
    for k in range(1, TICKS + 1):
        e.append(line(REC, Y - 13, REC, Y - 7 if k % 4 == 0 else Y - 10, BLUE, 'bt%d' % k, hidden=True))
    # The play heads at time 50 %, each with its tap to the bus; the file ships in pattern 0 (head 3).
    for h in range(3):
        x = REC + SPAN0 * (h + 1) / 3
        on = h == 2
        e.append(group('hd%d' % (h + 1), head(x, Y + 3, WHITE if on else MID, WHITE if on else BLACK, 'hp%d' % (h + 1)) +
                       line(x, Y + 9, x, BUS_Y, WHITE if on else DIM, 'tp%d' % (h + 1))))
    # The bracket round the heads while white is on page 2 (scaled with the time about its left edge).
    e.append(rect(REC + 18, Y + 1, SPAN0 - 10, 11, 'none', WHITE, id_='wbox', hidden=True))
    # The signal on the tape: a dot the screen moves from the record head to head 3.
    e.append(circle(REC, Y, 2.5, 'none', BLUE, 'dot'))
    # The return path (green while holding) and the feedback chevrons, a count set.
    e.append(line(SUM, Y + 6, SUM, RET_Y, MID, 'ret1'))
    e.append(line(SUM, RET_Y, REC, RET_Y, MID, 'ret2'))
    e.append(line(REC, RET_Y, REC, Y + 9, MID, 'ret3'))
    for i in range(4):
        x = CHEV_X - 12 * i
        e.append(polyline([(x + 4, CHEV_Y - 4), (x, CHEV_Y), (x + 4, CHEV_Y + 4)], GREEN, 'fb%d' % (i + 1), hidden=i >= 2))
    # The hold glyph, top right.
    e.append(polyline(lemniscate(HOLD_X, HOLD_Y, 7), MID, 'hold'))
    # Gain: the centre mark and the bar (drawn at +6 dB, scaled -1..1 about the centre).
    e.append(gauge(GAIN_X, line(GAIN_X - 4, CY, GAIN_X + 4, CY, BLUE, 'gzero') +
                   rect(GAIN_X - 2, CY - GAIN_BAR, 4, GAIN_BAR, BLUE, id_='gbar')))
    e.append(rect(8, 22, 16, 108, 'none', BLUE, id_='gbox', hidden=True))
    for i, ch in enumerate('+-'):
        e.append(group('gs%d' % i, label(ch, READOUT_X, READOUT_Y, BLUE), hidden=True))
    for d in range(7):
        e.append(group('gd%d' % d, label(str(d), READOUT_X + 7, READOUT_Y, BLUE), hidden=True))
    e.append(group('gdot', label('.', READOUT_X + 14, READOUT_Y, BLUE), hidden=True))
    for d in range(10):
        e.append(group('gt%d' % d, label(str(d), READOUT_X + 17, READOUT_Y, BLUE), hidden=True))
    # The readouts: the tempo division (blue) while locked, the head pattern (white) while white is on page 2.
    for i, text in enumerate(DIVISIONS):
        e.append(group('dv%d' % i, label(text, READOUT_X, READOUT_Y, BLUE), hidden=True))
    for i, text in enumerate(PATTERNS):
        e.append(group('pt%d' % i, label(text, READOUT_X, READOUT_Y, WHITE), hidden=True))
    # Mix: the bar drawn full, scaled about the foot.
    e.append(gauge(MIX_X, rect(MIX_X - 2, TOP, 4, BOTTOM - TOP, ORANGE, id_='mbar')))
    e.append('</svg>')
    return '\n'.join(e) + '\n'


def check(svg):
    ids = re.findall(r'id="([^"]*)"', svg)
    for i in ids:
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9]*', i):
            raise ValueError('bad id %r' % i)
    if len(ids) != len(set(ids)):
        raise ValueError('duplicate id')
    if re.search(r'\d\.\d{3,}', svg):
        raise ValueError('more than 2 decimals')
    for bad in ('<text', '<path', '<style', 'url('):
        if bad in svg:
            raise ValueError('forbidden: %r' % bad)
    for pts in re.findall(r'points="([^"]*)"', svg):
        p = [tuple(map(float, q.split(','))) for q in pts.split()]
        for a, b, c in zip(p, p[1:], p[2:]):
            ux, uy, vx, vy = b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1]
            lu, lv = math.hypot(ux, uy), math.hypot(vx, vy)
            if lu == 0 or lv == 0 or (ux * vx + uy * vy) / (lu * lv) < -0.985:
                raise ValueError('polyline turns back at %r' % (b,))
    return svg


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    svg = check(build())
    with open(sys.argv[1], 'w', encoding='utf-8', newline='\n') as f:
        f.write(svg)
    print('%s: %d bytes, %d ids' % (sys.argv[1], len(svg), len(re.findall(r'id="', svg))))


if __name__ == '__main__':
    main()
