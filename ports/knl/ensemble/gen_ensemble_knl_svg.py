#!/usr/bin/env python3
"""The ensemble kennel plugin's screen (the Claude Design canvas "Ensemble
screen", design C "Drift", 2026-10-27; driven by ensemble_knl.cc).

    python3 gen_ensemble_knl_svg.py ensemble.svg

Each voice a vertical white string, the L lane above and the R lane below,
swaying about a dim home mark with the LFO (the screen translates the
strings every frame); green ticks either side of each home mark show how far
it can drift (the mark-and-ticks group is scaled about the mark by depth).
At the right the blue sweep dial (its dot circling at the rate) with the rate
bar under it; the blue input-gain gauge at the far left (0 dB at its centre
mark, the bar up for +, down for -; a bracket and a readout above while blue
is on page 2); the orange mix gauge at the far right. Mono positions hide the
R lane.

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
X0, X1 = 44, 236                           # the lanes' span
LANES = ((30, 70), (86, 126))              # L, R: top and bottom of the strings
HOMES = (80, 140, 200)                     # the voices' home marks
SPAN = 28                                  # a voice's drift at full depth (px, drawn)
DIAL_X, DIAL_Y, DIAL_R = 268, 66, 10
RATE_X0, RATE_X1, RATE_Y = 252, 284, 92


def stroke(colour):
    return 'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"' % colour


def attrs(id_, hidden):
    return (' id="%s"' % id_ if id_ else '') + (' display="none"' if hidden else '')


def line(x1, y1, x2, y2, colour, id_=None, hidden=False):
    return '<line%s %s x1="%s" y1="%s" x2="%s" y2="%s"></line>' % (attrs(id_, hidden), stroke(colour), num(x1), num(y1), num(x2), num(y2))


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


def build():
    e = ['<?xml version="1.0"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">']
    e.append(group('palette', ''.join(line(0, 0, 1, 0, c, i) for i, c in (
        ('palwhite', WHITE), ('paldim', DIM), ('palblue', BLUE), ('palgreen', GREEN), ('palorange', ORANGE),
        ('palblack', BLACK), ('palmid', MID))), hidden=True))
    # The lanes' guides (the R lane's hides on mono positions) and the dial.
    (l0, l1), (r0, r1) = LANES
    e.append(group('static', line(X0, (l0 + l1) / 2, X1, (l0 + l1) / 2, DIM) + circle(DIAL_X, DIAL_Y, DIAL_R, MID) +
                   line(RATE_X0, RATE_Y, RATE_X1, RATE_Y, DIM)))
    e.append(group('laneR', line(X0, (r0 + r1) / 2, X1, (r0 + r1) / 2, DIM)))
    # Per lane and voice: the home mark with its depth ticks (one group, scaled
    # about the mark), and the string (translated).
    for lane, (y0, y1), tag in ((0, LANES[0], 'L'), (1, LANES[1], 'R')):
        cy = (y0 + y1) / 2
        for i, hx in enumerate(HOMES):
            marks = line(hx, cy - 3, hx, cy + 3, MID) + line(hx - SPAN, y0 - 2, hx - SPAN, y0 + 2, GREEN) + line(hx + SPAN, y0 - 2, hx + SPAN, y0 + 2, GREEN)
            e.append(group('h%s%d' % (tag, i + 1), marks, hidden=i >= 2))
            e.append(group('v%s%d' % (tag, i + 1), line(hx, y0, hx, y1, WHITE), hidden=i >= 2))
    # Rate: the dot on the dial (translated round it) and the bar (scaled about its left end).
    e.append(group('sweep', circle(DIAL_X, DIAL_Y - DIAL_R, 2.5, 'none', BLUE)))
    e.append(rect(RATE_X0, RATE_Y - 1.5, RATE_X1 - RATE_X0, 3, BLUE, id_='rbar'))
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
