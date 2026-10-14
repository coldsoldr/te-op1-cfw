"""Stats and comparison for harness WAV files (float32 or int16 stereo); stdlib only.

  compare.py stats A.wav [B.wav ...]   peak, RMS, silence check per file
  compare.py diff REF.wav TEST.wav     SNR of TEST against REF, per channel
  compare.py dirdiff REFDIR TESTDIR    diff every WAV present in both directories
  compare.py spec REF.wav TEST.wav     per-frame band-level difference (phase-blind)
  compare.py dirspec REFDIR TESTDIR    spec for every WAV present in both directories
"""
import array
import math
import os
import struct
import sys


def read_wav(path):
    """Returns (rate, [left, right]) as lists of floats in [-1, 1]."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError('%s: not a WAV file' % path)
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', body[:16])
        elif cid == b'data':
            samples = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    if tag == 3 and bits == 32:
        a = array.array('f'); a.frombytes(samples); scale = 1.0
    elif tag == 1 and bits == 16:
        a = array.array('h'); a.frombytes(samples); scale = 1.0 / 32768.0
    else:
        raise ValueError('%s: unsupported format tag %d bits %d' % (path, tag, bits))
    chans = [[v * scale for v in a[c::channels]] for c in range(channels)]
    return rate, chans


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))


def db(x):
    return 20.0 * math.log10(x) if x > 0 else -999.0


def stats(paths):
    for p in paths:
        rate, chans = read_wav(p)
        parts = []
        for name, x in zip('LR', chans):
            peak = max(abs(v) for v in x)
            parts.append('%s peak %6.3f (%6.1f dBFS) rms %6.1f dBFS' % (name, peak, db(peak), db(rms(x))))
        print('%-28s %d Hz %5.2f s  %s' % (os.path.basename(p), rate, len(chans[0]) / rate, ' | '.join(parts)))


def diff(ref_path, test_path):
    r_rate, ref = read_wav(ref_path)
    t_rate, test = read_wav(test_path)
    if r_rate != t_rate:
        return '%s: rate mismatch %d vs %d' % (os.path.basename(test_path), r_rate, t_rate)
    out = []
    for name, a, b in zip('LR', ref, test):
        n = min(len(a), len(b))
        err = [b[i] - a[i] for i in range(n)]
        sig = rms(a[:n])
        e = rms(err)
        snr = db(sig / e) if e > 0 else float('inf')
        out.append('%s SNR %6.1f dB maxerr %.4f' % (name, snr, max(abs(v) for v in err)))
    return '%-28s %s' % (os.path.basename(test_path), ' | '.join(out))


def fft(x):
    """In-place iterative radix-2 FFT of a list of complex numbers."""
    n = len(x)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    size = 2
    while size <= n:
        w = complex(math.cos(-2 * math.pi / size), math.sin(-2 * math.pi / size))
        half = size // 2
        for start in range(0, n, size):
            wk = 1.0 + 0j
            for k in range(start, start + half):
                t = wk * x[k + half]
                x[k + half] = x[k] - t
                x[k] = x[k] + t
                wk *= w
        size *= 2
    return x


def band_energies(x, frame, bands):
    """Per-frame energy in log-spaced bands (dB), Hann-windowed."""
    window = [0.5 - 0.5 * math.cos(2 * math.pi * i / frame) for i in range(frame)]
    out = []
    for start in range(0, len(x) - frame + 1, frame):
        spec = fft([complex(x[start + i] * window[i], 0.0) for i in range(frame)])
        power = [abs(c) ** 2 for c in spec[:frame // 2]]
        out.append([10.0 * math.log10(sum(power[lo:hi]) + 1e-12) for lo, hi in bands])
    return out


def spectral(ref_path, test_path, frame=2048):
    """Compares the per-frame spectra in third-octave-ish bands.

    Reports the mean absolute band level difference (dB) over bands that are
    within 60 dB of the frame's loudest band, which ignores phase and is closer
    to what is audible than a waveform SNR.
    """
    r_rate, ref = read_wav(ref_path)
    _, test = read_wav(test_path)
    edges = [0]
    f = 40.0
    while f < r_rate / 2:
        edges.append(min(frame // 2, int(f * frame / r_rate)))
        f *= 2 ** (1.0 / 3)
    edges.append(frame // 2)
    bands = [(lo, hi) for lo, hi in zip(edges, edges[1:]) if hi > lo]
    out = []
    for name, a, b in zip('LR', ref, test):
        ea = band_energies(a, frame, bands)
        eb = band_energies(b, frame, bands)
        diffs = []
        for fa, fb in zip(ea, eb):
            top = max(fa)
            diffs += [abs(x - y) for x, y in zip(fa, fb) if x > top - 60.0]
        diffs.sort()
        mean = sum(diffs) / max(1, len(diffs))
        p95 = diffs[int(0.95 * (len(diffs) - 1))] if diffs else 0.0
        out.append('%s band diff mean %5.2f dB p95 %5.2f dB' % (name, mean, p95))
    return '%-28s %s' % (os.path.basename(test_path), ' | '.join(out))


def main(argv):
    if len(argv) >= 3 and argv[1] == 'stats':
        stats(argv[2:])
    elif len(argv) == 4 and argv[1] == 'diff':
        print(diff(argv[2], argv[3]))
    elif len(argv) == 4 and argv[1] in ('dirdiff', 'dirspec'):
        compare = diff if argv[1] == 'dirdiff' else spectral
        for name in sorted(os.listdir(argv[3])):
            ref = os.path.join(argv[2], name)
            if name.endswith('.wav') and os.path.exists(ref):
                print(compare(ref, os.path.join(argv[3], name)))
    elif len(argv) == 4 and argv[1] == 'spec':
        print(spectral(argv[2], argv[3]))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
