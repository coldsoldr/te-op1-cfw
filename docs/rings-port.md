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

## Remaining work

- **Per-block maths.** Software float is too slow even once per block (60 modes × ~40 float ops ≈ 800k cycles). Plan:
  - a small custom float type (32-bit mantissa plus exponent, using the 5-cycle multiply and the `SIGNBITS` instruction; roughly 30× faster than libgcc's) for control-rate code;
  - quarter-wave sine and cosine tables that include upstream's `tan` approximation, giving `f`, `q` and gain without `tan` or `sqrt`;
  - recompute coefficients only when a parameter changes, spreading the work over a few blocks while knobs move.
- **The rest of the voice, in fixed point:**
  - excitation filter;
  - plucker (noise burst plus comb);
  - DC blocker;
  - limiter;
  - note filter;
  - string model: delay lines with Hermite interpolation, FIR and IIR damping, dispersion allpass. Budget estimate ~40–60 cycles per string per sample in assembly.
  - sympathetic strings (8 strings) and the reverb (16-bit buffer, as upstream).
- **OP-1 integration** (see `docs/engine-interface.md`):
  - redirect the phase engine's vtable entries for render (`0x1911150`) and note-on (`0x1911158`) to our functions;
  - add our code as a new LDR block, using the memory region from `docs/memory-map.md`;
  - map 128-sample blocks, the per-sample phase-increment pitch at `0xff801950`, and four `int16` params.
- **On-device measurements before committing to a voice count:**
  - CPU headroom: a CPU-burner hook on the phase engine, increased until audio glitches;
  - whether idle voices are rendered;
  - the clock speed.
- **Sample rate.** Upstream assumes 48 kHz. The port takes `OP1RINGS_SAMPLE_RATE` (default 44100). The lookup tables don't depend on sample rate.
