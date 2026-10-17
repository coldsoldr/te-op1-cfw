#!/usr/bin/env python3
"""Check that the Blackfin render printed the same checksums as the host
render, and summarise the simulated cycles per OP-1 LFO call (tick).

Usage: check_bench.py HOST.txt BFIN.txt
"""
import re
import sys

TICK_RATE = 44100.0 / 64.0


def checksums(path):
    out = {}
    for line in open(path):
        m = re.match(r"\s*(\w+)\s+(\S+)\s+ticks\s+(\d+)\s+checksum\s+([0-9a-f]{8})(.*)", line)
        if m:
            out[m.group(2)] = (m.group(4), m.group(5))
        m = re.match(r"\s*(\w+)\s+all scenarios checksum\s+([0-9a-f]{8})", line)
        if m:
            out["ALL"] = (m.group(2), "")
    return out


def main():
    host = checksums(sys.argv[1])
    bfin = checksums(sys.argv[2])
    bad = 0
    worst_avg = worst_max = 0.0
    for name, (h, _) in host.items():
        b = bfin.get(name)
        if b is None or b[0] != h:
            print("MISMATCH %-16s host %s bfin %s" % (name, h, b[0] if b else None))
            bad += 1
        if b:
            m = re.search(r"avg\s+(\d+)\s+max\s+(\d+)", b[1])
            if m:
                worst_avg = max(worst_avg, float(m.group(1)))
                worst_max = max(worst_max, float(m.group(2)))
    print("checksums: %d scenarios, %d mismatches (Blackfin vs host)" % (len(host), bad))
    if worst_avg:
        print("cycles per tick (simulator): worst scenario average %.0f, worst single tick %.0f" % (
            worst_avg, worst_max))
        print("  at %.4f ticks/s: %.3f MHz average, %.3f MHz if every tick were the worst;"
              " device ~1.6x: %.3f / %.3f MHz" % (
                  TICK_RATE, worst_avg * TICK_RATE / 1e6, worst_max * TICK_RATE / 1e6,
                  1.6 * worst_avg * TICK_RATE / 1e6, 1.6 * worst_max * TICK_RATE / 1e6))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
