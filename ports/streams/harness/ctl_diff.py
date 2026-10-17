"""Inspects the control traces of one scenario (stdlib only).

  ctl_diff.py REF.ctl PORT.ctl [threshold_db]
      prints the regions where the VCA gain traces differ by more than the
      threshold (default 6 dB) while the reference is above -40 dB, with a
      few samples of both traces, and summary statistics of both traces.
"""
import array
import sys


def read(path):
    a = array.array('f')
    with open(path, 'rb') as f:
        a.frombytes(f.read())
    return list(a[0::2]), list(a[1::2])


def stats(name, x):
    v = [s for s in x if s > -119]
    if not v:
        print('%s: empty' % name)
        return
    mean = sum(v) / len(v)
    sd = (sum((s - mean) ** 2 for s in v) / len(v)) ** 0.5
    print('%-10s mean %8.2f sd %7.2f min %8.2f max %8.2f (n %d)' % (name, mean, sd, min(v), max(v), len(v)))


def main(argv):
    rg, rc = read(argv[1])
    pg, pc = read(argv[2])
    thr = float(argv[3]) if len(argv) > 3 else 6.0
    n = min(len(rg), len(pg))
    stats('ref gain', rg[:n]); stats('port gain', pg[:n])
    stats('ref cut', [c for c in rc[:n] if c >= 0]); stats('port cut', [c for c in pc[:n] if c >= 0])
    i = 0
    shown = 0
    while i < n and shown < 12:
        if rg[i] > -40 and abs(pg[i] - rg[i]) > thr:
            j = i
            while j < n and abs(pg[j] - rg[j]) > thr:
                j += 1
            print('frames %d..%d (%.4f..%.4f s), %d frames' % (i, j, i / 44100.0, j / 44100.0, j - i))
            for k in range(max(0, i - 3), min(n, i + 4)):
                print('   %7d ref %8.2f port %8.2f' % (k, rg[k], pg[k]))
            shown += 1
            i = j
        i += 1


if __name__ == '__main__':
    main(sys.argv)
