#!/usr/bin/env python3
"""Make the device's clouds.svg from the user's design (the weather theme).

    python gen_clouds_svg.py DESIGN.svg OUT.svg

The design is kept as drawn, with what the OP-1's SVG parser needs:
- the <metadata> block (a content-credentials blob) and its namespace go;
- path data is rewritten with absolute M, L, C and Z only: the design uses
  elliptical arcs (A), which no stock SVG does (the parser is assumed not
  to know them), so each arc becomes cubic Beziers of at most 90 degrees;
- page2ind's fill "none" becomes #010101 (the design's mask colour, black
  on the screen): a fill colour can only be changed where the file gives
  one (op1/clouds_screen.cc lights the indicator by writing it).
"""

import math
import re
import sys

NUMBER = r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?'


def fmt(x):
    s = '%.2f' % x
    s = s.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def arc_to_cubics(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    """SVG arc (endpoint form) to cubic segments [(c1x, c1y, c2x, c2y, x, y)]."""
    if (x1, y1) == (x2, y2):
        return []
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        return [(x1, y1, x2, y2, x2, y2)]
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1p / rx) ** 2 + (y1p / ry) ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den))
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def angle(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(1, int(math.ceil(abs(dt) / (math.pi / 2) - 1e-9)))
    step = dt / n
    k = 4.0 / 3.0 * math.tan(step / 4)

    def point(t):
        x, y = rx * math.cos(t), ry * math.sin(t)
        return cp * x - sp * y + cx, sp * x + cp * y + cy

    def deriv(t):
        x, y = -rx * math.sin(t), ry * math.cos(t)
        return cp * x - sp * y, sp * x + cp * y

    out = []
    t = t1
    for i in range(n):
        p0, p3 = point(t), point(t + step)
        d0, d3 = deriv(t), deriv(t + step)
        out.append((p0[0] + k * d0[0], p0[1] + k * d0[1],
                    p3[0] - k * d3[0], p3[1] - k * d3[1],
                    p3[0], p3[1]))
        t += step
    # Land exactly on the endpoint.
    c = out[-1]
    out[-1] = (c[0], c[1], c[2], c[3], x2, y2)
    return out


def absolute_path(d):
    tokens = re.findall(r'[A-Za-z]|' + NUMBER, d)
    out = []
    i = 0
    cmd = None
    x = y = sx = sy = 0.0

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    while i < len(tokens):
        if re.match(r'[A-Za-z]', tokens[i]):
            cmd = tokens[i]
            i += 1
        if cmd in 'Zz':
            out.append('Z')
            x, y = sx, sy
            continue
        if cmd in 'Mm':
            nx, ny = num(), num()
            if cmd == 'm':
                nx, ny = x + nx, y + ny
            x, y, sx, sy = nx, ny, nx, ny
            out.append('M%s,%s' % (fmt(x), fmt(y)))
            cmd = 'L' if cmd == 'M' else 'l'   # implicit lineto
        elif cmd in 'Ll':
            nx, ny = num(), num()
            if cmd == 'l':
                nx, ny = x + nx, y + ny
            x, y = nx, ny
            out.append('L%s,%s' % (fmt(x), fmt(y)))
        elif cmd in 'Hh':
            nx = num()
            x = x + nx if cmd == 'h' else nx
            out.append('L%s,%s' % (fmt(x), fmt(y)))
        elif cmd in 'Vv':
            ny = num()
            y = y + ny if cmd == 'v' else ny
            out.append('L%s,%s' % (fmt(x), fmt(y)))
        elif cmd in 'Cc':
            v = [num() for _ in range(6)]
            if cmd == 'c':
                v = [v[0] + x, v[1] + y, v[2] + x, v[3] + y, v[4] + x, v[5] + y]
            x, y = v[4], v[5]
            out.append('C' + ','.join(fmt(t) for t in v))
        elif cmd in 'Aa':
            rx, ry, phi, large, sweep, nx, ny = [num() for _ in range(7)]
            if cmd == 'a':
                nx, ny = x + nx, y + ny
            for c in arc_to_cubics(x, y, rx, ry, phi, int(large), int(sweep), nx, ny):
                out.append('C' + ','.join(fmt(t) for t in c))
            x, y = nx, ny
        else:
            raise SystemExit('unsupported path command %r in %r' % (cmd, d))
    return ''.join(out)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    svg = open(sys.argv[1], encoding='utf-8').read()
    svg = re.sub(r'<metadata>.*?</metadata>', '', svg, flags=re.S)
    svg = svg.replace(' xmlns:c2pa="http://c2pa.org/manifest"', '')
    svg = re.sub(r'(\sd=")([^"]*)(")', lambda m: m.group(1) + absolute_path(m.group(2)) + m.group(3), svg)
    svg, n = re.subn(r'(<rect id="page2ind" )fill="none"', r'\1fill="#010101"', svg)
    if n != 1:
        raise SystemExit('page2ind not found')
    if re.search(r'\sd="[^"]*[Aa]', svg):
        raise SystemExit('an arc is left')
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write(svg)


if __name__ == '__main__':
    main()
