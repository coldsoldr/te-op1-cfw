#!/usr/bin/env python3
"""Instructions per function from a bfin-elf-run instruction trace.

    bfin-elf-run --trace-insn=on ELF ... 2> trace.txt
    python3 harness/profile.py ELF trace.txt [NM]

Counts trace lines per PC, maps each PC to the enclosing symbol (nm -n -C),
and prints the functions by instruction count. Instructions, not cycles:
stalls (multiplies, taken branches, loads) come on top.
"""

import bisect
import collections
import re
import subprocess
import sys


def symbols(elf, nm):
    out = subprocess.run([nm, '-n', '-C', elf], capture_output=True, text=True, check=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        parts = line.split(' ', 2)
        if len(parts) == 3 and parts[1] in 'tTwW':
            addrs.append(int(parts[0], 16))
            names.append(parts[2])
    return addrs, names


def main():
    elf, trace = sys.argv[1], sys.argv[2]
    nm = sys.argv[3] if len(sys.argv) > 3 else 'bfin-elf-nm'
    addrs, names = symbols(elf, nm)
    pc_re = re.compile(r'0x([0-9a-f]+)')
    counts = collections.Counter()
    total = 0
    with open(trace, errors='replace') as f:
        for line in f:
            if not line.startswith('insn:'):
                continue
            m = pc_re.search(line)
            if not m:
                continue
            pc = int(m.group(1), 16)
            i = bisect.bisect_right(addrs, pc) - 1
            counts[names[i] if i >= 0 else '?'] += 1
            total += 1
    print('total instructions: {}'.format(total))
    for name, n in counts.most_common(30):
        print('{:10d} {:5.1f}%  {}'.format(n, 100.0 * n / max(total, 1), name[:100]))


if __name__ == '__main__':
    main()
