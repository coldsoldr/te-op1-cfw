#!/usr/bin/env python3
"""The firmware's effect screen -> the kennel plugin's screen.

    python3 tools/knl_svg.py ports/fx/op1/plate.svg ports/knl/cs_plate/plate.svg

Removes the category dots (the circles with ids eng0..eng7: the firmware
hosts several engines behind one stock effect and lights the current one's
dot; a kennel plugin is its own list entry). Nothing else changes, so the
screen keeps passing the native parser's rules the generator enforced.
"""
import re
import sys

DOT = re.compile(r'<circle\b[^>]*\bid="eng\d"[^>]*?(?:/>|>\s*</circle>)')


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    svg = open(sys.argv[1], encoding='utf-8').read()
    out, n = DOT.subn('', svg)
    if n == 0:
        sys.exit('%s: no category dots found' % sys.argv[1])
    if re.search(r'id="eng\d"', out):
        sys.exit('%s: a dot survived (not a plain circle?)' % sys.argv[1])
    with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as f:
        f.write(out)
    print('%s: %d dots removed, %d bytes' % (sys.argv[2], n, len(out)))


if __name__ == '__main__':
    main()
