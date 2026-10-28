#!/usr/bin/env python3
"""The drive kennel plugin's screen (the Claude Design canvas "Drive screen",
design A "Schematic", 2026-10-27; driven by drive_knl.cc).

    python3 gen_drive_knl_svg.py drive.svg

The signal path drawn as a circuit, left to right: the input jack, the input
gain as a potentiometer (a dim resistor with a blue wiper arrow the screen
slides along it, -6 dB at the left end, +6 dB at the right), the node, the
clipper (two anti-parallel diodes to ground, blue: their triangles fill from
the tip as drive rises, in seven variants, with conduction rays at the top),
the triode (a white circle with plate, grid and cathode; the filament's colour
and the glow under the cathode warm with bias), the tilt EQ after it as its
S-shaped response (green, drawn at full tilt, the screen scales it about the
pivot: positive bright, negative dark, flat at the centre), the output arrow,
and the orange mix gauge at the far right. Blue pressed: a blue dB readout
above the pot (sign, digit, point, digit: stroked glyphs shown one of each)
and the diodes in their dim copy.

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

WHITE, DIM, BLUE, GREEN, ORANGE, BLACK, MID, GLOW = (
    '#DED7FF', '#38354D', '#638BFF', '#00EE92', '#FF395A', '#010101', '#514959', '#7A6E8C')

Y = 78                                       # the signal rail
TOP, BOTTOM = 28, 124                        # the gauge's rows (as the bitcrusher's)
MIX_X = 304
POT_X0, POT_X1 = 30, 62                      # the pot's resistor
WIPER_X, WIPER_SWING = 46, 13                # the wiper's centre and half travel (px)
NODE_X = 66
DIODE_L, DIODE_R = 52, 80                    # the two diodes' columns
TUBE_X, TUBE_R = 152, 34                     # the triode
PIVOT_X, TILT_HALF = 248, 22                 # the tilt's pivot and half width
READOUT_X, READOUT_Y = 24, 36                # the gain readout's cell top left


def stroke(colour):
    return 'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"' % colour


def attrs(id_, hidden):
    return (' id="%s"' % id_ if id_ else '') + (' display="none"' if hidden else '')


def line(x1, y1, x2, y2, colour, id_=None, hidden=False):
    return '<line%s %s x1="%s" y1="%s" x2="%s" y2="%s"></line>' % (attrs(id_, hidden), stroke(colour), num(x1), num(y1), num(x2), num(y2))


def polyline(points, colour, id_=None, hidden=False):
    return '<polyline%s %s points="%s"></polyline>' % (attrs(id_, hidden), stroke(colour), ' '.join('%s,%s' % (num(x), num(y)) for x, y in points))


def polygon(points, colour, fill='none', id_=None):
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


def zigzag(x0, x1, y, amp=4, teeth=6):
    pts = [(x0, y)]
    w = (x1 - x0) / (teeth * 2)
    for i in range(teeth * 2):
        pts.append((x0 + w * (i + 0.5), y - amp if i % 2 == 0 else y + amp))
    pts.append((x1, y))
    return pts


def ground(cx, y):
    return line(cx - 9, y, cx + 9, y, MID) + line(cx - 6, y + 3.5, cx + 6, y + 3.5, MID) + line(cx - 3, y + 7, cx + 3, y + 7, MID)


def diode(cx, ytop, ybot, up, colour):
    """The outline of a vertical diode between two wires: triangle and bar."""
    a = ytop + (ybot - ytop - 12) / 2
    b = a + 12
    if up:
        tri = [(cx - 5, b), (cx + 5, b), (cx, a)]
        bar = line(cx - 5, a, cx + 5, a, colour)
    else:
        tri = [(cx - 5, a), (cx + 5, a), (cx, b)]
        bar = line(cx - 5, b, cx + 5, b, colour)
    return polygon(tri, colour) + bar


def diode_fill(cx, ytop, ybot, up, f):
    """The filled part of a diode's triangle, from the tip, fraction f."""
    a = ytop + (ybot - ytop - 12) / 2
    b = a + 12
    if up:
        pts = [(cx, a), (cx - 5 * f, a + 12 * f), (cx + 5 * f, a + 12 * f)]
    else:
        pts = [(cx, b), (cx - 5 * f, b - 12 * f), (cx + 5 * f, b - 12 * f)]
    return polygon(pts, 'none', BLUE)


def gauge(x, bar):
    return line(x, TOP, x, BOTTOM, DIM) + line(x - 4, TOP, x + 4, TOP, DIM) + line(x - 4, BOTTOM, x + 4, BOTTOM, DIM) + bar


def build():
    e = ['<?xml version="1.0"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">']
    e.append(group('palette', ''.join(line(0, 0, 1, 0, c, i) for i, c in (
        ('palwhite', WHITE), ('paldim', DIM), ('palblue', BLUE), ('palgreen', GREEN), ('palorange', ORANGE),
        ('palblack', BLACK), ('palmid', MID), ('palglow', GLOW))), hidden=True))
    s = []
    # Input: jack, the pot's resistor, the node, the wire to the grid.
    s.append(circle(20, Y, 3, MID))
    s.append(line(23, Y, POT_X0, Y, MID))
    s.append(polyline(zigzag(POT_X0, POT_X1, Y), MID))
    s.append(line(POT_X1, Y, NODE_X, Y, MID))
    s.append(circle(NODE_X, Y, 1.8, 'none', MID))
    s.append(line(NODE_X, Y, TUBE_X - TUBE_R, Y, MID))
    # The clipper's wires and ground.
    s.append(line(NODE_X, Y, NODE_X, 88, MID))
    s.append(line(DIODE_L, 88, DIODE_R, 88, MID))
    for cx in (DIODE_L, DIODE_R):
        s.append(line(cx, 88, cx, 96, MID))
        s.append(line(cx, 108, cx, 116, MID))
    s.append(line(DIODE_L, 116, DIODE_R, 116, MID))
    s.append(line(NODE_X, 116, NODE_X, 122, MID))
    s.append(ground(NODE_X, 122))
    # The triode: envelope, grid (dashes), plate and its lead, cathode and its lead, ground.
    cx, cy, r = TUBE_X, Y, TUBE_R
    s.append(circle(cx, cy, r, WHITE))
    for gx in range(cx - 18, cx + 18, 8):
        s.append(line(gx, cy, gx + 4, cy, MID))
    s.append(line(cx - 12, cy - 15, cx + 12, cy - 15, WHITE))
    s.append(line(cx, cy - 15, cx, cy - r, WHITE))
    s.append(line(cx, cy - r, cx, 34, MID))
    s.append(line(cx, 34, 214, 34, MID))
    s.append(line(214, 34, 214, Y, MID))
    s.append(line(214, Y, PIVOT_X - TILT_HALF - 4, Y, MID))
    s.append(polyline([(cx - 9, cy + 8), (cx - 9, cy + 11), (cx + 9, cy + 11), (cx + 9, cy + 8)], WHITE))
    s.append(line(cx, cy + 11, cx, cy + r, MID))
    s.append(line(cx, cy + r, cx, 122, MID))
    s.append(ground(cx, 122))
    # After the tilt: the output arrow.
    s.append(circle(PIVOT_X, Y, 1.5, MID))
    s.append(line(PIVOT_X + TILT_HALF, Y, 280, Y, MID))
    s.append(polyline([(277, Y - 4), (281, Y), (277, Y + 4)], MID))
    e.append(group('static', ''.join(s)))
    # The diodes: the blue outlines, and the dim copy shown while blue is on page 2.
    pair = lambda colour: diode(DIODE_L, 88, 116, False, colour) + diode(DIODE_R, 88, 116, True, colour)
    e.append(group('dio', pair(BLUE)))
    e.append(group('diodim', pair(DIM), hidden=True))
    # Drive: the fills, variant k = 1..7 (none at 0), rays from 5.
    for k in range(1, 8):
        f = k / 7
        v = diode_fill(DIODE_L, 88, 116, False, f) + diode_fill(DIODE_R, 88, 116, True, f)
        if k >= 5:
            for dx, yy in ((DIODE_L, 101), (DIODE_R, 103)):
                v += line(dx - 9, yy - 3, dx - 7, yy - 3, BLUE) + line(dx + 7, yy - 3, dx + 9, yy - 3, BLUE)
                if k >= 7:
                    v += line(dx - 9, yy + 3, dx - 7, yy + 3, BLUE) + line(dx + 7, yy + 3, dx + 9, yy + 3, BLUE)
        e.append(group('clip%d' % k, v, hidden=k != 3))
    # The input gain: the wiper (the screen translates it by -13..13 px).
    e.append(group('wiper', line(WIPER_X, 58, WIPER_X, 68, BLUE) + polygon([(WIPER_X - 3, 67), (WIPER_X + 3, 67), (WIPER_X, 72)], 'none', BLUE)))
    # The readout: one of each glyph set shown while blue is on page 2, and its rule.
    for i, ch in enumerate('+-'):
        e.append(group('gs%d' % i, label(ch, READOUT_X, READOUT_Y, BLUE), hidden=True))
    for d in range(7):
        e.append(group('gd%d' % d, label(str(d), READOUT_X + 7, READOUT_Y, BLUE), hidden=True))
    e.append(group('gdot', label('.', READOUT_X + 14, READOUT_Y, BLUE), hidden=True))
    for d in range(10):
        e.append(group('gt%d' % d, label(str(d), READOUT_X + 17, READOUT_Y, BLUE), hidden=True))
    e.append(line(READOUT_X - 3, READOUT_Y + 10, READOUT_X + 29, READOUT_Y + 10, DIM, 'grule', hidden=True))
    # Bias: the glow under the cathode (fill) and the filament (line colour).
    e.append(circle(cx, cy + 20, 6, 'none', '#29252D', id_='glow'))
    fx, ft, fb = cx, cy + 15, cy + 26
    e.append(polyline([(fx - 4.5, fb), (fx - 4.5, ft), (fx - 2.25, ft + 6), (fx, ft), (fx + 2.25, ft + 6), (fx + 4.5, ft), (fx + 4.5, fb)], '#8B86A6', 'fil'))
    # Tone: the S response at full tilt (the screen scales it about the pivot).
    e.append(polyline([(PIVOT_X - TILT_HALF + 2 * TILT_HALF * i / 24, Y - 12 * math.tanh(3.0 * (2 * i / 24 - 1))) for i in range(25)], GREEN, 'tilt'))
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
