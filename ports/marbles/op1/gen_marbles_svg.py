#!/usr/bin/env python3
"""Make the device's marbles.svg from the user's design (v2, "Dots").

    python gen_marbles_svg.py DESIGN.svg OUT.svg

The design is kept as drawn; only its <metadata> block (a content-credentials
blob) and that namespace go. It has no arcs. The screen code
(op1/marbles_op1.cc) reads its colours from the file's example state, so
Ids lose any "_x" (the parser decodes "_xHH_" escapes in ids and asserts
without the closing "_"). matrix() transforms are rewritten with spaces between the numbers, the
only form the OP-1 parses (commas there fail at boot). That state is
checked here: loop_t green (locked), loop_x dim green (on),
src_scale selected, src_keys not; and no driven element may sit inside a
transformed group (the OP-1 folds group transforms into their children).
A path with several subpaths becomes one path per subpath (the OP-1 joins
subpaths with a line); a driven id's extra parts are idP2, idP3, ...
"""

import re
import sys
import xml.etree.ElementTree as ET


def attrs(svg, ident):
    m = re.search(r'<[a-z]+ id="%s"([^>]*)>' % ident, svg)
    if not m:
        raise SystemExit('missing id %s' % ident)
    return dict(re.findall(r'([a-z-]+)="([^"]*)"', m.group(1)))


def split_subpaths(svg):
    """One <path> per subpath: the OP-1 joins a path's subpaths with a line
    (stock never writes a second "M"; the keyboard icon and the grids icons
    showed the joins on the device). A driven id stays on the first part;
    the others are idP2, idP3, ... (the screen code drives them all; no
    underscore: the parser deletes "_..._" from ids, so "accent_vel_2" and
    "accent_key_2" both became "accent2" and seq5 didn't boot)."""
    def one(m):
        tag = m.group(0)
        d = re.search(r'\sd="([^"]*)"', tag).group(1)
        if 'm' in d:
            raise SystemExit('a relative moveto in %s: make it absolute' % d[:40])
        parts = [p.strip() for p in re.split(r'(?=M)', d) if p.strip()]
        if len(parts) == 1:
            return tag
        ident = re.search(r'\sid="([^"]*)"', tag)
        if ident and len(parts) > 9:
            raise SystemExit('%s has more than 9 subpaths' % ident.group(1))
        out = []
        for i, p in enumerate(parts):
            t = re.sub(r'(\sd=")[^"]*"', lambda a: a.group(1) + p + '"', tag, count=1)
            if ident and i:
                t = t.replace(ident.group(0), ident.group(0)[:-1] + 'P%d"' % (i + 1), 1)
            out.append(t)
        return '\n'.join(out)
    # The whole element, "<path .../>" or "<path ...></path>" (the designs
    # write the latter: copying only the start tag left one "</path>" for
    # several paths, and seq5 stopped at boot with a parse error).
    svg = re.sub(r'<path\s[^>]*?(?:/>|>\s*</path>)', one, svg)
    if svg.count('<path') != svg.count('</path>') + len(re.findall(r'<path\s[^>]*/>', svg)):
        raise SystemExit('unbalanced <path> elements')
    if re.search(r'\sd="M[^"]*M', svg):
        raise SystemExit('a path still has several subpaths')
    # Ids: the parser deletes everything from one "_" to the next.
    bad = [i for i in re.findall(r'\sid="([^"]*)"', svg) if i.count('_') > 1]
    if bad:
        raise SystemExit('ids with two underscores (the parser cuts "_..._"): %s' % bad[:5])
    return svg


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    svg = open(sys.argv[1], encoding='utf-8').read()
    svg = re.sub(r'<metadata>.*?</metadata>', '', svg, flags=re.S)
    svg = svg.replace(' xmlns:c2pa="http://c2pa.org/manifest"', '')
    # The OP-1's parser reads matrix() numbers separated by spaces only, as
    # every stock SVG writes them: "matrix(0.5,0,0,1,119.5,0)" trips its
    # assert at boot (parser.cpp:231 x_end != npos; marbles5 didn't boot).
    svg = re.sub(r'matrix\(([^)]*)\)',
                 lambda m: 'matrix(%s)' % ' '.join(re.split(r'[\s,]+', m.group(1).strip())), svg)
    if re.search(r'matrix\([^)]*,', svg):
        raise SystemExit('a matrix() still has commas')
    # Every transform in the design sits on a driven element, which the code
    # repositions on its first frame: the example values are dropped, so the
    # parser has no transform to read at all.
    svg = re.sub(r'(<(?!g[\s>])[a-z]+(?:\s[^>]*?)?)\s+transform="[^"]*"', r'\1', svg)
    if re.search(r'<(?!g[\s>])[a-z]+ [^>]*transform=', svg):
        raise SystemExit('a transform is left on an element')
    # display="none" is proven on groups, lines, polylines, rects, paths and
    # ellipses (stock, clouds.svg), not on circles (the history marks):
    # dropped from elements (all driven: the code sets their visibility on
    # its first frame), kept on groups.
    svg = re.sub(r'(<(?!g[\s>])[a-z]+(?:\s[^>]*?)?)\s+display="none"', r'\1', svg)
    if re.search(r'\sd="[^"]*[Aa]', svg):
        raise SystemExit('the design has an arc: convert it (see ports/clouds/op1/gen_clouds_svg.py)')
    # The parser decodes Illustrator's "_xHH_" escapes in ids: an id with
    # "_x" and no closing "_" trips its assert at boot (Parser.cpp:231
    # x_end != npos: marbles5 and marbles6 didn't boot). "_x" -> "X".
    svg = re.sub(r'(\sid=")([^"]*)"', lambda m: m.group(1) + m.group(2).replace('_x', 'X') + '"', svg)
    if re.search(r'\sid="[^"]*_x', svg):
        raise SystemExit('an id still contains _x')
    svg = split_subpaths(svg)
    bright, dim = attrs(svg, 'loop_t'), attrs(svg, 'loopX')
    on, off = attrs(svg, 'src_scale'), attrs(svg, 'src_keys')
    checks = [
        (bright.get('stroke', '').upper(), '#00EE92', 'loop_t stroke (locked: green)'),
        (dim.get('stroke', '').upper(), '#13704D', 'loopX stroke (on: dim green)'),
        (on.get('stroke', '').upper(), '#DED7FF', 'src_scale stroke (selected)'),
        (off.get('stroke', '').upper(), '#514959', 'src_keys stroke (not selected)'),
    ]
    if re.search(r'<g [^>]*transform=', svg):
        raise SystemExit('a group has a transform: driven elements must use screen coordinates')
    for value, expected, what in checks:
        if value != expected:
            raise SystemExit('%s is %s, expected %s' % (what, value, expected))
    # The OP-1 stops at boot on a malformed file (seq5): parse it here first.
    try:
        ET.fromstring(svg)
    except ET.ParseError as e:
        raise SystemExit('not well-formed XML: %s' % e)
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write(svg)


if __name__ == '__main__':
    main()
