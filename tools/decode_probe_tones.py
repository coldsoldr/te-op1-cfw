#!/usr/bin/env python3
"""Decode the probe build's report tones from a recording (docs/device-tests.md).

The probe repeats: tone 1 (clock MHz as Hz), gap, tone 2 (free MHz as Hz), gap,
tone 3 (100 * min voices rendered per block + 50 Hz), pause. This finds the
tone segments, measures each one's pitch, and prints the decoded values.

Any format ffmpeg can read works (phone recordings, AIFF from the OP-1 tape).

Usage:
    python tools/decode_probe_tones.py RECORDING [--ffmpeg PATH]
"""

import argparse
import array
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave

RATE = 16000


def load(path, ffmpeg):
    with tempfile.TemporaryDirectory() as tmp:
        wav_path = os.path.join(tmp, 'mono.wav')
        subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', path, '-ac', '1', '-ar', str(RATE),
                        '-sample_fmt', 's16', wav_path], check=True)
        with wave.open(wav_path) as w:
            samples = array.array('h', w.readframes(w.getnframes()))
    return [s / 32768.0 for s in samples]


def frame_rms(x, frame):
    return [math.sqrt(sum(v * v for v in x[i:i + frame]) / frame)
            for i in range(0, len(x) - frame + 1, frame)]


def pitch(x):
    """Fundamental by autocorrelation over 40-2000 Hz, refined by parabolic
    interpolation."""
    n = len(x)
    mean = sum(x) / n
    x = [v - mean for v in x]
    lo, hi = RATE // 2000, RATE // 40
    best_lag, best = None, 0.0
    energy = sum(v * v for v in x)
    corr = {}
    for lag in range(lo, hi + 2):
        c = sum(x[i] * x[i + lag] for i in range(0, n - lag, 2))
        corr[lag] = c
    # First strong peak (avoids octave errors on triangle waves).
    peak = max(corr[l] for l in range(lo, hi + 1))
    for lag in range(lo + 1, hi + 1):
        if corr[lag] > 0.8 * peak and corr[lag] >= corr[lag - 1] and corr[lag] >= corr[lag + 1]:
            best_lag = lag
            break
    if best_lag is None:
        return None
    a, b, c = corr[best_lag - 1], corr[best_lag], corr[best_lag + 1]
    shift = 0.5 * (a - c) / (a - 2 * b + c) if a - 2 * b + c else 0.0
    return RATE / (best_lag + shift)


def segments(x, frame=320):
    """Tone segments: runs of frames above a threshold relative to the loudest."""
    rms = frame_rms(x, frame)
    top = max(rms) if rms else 0
    on = [r > 0.25 * top for r in rms]
    segs, start = [], None
    for i, flag in enumerate(on + [False]):
        if flag and start is None:
            start = i
        elif not flag and start is not None:
            if (i - start) * frame >= 0.5 * RATE:   # tones last 1.5 s
                segs.append((start * frame, i * frame))
            start = None
    return segs


def main():
    parser = argparse.ArgumentParser(description='Decode probe report tones')
    parser.add_argument('recording')
    parser.add_argument('--ffmpeg', default=shutil.which('ffmpeg') or 'ffmpeg')
    args = parser.parse_args()
    x = load(args.recording, args.ffmpeg)
    print('{}: {:.1f} s'.format(args.recording, len(x) / RATE))
    found = []
    for s, e in segments(x):
        mid = (s + e) // 2
        chunk = x[max(s, mid - RATE // 4):min(e, mid + RATE // 4)]
        f = pitch(chunk)
        found.append((s / RATE, (e - s) / RATE, f))
        print('  tone at {:6.2f} s, {:.2f} s long: {}'.format(
            s / RATE, (e - s) / RATE, '{:.1f} Hz'.format(f) if f else 'no clear pitch'))
    # Group into report cycles: three tones ~2 s apart, then a longer pause.
    for i in range(len(found) - 2):
        t = [found[i + k] for k in range(3)]
        if all(v[2] for v in t) and 1.5 < t[1][0] - t[0][0] < 2.5 and 1.5 < t[2][0] - t[1][0] < 2.5:
            clock, free, voices = (v[2] for v in t)
            print('Report cycle at {:.1f} s:'.format(t[0][0]))
            print('  clock    ~{:.0f} MHz'.format(clock))
            print('  free CPU ~{:.0f} MHz ({:.0f}% of the clock)'.format(free, 100 * free / clock))
            print('  voices   min {:.1f} rendered per block'.format((voices - 50) / 100))
            break
    else:
        print('No complete report cycle (3 tones ~2 s apart) found.')


if __name__ == '__main__':
    main()
