#!/usr/bin/env python3
"""The tape_drive kennel plugin's screen (id tape_drive, "tape drive" in the list; was tapesat, cs_tape before that) (the Claude Design canvas "Tape screen",
design A "Reels, aligned", 2026-10-27; driven by tape_knl.cc).

    python3 gen_tape_drive_knl_svg.py tape_drive.svg

The firmware's tape composition without its box, spread over the width on
the family's rows: a blue input-gain gauge at the far left (0 dB at its
centre mark, the bar up for +, down for -; a bracket and a readout above
while blue is on page 2), the two reels (r 30, dim) with their green hubs
(the screen jitters them at the wobble's rate), the tape path down to the
rail at y 100 with the blue saturation wave on it (eight variants), the dim
head under the gap, the wow -> flutter scale on the gauges' foot row with
its green marker, the white low-pass glyph (its corner walking left as the
tape wears) with the hiss specks above it, and the orange mix gauge at the
far right.

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
GAIN_BAR = 24                              # the gain bar's height at +6 dB (drawn, scaled -1..1)
READOUT_X, READOUT_Y = 4, 8
REEL_L, REEL_R, REEL_Y, REEL_R_PX = 96, 208, 48, 30
HUB_R = 10
RAIL_Y = 100
WAVE_AMP = 8
SCALE_X0, SCALE_X1, SCALE_Y = 100, 204, 124
AGE_X, AGE_CORNER, AGE_END = 248, 276, 288
AGE_Y0, AGE_Y1 = 64, 88
SPECKS = [(252, 52), (272, 44), (260, 56), (284, 50), (256, 40), (278, 56)]


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


def gauge(x, bar):
    return line(x, TOP, x, BOTTOM, DIM) + line(x - 4, TOP, x + 4, TOP, DIM) + line(x - 4, BOTTOM, x + 4, BOTTOM, DIM) + bar


def hub(cx, cy):
    pts = [(cx + HUB_R * math.cos(math.radians(-90 + 120 * i)), cy + HUB_R * math.sin(math.radians(-90 + 120 * i))) for i in range(3)]
    return polygon(pts, GREEN)


def wave(k, x0, x1, steps=52):
    g = 0.6 * 1.45 ** k
    return [(x0 + (x1 - x0) * i / steps, RAIL_Y - WAVE_AMP * math.tanh(g * math.sin(2 * math.pi * 2 * i / steps)) / math.tanh(g))
            for i in range(steps + 1)]


def build():
    e = ['<?xml version="1.0"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">']
    e.append(group('palette', ''.join(line(0, 0, 1, 0, c, i) for i, c in (
        ('palwhite', WHITE), ('paldim', DIM), ('palblue', BLUE), ('palgreen', GREEN), ('palorange', ORANGE),
        ('palblack', BLACK), ('palmid', MID))), hidden=True))
    s = []
    lx, rx, ry, r = REEL_L, REEL_R, REEL_Y, REEL_R_PX
    s.append(circle(lx, ry, r, MID))
    s.append(circle(rx, ry, r, MID))
    s.append(polyline([(lx - r, ry), (lx - r + 2, RAIL_Y), (lx + r, RAIL_Y)], MID))
    s.append(polyline([(rx - r, RAIL_Y), (rx + r - 2, RAIL_Y), (rx + r, ry)], MID))
    hx = (lx + rx) / 2
    s.append(polygon([(hx - 6, 108), (hx + 6, 108), (hx + 4, 114), (hx - 4, 114)], MID, BLACK))
    # The wow -> flutter scale: slow-wave glyph, track, fast-wave glyph.
    s.append(polyline([(SCALE_X0 - 18 + 1.5 * i, SCALE_Y + 3 * math.sin(math.pi * i / 4)) for i in range(9)], MID))
    s.append(polyline([(SCALE_X1 + 6 + i, SCALE_Y + 3 * (1 if i % 2 else -1)) for i in range(13)], MID))
    s.append(line(SCALE_X0, SCALE_Y, SCALE_X1, SCALE_Y, DIM))
    e.append(group('static', ''.join(s)))
    # The hubs (the screen translates them).
    e.append(group('reelL', hub(lx, ry)))
    e.append(group('reelR', hub(rx, ry)))
    # Saturation: the wave on the rail, eight variants.
    for k in range(8):
        e.append(polyline(wave(k, lx + r, rx - r), BLUE, 'sat%d' % k, hidden=k != 3))
    # Wobble: the marker (translated 0..104 px).
    e.append(line(SCALE_X0, SCALE_Y - 5, SCALE_X0, SCALE_Y + 5, GREEN, 'wobble'))
    # Age: the low-pass glyph in two parts (the flat scaled about its left end,
    # the slope translated with the corner), and the hiss specks (a count set).
    e.append(line(AGE_X, AGE_Y0, AGE_CORNER, AGE_Y0, WHITE, 'agef'))
    e.append(polyline([(AGE_CORNER, AGE_Y0), (AGE_END, AGE_Y1)], WHITE, 'ages'))
    for i, (x, y) in enumerate(SPECKS):
        e.append(line(x, y, x + 0.5, y, WHITE, 'age%d' % i, hidden=i >= 2))
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
