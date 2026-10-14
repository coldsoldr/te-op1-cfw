"""Makes the package's rings.svg from the Rings screen design
(incoming/rings.svg, spec incoming/rings-handover.md), adding what
op1/screen.cc needs (stdlib only):

  - drops the <metadata> block (unused, and most of the file);
  - replaces the #pluck polyline with 24 variants pluck0..pluck23, middle
    vertex at x = 16 + 4 i (the OP-1 has no call to move a polyline point;
    the screen shows one variant);
  - gives each model / polyphony button's box an id (model0box, poly0box) and
    wraps its glyph in a group (model0num, poly0dots), so the box and the
    glyph can take different colours (GetLineStyle on a group gives all its
    children one style).

Everything else is copied as is.

  python op1/gen_rings_svg.py ../../incoming/rings.svg op1/rings.svg
"""
import re
import sys
import xml.dom.minidom

PLUCK_VARIANTS = 24       # must match kPluckVariants (op1/screen_math.h)

REQUIRED_IDS = (['ring%d' % i for i in range(6)] + ['core', 'brightness', 'damping', 'posdot'] +
                ['pluck%d' % i for i in range(PLUCK_VARIANTS)] +
                ['model%dbox' % i for i in range(6)] + ['model%dnum' % i for i in range(6)] +
                ['poly%dbox' % i for i in range(3)] + ['poly%ddots' % i for i in range(3)])


def convert(svg):
    svg, n = re.subn(r'<metadata>.*?</metadata>', '', svg, flags=re.S)
    if n != 1:
        raise ValueError('expected one <metadata> block, found %d' % n)
    svg = svg.replace(' xmlns:c2pa="http://c2pa.org/manifest"', '')

    pluck = re.search(r'<polyline id="pluck" ([^>]*)points="16,130 44,125 108,130"></polyline>', svg)
    if not pluck:
        raise ValueError('#pluck polyline not found')
    style = pluck.group(1)
    variants = ''.join('<polyline id="pluck%d" %spoints="16,130 %d,125 108,130"></polyline>'
                       % (i, style, 16 + 4 * i) for i in range(PLUCK_VARIANTS))
    svg = svg.replace(pluck.group(0), '<g id="pluck">' + variants + '</g>')

    def button(m):
        group, rect, glyph = m.group(1), m.group(2), m.group(3)
        suffix = 'num' if group.startswith('model') else 'dots'
        rect = rect.replace('<rect ', '<rect id="%sbox" ' % group, 1)
        return '<g id="%s">%s<g id="%s%s">%s</g></g>' % (group, rect, group, suffix, glyph)

    svg, n = re.subn(r'<g id="((?:model|poly)\d)">(<rect [^>]*></rect>)(.*?)</g>', button, svg)
    if n != 9:
        raise ValueError('expected 9 model/poly buttons, found %d' % n)
    return svg


def check(svg):
    dom = xml.dom.minidom.parseString(svg.encode('utf-8'))   # well-formed
    ids = {e.getAttribute('id') for e in dom.getElementsByTagName('*') if e.hasAttribute('id')}
    missing = [i for i in REQUIRED_IDS if i not in ids]
    if missing:
        raise ValueError('missing ids: %s' % ', '.join(missing))
    tags = {e.tagName for e in dom.getElementsByTagName('*')}
    supported = {'svg', 'g', 'line', 'polyline', 'polygon', 'ellipse', 'circle', 'rect', 'path'}
    if tags - supported:
        raise ValueError('elements the OP-1 parser lacks: %s' % ', '.join(sorted(tags - supported)))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    svg = open(sys.argv[1], encoding='utf-8').read()
    out = convert(svg)
    check(out)
    with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as f:
        f.write(out)
    print('wrote %s: %d bytes, %d required ids present' % (sys.argv[2], len(out.encode('utf-8')),
                                                          len(REQUIRED_IDS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
