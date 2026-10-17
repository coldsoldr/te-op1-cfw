# Porting Mutable Instruments Plaits to the OP-1

Status as of 2026-10-16: all 16 original Plaits engines (`plaits/dsp/engine`, not `engine2`) are converted to fixed point, compared with upstream, and measured in the Blackfin simulator, together with the voice (trigger, decay envelope, low-pass gate, post-processing) and a self-contained OP-1 glue layer for the MULTI slot. Host and simulator work only: nothing has been linked into a device image or run on the OP-1. The code is in `ports/plaits/` (see its README).

The Rings port (`docs/rings-port.md`, `ports/rings/`) is the model, and its `fixed.h`, `ffloat.h`, `compare.py`, `wav.h`, `cycles.h`, `op1/op1.h`, `op1/param_smoother.h`, `op1/link.ld` and `op1/support.cc` are used unchanged, by relative path.

## Summary

- **Accuracy.** Against upstream built with `double` (`ref64`; see "Verification"), the tonal engines mostly render at 50–90 dB waveform SNR. Per spectral band, 14 engines are within 0.6 dB of upstream; the noise engine is at 0.86 dB and the snare at 1.35 dB.
  - For swarm, noise, particle, the synthetic snare and some speech runs, upstream `float` and upstream `double` differ as much as the port does.
  - The port is clearly below that floor in a few places, all with matching spectra:
    - FM combo;
    - chords OUT in 5 scenarios;
    - modal notes;
    - string low triggers;
    - hi-hat.
- **CPU.** Measured as simulated MHz per voice in the full OP-1 path (voice, LPG, 2x upsampling): 35–70 MHz for most engines, 84–91 MHz for the analog drums, 136 MHz for modal, and 18 MHz for speech in its word-bank range. Only speech in that range fits six voices in the ~200 MHz simulated budget. So the glue needs a per-engine voice limit, and the expensive engines need assembly (see "CPU").
- **Size.** The glue image is 124 KB of code, 112 KB of read-only data (35 KB of it speech frames) and 110 KB of per-voice state for 6 voices (`make image`). There are no static constructors and no soft-float or division helpers.
- **Not done.** Linking into the MULTI dispatcher (`ports/rings/op1/engine.cc`), the screen, assembly kernels, and any device test.

## Layout and build

See `ports/plaits/README.md`. In short:

- `src/` is the upstream copy (namespace `op1plaits`, bit-identical renders: `make copy-check`, 112 files);
- `fixed/` is the port;
- `op1/` is the glue;
- `harness/` and `tests/` hold the comparisons, benchmarks and simulator tests.

Every target runs in WSL from `ports/plaits` (`make port compare`, `make ref64`, `make bench`, `make test-voice`, `make image`, ...).

## Internal rate and block size

| Build | Rate | Block | Used for |
|---|---|---|---|
| OP-1 (`OP1_DEFS`) | 44.1 kHz | 16 samples (4 per OP-1 call) | bench, test-voice, image |
| compare (`COMPARE_DEFS`) | 48 kHz (a0 from upstream's corrected 47872.34 Hz) | 12 samples (upstream's) | all comparisons with upstream |

**Why 44.1 kHz.** The OP-1 renders a voice as 128 samples at 88.2 kHz. Rings already runs at 44.1 kHz and upsamples 64 samples 2x with a 4-point Lagrange midpoint, and the same upsampler is used here (`op1/plaits_op1.cc`). The alternatives are worse:

- Running at 88.2 kHz doubles the cost of every engine, which the CPU table rules out.
- Running at 48 kHz needs a fractional 48 → 88.2 kHz resampler (more cost and its own artefacts).
- 22.05 kHz would audibly band-limit the oscillators.

Plaits is band-limited for 48 kHz (PolyBLEP, oversampled FM), so 44.1 kHz behaves the same way, with 8 % less headroom.

**Rate-dependent constants.** Everything that depends on the rate is computed from `kSampleRate`/`kSampleRateD` at compile time: note-frequency tables for 48 kHz and 44.1 kHz, filter frequencies, decay coefficients, delay sizes and the speech frame length. So the same source builds both configurations, and the OP-1 build plays in tune (A3 = 218–220 Hz measured in `test-voice`).

**Why 16-sample blocks.** 64 samples per call gives 4 blocks of 16. That is 0.36 ms per block, against upstream's 0.25 ms (12 at 48 kHz). Parameters are interpolated per block and triggers are block-quantised, as upstream, so the difference is only the slightly coarser granularity. Fewer per-block computations also save CPU. `kMaxBlockSize` is 24, so the engines' scratch buffers fit both builds.

## Numeric formats and primitives (`fixed/fixed_math.h`, `fixed/svf.h`)

**Formats:**
- Parameters are Q15 (0..32768). Notes are Q16 semitones.
- Frequencies are Q32 phase increments.
- Engine outputs are Q26 (±32 headroom; upstream's nominal ±1 sits at 2^26).
- Coefficients are mostly Q31, with per-module formats where needed. Examples:
  - Q25 particle states, fed by impulse injection;
  - Q23 speech filters;
  - Q30 string phase;
  - Q24 hi-hat noise clock.

**Primitives:**
- **Multiplies.** `Mul` is the 4-instruction 32×32 fract multiply (drops lo×lo, error < 2 LSB). `MulRound` is a rounded 7-instruction inline-assembly multiply with an `(IS)` accumulate, used in filter feedback. Its C reference is checked bit-exact on Blackfin and host in `make test-fixed` (0 mismatches).
- **Reciprocals and divisions.**
  - `Fraction`/`ReciprocalNormalized` use a 64-entry seed and 2 Newton steps; their saturation at x = 0.5 is handled.
  - `CountLeadingZeros` uses `SIGNBITS` (the BF524 has no CLZ, so `__builtin_clz` would call `__clzsi2`).
  - Divisions by block size use a constant reciprocal table.
  - No division instruction or helper is used anywhere (`make check-calls`, `make image`).
- **Pitch.**
  - `NoteToFrequency` uses a Q32 table per semitone plus upstream's 1/256-semitone low table, truncating like upstream.
  - `SemitonesToRatio` returns an FFloat (Rings' software float) for per-block maths.
- **Oscillator helpers.**
  - `Sine`: 16×16 interpolation over upstream's table, clamped to ±(1−2^-20) so peaks cannot overflow.
  - PolyBLEP in Q31.
- **Interpolators.** The parameter interpolator rounds its increment and writes back the target. Upstream writes back the value reached; with a truncated increment, that left a permanent 2×10⁻⁶ pitch offset. `InterpolatorReached` writes back the reached value, for loops that call `Next()` conditionally.
- **Noise.** `Random` uses stmlib's LCG, so noise sequences match upstream.
- **Filters (`fixed/svf.h`).**
  - Plaits' TPT state-variable filter in fixed point: g is a Q31 mantissa plus a shift (0..3), r is Q29, h is Q31.
  - `MulRound` in the feedback paths: truncation there biased low cutoffs, and rounding gained 30 dB.
  - The tan approximations (`FREQUENCY_ACCURATE/FAST/DIRTY`) are reproduced. The accurate one takes its first Horner step in Q18 to avoid overflow.
  - `Inject()` adds a large input (the particle engine's impulses) through its linear contribution to the states.
  - The filters are checked against a `double` TPT reference in `make test-filters`.

**Engine dispatch.** A union of the 16 engine classes plus one RAM block for their buffers (`fixed/engines.{h,cc}`, `kEngineRamSize` = 17 KB; the particle engine's diffuser needs 16 KB). The dispatch is a static switch generated from the X-macro list `fixed/engine_list.h`. There are no virtual functions and no constructors that run code, so nothing needs `.init_array`.

## Verification

Tools:

| Tool | What it checks |
|---|---|
| `make copy-check` | The copy in `src/` renders bit-identically to upstream (112 WAVs). |
| `make port` / `make ref` / `make ref64` | 7 deterministic scenarios per engine (`harness/scenarios.h`): note sequence, HARMONICS / TIMBRE / MORPH ramps, triggered hits with hashed parameters, a combination of LFOs with vibrato, low triggered notes. `ref64` is upstream compiled with `float` → `double` (`harness/ref64_prelude.h`). |
| `harness/report.py` | Per file: waveform SNR against upstream float, over the first 100 ms, and against `ref64`; plus the mean and 95th-percentile band-level difference. |
| `make voice-compare` | Voice level (trigger, LPG, decay, limiter, int16 output): the copy's `Voice` against `VoiceFixed`. |
| `make bench [E=n]` | Each engine at 44.1 kHz / 16 on Blackfin (`bfin-elf-run`), with a checksum that must equal the host build's; cycles per 64-sample call. |
| `make test-voice [E=n]` | The OP-1 glue in the simulator: 6 voice objects, 128-sample calls, checksum Blackfin = host, A3 pitch, cycles per call with 1 and 6 voices. |
| `make test-fixed`, `make test-oscillators`, `make test-filters` | Primitives, the inline assembly against its C reference, oscillators against stmlib, SVF against a double TPT. |
| `make check-calls`, `make image` | No soft-float and no division helpers are reachable; the freestanding image has an empty `.init_array`. |

`render_port` was also run under AddressSanitizer (`setarch x86_64 -R` is needed under this WSL), clean on the engines tested (0, 1, 2, 5, 6).

**How to read the SNRs.** For some engines, upstream's float output is itself sensitive to rounding, so waveform SNR against float is not a measure of port quality:

- float phase accumulators drift;
- FM with feedback is chaotic;
- the swarm's random grains, sample-and-hold timing of noise clocks, chord voice reassignment, and the synthetic snare are all sensitive.

`ref64` separates the two cases. Where upstream float and upstream double disagree by a similar amount, the port can't be expected to match either one. The band-level difference then shows whether the sound is the same.

## Engine status

Columns:
- **OUT / AUX vs ref64**: waveform SNR in dB against upstream built with `double`, as the range over the 7 scenarios (`harness/report.py`, final run).
- **Float vs double**: the same measure for upstream `float` against upstream `double`, which is the floor that upstream's own rounding sets.
- **Bands**: the largest mean per-band level difference over the scenarios and both channels.
- **CPU**: simulated MHz per voice at 44.1 kHz.
  - **Engine** is the engine alone: the `make bench` average over 3 scenarios, with the peak call in brackets.
  - **OP-1 voice** is the whole glue path (`make test-voice`: voice, LPG, post-processing, upsampler and smoothing), for 1 voice and for 6 voices together.

| # | Engine | OUT / AUX vs ref64 | Float vs double (OUT / AUX) | Bands (dB) | Engine MHz | OP-1 voice MHz (1 / 6) |
|---|---|---|---|---|---|---|
| 0 | virtual analog | 36–88 / 31–84 | 34–52 / 30–48 | 0.07 | 25.8 (37.5) | 36.4 / 218 |
| 1 | waveshaping | 54–82 / 57–77 | 30–66 / 29–65 | 0.01 | 23.0 (24.3) | 34.9 / 210 |
| 2 | FM | 17–70 / 14–73 (combo 17 / 14) | 47–73 / 44–75 | 0.09 | 32.9 (33.0) | 44.9 / 269 |
| 3 | grain / formant | 60–92 / 60–89 | 30–74 / 25–58 | 0.00 | 47.3 (53.3) | 59.1 / 355 |
| 4 | additive | 60–77 / 65–77 | 29–69 / 36–74 | 0.02 | 56.3 (57.5) | 69.1 / 415 |
| 5 | wavetable | 68–92 / 48–59 (5-bit AUX) | 36–70 / 33–49 | 0.09 | 34.3 (38.4) | 45.2 / 271 |
| 6 | chords | 11–67 / 67–75 (OUT 11–26 in 5 scenarios) | 42–69 / 39–68 | 0.21 | 37.2 (44.3) | 51.8 / 310 |
| 7 | speech | 44–100 / 45–97 | −1–62 / 1–58 | 0.02 | 20.1 (44.2) | 18.0 / 108 |
| 8 | swarm | 8–28 / 18–45 | 20–42 / 31–58 | 0.53 | 38.4 (39.8) | 53.7 / 324 |
| 9 | noise | 21–93 / 25–59 | 13–103 / 11–103 | 0.86 | 29.4 (32.5) | 46.6 / 273 |
| 10 | particle | 42–84 / 73–106 | 38–124 / 107–149 | 0.25 | 53.9 (62.9) | 71.3 / 428 |
| 11 | string | 45–83 / 40–112 | 66–84 / 99–136 | 0.04 | 56.7 (65.5) | 57.3 / 344 |
| 12 | modal | 27–65 / 81–117 (notes 27) | 52–94 / 105–136 | 0.00 | 127.4 (131.3) | 136.1 / 824 |
| 13 | bass drum | 48–66 / 66–83 | 60–101 / 52–93 | 0.05 | 54.8 (78.7) | 84.3 / 502 |
| 14 | snare drum | 60–84 / 0–59 | 52–90 / 2–78 | 1.35 | 75.6 (84.9) | 91.2 / 546 |
| 15 | hi-hat | 7–32 / 9–51 | 27–38 / 34–46 | 0.60 | 53.0 (54.0) | 58.5 / 351 |

**Voice level** (`make voice-compare`: the copy's `Voice` against `VoiceFixed`, int16 output through the LPG and limiter). There are three cases: free-running (combo scenario), triggered through the LPG, and triggered with LEVEL patched. OUT channel, in dB:

| Engines | Free-running | Triggered (LPG / LEVEL) |
|---|---|---|
| waveshaping, grain, additive, wavetable, chords | 63–70 | 38–59 |
| string, modal, bass drum, snare | | 52–78 |
| FM | | 56–58 |
| virtual analog | 33 (its float-sensitive combo, as at engine level) | |

- Chords is at 14–17 dB triggered. This is the chord-reassignment issue above.
- Swarm, noise and hi-hat are at 6–38 dB: noise-clock sensitive, as at engine level.
- Speech is at −2 dB triggered. That run's first hit has TIMBRE at exactly 0.5, where the LPC clock runs at exactly 1/6 of the sample rate (`SemitonesToRatio(0) / 6`). LPC ticks then fall exactly on sample boundaries, and float and fixed point break the tie differently at the block where the trigger arrives. Upstream float takes the tick before the new word's first frame, the port after it, so the whole word plays one LPC sample (125 µs at 8 kHz) later against its frame boundaries. With the same delayed trigger at TIMBRE 0.45, 0.51 or 0.7, the engines match at 80–95 dB (`build/probe_engine 7 48 0.5 T 0.5 2`). The port's truncated clock rate behaves like upstream built with `double` in the untriggered runs (notes, 90 dB against `ref64`).

The triggered LPG runs lose most of their SNR in the decaying tails, where the outputs differ by int16 LSBs.

Every Blackfin run in `make bench` and `make test-voice` gives the same checksums as the host build.

### Notes per engine

- **Virtual analog, waveshaping, wavetable, additive, grain.**
  - Straight conversions of the oscillators: variable-shape oscillator, Plaits oscillator shapes, wavetables from int16 tables, the additive harmonic bank, the grainlet/formant oscillators.
  - Indices are clamped where upstream relies on float slack (waveshaper shape, chord waveform, wavetable x/y/z).
  - Waveshaping gain uses a saturating 64-bit multiply, because at TIMBRE = 1 it exceeds Q31.
  - The grain carrier phase saturates.
- **FM.** The 2-operator FM with feedback.
  - In the combo scenario (feedback and vibrato sweeping), the port is at 14–17 dB, while float vs double is 53–57 dB. The feedback path amplifies the port's rounding, but the bands still match within 0.09 dB.
  - This is the first place to look if a listening test finds FM feedback different.
- **Chords.** The chord oscillators (saw/square bank and wavetable).
  - OUT is at 11–26 dB in 5 scenarios, where float vs double is 42–51 dB. AUX is at 67–75 dB, and the bands are within 0.21 dB.
  - Earlier diagnosis: chord-inversion and voice-reassignment instants depend on rounding at thresholds, so a voice can switch a block apart. Not re-traced since.
- **Speech.** Converted last. All three synths:
  - the naive formant synth: impulse train, coloration band-pass, 5 resonant band-passes at Q 20 in Q23;
  - SAM: phase-reset formant sines with BLEP pulses;
  - LPC: 10th-order lattice at 8 kHz, with BLEP resampling of the LPC clock.

  Also converted: the word-bank playback and the hysteresis quantiser for the 5 banks.
  - **Word banks.** These are decoded at table-generation time by upstream's own decoder (`resources/gen_tables.cc`), into 2,512 shared 14-byte frames (35 KB, read-only). Upstream decodes a whole bank in one block when HARMONICS selects it. That spike was measured at ~140k Blackfin cycles in one call, before this change; peak per call is now 64k. Upstream also needs a 14 KB frame buffer per voice, which the port no longer has.
  - **Bugs found while converting**, each checked with traces against an instrumented copy of upstream:
    - Reflection-coefficient blending overflowed when k crossed from about −0.98 to +0.85, so the difference is now taken in Q30.
    - A Q16 pitch shift drifted the glottal phase, so it is now an FFloat.
    - A Q16 frame position quantised the k blend, so it is now Q31.
    - The impulse train's first sample (4·lp starts at 3) saturated Q30, so that shape's output is now Q29.
  - **Remaining differences.** Timbre and combo runs, at 44–46 dB, show isolated 100 ms segments at ~38 dB. There, an LPC clock tick, an excitation-pulse index (`int(32 · reset_time)`) or a block boundary lands one sample differently. Upstream float vs double has the same kind of events, at 15–60 dB.
- **Swarm, noise, particle, hi-hat.** These are driven by random numbers and clocked noise. The sequences match upstream (same LCG), but sample-and-hold and clock instants are threshold-sensitive. Judge them by the band spectra (swarm 0.53, noise 0.86, particle 0.25, hi-hat 0.60 dB), not the waveform SNR. The hi-hat sits below its float-vs-double range (7–51 against 27–46 dB). Its noise clock is the threshold-sensitive part.
  - Particle states are Q25, and impulses are injected into the SVF through its linear contribution.
  - The hi-hat noise clock uses Q24, because its 32× multiplier overflowed Q26.
- **String.** Karplus-Strong with dispersion and the stretch allpass. The fractional source phase is Q30 (Q31 overflowed at ratio 2). The low-trigger scenario is the weakest, at 45 / 40 dB (float vs double 72 / 128), with bands within 0.04 dB.
- **Modal.** The modal resonator (24 modes) with the exciter.
  - Stretch is Q25, harmonic accumulation is 64-bit, the cosine oscillator saturates, and `SvfHFast` goes to Q26 because high modes reach g > 4.
  - Notes scenario: 27 dB waveform SNR with 0.00 dB band difference. This is the per-mode coefficient rounding, not a tuning error.
- **Bass drum, snare drum.** Analog and synthetic models.
  - The analog bass drum's self-FM shifts after the multiply.
  - The snare's noise SVF uses Q26 near Nyquist.
  - The synthetic snare's sustain is chaotic in float vs double too.

### Known deviations from upstream (all deliberate, all inaudible in the comparisons)

- **Engine switching.** Changing engine runs that engine's `Init` (fresh state), as upstream does on a model change. Here it also clears the engine RAM.
- **Rounding.**
  - Parameter interpolators round the per-sample increment and write back the target (see above).
  - Values that upstream truncates from float are mostly truncated here too. A few fixed-point conversions round instead, which can move a 1/256-semitone or threshold boundary.
- **Block-rate updates.** In the noise engine, gains and filter coefficients are interpolated per block, where upstream interpolates per sample.
- **BLEP and FastSine.** BLEP times are clamped to [0, 1). FastSine's normalisation uses an exact reciprocal square root.
- **Word banks.** Speech word banks are pre-decoded (identical frames; checked by the unchanged comparison results).
- **OP-1 extension.** `VoicePatch::lpg_off` bypasses the LPG (see "Glue design").

## CPU

**Budget.** About 200 MHz simulated for all six voices. Device cycles are about 1.6× simulated (`docs/rings-port.md`, device tests), so this is roughly 320 MHz on the device.

**Glue overhead.** The full OP-1 path adds about 10–16 MHz per voice to the engine itself. That covers the trigger and decay envelope, the LPG (an SVF and a one-pole per sample), the limiter and int16 post-processing, the 2x upsampler, the encoder smoother, and the per-block engine dispatch. The engine-only and voice columns above differ by this amount.

Voices that fit in 200 MHz simulated, from the 6-voice measurements:

| Engines | Voices |
|---|---|
| virtual analog, waveshaping | 5 |
| FM, wavetable, noise, chords, swarm, grain, string, hi-hat | 3–4 |
| additive, particle | 2–3 |
| speech | 6 in the word-bank range of HARMONICS (108 MHz for 6); about 3 in the naive/SAM range |
| bass drum, snare drum | 2 |
| modal | 1 |

The two CPU columns use different material. The engine column averages the harness scenarios (all notes and parameter sweeps, 3 of them per engine in `bench`). The voice column holds one A3 note with every encoder at 0.5 and DECAY at 0.7. So an engine can cost more alone than in the voice column (string, at high notes). Peaks per call are in the `bench` and `test-voice` output; the 6-voice peaks are 1–23 % above the averages.

**Plan.**
1. **Voice limit per engine in the glue.** The MULTI dispatcher already latches the engine per voice. A table of maximum voices per Plaits model, with voice stealing above it, keeps the worst case under budget without assembly. Since 6 voices of almost any engine overrun, the glue needs this limit before going to the device, at least as a fallback.
2. **Assembly kernels**, in the Rings manner (C reference, bit-exact check, wrap tests with guard words for anything driving ring buffers). By expected gain:
   - the modal bank: 24 SVFs per sample, as Rings' `mode_bank_bfin.S` halved its C cost;
   - the additive harmonic bank (sine lookups + MACs);
   - the analog drums' resonators;
   - the particle filter;
   - the FM operator loop;
   - the string delay line with interpolation.

   Expect roughly 1.5–2× on the hot loops. Modal coefficient caching (recomputing only when the note or HARMONICS changes) would take a further large share off modal.
3. **Glue overhead.** Process the LPG only when it is on and the envelope is non-zero. Merge the upsampler with the int16 conversion. Skip post-processing of the unused channel (already done: `set_channels`).

## Glue design (`ports/plaits/op1/`)

`plaits_op1.{h,cc}` implements the two entry points the MULTI dispatcher calls for a voice whose latched engine is Plaits (`knobs[6]` = 3: phase 0, Rings 1, Braids 2, Plaits 3):

- `PlaitsRender(self, out, params)`: 128 int16 samples at 88.2 kHz.
  - Pitch is read per call from the voice's phase increment (`op1::PitchBuffer()[0]`, f / 88200 · 2^32). `IncrementToNote` turns it into a Q16 note using a log2 table (no division, no float).
  - The voice renders 4 blocks of 16 at 44.1 kHz, and the selected channel is upsampled 2x (4-point Lagrange, as Rings).
- `PlaitsNoteOn(self, knobs)`: latches the model and options from `knobs[4]`/`knobs[5]`, requests a retrigger, and snaps the parameter smoother.

`tests/sim_voice.cc` drives the glue exactly as the dispatcher would: voice objects at `0xffb00000 + 28 i` and the simulated increment buffer. `op1/image_check.cc` links it freestanding with Rings' linker script.

**Settings (`knobs[4]`, `knobs[5]`).** Both are stored as `setting · 2048` in the 16-bit knob slots, like Rings' model and polyphony settings.
- `knobs[4]` is the model, 0..15, in upstream's order.
- `knobs[5]` is a 4-bit option field:
  - 1 = LPG off;
  - 2 = play AUX instead of OUT;
  - 4 = play (OUT + AUX) / 2.

**Encoders (proposed; the glue reads only the four values).**

| Encoder | Turn | Shift + turn | Click | Shift + click |
|---|---|---|---|---|
| blue | HARMONICS | fine | next model | previous model |
| green | TIMBRE | fine | cycle the output option (OUT → AUX → mix) | toggle LPG off |
| white | MORPH | fine | CPU meter (as Rings) | |
| orange | LPG decay (Plaits' DECAY) | fine | | |

The clicks follow the Rings controls decisions (model selection on clicks, shift for fine adjustment). The click handling itself lives in the dispatcher's input path (`ports/rings/op1/input.cc`), which this work does not touch. The glue only reads `params[0..3]` and the two knobs. The parameters are smoothed over about 50 ms (Rings' `ParamSmoother`).

**LPG vs the OP-1's envelope.** Plaits' low-pass gate is part of its sound: the drums and plucks are "already enveloped", the other models get pinged by the LPG.

- **Default.** The LPG is on and is pinged by every note-on. DECAY is on the orange encoder; colour is fixed at 0.5 (a possible shift-orange later). The OP-1's own ADSR still multiplies the voice output, so for LPG sounds the natural setting is a fast attack and full sustain on the OP-1 envelope, letting Plaits shape the note.
- **LPG off (option 1).** The voice output bypasses the LPG and the decay envelope, so the OP-1 envelope alone shapes sustained models (virtual analog, wavetable, chords, ...). This is the `lpg_off` extension in `VoiceFixed`.
- **Gates.** The OP-1 reports no note-off to the engine, so each note-on retriggers the voice (`VoiceFixed::Retrigger`, a rising edge) and the trigger input is then held high (`trigger_patched` = true). Engines that use gate length (speech words, the hi-hat's and snare's gate behaviour) see a permanently held gate. The trigger delay is set to 1 block (no added latency).

**State and memory.**
- **Per-voice state.** Each voice has a `PlaitsVoiceState`: `VoiceFixed`, which holds the engine union and its 17 KB RAM, plus the upsampler, smoother, model and options. All six live in static arrays in the image's `.bss` (SDRAM), indexed by the voice object's position (`op1::VoiceIndex(self)`). Nothing is stored in the 28-byte L1 voice object.
- **Initialisation.** State is initialised lazily on the first call, because the image has no startup code and every type is trivially constructible. `.init_array` is empty (checked by `make image`).
- **Image sizes** (`make image`, 6 voices): `.text` 124,324 bytes, `.rodata` 111,600 bytes, `.data` 4 bytes, `.bss` 110,340 bytes. The `.bss` is about 18.4 KB per voice. The only libgcc routine linked is `___muldi3`, for 64-bit multiplies at control rate.
- **Speech data.** The word banks (35 KB of decoded frames) and the LPC phonemes are in `.rodata`, shared by all voices.

**Screen.** Not done. Plaits needs its own SVG/patch-table screen like Rings' (`docs/rings-op1-reference.md`). It should show:
- the model name (16 entries);
- HARMONICS / TIMBRE / MORPH / DECAY values;
- the output option and LPG state.

## Remaining integration steps

1. **Dispatcher.** Add Plaits as engine 3 in the MULTI dispatcher (`ports/rings/op1/engine.cc`): call `PlaitsRender`/`PlaitsNoteOn` for voices latched to 3, and link `ports/plaits/op1` and `fixed/` into the image. Check the combined image size against the free SDRAM and L1 budget used by Rings.
2. **Input.** Map the encoder clicks above, extending `input.cc`, and choose how `knobs[4]`/`knobs[5]` are presented (as Rings' model and polyphony settings are).
3. **Voice limit.** Add a per-model voice limit (CPU table above), or ship a first version with the cheap engines only.
4. **Screen.** Add the screen patch table and model names.
5. **Assembly, highest gain first.** Modal bank, additive bank, drum resonators. For each: a C reference, a bit-exact check in `test-fixed` style, and Blackfin = host checksums in `bench`.
6. **Device tests.** Pitch, CPU meter per model, LPG on and off with the OP-1 envelope, model switching while notes sound, and speech word banks.
