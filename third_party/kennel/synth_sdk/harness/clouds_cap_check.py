#!/usr/bin/env python3
"""Host checks for clouds' CPU work (plugins/clouds): the output is locked, and the grain pool follows the effects.

    python3 synth_sdk/harness/clouds_cap_check.py [OUT]     (OUT default harness/build/clouds_cap; exit 1 on a failure)

Builds harness/clouds_cap_test.c with UBSan and checks the released3.0.1 sound mapping:
  locked    exact four-second output for defaults, minimum and diffusion controls.
  budget    the unchanged 24-grain pool with reverb and diffusion.
  level     maximum controls remain within 1 dB of the locked925 RMS reference.
The optimization must retain full-rate envelopes/reverb, all24 grains and the random sequence.
Writes clouds-{defaults,allmax}.wav (4 s, mono) for listening.
Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import hashlib
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SDK = HERE.parent
fails = []
# position size density texture | pitch dry/wet reverb | freeze; sha256 of the locked 4 s output
EXACT = {
    'defaults': ([16384, 16384, 24576, 16384, 16384, 22938, 0, 0],
                 '2c03eebeca30fe5c2c72a28e66469b873eea7172c732ed44599e5cdd686d5162'),
    'all maximum': ([32767] * 7 + [0],
                    '87ed3272e969e854c9d11db49b17be7aa107821bbc8c52533a796a7f5cbc2f42'),
    'all zero': ([0, 0, 0, 0, 0, 0, 0, 0], 'aba09dcca57968ec922383be093aea8972102b81ea936b44e5435ee89cd8476a'),
    'diffusion': ([20000, 30000, 2000, 30000, 4000, 32767, 0, 0],
                  '8b72f1f9bdd4b0148cf68717b2f2c5def17d491bcdd60610827394314f37fd7c'),
}


def check(ok, msg):
    print(('ok    ' if ok else 'FAIL  ') + msg)
    if not ok:
        fails.append(msg)


def run(exe, out, k, secs=4.0):
    raw, log = out / 'tmp.raw', out / 'tmp.log'
    subprocess.run([str(exe), str(raw), '0', *map(str, k), str(secs), str(log)], check=True)
    d = raw.read_bytes()
    n = np.loadtxt(log)
    raw.unlink()
    log.unlink()
    return d, n


def wav(path, d):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(d)


def main(argv):
    out = Path(argv[0]) if argv else HERE / 'build' / 'clouds_cap'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'clouds_cap_test'
    subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wno-unknown-pragmas', '-fsanitize=undefined',
                    '-fno-sanitize-recover=undefined', '-DOP1_HOST', '-I', str(SDK), '-o', str(exe),
                    str(HERE / 'clouds_cap_test.c'), '-lm'], check=True)
    for name, (k, want) in EXACT.items():
        d, _ = run(exe, out, k)
        check(hashlib.sha256(d).hexdigest() == want, f'locked: {name}, the output as locked')
        if name == 'defaults':
            wav(out / 'clouds-defaults.wav', d)
    _, n = run(exe, out, [8000, 32767, 0, 0, 32767, 32767, 32767, 0])
    check(n.max() <= 24, f'budget: reverb on, up to all 24 grains ({int(n.max())})')
    d, n = run(exe, out, [32767] * 7 + [0])
    check(n.max() <= 24, f'budget: reverb and diffusion on, all 24 grains available ({int(n.max())})')
    x = np.frombuffer(d, '<i2').astype(float)
    rms = np.sqrt((x ** 2).mean())
    check(abs(20 * np.log10(rms / 925)) < 1.0, f'level: everything at maximum, rms {rms:.0f} (locked 925)')
    wav(out / 'clouds-allmax.wav', d)
    exact = out / 'clouds_exact_test'
    subprocess.run(['cc', '-O2', '-g', '-Wall', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=undefined', str(HERE / 'clouds_exact_test.c'),
                    '-o', str(exact)], check=True)
    for mode, want in [('dynamic', 'b6181e7578bd0f1865c665ac0a4c82c62afa2e5930eb3d28f9d275db248c4452'),
                       ('rails', 'f9b8bcf561c2b35754a6bef72cbd2b053c882f5ef5e9349b16da66d8690bbc8f'),
                       ('edge', '784e6df1b4ff4afa9b57407c74c03b14c523a3b92c2fe6b3b542b7f126dc0555')]:
        raw = out / (mode + '.raw')
        subprocess.run([str(exact), str(raw), mode], check=True)
        check(hashlib.sha256(raw.read_bytes()).hexdigest() == want,
              f'exact: {mode}, released output unchanged under ASan/UBSan')
    print('clouds_cap_check: ' + ('all passed' if not fails else f'{len(fails)} FAILED'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
