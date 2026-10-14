# Device tests

Packages are built here and flashed by the owner. Every test package:
- leaves out te-boot.ldr;
- passes `tools/verify_bootloader.py`;
- decodes to stock firmware 246 with only `OP1_vdk.ldr` changed.

Recovery always works the same way: hold COM at power-on, choose "1. Upload firmware", and copy stock `op1_246.op1`. That path runs before any app code (see `docs/brick-safety.md`).

## How the packages are built

```
make -C ports/rings op1-probe op1-engine      (in WSL)
python tools/inject_app.py "op1 firmware/op1_246/OP1_vdk.ldr" OUT.ldr ports/rings/build/op1_probe.elf --fill-pattern 0xa5a5a5a5
cp -r "op1 firmware/op1_246" "op1 firmware/work_probe"; cp OUT.ldr "op1 firmware/work_probe/OP1_vdk.ldr"
python tools/build_package.py "op1 firmware/work_probe" "op1 firmware/op1_246_rings_probe.op1"
```

`inject_app.py` changes, and only changes:
- the FIRST block's length field and header checksum;
- the phase engine's vtable entries (render `0x1911150`, and for the engine build note-on `0x1911158`);
- the new blocks, inserted before FINAL. The stock FINAL and the trailing IGNORE block are appended byte-identical.

## Test 1: probe v1 (`op1_246_rings_probe.op1`), run 2026-10-14

**Result:**
- **Injected code runs from `0x02c00000`.** The phase engine worked and the probe produced its report.
- **The CPU ramp didn't crash the OP-1.** It overloaded the audio, backed off and recovered.
- **No siren**, so nothing overwrote the free region while it was checked.

**The report itself was unreadable** (`incoming/probe/probe_report.ogg`, decoded with `tools/decode_probe_tones.py`). There were three tones of about 378, 643 and 905 Hz, at twice the expected speed.
- **Cause:** a probe bug. All render calls shared one tone oscillator, so with several calls per block each voice's audio jumped in phase and the pitches came out scrambled. A model of this reproduces the scrambling (`%TEMP%` script, not kept).
- **What it does tell us:** there are several render calls per audio block, and the old gap-based block detection fired twice per block. That points to idle voices being rendered, or extra render calls.

## Test 1b: probe v2 (`op1_246_rings_probe2.op1`)

The report no longer uses pitch. It uses beep counts timed by the CPU's cycle counter (`ports/rings/op1/probe.cc`). Free CPU is measured from one reference voice's call interval. The report also includes the raw call pattern.

It was checked end to end on the host (`tests/host_probe.cc`, a simulated OP-1 with a known clock, free CPU and call pattern), plus `tools/decode_probe_beeps.py`. For a simulated 400 MHz with 55% free, it recovers 399.8 MHz and 55%.

**Procedure:**
1. Flash `op1_246_rings_probe2.op1` through te-boot.
2. Select the phase engine and hold one key for about 45 s.
   - First the normal sound plays.
   - Then the CPU ramps; stutters are expected.
   - Then it beeps: a long sync tone, then groups of short beeps.
3. Keep holding the key and record at least 40 s of beeping (a full report is about 17 s at 400 MHz). Save the file in `incoming/`.
4. A continuous tone with no beeps means something wrote into the free region.

**Decoding:** `python tools/decode_probe_beeps.py RECORDING` prints the clock, the free CPU, the render calls per block, the number of distinct voice objects called, and the calls per second.

**Result (2026-10-14, `incoming/probe/probe2_report.ogg`):**

Decoded numbers, the same in every cycle: `[524, 0, 1, 1, 1, 463]`.

| Measurement | Value | Meaning |
|---|---|---|
| Clock | 399.6 MHz | from the beep timing |
| Reference voice call interval (shortest) | 524 × 1024 = 536,576 cycles = 1.34 ms | |
| Render calls in 2^28 cycles (0.67 s) | 463 | ~689 per second |
| Calls per interval | 1 | |
| Distinct voice objects | 1 | only the sounding voice is rendered; no other render calls |
| Beep pitch | 2760 Hz | the 32-sample square, so the **voice output rate is 88.2 kHz**; 128-sample blocks give 689 Hz |

Probe v1's doubled pitches and halved durations have the same cause.

- **The keyboard octave doesn't affect these numbers.** The recording was made with the keyboard an octave up, but the probe ignores the key's pitch.
  - The beep is a fixed 32-sample square written straight into the output, and the call rate comes from the cycle counter.
  - Re-measured from the recording, the beeps sit at ~2760 Hz.
  - 64-sample blocks at 44.1 kHz would also give 689 calls per second, but would put the beep at 1378 Hz.
- **Free CPU: invalid (0).** The overload test compared intervals against the *shortest* one (1.34 ms), but they jitter around a 1.45 ms mean, so it tripped immediately.

## Test 1c: probe v3 (`op1_246_rings_probe3.op1`)

Probe v3 fixes v2's free-CPU measurement and reads the held key's pitch (`ports/rings/op1/probe.cc`):
- **Calibration (~2 s):** records three things:
  - the reference voice's *mean* call interval over ~1400 calls (the block period);
  - its longest interval;
  - the held key's phase increment.
- **Overload:** judged on the mean interval over each 64-call ramp step.
  - The intervals telescope, so jitter barely moves the mean.
  - A step whose mean is over 2% longer than the period counts as overload.
  - The report gives the last burn that ran clean, less that 2% margin.
- **Report:** five numbers.
  1. `3`, the version;
  2. block period / 1024;
  3. free cycles per block / 1024;
  4. the held key's pitch in 0.1 Hz, assuming 88.2 kHz;
  5. the longest call interval / 1024.

**Checked on the host** (`make host-probe`). The test simulates an OP-1 with 88.2 kHz blocks, calls jittered by up to 8% of a block as on the device, and a held A4. The decoder recovers:
- 55% at 400 MHz;
- 30% with 3 calls per block and stray calls;
- 70% at 380 MHz.

It reads the key as 440.0 Hz in every case.

**Caveats:**
- **The figure is what the audio path leaves.** The UI may run in that time too, so watch the screen during the ramp. If the screen gets sluggish before the audio stutters, the real budget for an engine is lower.
- **Overload could show late.** If the core buffers audio ahead, overload could show a step or two late, which would make free CPU read up to ~1% high.

**Procedure:**
1. Flash `op1_246_rings_probe3.op1` through te-boot.
2. Set the keyboard octave back to 0. Select the phase engine and hold one key you can name (e.g. "the lowest F") for about 45 s.
   - First the normal sound plays (~2 s).
   - Then the CPU ramps (up to ~20 s); stutters are expected near the end.
   - Then it beeps: a long sync tone, then groups of short beeps.
3. Keep holding the key and record at least 40 s of beeping (a full report is about 12 s at 400 MHz). Save the file in `incoming/probe/`.
4. A continuous tone with no beeps means something wrote into the free region.

**Decoding:** `python tools/decode_probe_beeps.py RECORDING` prints:
- the clock;
- the block period and rate;
- the free CPU, in % and MHz;
- the held key as a note name;
- the longest call interval.

It still decodes v2 recordings.

**What the pitch tells us.**
- The stock phase engine does `phase += inc` per sample, with one cycle per 2^32, and its output is consumed at 88.2 kHz.
- So Rings with `f = inc / 2^32 × 88200` plays at the same pitch as the stock engine on any key.
- The reported note checks the absolute tuning against the key held.

**Result (2026-10-14, `incoming/probe/probe3_report.ogg`):** the lowest F held, octave 0. Three complete reports, all identical: `[3, 566, 459, 1746, 603]`.

| Measurement | Value | Meaning |
|---|---|---|
| Clock | 399.8 MHz | from the beep timing |
| Block period (mean) | 566 × 1024 = 579,584 cycles = 1.450 ms | 128 samples → **88.3 kHz** (exact 88.2 kHz is 580,499 cycles; the difference is the ÷1024 rounding) |
| Free cycles per block | 459 × 1024 = 470,016 | **81% of a block, ~324 MHz** |
| Held key | 174.6 Hz | **F3, 0 cents**, assuming `inc = f / 88200 × 2^32`; at 44.1 kHz it would be F2 (87.3 Hz) |
| Longest call interval | 603 × 1024 cycles = 1.54 ms | 107% of a block |

- **88.2 kHz confirmed again** with the octave at 0. The pitch formula gives a sensible note (F3) for the lowest key.
- **Free CPU is large.** The figure has some limits:
  - it is with one phase voice playing and no FX or other tracks;
  - the UI may share that time;
  - the OP-1's per-voice work (envelopes) grows with more voices.
- **Sizing.** The engine moves from 10 to 16 modes per voice (see Test 2).

## Test 2: Rings engine (now `op1_246_rings_engine16b.op1`; 10-mode fallback `op1_246_rings_engine10b.op1`)

The phase engine's render and note-on become Rings' modal model (`ports/rings/op1/engine.cc`), with 6 voices of 16 modes each (`OP1RINGS_MODES_PER_VOICE`).
- **Rate:** Rings runs at 44.1 kHz on 64 samples per call. A 4-point Lagrange 2× upsampler then produces the 128 output samples at 88.2 kHz.
- **Pitch:** Rings' frequency is the OP-1 increment doubled (`f / 44100`), latched at the start of the block.

**In the simulator** (`make sim-engine`, at the real call rate):

| Modes per voice | 1 voice ringing | All 6 ringing | Share of the 324 MHz free (6 voices) |
|---|---|---|---|
| 10 | 37 MHz | 121 MHz | 37% |
| **16** | **47 MHz** | **164 MHz** | **50%** |
| 20 | 53 MHz | 191 MHz | 59% |
| 24 | 59 MHz | 218 MHz | 67% |

- **Knob movement:** earlier benchmarks put moving knobs at roughly +30%, so 16 modes would peak at about 210 MHz (65%). That leaves room for other tracks, FX and the UI.
- **Pitch:** a 220 Hz key gives a 220.0 Hz fundamental, with no energy at 110 Hz.
- **Upsampling images:** at −83 dB.

**Result of the first flash (2026-10-14, `op1_246_rings_engine16.op1`):**
- **It sounds right** ("sounds amazing").
- **No stutters or UI sluggishness.**
- **Slight pops when turning the encoders while a note rings.**

**Cause of the pops: a wrong constant in the mono limiter** (`src/op1_voice.cc`).
- Below the limiter threshold, the gain was `962072674` = 0.896 in Q30, i.e. 1.4 × 0.8 × 0.8, with Rings' 0.8 output factor counted twice.
- Above the threshold, the gain is 1.12 / peak.
- So whenever the peak follower crossed 1.0, the gain jumped by 25% (1.9 dB) at a sub-block boundary.
- Turning an encoder raises the level across that threshold, which made the pop. A decaying note crossing back down made another one (at 0.28 s in the test note).
- Upstream's gain is continuous there: `peak ≤ 1 ? 1 : 1 / peak`, times 1.4 × 0.8.
- **Fixed:** the constant is now `1202590843` = 1.12, which also makes notes below the threshold 1.9 dB louder, matching upstream.

**Evidence** (`make step-params`, `tests/step_params.cc`):
- **Sustained note, fixed parameters:** the worst burst above 9 kHz was −44.7 dB re the note. After the fix it is −75.2 dB. What remains is the limiter's per-sub-block gain update, which is small. The test fails above −60 dB.
- **Blackfin simulator** (assembly mode bank and upsampler, 88.2 kHz output): −47.8 dB before the fix, −70.4 dB after.
- **Raw parameter steps** cause a few clicks of their own, at the same level as upstream's float resonator given the same steps:
  - a structure step of +0.2 gives −56 dB re note (upstream: −61.5 dB);
  - position steps give −63 to −85 dB;
  - brightness and damping steps cause none.
  - Upstream never sees raw steps: its pots are low-passed (`cv_scaler.cc`, coefficient 0.01 per 2 kHz block, ~50 ms). A similar smoother would remove the structure click (−83 to −91 dB in the test). It isn't added yet; it's the next step if any zipper or click remains.

**Packages** (all verified against stock):
- **`op1_246_rings_engine16b.op1`: 16 modes, limiter fixed. Flash this one.** Its image differs from the flashed `engine16` only in the two limiter constants (8 bytes).
- **`op1_246_rings_engine10b.op1`:** 10 modes, limiter fixed, as a fallback.
- The unfixed `op1_246_rings_engine16.op1` and `op1_246_rings_engine.op1` were deleted to avoid mix-ups.

**Second flash (`engine16b`):**
- The user reports it "sounding amazing".
- Encoders confirmed: blue = structure, green = brightness, white = damping, orange = position.
- The ranges and release behaviour feel right.

**Knob smoothing (`op1_246_rings_engine16c.op1`, not yet flashed).** This is `op1/param_smoother.h`:
- **The filter:** a one-pole per voice and parameter, ~50 ms like upstream's `cv_scaler`, updated once per call.
- **Snapping:** each value snaps to the target within 2^-12, so recomputing stops soon after a knob stops. Note-on snaps the voice to the current values.
- **Effect** (`make step-params`, smoothed vs raw):
  - a sudden +0.5 structure step goes from −31 dB to −70 dB re note;
  - a +0.5 position step goes from −42 to −62 dB;
  - all smoothed steps stay below −60 dB, and the test checks this.
- **Sound with still knobs** is bit-identical to `engine16b` (same simulator checksum).
- **Cost:** with all six ringing and three knobs turning continuously, the simulator measures ~226 MHz, about 70% of the measured free CPU. With knobs still it's 164 MHz. If that ever stutters, the coefficients could be recomputed every other call while smoothing.

**To check on the next flash:** encoder turns, fast and slow, on six ringing notes. Listen for clicks and stutters.

## Test 3: FM voice (`op1_246_rings_fm.op1`, not yet flashed)

This is the engine of `engine16c` (knob smoothing included) with Rings' FM voice in place of the modal model (`-DOP1RINGS_MODEL=3`, `make op1-engine-fm`). It's a temporary build until model select (shift + blue) exists.

**The FM voice** (`src/fm_voice_fixed.*`) is a fixed-point port of upstream's `FMVoice`. Each strike triggers its internal envelope.

| Encoder | FM parameter |
|---|---|
| Blue (structure) | frequency ratio, quantized to Rings' table (0.5 to 8, including detuned and irrational ratios) |
| Green (brightness) | FM amount |
| White (damping) | decay; above 0.9 it fades into a sustained oscillator |
| Orange (position) | feedback: centre = none, right = operator feedback, left = phase feedback |

**Against upstream** (`make test-fm`, 48 kHz, 11 scenarios: ratios, pitches up to 1760 Hz, decays, oscillator mode, both feedbacks):
- **Match:** 42–78 dB SNR, with level within 0.01 dB and the same spectral centroid.
- **Ratio tuning:** the modulator ratio is floored to 1/256 semitone, as upstream's `SemitonesToRatio` does. Tuned exactly, it drifted from upstream (5 dB SNR at 1760 Hz).

**In the simulator** (`make sim-engine-fm`):
- **Cost:** ~106 MHz with six voices ringing, 110 MHz with knobs turning (modal: 164 / 226).
- **Discontinuities:** none; the worst high-frequency burst is −74.5 dB.

**To check on the device:**
- the sound across the four encoders;
- the level against the modal build: upstream's FM limiter pre-gain is 0.7, half the modal model's;
- clicks and stutters.
