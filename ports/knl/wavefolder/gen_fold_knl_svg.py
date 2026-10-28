#!/usr/bin/env python3
"""The wavefolder kennel plugin's screen (the Claude Design canvas "Fold screen",
design A "Wave, aligned", 2026-10-27; driven by fold_knl.cc).

    python3 gen_fold_knl_svg.py fold.svg

The firmware's fold composition without its box, spread over the width on
the family's rows: a blue input-gain gauge at the far left (0 dB at its
centre mark, the bar up for +, down for -; a bracket and a readout above
while blue is on page 2), one cycle of the output wave drawn large between
two dim fold lines (eight variants, more wiggles in the tops and bottoms as
fold rises), the green offset line the screen slides up and down with
symmetry, the white low-pass glyph at the right (its corner walking left as
tone darkens) and the orange mix gauge at the far right.

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
X0, X1, AMP = 44, 236, 36                  # the wave's span and amplitude about CY
TONE_X, TONE_CORNER, TONE_END = 248, 276, 288   # the glyph, drawn bright (the corner at its right)
TONE_Y0, TONE_Y1 = 64, 88


def stroke(colour):
    return 'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"' % colour


def attrs(id_, hidden):
    return (' id="%s"' % id_ if id_ else '') + (' display="none"' if hidden else '')


def line(x1, y1, x2, y2, colour, id_=None, hidden=False):
    return '<line%s %s x1="%s" y1="%s" x2="%s" y2="%s"></line>' % (attrs(id_, hidden), stroke(colour), num(x1), num(y1), num(x2), num(y2))


def polyline(points, colour, id_=None, hidden=False):
    return '<polyline%s %s points="%s"></polyline>' % (attrs(id_, hidden), stroke(colour), ' '.join('%s,%s' % (num(x), num(y)) for x, y in points))


def rect(x, y, w, h, fill, stroke_colour='none', id_=None, hidden=False):
    return '<rect%s fill="%s" stroke="%s" stroke-width="1.5" x="%s" y="%s" width="%s" height="%s"></rect>' % (
        attrs(id_, hidden), fill, stroke_colour, num(x), num(y), num(w), num(h))


def group(id_, inner, hidden=False):
    return '<g id="%s"%s>%s</g>' % (id_, ' display="none"' if hidden else '', inner)


def gauge(x, bar):
    return line(x, TOP, x, BOTTOM, DIM) + line(x - 4, TOP, x + 4, TOP, DIM) + line(x - 4, BOTTOM, x + 4, BOTTOM, DIM) + bar


def wave(k, steps=96):
    """One output cycle: y = CY - AMP sin(g pi/2 sin th), g = 1 + 0.6 k (the firmware's)."""
    g = 1 + 0.6 * k
    return [(X0 + (X1 - X0) * i / steps, CY - AMP * math.sin(g * math.pi / 2 * math.sin(2 * math.pi * i / steps)))
            for i in range(steps + 1)]


def build():
    e = ['<?xml version="1.0"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">']
    e.append(group('palette', ''.join(line(0, 0, 1, 0, c, i) for i, c in (
        ('palwhite', WHITE), ('paldim', DIM), ('palblue', BLUE), ('palgreen', GREEN), ('palorange', ORANGE),
        ('palblack', BLACK), ('palmid', MID))), hidden=True))
    e.append(group('static', line(X0, CY - AMP, X1, CY - AMP, DIM) + line(X0, CY + AMP, X1, CY + AMP, DIM)))
    # Symmetry: the offset line (translated -24..24 px).
    e.append(line(X0, CY, X1, CY, GREEN, 'symmetry'))
    # Fold: the output wave, eight variants.
    for k in range(8):
        e.append(polyline(wave(k), BLUE, 'wave%d' % k, hidden=k != 3))
    # Tone: the low-pass glyph in two parts (the flat scaled about its left end,
    # the slope translated with the corner).
    e.append(line(TONE_X, TONE_Y0, TONE_CORNER, TONE_Y0, WHITE, 'tonef'))
    e.append(polyline([(TONE_CORNER, TONE_Y0), (TONE_END, TONE_Y1)], WHITE, 'tones'))
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
