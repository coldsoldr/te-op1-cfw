#!/usr/bin/env python3
"""Make a device SVG for one of the new effect screens from its design.

    python gen_fx_svg.py DESIGN.svg OUT.svg [--ids id1,id2,...]

The designs (docs/fx-briefs/designs/*.svg) are drawn in the subset the
OP-1's SVG parser takes; this checks them again and writes the device file
(docs/device-tests.md Tests 22-23 and the memory notes on the parser):
- the <metadata> block and any namespace other than SVG's go;
- elements: g, line, polyline, polygon, circle, ellipse, rect, path only;
- ids: ASCII, no "_x" (the parser decodes "_xHH_" escapes and asserts at
  boot without the closing "_", Parser.cpp:231), at most one "_" (it cuts
  "_..._" out of ids);
- the example transforms on elements go (the code sets every driven
  element's transform on its first frame); a transform on a group is
  refused (the OP-1 folds group transforms into the children);
- display="none" only on groups (proven there; not on circles);
- paths: absolute M, L, H, V, C, Z only, a single M (the OP-1 joins
  subpaths with a line), no arcs;
- numbers: no exponents (Parser.cpp:103), at most 2 decimals (Parser.cpp:116);
- polylines and polygons: no stroke that doubles straight back on itself
  (a turn under 10 degrees); the OP-1 draws those wrong (Test 40: the
  snowflakes, stems and H bars of plate, hall, ensemble and shift);
- every element closed with its own end tag, as the stock files are.
It also warns about content outside the viewing-angle safe area
(x 16-296, y 8-140: docs/fx-briefs/README.md), and with --ids checks that
the ids the screen code needs are all there (a missing id would leave the
screen static).
"""

import math
import re
import sys
import xml.etree.ElementTree as ET

SVG_NS = 'http://www.w3.org/2000/svg'
ALLOWED = {'svg', 'g', 'line', 'polyline', 'polygon', 'circle', 'ellipse', 'rect', 'path'}
SAFE = (16, 8, 296, 140)
NUMBER_ATTRS = {'x', 'y', 'x1', 'y1', 'x2', 'y2', 'cx', 'cy', 'r', 'rx', 'ry', 'width', 'height',
                'stroke-width'}
NUM = re.compile(r'-?\d*\.?\d+(?:[eE][-+]?\d+)?')


def fail(msg):
    raise SystemExit('gen_fx_svg: ' + msg)


def local(tag):
    return tag.split('}', 1)[1] if '}' in tag else tag


def fmt(v):
    s = ('%.2f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def round_numbers(text, where):
    def one(m):
        t = m.group(0)
        if 'e' in t or 'E' in t:
            fail('%s: exponent in %r' % (where, t))
        return fmt(float(t))
    return NUM.sub(one, text)


def check_path(d, where):
    if re.search(r'[a-zA-Z]', d.replace('M', '').replace('L', '').replace('H', '').replace('V', '')
                 .replace('C', '').replace('Z', '')):
        fail('%s: path commands other than absolute M L H V C Z: %s' % (where, d[:60]))
    if d.count('M') != 1:
        fail('%s: %d subpaths (one M per path)' % (where, d.count('M')))


def check_turns(points, closed, where):
    nums = [float(n) for n in NUM.findall(points)]
    p = list(zip(nums[0::2], nums[1::2]))
    if closed:
        p = p + p[:2]
    for a, b, c in zip(p, p[1:], p[2:]):
        v1 = (a[0] - b[0], a[1] - b[1])
        v2 = (c[0] - b[0], c[1] - b[1])
        n1, n2 = math.hypot(*v1), math.hypot(*v2)
        if n1 == 0 or n2 == 0:
            continue
        cos = max(-1.0, min(1.0, (v1[0] * v2[0] + v1[1] * v2[1]) / (n1 * n2)))
        if math.degrees(math.acos(cos)) < 10:
            fail('%s: the stroke doubles back at (%s, %s)' % (where, fmt(b[0]), fmt(b[1])))


def points_of(el):
    """Rough coordinates of an element, for the safe-area warning."""
    tag = local(el.tag)
    a = el.attrib
    f = lambda k: float(a.get(k, 0))
    if tag in ('polyline', 'polygon'):
        nums = [float(n) for n in NUM.findall(a.get('points', ''))]
        return list(zip(nums[0::2], nums[1::2]))
    if tag == 'line':
        return [(f('x1'), f('y1')), (f('x2'), f('y2'))]
    if tag == 'rect':
        return [(f('x'), f('y')), (f('x') + f('width'), f('y') + f('height'))]
    if tag == 'circle':
        return [(f('cx') - f('r'), f('cy') - f('r')), (f('cx') + f('r'), f('cy') + f('r'))]
    if tag == 'ellipse':
        return [(f('cx') - f('rx'), f('cy') - f('ry')), (f('cx') + f('rx'), f('cy') + f('ry'))]
    if tag == 'path':
        cmds = re.findall(r'([MLHVCZ])([^MLHVCZ]*)', a.get('d', ''))
        pts, x, y = [], 0.0, 0.0
        for c, args in cmds:
            n = [float(v) for v in NUM.findall(args)]
            if c in 'MLC':
                for i in range(0, len(n) - 1, 2):
                    pts.append((n[i], n[i + 1]))
                if len(n) >= 2:
                    x, y = n[-2], n[-1]
            elif c == 'H':
                for v in n:
                    pts.append((v, y))
                    x = v
            elif c == 'V':
                for v in n:
                    pts.append((x, v))
                    y = v
        return pts
    return []


def convert(src, ids_needed):
    text = open(src, encoding='utf-8').read()
    text = re.sub(r'<metadata>.*?</metadata>', '', text, flags=re.S)
    root = ET.fromstring(text)
    if local(root.tag) != 'svg':
        fail('not an svg')
    if root.get('viewBox') != '0 0 320 160':
        fail('viewBox must be "0 0 320 160"')
    seen = set()
    warnings = []
    for el in root.iter():
        tag = local(el.tag)
        if tag not in ALLOWED:
            fail('element <%s> not supported on the OP-1' % tag)
        ident = el.get('id')
        where = '<%s id=%s>' % (tag, ident)
        if ident is not None:
            if not ident.isascii() or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', ident):
                fail('%s: id must be ASCII letters, digits, at most one _' % where)
            if '_x' in ident or ident.count('_') > 1:
                fail('%s: no "_x" and at most one "_" in ids' % where)
            if ident in seen:
                fail('%s: duplicate id' % where)
            seen.add(ident)
        if 'transform' in el.attrib:
            if tag in ('g', 'svg'):
                fail('%s: a group transform (the OP-1 folds it into the children)' % where)
            del el.attrib['transform']   # an example state: the code sets it
        if 'display' in el.attrib and tag not in ('g', 'svg'):
            fail('%s: display only on groups' % where)
        for k in list(el.attrib):
            if local(k) != k:
                del el.attrib[k]   # foreign attributes
        if tag == 'path':
            d = el.get('d', '')
            check_path(d, where)
            el.set('d', round_numbers(d, where))
        if tag in ('polyline', 'polygon'):
            el.set('points', round_numbers(el.get('points', ''), where))
            check_turns(el.get('points'), tag == 'polygon', where)
        for k in NUMBER_ATTRS & set(el.attrib):
            el.set(k, round_numbers(el.get(k), where))
        if tag not in ('svg', 'g'):
            for x, y in points_of(el):
                if not (SAFE[0] - 1 <= x <= SAFE[2] + 1 and SAFE[1] - 1 <= y <= SAFE[3] + 1):
                    warnings.append('%s: (%s, %s) outside the safe area' % (where, fmt(x), fmt(y)))
                    break
    missing = [i for i in ids_needed if i not in seen]
    if missing:
        fail('ids the screen code needs are missing: ' + ', '.join(missing))
    ET.register_namespace('', SVG_NS)
    body = ET.tostring(root, encoding='unicode', short_empty_elements=False)
    out = '<?xml version="1.0"?>' + body
    ET.fromstring(out[len('<?xml version="1.0"?>'):])   # well formed
    return out, warnings


def main():
    args = sys.argv[1:]
    ids = []
    if '--ids' in args:
        i = args.index('--ids')
        ids = [s for s in args[i + 1].split(',') if s]
        del args[i:i + 2]
    if len(args) != 2:
        fail('usage: gen_fx_svg.py DESIGN.svg OUT.svg [--ids a,b,...]')
    out, warnings = convert(args[0], ids)
    for w in warnings:
        print('warning: ' + w)
    open(args[1], 'w', encoding='utf-8', newline='\n').write(out)
    print('wrote %s (%d bytes, %d warnings)' % (args[1], len(out), len(warnings)))


if __name__ == '__main__':
    main()
