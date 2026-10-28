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

## OP-1 build and injection (done; not yet run on the device)

- `ports/rings/op1/` holds the freestanding images, built with `make op1-probe` and `make op1-engine`:
  - linked at `0x02c00000` (`link.ld`);
  - no C library and no startup code;
  - an `OP1X` header with the entry points;
  - no run-time initialisers;
  - large temporaries kept off the stack, because render may run on a small interrupt stack.
- `op1/engine.cc`:
  - six `RingsVoice`s, voice index = `(this - 0xffb00000) / 28`;
  - pitch from the phase-increment buffer at `0xff801950`, latched at the start of the block;
  - parameters from the four `int16` values;
  - mono output (odd + even modes) through a mono limiter.
- **88.2 kHz:** the OP-1 renders voices at 88.2 kHz in 128-sample blocks (measured on the device).
  - Rings runs at 44.1 kHz on 64 samples per call, so its Q31 frequency `f / 44100` is the OP-1 increment `f / 88200 × 2^32` unchanged.
  - A 4-point Lagrange 2× upsampler (`Upsampler` in `op1/engine.cc`) produces the 128 output samples: −0.4 dB at 10 kHz, −1.6 dB at 15 kHz, images at about −83 dB, and 2 samples of latency.
  - Running at half rate keeps the CPU near the 44.1 kHz figures instead of doubling it.
- **Knob smoothing** (`op1/param_smoother.h`): this stands in for upstream's `cv_scaler` pot filter, a one-pole of ~50 ms per voice and parameter.
  - Values snap to the target within 2^-12, and note-on snaps.
  - Raw encoder steps would otherwise reach the resonator as coefficient steps, which click on large jumps: −31 dB re note for a +0.5 structure step, −70 dB smoothed.
  - Worst case, with six voices ringing and three knobs turning, it costs ~226 MHz in the simulator.
- **Mono limiter fix:** its gain below threshold was 0.896 instead of 1.12, so it jumped 25% whenever the peak crossed 1.0. That caused the first flash's pops. `make step-params` checks a sustained note for discontinuities.
- **Idle cost:**
  - idle voices sleep after 8 silent blocks;
  - the exciter sleeps a few ms after each strum.
- `tests/sim_engine.cc` (`make sim-engine`) calls the real entry points in the simulator at the real call rate, with 16 modes per voice (`OP1RINGS_MODES_PER_VOICE`, sized from probe v3's ~324 MHz free). It also writes voice 0's output to `build/sim_engine.raw`; a 220 Hz key gives a 220.0 Hz fundamental.

| Case | CPU |
|---|---|
| 1 voice ringing, 5 idle | ~47 MHz (10 modes: ~37) |
| All 6 ringing | ~164 MHz (10 modes: ~121) |

  - At 10 modes, the upsampler plus per-call overhead (now run twice as often) adds about 5 MHz over the old 44.1 kHz figures (31 and 116 MHz). On the device only sounding voices are rendered, so idle voices cost nothing.
- `tools/inject_app.py` adds the image to `OP1_vdk.ldr` just before FINAL and patches the vtable.
- Device tests and packages: `docs/device-tests.md`.

## FM voice (done; not yet run on the device)

`src/fm_voice_fixed.*` is a fixed-point port of upstream's `FMVoice`. The float copy in `src/fm_voice.*` stays for the harness `Part`.
- **Kept per sample, in upstream's order:** the operators (Q31 sine table, 4096 entries), the FM amount slew, the envelopes, the feedback paths and the gain one-pole.
- **Phase modulation** is applied as an offset modulo 2^32, which is what upstream's `(fm + 4)` bias computes.
- **Dropped: the envelope follower.** With the internal exciter its input is silent and its outputs are exactly 0, so the envelopes only decay from each strum.
- **Modulator ratio:** from Rings' quantizer table (`kFmRatioSemitonesQ24`, regenerated from upstream's Python). It's floored to 1/256 semitone like upstream's `SemitonesToRatio`. Without that floor, the 16-cent detuned ratios drifted from upstream's phase.
- **Output:** upstream's out + aux (carrier + modulator) into the mono limiter with FM's pre-gain of 0.7. `MonoLimiter` now takes per-model gains; the modal output is bit-identical.
- **Match:** `make test-fm` gives 42–78 dB SNR against upstream over 11 scenarios.
- **Cost:** ~106 MHz for six voices on the Blackfin simulator.

## String models (done; on the device since strings1)

Models 1 (sympathetic strings), 2 (string), 4 (quantized sympathetic strings) and 5 (string + reverb, below). The float copies (`src/string.*`, `src/plucker.h`) stay for the harness `Part`.

**`src/string_fixed.*`: `StringFixed`, `PluckerFixed`, `DcBlockerFixed`.**
- **Formats:** string signals Q25 (±64). Values written to the delay line are clamped to ±7, so Hermite's terms (up to 9× a sample) can't overflow; upstream's strings stay within about ±2 (the stress test, repeated strums at infinite decay, peaks at ~4.6 at the output). Delays in samples are Q19; coefficients Q31; the plucker runs in Q24.
- **Delay lines:** 2048 words per string, plus one 1024-word stretch line per voice for the dispersion allpass (only the dispersion models use it, with one string).
- **Per sample**, upstream's loop in its order: Hermite read, dispersion (noise-modulated delay, stretch allpass, DC-blocked curved bridge), input, FIR damping, IIR damping, write, linear pickup read.
- **Changes from upstream:**
  - **The damping low-pass** (`stmlib::Svf` at resonance 0.5) runs as two identical TPT one-poles. At resonance 0.5 the SVF is critically damped and its low-pass is exactly that cascade, which needs only `G = g/(1+g) < 1` instead of `g` (up to ~7 with stmlib's polynomial `tan`) and `h`.
  - **Interpolated products.** Upstream interpolates delay, compensation, pickup position, damping and brightness separately and multiplies them per sample. The port interpolates the products the loop uses: loop delay, pickup delay and the FIR's two damped taps; the dispersion-derived values are interpolated at the block's ends the same way. Exact while the parameters hold still.
  - **The first block after `Init` starts settled.** Upstream's interpolators start from `String::Init`'s defaults (compensation, position and damping 0). That's harmless on Rings, whose strings are set up long before a strum, but an OP-1 voice is reset and struck in the same block, and a high note goes round its loop inside that block.
  - **`SemitonesToRatio`** is mirrored: floored to 1/256 semitone after float's rounding of `semitones + 128`, which snaps near-integers (11.999999 → 12). Without the rounding, octave strings came out 0.4 cents flat and the sympathetic tests drifted to 17 dB SNR.
  - **The input DC blocker** clears itself once input and output are quiet. Left alone, a fixed-point DC blocker settles on a small constant, which a string at infinite decay would integrate.
- **Noise** comes from upstream's LCG (`RandomFixed`, same seed and draw order), so the tests compare waveforms.
- **Coefficients per block** with FFloat, split so that a glide (frequency only) skips the brightness/damping half: ~2,500 cycles when the tone changes.

**`src/string_voice.*`: `StringVoice`** is `Part::RenderStringVoice` for one voice with the internal exciter:
- strum → plucker (cutoff = the excitation cutoff × 8) → DC blocker → string 0 → `0.2/n × (out − aux)` → strings 1..n−1;
- sympathetic strings: brightness `b(2−b)` twice, damping `0.7 + 0.27 d`, position plus an LFO (upstream's approximate cosine oscillator, as a phase accumulator at its true rate), glide `2^(3(b−1))` per block, and damping spread `d + k/n (0.95 − d)`;
- tuning from `ComputeSympatheticStringsNotes`, or from upstream's chord table (Bryan Noll's, which upstream's `dsp.h` selects) with the cv_scaler's hysteresis;
- `n = 8 / polyphony`; "all" plays as Rings' polyphony 4 (2 strings per voice).
- **Tonic:** the played note (the OP-1 has no separate FREQUENCY knob).
- **Mono:** `out − aux` per string, which is what upstream's `Part` sends per voice when polyphonic: the string minus its pickup tap.

**OP-1 voice:** `RingsVoice` switches on the model; limiter pre-gains string 1.4, sympathetic 1.0. `RingsVoice::CopyFrom` replaces the struct copy on a steal and copies only the strings in use (up to 64 KB, ~45k cycles); `op1/support.cc`'s `memcpy`/`memset` now go word by word.

**Match** (`make test-string`, 19 scenarios at 48 kHz against upstream's float `String`/`Plucker` driven like `Part`): 46–76 dB SNR over the first 0.25 s, levels within 0.1 dB, quarter-octave band spectra within 0.05 dB.

**Cost** (`make bench-string`, one string, 32 samples at 44.1 kHz): ~4,300 cycles steady without dispersion (~135 per sample), ~12,000 with dispersion; ~1,800 more when brightness or damping change, a few hundred for a frequency change; the plucker ~2,200 while sounding, nothing once quiet. Engine figures: `docs/device-tests.md` Tests 6 and 7.

**After the first device test** (strings1: sympathetic strings crackled when busy, though the simulator put them below the modal model), the string models were made lighter:
- **Steals hand over the state.** The engine keeps `RingsVoice* voices[6]` over six states; a steal swaps pointers, and the fading voice gets `CopyFrom`: the plain state plus, per string, only the history it can read in the fade (1.5x its delays, `StringFixed::CopyRecentFrom`). The full copy was up to 64 KB, probably over half a block on the device's 16-bit SDRAM.
- **The plucker sleeps** once its burst is over and its comb and filter are below ~1e-6; it used to run every block forever. The DC blocker skips silent blocks.
- **Glides end** within 2^-14 (0.1 cent): upstream's converge forever and kept every sympathetic string recomputing coefficients.
- **Frequency-only updates are cheap:** the damping low-pass's `G = g/(1+g)` comes from a 1025-point table of upstream's polynomial `tan` (`kStringLowpassQ31`).
- **Brightness/damping updates are capped** at 2 sympathetic strings per block, in turn (string 0 always updates). Frequency changes always apply: capping glides too desynchronised the sympathetic strings after a new note (7 dB SNR against upstream).
- **The plain string loop is in assembly** (`src/string_kernel_bfin.S`, bit-exact with `StringKernelC` in `src/string_kernel.h`, checked by `make bench-string`). The delay line carries 4 guard words mirroring its first samples, so reads never wrap. It's a straightforward first version (~135 cycles per sample against ~160 in C); scheduling loads into the multiplies' parallel slots would gain more.
- A lambda GCC didn't inline had cost 40% in C; it's forced inline.

## Assembly round (strings3)

Test 7 showed the device runs ~1.6x the simulator's figures for every model (the modal model reads 4 rings at ~41% simulated). The string models' hot loops went to assembly, each bit-exact with a C reference checked by `make bench-string` / `make bench-models`:
- `src/limiter_bfin.S` (`MonoLimiterAsm`): the soft-limit table lookup clamps its magnitude to 4.0, where the index is 256 and the fraction exactly 0, matching the C's out-of-range branch without one.
- `src/string_kernel_bfin.S`: `StringKernelAsm` (plain string, hand-scheduled) and `StringKernelStretchAsm` (the stiff string while only its stretch allpass is active: dispersion in [0, 0.75], no noise or curved bridge; its C reference `StringKernelStretchC` is bit-exact with the general dispersion loop with those terms at 0: the loop delay goes through `delay_fm = 1` in Q30, which clears its lowest bit). The noise generator advances one draw per sample regardless, so the plucker's sequence matches upstream's.
- `src/reverb_bfin.S`: see the reverb below.
- `ShlSat` with a constant shift is one `<< n (S)` instruction on Blackfin (inline asm; GCC 14's builtin is the one that miscompiles, `src/fixed.h`).
- String models render a 64-sample block per call (`RingsVoice::RenderStringBlock`); the sympathetic glide is squared per 64 samples to keep its timing. Frequencies are cached; the plucker and DC blocker are skipped while quiet.
- `make bench-models` gives per-voice cycles per model and their parts.

## String + reverb (model 5)

`src/reverb_fixed.*`: upstream's `Reverb` (Griesinger topology on a 32768-sample 16-bit buffer, `fx/reverb.h` and `fx/fx_engine.h`) in fixed point.
- **Mono:** upstream feeds the reverb with left + right and mixes it back into each side; the sum of the sides is `dry + amount (wet_1 + wet_2 − dry)`, which the port computes. Upstream's odd/even pan doesn't change left + right. The dry signal is the string's `out − aux`, as in model 2.
- **One reverb shared by the voices** (since strings6; `SharedReverb` in `src/op1_voice.*`, bookkeeping in `op1/reverb_bus.h`), as upstream runs one reverb on the sum of its voices. Until strings5 each voice ran its own, which cost ~4× the CPU with 4 voices and glitched on the device beyond polyphony 1 (Test 9). The OP-1 renders voices separately, one call each per block, and applies each voice's envelope after the call, so:
  - each voice plays `dry (1 − amount)` and adds its dry signal to the send; its share of `amount × wet` is mixed in before its limiter, as upstream limits dry + wet;
  - a block ends when a voice renders a second time (any render order); the reverb runs once on that block's send, and the next block's carriers share its wet signal equally. The wet signal is one block late (64 samples at 44.1 kHz, 1.45 ms);
  - a new voice takes no share in its first block, then ramps in with the others, so the shares always sum to 1; a steal hands the stolen voice's share to the voice that takes over its ring (`Transfer`). A voice fading out after a steal neither sends nor shares;
  - a reverb idle for 50 ms (nothing sounding in this model) restarts empty;
  - **"all" still plays 4 voices** in this model (Rings' maximum polyphony, chosen with the per-voice reverbs).
  - `make test-reverb-bus` checks it on the host against a second reverb fed the same sends: one run per block in shuffled render orders, the carriers' sum within 10 LSB of the reference through joins and steals, no runs without carriers, the restart after a gap.
- **The buffer is indexed without wrapping** (`write_ptr + offset`, 54,400 words, 109 KB): every access is a fixed offset from the write pointer. Data is read at most 6,312 samples after it's written, so only a write within 6,312 samples of the write pointer's wrap also goes `kSize` further on, where the read after the wrap finds it. Values are stored with a saturating shift (floored where upstream truncates towards zero: 1 LSB).
- **Assembly** (`src/reverb_bfin.S`) for the settled case; C (`ProcessAsm`) splits blocks where the LFOs step (every 32 samples) and at the wrap zone's edges. The C loop stays for a reverb within 6,312 samples of a Clear, and as the reference (the Blackfin checksum must equal the host's).
- **`Clear()` is O(1):** every read fetches what the same delay line wrote `k` samples before, so after a clear, reads further back than the samples written since return 0, as upstream's zeroed buffer would, until the longest delay (6312) has been written through.
- **LFOs:** upstream's approximate cosine oscillators, stepped every 32 samples, at their true rate (sqrt(32)/(2π) of nominal) and in Hz at the OP-1's rate; the delay lengths stay in samples, as upstream's.
- **Match** (`make test-reverb`, 48 kHz): wet level within 0.02 dB; 27–52 dB SNR, lowest for the longest, brightest tail (one-LSB differences recirculate through the 16-bit buffer at loop gain 0.98). A reverb cleared after loud noise matches a fresh one.
- **Cost:** ~230 cycles per sample settled (assembly); ~390 in the C loop just after a Clear.

## CPU meter (device tests)

`op1/cpu_meter.h`: the engine's share of time, from the cycle counter around `RingsRender` and `RingsNoteOn`, over windows of four blocks, peak held and decaying ~0.6 s. A white-encoder click shows it on the screen's six rings, each 15% of a block. It shows the engine's own share only.

## Remaining work

- **Device CPU** after Test 8 decides what's next. Left in C: the stiff string's full loop (structure below 0.24 or above 0.81, ~375 cycles per sample), the reverb right after a Clear, the modal model's per-block coefficient maths. The plain string kernel could gain a little more by keeping state in registers across samples.
- **Trim per-voice overhead:**
  - idle-voice skipping;
  - recompute the exciter only on change;
  - optionally fewer modes per voice (Rings itself uses `64/poly − 4`).
- **Done since:** the device tests, the Rings screen, and model and polyphony select on encoder clicks (`docs/device-tests.md`, Tests 2–5; `docs/HANDOVER.md`).
- **Sample rate.** Upstream assumes 48 kHz. The port takes `OP1RINGS_SAMPLE_RATE` (default 44100). The lookup tables don't depend on sample rate.

## The 22.05 kHz build (2026-10-26, Test 51)

A test build for the user's question whether half the rate is audible and whether it lets Rings and
Clouds run together. `src/rate_config.h` is the one place the rate enters; `make op1-seq-r22` builds
it (`-DOP1RINGS_SAMPLE_RATE=22050 -DOP1RINGS_CONTROL_BLOCK_SIZE=32 -DOP1RINGS_UPSAMPLE=4`), and every
check target takes `RINGS_RATE_FLAGS`. What had to change to keep the sound, found by rendering the
same note at both rates in the simulator (`tests/sim_engine.cc`, `sim-model M=`) and comparing band
energies: the reverb's delay lines (sample counts, C and assembly), the limiters' coefficients, the
modes' Q (upstream's `1 + f q` is in fractions of the rate: without the halving every mode rang
twice as long, +2.4 dB and a thick midrange), the string FIR (remapped; without it 2.8 dB quieter
and 4 dB duller in 2-5 kHz). What did not need changing, and why: the strike (upstream's `1 / fc`
already makes its area rate-independent; scaling it cost 5 dB), the exciter's noise coefficient
(scaling it moves the level, not just the colour). The 4x upsampler is two stages: a 47-tap
half-band FIR (`resources/gen_halfband.py` -> `src/halfband_taps.h`, Kaiser 60 dB) then the 2x
Lagrange stage, so images stay below -59 dB; the response is flat to 9 kHz and -6 dB at 11 kHz.
CPU in the simulator, six voices: modal 123 -> 82 MHz, string 89 -> 68, string + reverb 76 -> 54,
FM 95 -> 68. The 44.1 kHz build is unchanged: its load segment is byte-identical to the shipped
image. Test 51 in `docs/device-tests.md` has the listening procedure.
