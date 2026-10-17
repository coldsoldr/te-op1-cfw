"""Markdown cycle table from `make bench` output (stdlib only).

  cycle_table.py BENCH_LOG
      per scenario: simulated cycles per 64-sample call (avg / max) and MHz
      at 689 calls/s, mono and stereo, 44.1 kHz and 22.05 kHz profiles, and
      the device estimate (x1.6, docs/HANDOVER.md §2) of the peak.
"""
import re
import sys

rows = {}
order = []
for line in open(sys.argv[1]):
    m = re.match(r'CYC core (44k|22k) (\S+)\s+(mono|stereo)\s+avg\s+(\d+) max\s+(\d+).*?\[detect (\d+) proc (\d+) coef (\d+) audio (\d+)\]', line)
    if not m:
        continue
    prof, name, kind, avg, mx = m.group(1), m.group(2), m.group(3), int(m.group(4)), int(m.group(5))
    if name not in rows:
        rows[name] = {}
        order.append(name)
    rows[name][(prof, kind)] = (avg, mx, [int(m.group(i)) for i in range(6, 10)])


def mhz(c):
    return c * 44100.0 / 64 / 1e6


print('| Scenario | 44.1k mono avg/max kcyc | 44.1k stereo | 22.05k mono | 22.05k stereo | device est. peak, MHz (44.1k mono / stereo; 22.05k mono / stereo) |')
print('|---|---|---|---|---|---|')
for name in order:
    r = rows[name]
    cells = []
    peaks = []
    for key in [('44k', 'mono'), ('44k', 'stereo'), ('22k', 'mono'), ('22k', 'stereo')]:
        if key in r:
            avg, mx, _ = r[key]
            cells.append('%.1f / %.1f' % (avg / 1000.0, mx / 1000.0))
            peaks.append('%.0f' % (mhz(mx) * 1.6))
        else:
            cells.append('-')
            peaks.append('-')
    print('| %s | %s | %s |' % (name, ' | '.join(cells), ' / '.join(peaks)))

print()
print('Stage breakdown (44.1k mono, cycles per call): detect / processor / gains+coefficients / VCA+filter+mix')
for name in order:
    r = rows[name]
    if ('44k', 'mono') in r:
        d = r[('44k', 'mono')][2]
        print('  %-22s %6d %6d %6d %6d' % (name, d[0], d[1], d[2], d[3]))
