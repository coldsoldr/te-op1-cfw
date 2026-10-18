#!/usr/bin/env python3
"""Code footprint of a traced simulator run (ports/*/tests/hot_set.cc).

    bfin-elf-run --trace-insn=on prog.elf 2>&1 | grep -o 'insn: *0x[0-9a-f]*' > trace.txt
    bfin-elf-nm -n prog.elf > nm.txt
    python tools/hot_set.py trace.txt nm.txt

The trace is split at the calls of HotSetMarker; for each segment between
two markers (one effect call, or one block of voices) the distinct 32-byte
code lines executed are counted: that's what the OP-1's 16 KB instruction
cache (512 lines) must hold for the call to run without fetch misses. The
lines are attributed to the functions holding them (nm -n), largest first.
"""

import re
import sys


def main():
    trace, nm = sys.argv[1], sys.argv[2]
    symbols = []
    marker = None
    for line in open(nm):
        parts = line.split()
        if len(parts) != 3 or parts[1] not in 'tTwW':
            continue
        address, name = int(parts[0], 16), parts[2]
        symbols.append((address, name))
        if name == '_HotSetMarker':
            marker = address
    symbols.sort()
    if marker is None:
        raise SystemExit('no _HotSetMarker in the nm output')

    segments = []
    current = None
    for line in open(trace):
        m = re.search(r'0x([0-9a-f]+)', line)
        if not m:
            continue
        pc = int(m.group(1), 16)
        if pc == marker:
            if current is not None:
                segments.append(current)
            current = set()
            continue
        if current is not None:
            current.add(pc >> 5)
    if len(segments) < 2:
        raise SystemExit('fewer than two marked segments')

    def function_of(line_address):
        lo, hi = 0, len(symbols) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if symbols[mid][0] <= line_address:
                lo = mid
            else:
                hi = mid - 1
        return symbols[lo][1]

    union = set()
    for s in segments:
        union |= s
    sizes = [len(s) for s in segments]
    print('segments: %d; code lines per segment: %s; union %d lines = %.1f KB (cache: 512 lines)'
          % (len(segments), sizes, len(union), len(union) * 32 / 1024.0))
    per_function = {}
    for l in union:
        per_function[function_of(l << 5)] = per_function.get(function_of(l << 5), 0) + 1
    for name, n in sorted(per_function.items(), key=lambda kv: -kv[1])[:40]:
        print('%5d lines %6.1f KB  %s' % (n, n * 32 / 1024.0, name))


if __name__ == '__main__':
    main()
