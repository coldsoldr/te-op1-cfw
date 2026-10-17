#!/usr/bin/env python3
"""Make the device's marbles.svg from the user's design (v2, "Dots").

    python gen_marbles_svg.py DESIGN.svg OUT.svg

The design is kept as drawn; only its <metadata> block (a content-credentials
blob) and that namespace go. It has no arcs. The screen code
(op1/marbles_op1.cc) reads its colours from the file's example state, so
that state is checked here: loop_t bright (locked), loop_x dim (on),
src_scale selected, src_keys not; and no driven element may sit inside a
transformed group (the OP-1 folds group transforms into their children).
"""

import re
import sys


def attrs(svg, ident):
    m = re.search(r'<[a-z]+ id="%s"([^>]*)>' % ident, svg)
    if not m:
        raise SystemExit('missing id %s' % ident)
    return dict(re.findall(r'([a-z-]+)="([^"]*)"', m.group(1)))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    svg = open(sys.argv[1], encoding='utf-8').read()
    svg = re.sub(r'<metadata>.*?</metadata>', '', svg, flags=re.S)
    svg = svg.replace(' xmlns:c2pa="http://c2pa.org/manifest"', '')
    if re.search(r'\sd="[^"]*[Aa]', svg):
        raise SystemExit('the design has an arc: convert it (see ports/clouds/op1/gen_clouds_svg.py)')
    bright, dim = attrs(svg, 'loop_t'), attrs(svg, 'loop_x')
    on, off = attrs(svg, 'src_scale'), attrs(svg, 'src_keys')
    checks = [
        (bright.get('stroke', '').upper(), '#DED7FF', 'loop_t stroke (locked)'),
        (dim.get('stroke', '').upper(), '#514959', 'loop_x stroke (on)'),
        (on.get('stroke', '').upper(), '#DED7FF', 'src_scale stroke (selected)'),
        (off.get('stroke', '').upper(), '#514959', 'src_keys stroke (not selected)'),
    ]
    if re.search(r'<g [^>]*transform=', svg):
        raise SystemExit('a group has a transform: driven elements must use screen coordinates')
    for value, expected, what in checks:
        if value != expected:
            raise SystemExit('%s is %s, expected %s' % (what, value, expected))
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write(svg)


if __name__ == '__main__':
    main()
