#!/usr/bin/env python3
"""Instructions per source line from a bfin-elf-run instruction trace,
for one function (or all): the hot lines of an inlined loop.

    python3 harness/profile_lines.py ELF trace.txt ADDR2LINE [START END]

START/END (hex) restrict to a PC range, e.g. one function from nm.
"""

import collections
import re
import subprocess
import sys


def main():
    elf, trace, addr2line = sys.argv[1], sys.argv[2], sys.argv[3]
    lo = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
    hi = int(sys.argv[5], 16) if len(sys.argv) > 5 else 1 << 32
    pc_re = re.compile(r'0x([0-9a-f]+)')
    counts = collections.Counter()
    with open(trace, errors='replace') as f:
        for line in f:
            if not line.startswith('insn:'):
                continue
            m = pc_re.search(line)
            if m:
                pc = int(m.group(1), 16)
                if lo <= pc < hi:
                    counts[pc] += 1
    pcs = sorted(counts)
    out = subprocess.run([addr2line, '-e', elf, '-i'] + ['0x%x' % pc for pc in pcs],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    # With -i, each address may print several lines (inline chain); the
    # first is the innermost location. Re-run without -i for a 1:1 map.
    out = subprocess.run([addr2line, '-e', elf] + ['0x%x' % pc for pc in pcs],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    by_line = collections.Counter()
    for pc, loc in zip(pcs, out):
        by_line[loc.split('/')[-1]] += counts[pc]
    total = sum(counts.values())
    print('instructions in range: {}'.format(total))
    for loc, n in by_line.most_common(40):
        print('{:10d} {:5.1f}%  {}'.format(n, 100.0 * n / max(total, 1), loc))


if __name__ == '__main__':
    main()
