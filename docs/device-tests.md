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

## Test 2: Rings engine (`op1_246_rings_engine.op1`)

The phase engine's render and note-on become Rings' modal model (`ports/rings/op1/engine.cc`), with 6 voices of 10 modes each.
- **Rate:** Rings runs at 44.1 kHz on 64 samples per call. A 4-point Lagrange 2× upsampler then produces the 128 output samples at 88.2 kHz.
- **Pitch:** Rings' frequency is the OP-1 increment doubled (`f / 44100`), latched at the start of the block.

**In the simulator** (`make sim-engine`, at the real call rate):
- **CPU:** about 38 MHz with one voice ringing and 121 MHz with all six. The old engine was simulated at 31 and 116 MHz, but at 44.1 kHz; at 88.2 kHz it would have needed twice that.
- **Pitch:** a 220 Hz key gives a 220.0 Hz fundamental, with no energy at 110 Hz.
- **Upsampling images:** at −83 dB.

The previous `op1_246_rings_engine.op1` was built before the 88.2 kHz fix and played an octave low. It was never flashed, and it has been replaced.

**To check:**
- Pressing a key gives a struck, bell- or plate-like resonance at the key's pitch.
- Which encoder does what. The code assumes params 0–3 = structure, brightness, damping, position; the screen still shows the phase engine graphics.
- The output level, and whether there's distortion.
- Stutters when playing many notes. If so, rebuild with fewer modes (`OP1RINGS_MODES_PER_VOICE`).
