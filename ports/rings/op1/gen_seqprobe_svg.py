#!/usr/bin/env python3
"""Make seqprobe.svg: the sequencer probe's readout (op1/seq_probe.cc).

    python gen_seqprobe_svg.py OUT.svg

Ten numbers of six seven-segment digits, in two columns of five (number i
in column i / 5, row i % 5), each with its index drawn small and dim on its
left. Segment ids: n<i>d<j><s>, digit j = 0 the most significant, segment s
in "abcdefg" (a top, b top right, c bottom right, d bottom, e bottom left,
f top left, g middle). Lines only, as every stock SVG.
"""

import sys

NUMBERS = 10
DIGITS = 6
LIT = '#DED7FF'
DIM = '#514959'


def segments(x, y, w, h):
    m = y + h / 2
    return {
        'a': (x, y, x + w, y), 'b': (x + w, y, x + w, m), 'c': (x + w, m, x + w, y + h),
        'd': (x, y + h, x + w, y + h), 'e': (x, m, x, y + h), 'f': (x, y, x, m),
        'g': (x, m, x + w, m),
    }


GLYPHS = {0: 'abcdef', 1: 'bc', 2: 'abdeg', 3: 'abcdg', 4: 'bcfg', 5: 'acdfg', 6: 'acdefg',
          7: 'abc', 8: 'abcdefg', 9: 'abcdfg'}


def line(x1, y1, x2, y2, colour, width, ident=None):
    i = ' id="%s"' % ident if ident else ''
    return ('<line%s fill="none" stroke="%s" stroke-width="%s" stroke-linecap="square" '
            'x1="%g" y1="%g" x2="%g" y2="%g"></line>' % (i, colour, width, x1, y1, x2, y2))


def main():
    out = ['<?xml version="1.0"?>',
           '<svg version="1.1" xmlns="http://www.w3.org/2000/svg" x="0px" y="0px" width="320px" '
           'height="160px" viewBox="0 0 320 160">']
    for n in range(NUMBERS):
        x0 = 30 + (n // 5) * 160
        y0 = 8 + (n % 5) * 30
        for s in GLYPHS[n]:   # the index, small and dim
            out.append(line(*segments(x0 - 20, y0 + 6, 6, 12)[s], DIM, 1.5))
        for j in range(DIGITS):
            seg = segments(x0 + j * 17, y0, 10, 22)
            for s in 'abcdefg':
                out.append(line(*seg[s], LIT, 2, 'n%dd%d%s' % (n, j, s)))
    out.append('</svg>')
    open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
