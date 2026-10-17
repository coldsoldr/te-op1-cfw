#!/usr/bin/env python3
"""Instruction counts per function from a bfin-elf-run --trace-insn trace.

Usage: profile_trace.py TRACE.txt PROGRAM.dis [--top N]
(PROGRAM.dis from bfin-elf-objdump -d -C; function starts are its
"<name>:" lines.) Counts instructions, not cycles (the simulator's cycle
count adds stalls), which is enough to find the hot spots.
"""
import bisect
import collections
import re
import sys


def main():
    trace, dis = sys.argv[1], sys.argv[2]
    top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 30
    starts, names = [], []
    for line in open(dis, errors="replace"):
        m = re.match(r"^([0-9a-f]+) <(.*)>:$", line)
        if m:
            starts.append(int(m.group(1), 16))
            names.append(m.group(2))
    order = sorted(range(len(starts)), key=lambda i: starts[i])
    starts = [starts[i] for i in order]
    names = [names[i] for i in order]
    counts = collections.Counter()
    total = 0
    rx = re.compile(r"insn:\s+0x([0-9a-f]+)")
    with open(trace, errors="replace") as f:
        for line in f:
            m = rx.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
            i = bisect.bisect_right(starts, a) - 1
            counts[names[i] if i >= 0 else "?"] += 1
            total += 1
    print("total instructions: %d" % total)
    for name, c in counts.most_common(top):
        print("%10d %5.1f%%  %s" % (c, 100.0 * c / total, name[:110]))


if __name__ == "__main__":
    main()
