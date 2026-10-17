"""Per-segment SNR of a test WAV against a reference (stdlib only), to tell
a steady error from a slow divergence (chaotic feedback, phase drift).

  segments.py REF.wav TEST.wav [segment_ms]
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'rings', 'harness'))
import compare  # noqa: E402


def main(argv):
    seg_ms = float(argv[3]) if len(argv) > 3 else 100.0
    rate, ref = compare.read_wav(argv[1])
    _, test = compare.read_wav(argv[2])
    n = int(rate * seg_ms / 1000.0)
    for name, a, b in zip('LR', ref, test):
        parts = []
        for start in range(0, min(len(a), len(b)) - n + 1, n):
            sig = compare.rms(a[start:start + n])
            err = compare.rms([b[i] - a[i] for i in range(start, start + n)])
            parts.append('%5.1f' % (compare.db(sig / err) if err > 0 else 999.0))
        print('%s %s' % (name, ' '.join(parts)))


if __name__ == '__main__':
    main(sys.argv)
