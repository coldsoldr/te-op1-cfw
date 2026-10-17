#!/usr/bin/env python3
"""Make the device's marbles.svg from the user's design (the Orbit theme).

    python gen_marbles_svg.py DESIGN.svg OUT.svg

The design is kept as drawn; only its <metadata> block (a content-credentials
blob) and that namespace go. It has no arcs. The screen code
(op1/marbles_op1.cc) reads its colours from the file's example state, so
that state is checked here: gate1 lit and gate3 unlit (lit stroke and fill,
unlit stroke and fill), pageTind selected and pageXind not.
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
    lit, unlit = attrs(svg, 'gate1'), attrs(svg, 'gate3')
    on, off = attrs(svg, 'pageTind'), attrs(svg, 'pageXind')
    checks = [
        (lit.get('stroke', '').upper(), '#DED7FF', 'gate1 stroke (lit)'),
        (lit.get('fill', '').upper(), '#DED7FF', 'gate1 fill (lit)'),
        (unlit.get('stroke', '').upper(), '#514959', 'gate3 stroke (unlit)'),
        (unlit.get('fill', '').upper(), '#010101', 'gate3 fill (unlit)'),
        (on.get('stroke', '').upper(), '#DED7FF', 'pageTind stroke (selected)'),
        (off.get('stroke', '').upper(), '#514959', 'pageXind stroke (not selected)'),
    ]
    for value, expected, what in checks:
        if value != expected:
            raise SystemExit('%s is %s, expected %s' % (what, value, expected))
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write(svg)


if __name__ == '__main__':
    main()
