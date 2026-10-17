#!/usr/bin/env python3
"""Summarise harness output files (*.bin from harness/render.cc).

Per file: gate duty and rising edges per channel (T1, T2, T3), and per
voltage channel (X1, X2, X3, Y): min, max, mean and the number of distinct
values. Optionally dumps the first N records as CSV.

Usage: inspect_bin.py FILE.bin [FILE.bin ...] [--csv N]
"""
import struct
import sys

REC = struct.Struct("<B4f")


def summarise(path, csv_rows=0):
    data = open(path, "rb").read()
    n = len(data) // REC.size
    duty = [0, 0, 0]
    edges = [0, 0, 0]
    prev = 0
    vmin = [1e9] * 4
    vmax = [-1e9] * 4
    vsum = [0.0] * 4
    distinct = [set() for _ in range(4)]
    for i in range(n):
        g, a, b, c, d = REC.unpack_from(data, i * REC.size)
        if i < csv_rows:
            print("%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f" % (i, g & 1, (g >> 1) & 1, (g >> 2) & 1, a, b, c, d))
        for k in range(3):
            if g >> k & 1:
                duty[k] += 1
                if not (prev >> k & 1):
                    edges[k] += 1
        prev = g
        for k, v in enumerate((a, b, c, d)):
            vmin[k] = min(vmin[k], v)
            vmax[k] = max(vmax[k], v)
            vsum[k] += v
            if len(distinct[k]) < 100000:
                distinct[k].add(v)
    name = path.replace("\\", "/").split("/")[-1]
    print("%-20s n=%d  gates duty %s edges %s" % (
        name, n, " ".join("%.3f" % (x / n) for x in duty), edges))
    for k, lab in enumerate(("X1", "X2", "X3", "Y")):
        print("    %-2s min %8.4f max %8.4f mean %8.4f distinct %d" % (
            lab, vmin[k], vmax[k], vsum[k] / n, len(distinct[k])))


def main():
    args = sys.argv[1:]
    csv_rows = 0
    if "--csv" in args:
        i = args.index("--csv")
        csv_rows = int(args[i + 1])
        del args[i:i + 2]
    for p in args:
        summarise(p, csv_rows)


if __name__ == "__main__":
    main()
