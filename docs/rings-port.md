# Porting Mutable Instruments Rings to the OP-1

Status as of 2026-10-14: feasibility settled, and the modal resonator's hot loop is ported and measured. Nothing has been built for the device yet. The code is in `ports/rings/` (see its README).

## Why it can't just be recompiled

The OP-1's BF524 has no floating-point unit, and Rings is written entirely in float.

Measured in the Blackfin simulator, with GCC 14.2 (`tests/bench_ops.cc`):

| Operation | Cycles |
|---|---|
| float add or multiply | ~330 |
| float divide | ~590 |
| `sqrtf` | ~1,800 |
| 32×32 fract multiply (`__builtin_bfin_mult_fr1x32x32`) | ~5 |

The OP-1 app itself is fixed point: about 6,000 fractional MAC instructions, and only 45 loads of a `1.0f` constant.

Upstream Rings compiled as-is for the BF524 (`make bench-ref`):

| Scenario | Cycles per 24-sample block | CPU at 44.1 kHz |
|---|---|---|
| modal, 1 voice (60 modes) | ~9.0 M | ~16.5 GHz |
| string, 1 voice | ~1.2 M | ~2.3 GHz |

The chip runs at about 400 MHz, assuming the BF524's maximum; the real clock is set from OTP by the boot ROM and is unknown. **So every per-sample loop, and the per-block maths too, has to be fixed point.**

## Approach

1. **Bit-exact baseline.** `src/` started as an exact copy of the Rings DSP. The harness renders deterministic scenarios through both upstream and the port, and the copy matched bit-for-bit.
2. **Step-by-step conversion.** Each piece is converted separately and checked against the float reference:
   - waveform SNR (`compare.py diff`/`dirdiff`);
   - per-band spectral difference (`compare.py spec`/`dirspec`), which ignores phase.
3. **Hot loops in assembly.** These sit behind a C reference with exactly the same arithmetic. Host emulation of the Blackfin multiply builtins is bit-exact (`tests/test_fixed.cc`: 200,144 cases, 0 mismatches). So host renders predict device output, and C and assembly can be compared by checksum.

## Modal resonator (done: per-sample loop)

**Chamberlin restructure.** Upstream runs one TPT state-variable filter per mode, with 5 multiplies per sample and coefficients that need about 32-bit precision: the damping term `r·g` gets as small as 6×10⁻⁷. The port uses a Chamberlin SVF with converted coefficients instead:
- `f = 2g√h`, `q = r√h`;
- output gain `√h/2`;
- input `x[n] + x[n-1]`, shared by all modes.

This has exactly the upstream band-pass response (`tests/svf_equivalence.cc`: 100–140 dB at ordinary Q; down to ~51 dB only at extreme Q, which is float32 rounding). Damping now lives in its own coefficient.

**Coefficient changes.** When the coefficients change (a new note, a knob move), the state is translated through the closed-form map to and from the TPT state, derived with sympy. Here `u0` is the previous input and `E = 1 + g² − gr`:

```
s1 = (√h/2)(1−g²−gr)·bp − (g/2)·lp + g·u0
s2 = g√h·bp + lp/2
lp = 2[s2(1−g²−gr) − 2g·s1 + 2g²·u0] / E
bp = 2(s1 + g·s2 − g·u0) / (E√h)
```

Without the map, note changes at high structure differ by up to 13 dB in the top octaves. With it, the float Chamberlin version matches upstream to 72–93 dB SNR.

**Fixed-point bank (`mode_bank.h`, `mode_bank_bfin.S`):**
- **Formats:** Q27 states (±16; measured peak ±2.1), Q31 coefficients, 16-bit weights.
- **Stored state:** `lp/2` and `hp/2`, so only one shift is needed per mode.
- **Multiplies:** 4-instruction 32×32 multiplies that drop the low×low partial product (error under 2 LSB).

Results:

| Version | Cycles per mode per sample | 60 modes at 44.1 kHz | Match with upstream |
|---|---|---|---|
| C (GCC) | 49.5 | 131 MHz | |
| Assembly | 23.7 | ~63 MHz | 69–86 dB SNR in all modal scenarios; spectral difference ≈ 0.00 dB |

**Mode count.** Rings uses 60 modes for one voice. When polyphonic it uses `64/poly − 4` per voice (12 at 4 voices), so the total stays around 50–60. An OP-1 version with 6 voices × ~10 modes would cost about the same as one full voice.

## Per-block maths (done for the modal model)

- **FFloat** (`src/ffloat.h`): a software float with a 32-bit mantissa and a separate exponent, built on the fast multiply and `SIGNBITS`.
  - Worst relative error is 2–6×10⁻⁹ (float's epsilon is 1.2×10⁻⁷).
  - Host and Blackfin results are bit-identical (`tests/test_ffloat.cc`).
  - About 5–7× faster than libgcc's soft-float: roughly 50 cycles per multiply and 145 per divide.
  - Used only where few operations run per block.
- **Modal coefficients** (`src/modal_resonator.cc`) are fixed point from tables (`resources/gen_fixed_tables.py`):
  - sin, cos, sec and tan of `phi = atan(tan_fast(pf))` include upstream's `tan` approximation, so partials are tuned exactly as upstream, including its flattening near Nyquist;
  - `r = 1/Q` comes from log2/exp2 tables;
  - `rho = (1 + x)^-1/2` and the reciprocals come from tables;
  - the partial frequencies use FFloat.
  - Against upstream float over a parameter grid (`tests/test_modal_coefficients.cc`), the error is under 1×10⁻⁵ for `f` and gain, and 0.9% worst case for `q` at its smallest values.
- **The state translation is fixed point too.** `A = cos² − sin² − x` needs Q30, because it reaches about −1.4.
- **Caching:**
  - coefficients are recomputed only when a control value changes;
  - the partial geometry (pf, sin, cos, sec, tan) is reused when only brightness or damping change.
- **Cost of one coefficient update for 60 modes:**
  - about 34k cycles when only brightness or damping change;
  - about 58k cycles when pitch or structure change.

## OP-1 voice (done for the modal model)

`src/op1_voice.{h,cc}` (`RingsVoice`) is one Rings voice with no floating point at run time:
- note on → impulse → excitation low-pass → modal resonator → ×1.4 → limiter.

It replaces `rings::Part`, because the OP-1 core does voice allocation:
- **Pitch** is `phase_increment >> 1`, giving Q31 `f/fs`.
- **Strum impulse.** Upstream's impulse (up to ~6000) is fed to the filter as its exact linear effect: the first-sample output plus the state increment, all below ~1.
- **Limiter.** `1/peak` is computed by one Newton step per sample, and the soft-limit curve comes from a table.
- **Control rate.** Pitch and parameters are latched every `OP1RINGS_CONTROL_BLOCK_SIZE` (64) samples; audio runs in sub-blocks of 32.
- **Pickup-position jumps** larger than 0.02 use a 4-segment linear weight ramp.

**Against upstream Rings** (`harness/render_voice.cc`, 48 kHz, 24-sample blocks, polyphony 1, internal exciter): all 7 modal scenarios match at 44–66 dB SNR, with at most 0.05 dB mean spectral difference.

**Blackfin cost of one voice with 60 modes, at 44.1 kHz** (`bench_voice.elf`, simulator cycles):

| Case | Cycles per 64 samples | CPU |
|---|---|---|
| Steady notes | ~121k | ~84 MHz |
| Knobs moving every block | ~155–165k | ~107–113 MHz |
| Peak (strum, large position jump) | ~180k | ~125 MHz |

The mode bank is ~91k of the steady figure. The rest (weights, exciter, limiter, control maths) is per-voice overhead to trim before running 6 voices: skip idle voices, and only redo exciter and control maths on change.

**Compiler bug found:** with a constant shift, GCC 14's `__builtin_bfin_shl_fr1x32` emits a 16-bit shift. `ShlSat` uses `SIGNBITS` instead, and `tests/test_fixed.cc` checks constant shifts.

## Remaining work

- **Trim per-voice overhead:**
  - idle-voice skipping;
  - recompute the exciter only on change;
  - optionally fewer modes per voice (Rings itself uses `64/poly − 4`).
- **String model**, in fixed point:
  - delay lines with Hermite interpolation;
  - FIR and IIR damping;
  - dispersion allpass;
  - plucker (noise burst plus comb);
  - DC blocker.
  - Then sympathetic strings (8 strings) and the reverb (16-bit buffer, as upstream).
- **OP-1 integration** (see `docs/engine-interface.md`):
  - a freestanding build linked at the free region (`docs/memory-map.md`);
  - a tool that adds our code to `OP1_vdk.ldr` as new blocks inserted just before FINAL, so all existing blocks keep their offsets;
  - redirect the phase engine's vtable entries for render (`0x1911150`) and note-on (`0x1911158`).
- **First device step: a probe build** (asks the user before flashing):
  - the phase engine's render calls the stock render and then busy-waits for a fraction of the measured block period set by an encoder, so the glitch point gives the free CPU fraction;
  - it checks a fill pattern over the free region to catch anything else using it.
- **Sample rate.** Upstream assumes 48 kHz. The port takes `OP1RINGS_SAMPLE_RATE` (default 44100). The lookup tables don't depend on sample rate.
