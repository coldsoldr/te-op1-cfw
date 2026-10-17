#!/usr/bin/env python3
"""Sample-by-sample comparison of two harness output directories (*.bin).

Per scenario: the fraction of samples whose gates agree (per T channel), and
per voltage channel the fraction of samples within 1 mV, the max |diff|, and
the first sample index where the outputs part by more than 1 mV. A quick
check; tests/compare.cc does the event-aligned and statistical comparison.

Usage: compare_bin.py DIR_A DIR_B
"""
import os
import struct
import sys

REC = struct.Struct("<B4f")


def main():
    a_dir, b_dir = sys.argv[1], sys.argv[2]
    for name in sorted(os.listdir(a_dir)):
        if not name.endswith(".bin"):
            continue
        a = open(os.path.join(a_dir, name), "rb").read()
        b = open(os.path.join(b_dir, name), "rb").read()
        n = min(len(a), len(b)) // REC.size
        gate_ok = [0, 0, 0]
        v_ok = [0, 0, 0, 0]
        v_max = [0.0] * 4
        first = [None] * 4
        for i in range(n):
            ra = REC.unpack_from(a, i * REC.size)
            rb = REC.unpack_from(b, i * REC.size)
            for k in range(3):
                if (ra[0] >> k & 1) == (rb[0] >> k & 1):
                    gate_ok[k] += 1
            for k in range(4):
                d = abs(ra[1 + k] - rb[1 + k])
                if d <= 1e-3:
                    v_ok[k] += 1
                elif first[k] is None:
                    first[k] = i
                if d > v_max[k]:
                    v_max[k] = d
        print("%-20s gates %s  volts<=1mV %s  max %s  first>1mV %s" % (
            name[:-4],
            " ".join("%.4f" % (g / n) for g in gate_ok),
            " ".join("%.4f" % (v / n) for v in v_ok),
            " ".join("%.3g" % m for m in v_max),
            first))


if __name__ == "__main__":
    main()
