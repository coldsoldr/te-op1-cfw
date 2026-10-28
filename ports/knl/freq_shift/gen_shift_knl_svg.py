#!/usr/bin/env python3
"""The freq_shift kennel plugin's screen (the Claude Design canvas "Freq shift
screen", design B "Dial", 2026-10-27; driven by shift_knl.cc).

    python3 gen_shift_knl_svg.py shift.svg

Shift as a needle on a bipolar arc spanning the width between the gauges:
0 Hz at the top, -2 kHz at the left end, +2 kHz at the right (78 degrees
each way). The white spread arc (seven pre-drawn spans the screen rotates to
the shift's angle) shows the spread knob on every position; on the master
the two needles, L and R, part to its ends; in ring mode the needle mirrors
to both sides of the top. A green bracket round the mode marks while green
is on page 2. While sound plays, dim ripples (four arcs the screen scales
from r 20 out to the rim, fading) spread from the hub at the input level, as
a sonar's. Green
inner arcs for the feedback, a count set, sweeping the way the sound climbs
(the up set mirrored for down; a centred set in ring mode). The mode marks
top left (an arrow for the shifter, a ring for the ring mod; green, since
the green press flips the mode), the blue input-gain gauge at the far left
(0 dB at its centre mark; a bracket and a readout while blue is on page 2),
the orange mix gauge at the far right.

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

TOP, BOTTOM, CY = 28, 124, 76              # the gauges' rows (as the bitcrusher's)
GAIN_X, MIX_X = 16, 304
GAIN_BAR = 24
READOUT_X, READOUT_Y = 4, 8
CX, CYD, R = 160, 124, 100                 # the dial: centre and radius
SWEEP = 78                                 # degrees from the top to either end
NEEDLE = R - 6
RIPPLE_R = 20                              # a ripple's drawn radius (scaled up to R)
MODE_X, MODE_Y = 48, 14


def stroke(colour):
    return 'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"' % colour


def attrs(id_, hidden):
    return (' id="%s"' % id_ if id_ else '') + (' display="none"' if hidden else '')


def line(x1, y1, x2, y2, colour, id_=None, hidden=False):
    return '<line%s %s x1="%s" y1="%s" x2="%s" y2="%s"></line>' % (attrs(id_, hidden), stroke(colour), num(x1), num(y1), num(x2), num(y2))


def polyline(points, colour, id_=None, hidden=False):
    return '<polyline%s %s points="%s"></polyline>' % (attrs(id_, hidden), stroke(colour), ' '.join('%s,%s' % (num(x), num(y)) for x, y in points))


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


def pt(r, deg):
    return (CX + r * math.cos(math.radians(deg)), CYD - r * math.sin(math.radians(deg)))


def arc(r, a0, a1, colour, steps, id_=None, hidden=False):
    return polyline([pt(r, a0 + (a1 - a0) * i / steps) for i in range(steps + 1)], colour, id_, hidden)


def build():
    e = ['<?xml version="1.0"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">']
    e.append(group('palette', ''.join(line(0, 0, 1, 0, c, i) for i, c in (
        ('palwhite', WHITE), ('paldim', DIM), ('palblue', BLUE), ('palgreen', GREEN), ('palorange', ORANGE),
        ('palblack', BLACK), ('palmid', MID))), hidden=True))
    # The dial: the arc, the 0 Hz mark, the end ticks, the hub.
    s = [arc(R, 90 - SWEEP, 90 + SWEEP, MID, 64)]
    s.append(line(CX, CYD - R - 4, CX, CYD - R + 4, MID))
    for t in (-1, 1):
        a = 90 - SWEEP * t
        (x1, y1), (x2, y2) = pt(R - 4, a), pt(R + 4, a)
        s.append(line(x1, y1, x2, y2, MID))
    s.append(circle(CX, CYD, 3, 'none', MID))
    e.append(group('static', ''.join(s)))
    # The sound's ripples: four arcs at r 20 the screen scales out to the rim and fades.
    for i in range(4):
        e.append(arc(RIPPLE_R, 90 - SWEEP, 90 + SWEEP, MID, 32, 'rp%d' % (i + 1), hidden=True))
    # The mode marks: an arrow (the shifter) and a ring (the ring mod); green = active.
    e.append(line(MODE_X, MODE_Y, MODE_X + 12, MODE_Y, GREEN, 'modea1'))
    e.append(polyline([(MODE_X + 8, MODE_Y - 4), (MODE_X + 12, MODE_Y), (MODE_X + 8, MODE_Y + 4)], GREEN, 'modea2'))
    e.append(circle(MODE_X + 28, MODE_Y, 4, DIM, id_='moder'))
    # The bracket round the marks while green is on page 2.
    e.append(rect(MODE_X - 6, MODE_Y - 8, 44, 16, 'none', GREEN, id_='mbox', hidden=True))
    # Feedback: the up set (sweeping right from the top; mirrored for down) and
    # the ring set (centred on the top), count sets.
    for i in range(4):
        r = R - 30 - 12 * i
        e.append(arc(r, 90, 90 - 70, GREEN, 12, 'fu%d' % (i + 1), hidden=i >= 2))
        e.append(arc(r, 60, 120, GREEN, 12, 'fr%d' % (i + 1), hidden=True))
    # Spread: seven spans centred on the top (the screen rotates the shown one).
    for k in range(1, 8):
        d = SWEEP * 0.33 * k / 7
        e.append(arc(R - 16, 90 + d, 90 - d, WHITE, 16, 'sp%d' % k, hidden=k != 4))
    # The needles, drawn pointing up (the screen rotates them about the hub).
    e.append(line(CX, CYD, CX, CYD - NEEDLE, BLUE, 'ndlL'))
    e.append(line(CX, CYD, CX, CYD - NEEDLE, BLUE, 'ndlR'))
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
