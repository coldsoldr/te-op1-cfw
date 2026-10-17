#!/usr/bin/env python3
"""Decode the probe's beep report (v2, v3 or the FX probe's v4) from a
recording (docs/device-tests.md).

Report cycle (one unit = 2^23 CPU cycles):
  sync tone 12 units, 12 units off,
  then the numbers; each digit d is d beeps (0 = 10 beeps) of 2 units on and
  2 off, with 6 extra units off after each digit and 20 after each number.
v3 numbers (five, the first is 3): version, block period / 1024, free cycles
per block / 1024, the held key's pitch in 0.1 Hz assuming 88.2 kHz, longest
call interval / 1024.
v2 numbers (six): block period / 1024, free cycles per block / 1024, min and
max render calls per block, distinct voice objects (31 = anything outside the
voice array counts once), render calls in 2^28 cycles.
v4 numbers (nine, the first is 4; ports/rings/op1/fx_probe.cc): version,
kind (1 mono, 2 stereo), position (1 synth, 2 drum, 3 master, 9 other), call
period / 1024, free cycles per call / 1024, stock phone's cycles per call /
256, synth render calls per effect call x 10, longest call interval / 1024,
gaps (> 5 ms without a call) since idle started.

The beep timing gives the CPU clock (unit duration = 2^23 cycles). The tone
is a square wave with a 32-sample period, so its pitch gives the sample rate
of the code that wrote it.

Usage:
    python tools/decode_probe_beeps.py RECORDING [--ffmpeg PATH]
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

RATE = 8000
FRAME = 40   # 5 ms


def expected_count(numbers):
    """Numbers in a complete report: v3 and v4 start with their version."""
    if numbers and numbers[0] == 3:
        return 5
    if numbers and numbers[0] == 4:
        return 9
    return 6


def tone_hz(samples, r, unit):
    """The tone's pitch from zero crossings in the sync tones (the middle 80%
    of each), or None."""
    estimates = []
    for on, t, d in r:
        if not on or d < 8 * unit:
            continue
        a, b = int((t + 0.1 * d) * RATE), int((t + 0.9 * d) * RATE)
        seg = samples[a:b]
        crossings = sum(1 for x, y in zip(seg, seg[1:]) if (x < 0) != (y < 0))
        if len(seg) > 1:
            estimates.append(crossings / 2.0 / (len(seg) / RATE))
    if not estimates:
        return None
    estimates.sort()
    return estimates[len(estimates) // 2]


def load(path, ffmpeg):
    with tempfile.TemporaryDirectory() as tmp:
        wav_path = os.path.join(tmp, 'mono.wav')
        subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', path, '-ac', '1', '-ar', str(RATE),
                        '-sample_fmt', 's16', wav_path], check=True)
        with wave.open(wav_path) as w:
            return array.array('h', w.readframes(w.getnframes()))


def runs(samples):
    """(is_on, start_s, duration_s) runs of the beep envelope."""
    env = []
    for i in range(0, len(samples) - FRAME + 1, FRAME):
        env.append(math.sqrt(sum(v * v for v in samples[i:i + FRAME]) / FRAME))
    ordered = sorted(env)
    loud = ordered[int(0.98 * (len(ordered) - 1))]
    on = [e > 0.3 * loud for e in env]
    # Remove 1-frame glitches.
    for i in range(1, len(on) - 1):
        if on[i - 1] == on[i + 1] != on[i]:
            on[i] = on[i - 1]
    out, start = [], 0
    for i in range(1, len(on) + 1):
        if i == len(on) or on[i] != on[start]:
            out.append((on[start], start * FRAME / RATE, (i - start) * FRAME / RATE))
            start = i
    return out


def main():
    parser = argparse.ArgumentParser(description='Decode the probe v2/v3 beep report')
    parser.add_argument('recording')
    parser.add_argument('--ffmpeg', default=shutil.which('ffmpeg') or 'ffmpeg')
    args = parser.parse_args()
    samples = load(args.recording, args.ffmpeg)
    r = runs(samples)
    beeps = sorted(d for on, _, d in r if on)
    if len(beeps) < 10:
        print('Too few beeps found ({}).'.format(len(beeps)))
        return 1
    # Beep starts are 4 units apart within a digit (on-durations are
    # stretched by block quantisation, so time start to start).
    starts = [t for on, t, d in r if on and d < 3 * (beeps[len(beeps) // 2])]
    spacing = sorted(b - a for a, b in zip(starts, starts[1:]))
    beep_period = spacing[len(spacing) // 4]   # the shortest common spacing
    close = [x for x in spacing if abs(x - beep_period) < 0.15 * beep_period]
    unit = sum(close) / len(close) / 4.0
    clock = 2 ** 23 / unit
    print('{}: unit {:.2f} ms -> CPU clock ~{:.1f} MHz'.format(
        os.path.basename(args.recording), unit * 1000, clock / 1e6))

    reports = []
    i = 0
    while i < len(r):
        on, t, d = r[i]
        if on and d > 8 * unit:            # sync
            numbers, digits, count = [], [], 0
            j = i + 2                       # skip the sync's 12 units off
            complete = False
            while j < len(r):
                on2, _, d2 = r[j]
                if on2:
                    if d2 > 8 * unit:       # next sync: cycle ended
                        complete = len(numbers) == expected_count(numbers)
                        break
                    count += 1
                else:
                    units = d2 / unit
                    if units > 4:           # end of a digit
                        digits.append(count % 10)
                        count = 0
                    if units > 18:          # end of a number
                        numbers.append(int(''.join(str(x) for x in digits)))
                        digits = []
                j += 1
            if count:
                digits.append(count % 10)
            if digits:
                numbers.append(int(''.join(str(x) for x in digits)))
            reports.append((t, numbers, complete or len(numbers) == expected_count(numbers)))
            i = j
        else:
            i += 1

    for t, numbers, complete in reports:
        print('report at {:.1f} s{}: {}'.format(t, '' if complete else ' (incomplete)', numbers))
    good = [n for _, n, c in reports if c and len(n) == expected_count(n)]
    if not good:
        print('No complete report decoded.')
        return 1
    n = good[0]
    if len(n) == 9:
        return report_v4(n, clock, tone_hz(samples, r, unit))
    if len(n) == 5:
        names = ['probe version', 'block period / 1024 cycles', 'free cycles per block / 1024',
                 'held key, 0.1 Hz at 88.2 kHz', 'longest interval / 1024 cycles']
    else:
        names = ['block period / 1024 cycles', 'free cycles per block / 1024',
                 'min render calls per block', 'max render calls per block',
                 'distinct voice objects', 'render calls in 2^28 cycles']
    for name, v in zip(names, n):
        print('  {:<30} {}'.format(name, v))
    if len(n) == 5:
        period, free, pitch, longest = n[1] * 1024, n[2] * 1024, n[3] / 10.0, n[4] * 1024
    else:
        period, free = n[0] * 1024, n[1] * 1024
    print('Derived:')
    print('  block period {:.0f} cycles = {:.3f} ms -> {:.0f} Hz for 128-sample blocks{}'.format(
        period, period / clock * 1000, 128 * clock / period,
        '' if len(n) == 5 else ' (v2: the shortest interval, not the mean)'))
    print('  free CPU {:.0f}% of a block = ~{:.0f} MHz'.format(100.0 * free / period, clock / 1e6 * free / period))
    if len(n) == 5:
        semitones = 12 * math.log2(pitch / 440.0)
        note = round(semitones)
        names12 = ['A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#']
        octave = 4 + (note + 9) // 12
        print('  held key {:.1f} Hz at 88.2 kHz = {}{} {:+.0f} cents (half that, an octave lower, '
              'if the increments were meant for 44.1 kHz)'.format(
                  pitch, names12[note % 12], octave, 100 * (semitones - note)))
        print('  longest call interval {:.2f} ms ({:.0f}% of a block)'.format(
            longest / clock * 1000, 100.0 * longest / period))
    else:
        print('  render calls per second ~{:.0f}; per block {}..{}'.format(n[5] / (2 ** 28 / clock), n[2], n[3]))
    return 0


def report_v4(n, clock, tone):
    """The FX probe's numbers (ports/rings/op1/fx_probe.cc)."""
    names = ['probe version', 'kind (1 mono, 2 stereo)', 'position (1 synth, 2 drum, 3 master)',
             'call period / 1024 cycles', 'free cycles per call / 1024',
             'stock phone cycles per call / 256', 'synth renders per call x 10',
             'longest interval / 1024 cycles', 'gaps since idle']
    for name, v in zip(names, n):
        print('  {:<40} {}'.format(name, v))
    period, free, phone, longest = n[3] * 1024, n[4] * 1024, n[5] * 256, n[7] * 1024
    calls = clock / period
    print('Derived:')
    print('  {} effect, position {}'.format(
        {1: 'mono', 2: 'stereo'}.get(n[1], '?'),
        {1: 'synth', 2: 'drum', 3: 'master'}.get(n[2], 'other')))
    print('  call period {:.0f} cycles = {:.3f} ms -> {:.0f} calls/s ({:.0f} Hz at 64 samples per call)'.format(
        period, period / clock * 1000, calls, 64 * calls))
    if tone:
        print('  report tone {:.0f} Hz -> the effect writes at {:.0f} Hz (32-sample period); '
              '{:.1f} samples per call'.format(tone, 32 * tone, 32 * tone / calls))
    print('  free CPU {:.0f}% of a call period = ~{:.0f} MHz (with stock phone running)'.format(
        100.0 * free / period, clock / 1e6 * free / period))
    print('  stock phone {:.0f} cycles per call = ~{:.1f} MHz'.format(phone, phone * calls / 1e6))
    print('  synth voices rendered per effect call {:.1f}'.format(n[6] / 10.0))
    print('  longest call interval {:.2f} ms ({:.0f}% of a period)'.format(
        longest / clock * 1000, 100.0 * longest / period))
    print('  gaps since idle {} (each: the effect not called for > 5 ms)'.format(n[8]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
