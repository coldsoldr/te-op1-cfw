#!/usr/bin/env python3
"""The bitcrusher kennel plugin's screen (the Claude Design canvas "Bitcrusher
screen", option C "Ladder", 2026-10-27; driven by crush_knl.cc).

    python3 gen_crush_knl_svg.py crush.svg

The whole safe rectangle: a white input-gain gauge at the far left (0 dB at
its centre mark, the bar up for +, down for -), an orange mix gauge at the
far right (the bar up from the foot), and between them the quantiser: a dim
reference sine, the blue staircase (eight variants by rate, step widths 8 to
56 px, nine levels), the green bit ladder on its left edge (a count set), the
white clock row along the top (one tick per step, in two groups the screen
jitters against each other) and the white sampling dot the screen runs along
the steps. Blue pressed: a bracket around the gain gauge and a readout above
it (sign, digit, point, digit: stroked glyphs shown one of each).

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

WHITE, DIM, BLUE, GREEN, DIMGREEN, ORANGE, BLACK, MID = (
    '#DED7FF', '#38354D', '#638BFF', '#00EE92', '#1B3D33', '#FF395A', '#010101', '#8E86B0')

TOP, BOTTOM, CY, AMP = 28, 124, 76, 48      # the plot's rows: nine levels 12 px apart
X0, X1 = 40, 288                             # the staircase's span
LEVELS = 9
STEP_WIDTHS = [round(8 * 1.32 ** k, 1) for k in range(8)]   # rate index 0..7: 8 .. 55.9 px
GAIN_X, MIX_X = 16, 304
GAIN_BAR = 24                                # the bar's height at +6 dB (drawn, scaled -1..1)
BIT_ROWS = [4, 1, 7, 2, 6, 3, 5, 0]          # ladder ticks in count-set order (level index)
READOUT_X, READOUT_Y = 4, 8                  # the gain readout's cell top left


def stroke(colour):
    return 'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"' % colour


def line(x1, y1, x2, y2, colour, id_=None, hidden=False):
    i = ' id="%s"' % id_ if id_ else ''
    h = ' display="none"' if hidden else ''
    return '<line%s%s %s x1="%s" y1="%s" x2="%s" y2="%s"></line>' % (i, h, stroke(colour), num(x1), num(y1), num(x2), num(y2))


def polyline(points, colour, id_=None, hidden=False):
    i = ' id="%s"' % id_ if id_ else ''
    h = ' display="none"' if hidden else ''
    return '<polyline%s%s %s points="%s"></polyline>' % (i, h, stroke(colour), ' '.join('%s,%s' % (num(x), num(y)) for x, y in points))


def rect(x, y, w, h, fill, stroke_colour='none', id_=None, hidden=False):
    i = ' id="%s"' % id_ if id_ else ''
    hd = ' display="none"' if hidden else ''
    return '<rect%s%s fill="%s" stroke="%s" stroke-width="1.5" x="%s" y="%s" width="%s" height="%s"></rect>' % (
        i, hd, fill, stroke_colour, num(x), num(y), num(w), num(h))


def group(id_, inner, hidden=False):
    return '<g id="%s"%s>%s</g>' % (id_, ' display="none"' if hidden else '', inner)


def sine_y(x):
    return CY - AMP * math.sin(2 * math.pi * (x - X0) / (X1 - X0))


def level_y(x):
    """The sine at x snapped to the nine levels."""
    j = round((sine_y(x) - TOP) / 12)
    return TOP + 12 * min(LEVELS - 1, max(0, j))


def staircase(width):
    pts = []
    x = X0
    prev = None
    while x <= X1 + 0.01:
        y = level_y(x)
        if prev is None:
            pts.append((x, y))
        elif y != prev:
            pts += [(x, prev), (x, y)]
        prev = y
        x += width
    end = min(x, X1)
    if end > pts[-1][0]:
        pts.append((end, prev))
    return pts


def gauge(x, colour, bar):
    e = [line(x, TOP, x, BOTTOM, DIM), line(x - 4, TOP, x + 4, TOP, DIM), line(x - 4, BOTTOM, x + 4, BOTTOM, DIM)]
    e.append(bar)
    return ''.join(e)


def build():
    e = ['<?xml version="1.0"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">']
    e.append(group('palette', ''.join(line(0, 0, 1, 0, c, i) for i, c in (
        ('palwhite', WHITE), ('paldim', DIM), ('palblue', BLUE), ('palgreen', GREEN), ('paldimgreen', DIMGREEN),
        ('palorange', ORANGE), ('palblack', BLACK), ('palmid', MID))), hidden=True))
    # The reference sine (static, dim).
    e.append(polyline([(X0 + (X1 - X0) * i / 64, sine_y(X0 + (X1 - X0) * i / 64)) for i in range(65)], DIM, 'sine'))
    # The staircase variants and their clock rows.
    for k, w in enumerate(STEP_WIDTHS):
        e.append(polyline(staircase(w), BLUE, 'stair%d' % k, hidden=k != 3))
        xs = []
        x = X0
        while x <= X1 + 0.01:
            xs.append(x)
            x += w
        for half, name in ((0, 'a'), (1, 'b')):
            ticks = ''.join(line(t, 10, t, 18, WHITE) for i, t in enumerate(xs) if i % 2 == half)
            e.append(group('clk%d%s' % (k, name), ticks, hidden=k != 3))
    # The bit ladder (count set, bit0 first).
    for i, j in enumerate(BIT_ROWS):
        e.append(line(30, TOP + 12 * j, 36, TOP + 12 * j, GREEN, 'bit%d' % i, hidden=i >= 5))
    # The sampling dot (the screen moves it).
    e.append(group('dot', '<circle fill="%s" stroke="none" cx="%s" cy="%s" r="2.5"></circle>' % (WHITE, num(X0), num(CY))))
    # Gain: the centre mark and the bar (drawn at +6 dB, scaled -1..1 about the centre).
    e.append(gauge(GAIN_X, WHITE, line(GAIN_X - 4, CY, GAIN_X + 4, CY, WHITE, 'gzero') +
                   rect(GAIN_X - 2, CY - GAIN_BAR, 4, GAIN_BAR, WHITE, id_='gbar')))
    e.append(rect(8, 22, 16, 108, 'none', WHITE, id_='gbox', hidden=True))
    # The readout: one of each glyph set shown while blue is on page 2.
    for i, ch in enumerate('+-'):
        e.append(group('gs%d' % i, label(ch, READOUT_X, READOUT_Y, WHITE), hidden=True))
    for d in range(7):
        e.append(group('gd%d' % d, label(str(d), READOUT_X + 7, READOUT_Y, WHITE), hidden=True))
    e.append(group('gdot', label('.', READOUT_X + 14, READOUT_Y, WHITE), hidden=True))
    for d in range(10):
        e.append(group('gt%d' % d, label(str(d), READOUT_X + 17, READOUT_Y, WHITE), hidden=True))
    # Mix: the bar drawn full, scaled about the foot.
    e.append(gauge(MIX_X, ORANGE, rect(MIX_X - 2, TOP, 4, BOTTOM - TOP, ORANGE, id_='mbar')))
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
    print('%s: %d bytes, %d ids, step widths %s' % (sys.argv[1], len(svg), len(re.findall(r'id="', svg)), STEP_WIDTHS))


if __name__ == '__main__':
    main()
