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

## Test 2: Rings engine (`op1_246_rings_engine.op1`)

The phase engine's render and note-on become Rings' modal model (`ports/rings/op1/engine.cc`): 6 voices with 10 modes each. In the simulator this costs about 31 MHz with one voice ringing and about 116 MHz with all six.

**To check:**
- Pressing a key gives a struck, bell- or plate-like resonance at the key's pitch.
- Which encoder does what. The code assumes params 0–3 = structure, brightness, damping, position; the screen still shows the phase engine graphics.
- The output level, and whether there's distortion.
- Stutters when playing many notes. If so, rebuild with fewer modes (`OP1RINGS_MODES_PER_VOICE`).
