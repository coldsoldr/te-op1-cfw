"""Per-scenario comparison of the port with the references (stdlib only).

  report.py OUTDIR [prefix ...]      e.g. report.py out 05 11

For every WAV in OUTDIR/port matching a prefix: waveform SNR against
OUTDIR/ref (upstream, float) and OUTDIR/ref64 (upstream with float ->
double, if rendered: `make ref64`), the first 100 ms SNR against ref, and
the mean / 95th percentile band-level difference (compare.py spec) against
ref64 (or ref). One line per file, L (OUT) and R (AUX).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'rings', 'harness'))
import compare  # noqa: E402


def snr(a, b, n=None):
    n = min(len(a), len(b)) if n is None else min(n, len(a), len(b))
    sig = compare.rms(a[:n])
    err = compare.rms([b[i] - a[i] for i in range(n)])
    if err == 0:
        return 999.0
    return compare.db(sig / err) if sig > 0 else -999.0


def main(argv):
    out = argv[1]
    prefixes = argv[2:] or ['']
    port_dir = os.path.join(out, 'port')
    for name in sorted(os.listdir(port_dir)):
        if not any(name.startswith(p) for p in prefixes):
            continue
        rate, port = compare.read_wav(os.path.join(port_dir, name))
        _, ref = compare.read_wav(os.path.join(out, 'ref', name))
        ref64_path = os.path.join(out, 'ref64', name)
        ref64 = compare.read_wav(ref64_path)[1] if os.path.exists(ref64_path) else None
        spec_ref = ref64_path if ref64 is not None else os.path.join(out, 'ref', name)
        spec = compare.spectral(spec_ref, os.path.join(port_dir, name))
        parts = []
        for ch, label in ((0, 'L'), (1, 'R')):
            s_ref = snr(ref[ch], port[ch])
            s_100 = snr(ref[ch], port[ch], rate // 10)
            s_64 = snr(ref64[ch], port[ch]) if ref64 is not None else float('nan')
            parts.append('%s %5.1f/%5.1f/%5.1f' % (label, s_ref, s_100, s_64))
        bands = spec.split(' L band diff ')[-1] if ' L band diff ' in spec else spec
        print('%-26s SNR ref/100ms/ref64 %s | bands %s' % (name[:-4], ' '.join(parts),
              bands.replace('band diff ', '').replace('mean ', '')))


if __name__ == '__main__':
    main(sys.argv)
