# Mutable Instruments Streams as an OP-1 effect

Status 2026-10-16: **host and simulator work done; nothing on the device, no package, nothing committed.** The code is `ports/streams/` (README there for the build). This doc covers what Streams is (digital and analog halves), the effect built from it, its verification, its CPU cost, and the OP-1 glue design with the hosting options. Project context: `docs/HANDOVER.md` (§1 rules, §3 the effect interface, §4 the environment).

## 1. Summary

| Item | Result |
|---|---|
| Upstream copy (`src/dsp/`) vs upstream, 31089 Hz | **Bit-identical**: 24 cases (6 functions × normal/alternate × single/linked) × 186,534 samples, per sample and in 64-sample blocks, as upstream and with the exact speed-ups (`make copy-check`) |
| Unmodified integer processors, host vs Blackfin (`bfin-elf-run`) | **Bit-identical**: 24/24 checksums (`make bench-check`) |
| Port (processors retimed to 44.1 kHz + fixed-point VCA/SVF), host vs Blackfin | **Bit-identical**: 66/66 checksums; 22.05 kHz profile 66/66; OP-1 entry points 9/9 (`make sim-fx`) |
| Port vs float reference (upstream processors at their own 31089 Hz driving an ideal VCA/SVF) | 21 scenarios: vactrol 57–74 dB SNR, envelopes 37–77 dB, compressor 28–55 dB, follower 30–47 dB, filter controller 37 dB; Lorenz diverges after 0.5 s (chaotic) with matching statistics (§5) |
| Fixed-point audio path alone (port's own control codes through the float VCA/SVF) | 43–90 dB SNR |
| CPU, simulated, per 64-sample call (§6) | Processors at 44.1 kHz: mono 15.5k (filter controller) to 64k (follower) cycles average, most functions 28–37k; stereo +4–6k. Processors at 22.05 kHz: mono 15–38k, most 19–28k. **Device ≈ 1.6× → ~35–50 MHz for most functions, 70–80 MHz for the follower (44.1 kHz); ~25–35 and 40–50 MHz (22.05 kHz).** Stock phone costs ~16 MHz (synth) / ~35 MHz (master) |
| Image size (freestanding link at `0x02c00000`) | 11.1 KB `.text`, 20.7 KB `.rodata`, 36 B `.data`, 6.9 KB `.bss` (three positions); no static constructors (`make op1-size`) |
| Licence | **MIT** (every Streams source file; the brief said GPLv3, which doesn't match the files) |

## 2. Streams: what's digital, what's analog

Streams is a two-channel "dynamics gate": each channel is an analog voltage-controlled filter + amplifier (Streams' "VCFA") whose two control voltages come from an STM32F1. **The audio never goes through the microcontroller.**

**Hardware** (from `drivers/`, `streams.cc`, `gain.h` and the part list in `hardware_design/pcb/streams_v05.sch`, a binary Eagle file whose strings are readable but not its netlist):
- The ADC scans six channels (per channel: EXCITE, IN, and the VCA control voltage for the meter) at **31,089 Hz** (`drivers/adc.cc`: 12 MHz ADC clock, 71.5 + 55.5 + 28.5 cycles per channel pair: `resources/lookup_tables.py` uses `sample_rate = 31089`). Each scan calls `Process()` (`streams.cc`) once per channel.
- Outputs per channel: a **DAC8552** 16-bit code to the VCA control ("gain"), and a TIM1 **PWM** duty cycle, low-passed into the filter's cutoff control ("frequency", `pwm.Write(i, 65535 - frequency)`).
- The VCFA: two **V2164/SSM2164** quad gain-cell chips (8 cells, 4 per channel), TL072/TL074/MCP6004 op-amps. The filter's topology isn't recoverable from the binary schematic. The manual calls it an "analogue, voltage-controlled filter and amplifier" in the spirit of the Buchla low-pass gate; it has no resonance control.
- Analog-only controls: the **RESPONSE** pot (VCA exponential ↔ linear), **LEVEL MOD** (attenuator on the LEVEL CV input, normalled to +8 V, summed into the VCA control), and the jacks.

**What the firmware computes** (`processor.h`): per sample, `Process(audio, excite, &gain, &frequency)` gives two 16-bit codes:
- **gain**: 32767 = unity (`kUnityGain`). With the exponential response, 256 codes = 1.55 dB (`compressor.cc` `kGainConstant`), so 991 codes per octave and ~+18 dB at the top (`gain.h`). With the linear response, gain = code / 32767.
- **frequency**: 0 = closed, 65535 = open.

| Function | What it does (upstream file) | Uses |
|---|---|---|
| Envelope | AD (or AR in the alternate bank) adapted from Peaks: a Schmitt trigger on EXCITE (threshold 13,653) fires it; EXCITE's level speeds it up and adds level-dependent saturation. SHAPE = attack/decay contour, MOD = brightness (`envelope.*`, `meta_parameters.h`) | gain and cutoff |
| Vactrol | A photocell model: first- and second-order lag with asymmetric attack/decay, a 60 s memory ("sensitised") state, an overshoot term, a Gompertz curve to gain and a squared one to cutoff. Alternate: "plucked", EXCITE triggers a decaying pulse (`vactrol.*`) | gain and cutoff |
| Follower | EXCITE is split by two integer SVFs into <110 Hz, 110–1174 Hz and >1174 Hz bands; each band's energy is peak-ridden, then low-passed with attack/decay; the sum's square root is the gain, the spectral centroid moves the cutoff. Alternate: the centroid only, sent to the gain (`follower.*`, `svf.*`) | gain and cutoff |
| Compressor | Detector on EXCITE² (or IN² when EXCITE is silent), attack/decay low-pass, log2 domain, threshold/ratio/soft knee, makeup gain, in the exponential VCA's units. Alternate: slower, soft knee (`compressor.*`) | gain only (cutoff open) |
| Filter controller | Alternate bank only: cutoff = SHAPE + EXCITE × MOD; gain 0, so the level comes from the analog LEVEL CV (`filter_controller.h`) | cutoff |
| Lorenz | Easter egg (secret handshake): a Lorenz attractor (Euler steps) on gain and cutoff (`lorenz_generator.*`) | gain and cutoff |

Linked ("Siamese") mode: both channels share the function; the pots become **globals**: channel 1 SHAPE = attack, channel 2 SHAPE = decay, the MODs threshold and ratio for the compressor (`Configure(..., globals)`).

**Upstream details found on the way:**
- `Processor::set_global` sets `dirty_ = linked_`, so in single mode it *clears* a pending `set_parameter` unless it comes first. Upstream's UI calls `set_global` before `set_parameter`; the port does the same.
- Plucked vactrol: `fast_attack_coefficient_ <<= 4` on an int32 overflows for the 0.1 and 0.2 ms entries (wrapping to 0.406 and 0.379 per sample at 31089 Hz). At 44.1 kHz entry 0 would wrap *negative*; the port keeps upstream's effective values (retimed table).
- `Vactrol`'s `sensitivity = 1 << 31` is INT_MIN (negative) in int32.
- The follower divides once per sample (`centroid_numerator / denominator`).

## 3. The effect

```
audio --+--> detector: SENSE gain (0..+36 dB), raw or peak envelope --> EXCITE
        |                                                                 |
        |   upstream processor (retimed), per sample or every 2nd: gain code, cutoff code
        |                                        |                         |
        +--> VCA (2164 model: exp <-> lin, RESPONSE) --> SVF (LP/BP) --> OUTPUT trim
        |                                                                  |
        +--------------------------------- dry/wet MIX <-------------------+
```

- **Input:** the effect's own audio (self-sidechain); the OP-1 gives an effect no other input, so there's no external CV. SENSE scales the detector, which matters because Streams' thresholds assume Eurorack levels: the trigger threshold is −7.6 dBFS and the follower's envelope saturates near −3 dBFS.
- **Detector modes:** *raw* is the audio itself, as when audio is patched into EXCITE; with it, the envelope's Schmitt trigger re-fires every cycle while a note is loud. *Peak envelope* (rectified, 0.36 ms attack, 23 ms release, ours) gives one trigger per note, so the envelope and plucked vactrol react to transients. It applies to the envelope, vactrol, filter controller and Lorenz; the follower (it analyses the spectrum) and compressor always get raw audio.
- **Stereo (master), linked detection from one core:** follower and filter controller detect the mid signal (they need a waveform); the others detect whichever side is larger, sign kept. One processor and one set of gains and coefficients serve both sides; each side has its own filter state.
- **VCA:** gain = blend(exp, lin) with RESPONSE (0 = exponential, 32767 = linear; the compressor is always exponential, as the manual requires). Exponential: `2^((code − 32767) / 991)` from upstream's `lut_exp2`, capped at +18 dB, silent below −96 dB. The filter controller, whose upstream gain is 0 (level from the analog LEVEL CV), gets unity. Total gain is capped at ×8.
- **Filter: Andrew Simper's zero-delay-feedback (TPT) state-variable filter**, 2-pole:
  - `g = tan(π fc / fs)`, `a1 = 1/(1 + g(g + k))`, `a2 = g·a1`, `a3 = g·a2`: all three in [0, 1), so Q31 tables (257 cutoff codes × 2 dampings, `resources/gen_core_tables.py`) and no division at run time;
  - per sample: 4 multiplies (5 for band-pass), unconditionally stable up to Nyquist, low-pass and band-pass from the same states;
  - cutoff map: `20 Hz × 2^(10 × code / 65536)` (20 Hz – 20.5 kHz, capped at 0.45 fs); dampings k = √2 (Butterworth, "flat", the LPG-like default) and k = 0.5 (Q = 2, resonant); band-pass is scaled to unity peak.
  - Why not the alternatives: a Chamberlin SVF needs 2× oversampling to stay stable above ~fs/6, which doubles its cost and still caps the open cutoff near 15 kHz. A ladder is 4-pole, wants a nonlinearity and costs more, and Streams' VCF is a gentle low-pass gate, not a resonant ladder. Stmlib's TPT form needs `1/(1 + 2Rg + g²)`, a division per coefficient update; Simper's coefficients come straight from a table.
  - Coefficients are looked up for every 4th sample and stepped linearly (a quarter of the table work, ~2 samples of extra lag), except for the filter controller, whose cutoff moves at audio rate.
  - The compressor and the follower's cutoff mode bypass the filter (their cutoff is always open).
- **Order VCA → SVF:** the filter smooths the VCA's fast gain steps (vactrol and envelope attacks), as a low-pass gate does.
- **Formats:** int16 in and out; Q25 in int32 inside the VCA and filter (±64: room for +18 dB, the resonant peak and the doubled SVF states); gains Q16; output saturated to 16 bits; dry/wet ramped per block.
- **Controls** (`Controls`, all 0..32767): SHAPE, MOD (upstream's two pots), SENSE, MIX; ATTACK and DECAY (upstream's linked-mode globals, active with "split times"), RESPONSE, OUTPUT trim (−12..+12 dB); function, alternate, split, filter mode, detector.

## 4. Retiming

Upstream runs every processor once per sample at 31089 Hz. `OP1STREAMS_SAMPLE_RATE` (`src/dsp/retime.h`) selects 31089 (upstream, unchanged), **44100** (the port: every sample) or **22050** (the cheap profile: every other sample, EXCITE averaged over pairs, outputs interpolated back). What changes:
1. **Tables** (`lut_env_increments`, `lut_lp_coefficients`, `lut_svf_cutoff`, `lut_svf_damp`, `lut_lorenz_rate`) are regenerated from upstream's formulas by `resources/gen_tables.py` (stdlib only). At 31089 Hz it reproduces all five of upstream's tables exactly. Lorenz's Euler step is scaled by 31089/rate.
2. **Hard-coded one-pole coefficients** (compressor sidechain decay 14174; vactrol 67976239, 138132, 1151) become `1 − (1 − c)^(31089/rate)`.
3. **Shift smoothers** (`x += (t − x) >> s`, ten places: parameter smoothing, follower spectrum and centroid, envelope gate level and rate modulation, Lorenz amounts) go through `Slew()`. That's upstream's floored shift, then scaled by 31089/rate with rounding. The rounding matters: a first version scaled with floored shift-adds, which turns a −1 step into +1, and the envelope's rate modulation crept up until its decays ran 6% fast (found by the trace comparison, §5).
4. **Plucked vactrol's fast attack:** upstream's effective (wrapped) values, retimed (`lut_vactrol_pluck_attack`).
5. **Speed-ups.** Exact, proven bit-identical by `copy-check` (`OP1STREAMS_FAST`):
   - `SIGNBITS` instead of shift loops in `Compressor::Log2`;
   - a 32×32→64 product from 16-bit partials instead of libgcc's `__muldi3`;
   - 32-bit storage for the follower's accumulators;
   - the analysis SVF inlined;
   - `Processor::ProcessBlock`, which dispatches once per block instead of once per sample.

   Inexact: the follower's centroid division runs every 8 samples (every 4 at 22.05 kHz).

Everything is integer, so the 44.1 kHz build is "upstream at another rate", not a re-derivation.

## 5. Verification

**Copy check** (`make copy-check`, `tests/copy_check.cc`, `harness/drive.h`): upstream's `streams::Processor` and the copy run side by side at 31089 Hz on deterministic material (integer drums, riff, pad and mix generators, `harness/signals.h`). The knobs change every 4096 samples over 16 values, ends included. All 24 cases are bit-identical per sample and through `ProcessBlock`, both as upstream and with FAST. Each case reports its gain and cutoff ranges and change counts, so a pass can't come from constant outputs. (The first run did have constant cases: it called `set_parameter` before `set_global`, §2.)

**Host = Blackfin** (`make bench-check`, `make sim-fx`): checksums of the processors through the drive run (unmodified: 24; retimed: 24) and of the full effect for 21 scenarios forced mono and stereo (42), per profile. The OP-1 entry-point test checks the parameter encoding, the gap restart, the drum init and the factory-default parameters. All identical. **Found on the way: GCC 14 for Blackfin compiles `x < 0 ? -x : x` on an int16-range value to `ABS (V)`, the saturating 16-bit abs, so |−32768| = 32767 on Blackfin but 32768 on the host** (reproduced in isolation: `R0 = ABS R0 (V)`). It broke the test generator and would have broken the detector; both now use a symmetric abs. **Worth adding to `docs/HANDOVER.md` §4's gotchas.**

**Against the reference** (`make ref port compare`, `harness/ref_model.h`). The reference is upstream's unmodified processors at their own 31089 Hz: EXCITE is resampled (cubic) to 31089 Hz, the codes are interpolated back to 44.1 kHz, then an exact VCA and an exact-tan SVF in double. So the comparison covers retiming, fixed point and tables at once. 4 s per scenario:

| Scenario | 44.1 kHz SNR | 22.05 kHz SNR | Gain trace, rms |d| dB (44.1) | Cutoff, rms |d| oct (44.1) |
|---|---|---|---|---|
| comp_drums | 28.3 | 24.2 | 0.37 | bypass |
| comp_drums_makeup | 32.4 | 28.5 | 0.24 | bypass |
| comp_soft_parallel | 44.7 | 41.5 | 0.17 | bypass |
| comp_riff | 43.9 | 41.4 | 0.06 | bypass |
| comp_limiter | 29.5 | 26.8 | 0.29 | bypass |
| comp_split_st (stereo, split times) | 55.3 | 49.2 | 0.02 | bypass |
| vactrol_plonk (plucked) | 71.1 | 71.8 | 0.27* | 0.007 |
| vactrol_lpg | 74.1 | 75.9 | 0.001 | 0.001 |
| vactrol_backwards | 71.7 | 76.8 | 0.001 | 0.002 |
| vactrol_plonk_st | 56.8 | 54.3 | 0.25* | 0.004 |
| follower_wah (LP resonant) | 43.1 | 37.9 | 0.15 | 0.009 |
| follower_wah_bp (BP resonant) | 30.4 | 24.0 | 0.15 | 0.009 |
| follower_cutoff | 47.1 | 43.8 | 0.04 | bypass |
| follower_wah_st | 39.3 | 30.5 | 0.11 | 0.020 |
| env_transient | 37.0 | 42.5 | 1.10 | 0.019 |
| env_transient_exp | 37.7 | 51.2 | 0.25 | 0.019 |
| env_raw | 66.9 | 63.1 | 0.04 | 0.022 |
| env_ar | 76.5 | 79.1 | 0.001 | 0.001 |
| env_transient_st | 50.1 | 44.3 | 0.57 | 0.004 |
| filter_ctrl_bp | 36.5 | 22.4 | 0 | 0.012 |
| lorenz_pad | 5.6 | 4.5 | (chaotic) | (chaotic) |

\* one onset sample; the 99th percentile is 0.001 dB.

Level differences are within ±0.3 dB everywhere (±0.5 dB at 22.05 kHz).

Reading it:
- **Compressor on drums:** the 0.2 ms attack makes the detector nearly a peak detector of EXCITE², and hats' inter-sample peaks differ between 31 kHz and 44.1 kHz sampling (the port compresses 0.3 dB more; at 22.05 kHz 0.5 dB less). The same compressor on the riff (no hats) tracks to 0.06 dB, and the slow split-times one to 0.02 dB. This is the sample rate, not the port: Streams' own ADC sees different peaks again.
- **Follower:** its band energies see the same sampling effect; resonant band-pass magnifies 0.01-octave cutoff differences.
- **Envelope (linear response):** the residual is in the decay tails below −30 dB, where a few ms of timing is several dB.
- **Lorenz:** sample-identical for the first ~0.5 s, then it diverges, as any chaotic system does under a different step size. Over 4 s the statistics match: gain mean −5.0 vs −5.2 dB, both SD 11.4 dB; cutoff mean 6.00 vs 6.21 octaves above 20 Hz.
- **Fixed-point audio path alone** (`render port` prints it: the port's own codes through the float VCA/SVF): 60–90 dB, except resonant band-pass (43 dB) and the follower and vactrol in stereo (60 dB). Most of that is the 4-sample coefficient stride; per-sample coefficients gave 62–90 dB.

Tools: `harness/ctl_diff.py` (where two gain traces diverge), `make spec` (phase-blind band spectra with Rings' `compare.py`). Listening material: `out/input/` (dry), `out/ref/`, `out/port/`, `out/port22/` (float WAVs, gitignored).

## 6. CPU

Simulated cycles per 64-sample call (`make bench`, 1 s per scenario, `bfin-elf-run`; the OP-1 makes 689 calls per second per position). The device ran ~1.5–1.8× the simulator's figures for Rings (`docs/HANDOVER.md` §2); the last column uses 1.6× on the peak. Table from `harness/cycle_table.py build/bench_final.log`:

| Scenario | 44.1k mono avg / max kcyc | 44.1k stereo | 22.05k mono | 22.05k stereo | device est. peak MHz (44.1k mono / stereo; 22.05k mono / stereo) |
|---|---|---|---|---|---|
| comp_drums | 34.7 / 34.9 | 38.2 / 38.6 | 25.1 / 25.3 | 28.6 / 29.0 | 39 / 43 / 28 / 32 |
| comp_soft_parallel | 35.5 / 37.5 | 39.6 / 41.8 | 25.7 / 26.9 | 29.9 / 31.1 | 41 / 46 / 30 / 34 |
| comp_limiter | 34.2 / 34.3 | 37.7 / 38.0 | 24.6 / 24.8 | 28.1 / 28.4 | 38 / 42 / 27 / 31 |
| vactrol_plonk | 36.8 / 37.4 | 42.3 / 42.9 | 24.4 / 24.8 | 29.9 / 30.3 | 41 / 47 / 27 / 33 |
| vactrol_backwards | 34.8 / 38.0 | 39.9 / 43.3 | 23.3 / 25.2 | 28.4 / 30.5 | 42 / 48 / 28 / 34 |
| follower_wah | 62.4 / 64.7 | 66.4 / 68.7 | 37.0 / 38.5 | 41.0 / 42.4 | 71 / 76 / 42 / 47 |
| follower_wah_bp | 64.0 / 66.3 | 69.5 / 71.8 | 38.5 / 40.0 | 44.0 / 45.5 | 73 / 79 / 44 / 50 |
| follower_cutoff | 58.7 / 61.3 | 60.9 / 64.1 | 33.2 / 35.2 | 35.4 / 38.1 | 68 / 71 / 39 / 42 |
| env_transient | 31.3 / 31.5 | 36.6 / 37.0 | 21.8 / 22.0 | 27.1 / 27.5 | 35 / 41 / 24 / 30 |
| env_transient_exp | 37.7 / 41.3 | 43.1 / 46.8 | 28.2 / 31.7 | 33.6 / 37.2 | 46 / 52 / 35 / 41 |
| env_raw | 27.7 / 29.8 | 33.2 / 35.3 | 18.7 / 20.2 | 24.2 / 25.7 | 33 / 39 / 22 / 28 |
| env_ar | 31.5 / 32.3 | 37.0 / 37.8 | 21.8 / 22.4 | 27.3 / 27.9 | 36 / 42 / 25 / 31 |
| filter_ctrl_bp | 15.5 / 15.5 | 21.0 / 21.0 | 15.1 / 15.1 | 20.6 / 20.6 | 17 / 23 / 17 / 23 |
| lorenz_pad | 36.9 / 37.1 | 42.2 / 43.5 | 23.4 / 23.9 | 28.7 / 29.6 | 41 / 48 / 26 / 33 |

(All 21 scenarios are in `build/bench_final.log`; the ones left out repeat these figures within 1k.) The entry points in the simulator (`make sim-fx`: mono follower/vactrol/defaults with knobs sweeping, stereo compressor) give mono 50.1k average / 66.6k peak and stereo 38.3k / 41.1k at 44.1 kHz; with the 22.05 kHz profile, 31.9k / 41.3k and 28.7k / 31.3k.

**Where the cycles go** (44.1k mono, per call; the profile counters in `bench`):

| Stage | Cycles | Notes |
|---|---|---|
| Apply + detector | 1.9k raw, 4.0–4.3k peak envelope | |
| Processor | follower 54k, Lorenz 29k, vactrol 24–27k, envelope 20–21k, compressor 21k, filter controller 3.6k | ~850, ~450, ~400, ~330, ~320, ~57 cycles per sample |
| Gains + coefficients | 2.4k (linear VCA), ~9–10k (exponential VCA: compressor, exp response), 4.7k (filter controller: per-sample coefficients) | |
| VCA + filter + mix | 3.7k LP, 5.2k BP, 1.9k bypassed; ×2 in stereo | ~58–82 cycles per sample per channel |

**Why the processors are this heavy**, measured on the simulator (`build/scratch/micro*.cc`):
- a 32-bit `R *= R` costs 5 cycles;
- a libgcc division ~200;
- a taken, unpredicted branch ~8 extra;
- a load ~3–4 with its use stall.

The follower's per-sample path is ~200 instructions, including ~9 32-bit multiplies, ~19 taken branches and ~19 loads (instruction trace, `--trace-insn`). The rest of its ~850 cycles is address-register stalls: GCC runs out of data registers and does arithmetic in P/I/M/B registers. `-O3` gains 9% on the follower only.

**Against the OP-1's budget** (Test 16):
- stock phone costs ~35 MHz on the master and ~16 MHz as a synth effect, and Streams gets that back;
- free CPU is ~300 MHz with nothing playing, but only ~6 MHz (master) / ~58 MHz (synth) with Rings model 6 × 4.
- At 44.1 kHz Streams estimates 17–79 MHz on the device (most functions 35–50, the follower 70–80). At 22.05 kHz it's 17–50 MHz (most 25–35, the follower 40–50).
- So next to a light synth it fits easily; next to heavy Rings even phone barely fits on the master, and Streams wouldn't.

**Levers, in order of payoff:**
1. **The 22.05 kHz profile** (done; `CFG_PORT22`): about −10k (compressor, envelope) to −25k (follower) cycles per call, with the accuracy in §5.
2. **Assembly for the follower's band loop and the vactrol's per-sample update**, each with its C reference and a bit-exact check as usual. Keeping the state in registers removes most of the stalls: estimated 2–3× on those two (from ~850 to ~300–400 cycles per sample for the follower).
3. **The exponential VCA from one table indexed by the code** (a 1024-entry gain table over the useful 16-octave range, linear interpolation) instead of exp2 per sample: −6k to −7k per call for the compressor and exp response.
4. **A 32×32→64 multiply in 5 MAC instructions** (exact; the 4-instruction `MulQ31Fast` plus the low×low product) instead of four 5-cycle `*=`.
5. A coefficient stride of 8, and the 16-bit VCA multiply in assembly: small.

Clouds' load-governor idea applies: the core could pick the profile from the measured free time if both are linked (§8).

## 7. OP-1 glue (design only; `op1/streams_fx.cc` has the entry points)

### Entry points (written, tested in the simulator)
- `StreamsFxInit(this, position)`, `StreamsFxMono(this, buf, params)`, `StreamsFxStereo(this, l, r, params)`, the effect ABI of `docs/next-projects.md` §1.
- **State:** one `StreamsCore` per position (synth, drum, master), ~2.3 KB each, in the image's zero-filled .bss in SDRAM. **Nothing goes into the L1 object**: it belongs to the host effect, which must keep working when the user swaps back, and the position is enough to find the state. The synth and drum share the mono object (only the live instrument's effect exists): init's position says which one is live. If a pointer in L1 is preferred after all, it needs a free word in the host's object, which nobody has mapped.
- **Restart rule:** a gap of > 5 ms between calls (the effect was off) or an init gives a fresh core (Test 16: a switched-off effect isn't called, so its state goes stale).
- **No static constructors:** cores live in raw storage and are set up by `Init()`; the freestanding link has an empty `.init_array`.
- **Encoders:** the host slot's FX ranges apply to `fx_params[0..3]`. For phone they are blue 204..20480, green 3072..17408, white 1536..16896, orange 0..32767 (`docs/clouds-port.md` §5). The entry points normalise them to 0..32767 with constant scales.

### `fx_params[4..7]`
- Stock effects store them and never read them. Factory presets hold 8000 in all four; `op1.db` may hold 0.
- **Every word must stay within 0..32767**, the FX Ranges table's range for knobs 4..7: the effect LFO's apply function clamps into it (`docs/lfo-interface.md` §5).

| Word | Content |
|---|---|
| [4] | settings: bits 14..12 = 5 (the "Streams" marker; 8000 and 0 don't match, so they decode as defaults), bits 2..0 function, bit 3 alternate, bit 4 split times, bits 6..5 filter mode, bit 7 detector, bit 8 page (UI state) |
| [5] | ATTACK (split times) |
| [6] | DECAY (split times) |
| [7] | bits 14..8 RESPONSE (0..127), bits 7..0 OUTPUT trim (128 = 0 dB) |

**The effect LFO problem (applies to Clouds too):**
- `docs/lfo-interface.md` §5: an LFO targeting the effect adds its deltas to **all 8** values, k = 0..7, in the scratch copy at `0xff801724`. That copy is very probably what process receives: Test 16 found the synth effect's params pointer isn't the stored array.
- So with an LFO on the effect, settings bits in [4..7] would jitter, and the function would switch at random.
- **The process wrapper should take [4..7] from the stored values instead:**
  - synth: `0xff801444 + slot × 16`, with slot `[0xff8016d4]`;
  - drum: `0xff8015c4 + inst × 16`;
  - master: `0xff8016c4`.
- Keep [0..3] from the passed copy: LFO modulation of SHAPE, MOD, SENSE and MIX is a feature.

### Controls (proposal; the user decides)

| | Blue | Green | White | Orange |
|---|---|---|---|---|
| Page A (encoders, `fx_params[0..3]`) | SHAPE (compressor: threshold) | MOD (compressor: ratio / makeup) | SENSE | MIX |
| Page B (our hook writes [5..7]) | ATTACK | DECAY | RESPONSE | OUTPUT |
| Click | next function (shift: previous) | alternate bank (shift: filter mode LP/LP-res/BP/BP-res) | page A/B (shift: detector raw/envelope) | host swap (if hosted on phone's cycle) |

- Shift + turn is stock's fine adjustment (delta / 16) for encoders 0..3. Page B needs our knob hook to do the same.
- Turning ATTACK or DECAY switches "split times" on: upstream's linked-mode controls. A click on blue in page B could switch it off.
- Function order: envelope, vactrol, follower, compressor, filter controller, Lorenz. Upstream reaches the filter controller only in the alternate bank and Lorenz only by a secret handshake; here they're plain choices.
- Defaults (no marker): compressor, threshold at mid, ~2.5:1, linear response for the other functions, peak-envelope detector, LP.

### Hosting options (not decided)

| Option | Pros | Cons |
|---|---|---|
| **A. A third choice in phone's orange-click cycle** (stock phone → Clouds → Streams) | Displaces nothing; reuses the Clouds hooks already planned (phone's process/init words, the PhoneScreen words, the orange-click swap); "filter" stays free for op1repacker; both positions | Clouds and Streams can't run on the same position at once (one effect per position anyway, so this only matters if the user wants both on the master); a three-way swap is less discoverable; the screen hook has to draw two different screens; the [4] marker tells Clouds (its own marker), Streams and stock apart |
| **B. Another stock effect, swapped by its orange click** (e.g. cwo, or grid, spring, punch, nitro) | A separate menu entry with its own name and screen slot; Clouds and Streams on different types | Another set of vtable, screen and click hooks; the swapped stock effect needs the same "switch back" care as phone; that effect's FX ranges for encoders 0..3 apply (to read and normalise) |
| **C. "filter" (hidden type, needs its `fx_types` row)** | The name fits (a dynamics filter); unused today | Conflicts with the recorded decision to keep "filter" for op1repacker's filter mod |
| **D. Master only, or synth only** | Halves testing | Streams is useful in both: a master compressor/limiter, and LPG, envelope or wah on a synth |

**Recommendation: A**, because it reuses the Clouds hooks and displaces no stock effect. B with cwo is the alternative if the user wants Streams as its own menu entry.

### Code size
- Freestanding link (`make op1-size`, profile 44): **11.1 KB `.text`, 20.7 KB `.rodata`, 36 B `.data`, 6.9 KB `.bss`** (three positions).
- `.rodata` includes upstream's whole resource set, which the envelope's table-of-tables keeps alive: unused display tables (`wav_db`, `lut_2164_gain`) and the 31089 Hz rate tables could be trimmed, ~3 KB.

## 8. Remaining integration steps
1. **Decide** the host (§7) and the controls; draw a screen (a gain-reduction or gain meter and a cutoff indicator from `gain_codes()` / `cutoff_codes()` / `gain_reduction()`, plus the function name).
2. **Image restructure** shared with Clouds (`docs/HANDOVER.md` §3): one image, one patch table; Streams' `op1/streams_fx.cc` and `src/` join it. Shared code to factor: `fixed.h`, `cycles.h`, `support.cc`.
3. **The host wrapper:** process/init words of the chosen effect pointing at a dispatcher: the [4] marker selects stock / Clouds / Streams; init calls stock's init too. Take [4..7] from the stored arrays (§7).
4. **The FX page hooks:** clicks (function, alternate, page, detector, filter mode, swap) and page B's encoders writing `fx_params[4..7]` and saving through `0x19ff664` (synth). The master page's saving path is still to find (`docs/clouds-port.md` §5).
5. **CPU:** measure on the device (the cache!) with the CPU meter. If it's too heavy next to Rings, use the 22.05 kHz profile and/or the assembly levers in §6. A load governor could pick the profile at run time: both profiles can be linked side by side if the dsp copy is compiled twice under two namespaces.
6. **Device test** under `docs/device-tests.md`'s procedure. The user flashes; we prepare and verify.

## 9. Decisions for the user
- **Host:** A (phone cycle), B (another effect with its own swap), or C (filter).
- **Controls:** the page/click proposal in §7, and the default function.
- **CPU profile:** processors at 44.1 kHz (closer to upstream) or 22.05 kHz (about 10–25k fewer cycles per call), or both behind a governor.
- **Lorenz and the filter controller as plain functions:** upstream hides them.
- **fx LFO:** whether an LFO on the effect may modulate SHAPE, MOD, SENSE and MIX (proposed: yes; [4..7] are protected as above).
