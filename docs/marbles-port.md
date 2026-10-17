# Marbles on the OP-1: port and integration design

Mutable Instruments Marbles' generators in fixed point for the OP-1's Blackfin, to run as an LFO hosted on the stock "random" LFO. Port: `ports/marbles/` (layout and commands in its `README.md`). LFO interface: `docs/lfo-interface.md`.

**Status (2026-10-16): host and simulator work only.**
- The generators are ported and verified against upstream.
- The Blackfin build matches the host bit for bit.
- The cost is measured in the simulator.
- The OP-1 glue (hooks, screen, controls) is designed (§6) but not written; nothing has run on the device.

## 1. What's ported

| Upstream | Port | Notes |
|---|---|---|
| `random/random_generator.h`, `random_stream.h`, `random_sequence.h` | `src/random_fx.h` | the LCG, the deja vu loop, replay (X2/X3 sharing X1's clock), rewrite, clone; bit-exact |
| `random/distributions.h` | `src/distributions_fx.h` | beta inverse-CDF sampling (spread, bias), the jitter distribution |
| `random/t_generator.*` | `src/t_generator_fx.*` | all 7 models (complementary/independent Bernoulli, clusters, drums, divider, three states, Markov), ranges, jitter, pulse width mean/std, deja vu, length |
| `random/x_y_generator.*`, `output_channel.*`, `lag_processor.*` | `src/x_y_generator_fx.*`, `src/output_channel_fx.*` | X1–X3 and Y; identical/bump/tilt control modes; the 4 internal clock sources; spread/bias/steps; smoothing; voltage ranges |
| `random/quantizer.*` | `src/quantizer_fx.*` | the weighted-scale quantizer; the 6 preset scales |
| `ramp/slave_ramp.h`, `ramp_divider.h` | `src/ramps_fx.h` | |
| `resources.cc` tables, stmlib's pitch-ratio tables | `src/tables_fx.cc` (generated) | converted exactly from the floats (`scripts/gen_tables.py`) |

**Not ported, deliberately:**
- the external clock inputs (`ramp/ramp_extractor.*`, copied in `upstream/` but unused): the OP-1 has no clock jack. `TGenerator::set_external_frequency()` lets the glue drive the master clock from the OP-1 tempo instead (§6.6);
- register / ASR mode (the X section sampling a CV): no CV input. It could later sample the played note;
- the scale recorder, the UI and the CV reader;
- `discrete_distribution_quantizer.*`: not used by the generators;
- scales whose base interval isn't 1 V: none of the presets has one.

**One intentional behaviour difference:** upstream's Y channel keeps the quantizer's default one-degree scale (weight 0). With Y steps above about 0.57, the selected level has no degree and upstream reads `voltage_[0xff]`, past the array: undefined behaviour, and it was the one case where host and Blackfin builds disagreed. The port quantizes to all degrees there (`src/quantizer_fx.cc`). The harness keeps Y steps below that range.

## 2. Control rate

`docs/lfo-interface.md` §4: an LFO's process is called **689.0625 times per second** (once per 64-sample block at 44.1 kHz; inferred from the tempo-sync constant). The port runs **one Marbles sample per call**: Marbles' own sample rate becomes 689.0625 Hz.
- Every generator is written in terms of cycles per sample, so its behaviour carries over.
- The OP-1 consumes one modulation value per block anyway.
- The clock's timing resolution becomes 1.45 ms. At the fastest pot rate (64 Hz at range 1x) a period is ~11 samples.
- `SUBSTEPS=4` (2756.25 Hz, 4 samples per call) is built and verified too, for finer timing at 2.5× the cost (§5). Decision for the user (§8).

## 3. Fixed-point design

Formats:
- Q32 for uniforms and ramp phases;
- Q40 (int64) inside the clock and slave ramps;
- Q31 for distribution samples;
- Q16 for controls, Q16 volts for outputs (1 V/oct: a semitone is 5461.33);
- Q24 volts in the lag filter;
- Q8 semitones for the rate.

All arithmetic is portable integer C++: no Blackfin builtins, and no 64 × 64 multiplies or variable 64-bit shifts on the per-sample paths (GCC turns those into libgcc calls). So **the host build predicts the device bit for bit**.

**Emulating upstream's float roundings where it pays.** Marbles moves its random numbers between float and integer form: uniforms are `float(word) / 2^32`, the T generator's random vector is an LCG seeded from a float, X2/X3 replay X1's values through a hash of their float bits. A plain fixed-point version decorrelates from upstream after one draw, so it can only be checked statistically. The port instead reproduces those roundings exactly with cheap integer helpers (`src/fx.h`: `WordToUnit`, `RoundToFloat`, `Float24`, `FloatAdd`, `FloatMul`):
- the random stream, deja vu loops and replays are **bit-identical** to upstream's;
- the T decisions (Bernoulli and three-state thresholds, drum pattern index, the Markov logit at Q34, the divider pattern's hysteresis quantizer) are **exact**;
- the clock frequency is stmlib's `SemitonesToRatio` result exactly;
- the master and slave phases follow upstream's float accumulation, and the jitter multiplier is computed as upstream does.

So the **gates land on the same samples as upstream's**, with or without jitter.

**Plain fixed point** (where float-exactness buys nothing):
- the beta interpolation (Q31: error < 1e-7);
- the voltage arithmetic (Q16 volts);
- the quantizer's degree voltages (rounded to Q16);
- the clusters model's probability;
- the lag/smoothing filter (32-bit multiply-high helpers: error < 0.7 mV).

Per-sample work is limited to the phase accumulations, the slave ramps, the ramp divider and the output channels. Everything else runs at clock ticks.

## 4. Verification

All results from `make copy-check test compare bench` (2026-10-16, default build, 1 sample per call):

1. **The copy is upstream** (`make copy-check`):
   - `scripts/copy_upstream.py --check`: undoing the renames gives every file byte for byte (26 files);
   - upstream and the copy render **byte-identical outputs** for all 22 scenarios (30 s each).
2. **Component tests** (`make test`, port vs float copy, same inputs):

   | Test | Result |
   |---|---|
   | RandomSequence (NextValue, NextVector, deja vu, replay with hash/shift, rewrite, clone, deterministic values) | 465,377 values, **0 mismatches** |
   | RoundToFloat / WordToUnit vs C++ float conversion | 4 × 10^6 values, 0 mismatches |
   | Float24Reciprocal vs `1.0f / x` | 10^6, 0 mismatches |
   | SemitonesToRatio, every Q8 value in ±127 st | 65,025, 0 mismatches |
   | Divider-pattern hysteresis quantizer | 196,611, 0 mismatches |
   | Quantizer level selection | 2 × 10^6, 0 mismatches |
   | Quantizer note | 1.2 × 10^6 inputs: 3 different notes, all 3 within 1e-4 V of a decision midpoint |
   | Quantizer voltage on equal notes | max 6.7e-6 V (degree voltages rounded to Q16) |
   | Beta sampling | max error 8.7e-8 (mean 1.5e-8) of the unit range |
   | Jitter distribution | max 3.4e-8 |
   | Lag processor | max 0.66 mV over 1.2 × 10^6 samples |

3. **Whole generator, sample by sample** (`scripts/compare_bin.py out/ref out/port`): in all 22 scenarios (30 s), **the three gates agree on every sample** and the four voltages stay within 0.4 mV (0.3 mV in the smoothing paths, 15 µV elsewhere).
4. **Side by side, many seeds** (`make compare` → `out/compare.txt`):
   - 22 scenarios plus a bias sweep per T model with randomized settings (jitter, deja vu modes, length, spread, bias, steps, scale, range, control mode, clock source, pulse widths), each 32 seeds × 300 s: **928 runs**;
   - **all 928 runs have identical gates on every sample** (rising edges equal per channel);
   - **69 of 65.4 million voltage steps differ by more than 1 mV**: quantized notes where the beta sample falls within float rounding of a scale midpoint (a semitone or so, held until the next clock); 0.006% of samples;
   - means, standard deviations and 64-bin histograms of X1–X3 and Y agree (histogram L1 distance ≤ 4e-4).
   - With `SUBSTEPS=4` (8 seeds × 120 s): again every run has identical gates; 797 of 16.8 million steps differ.
5. **Blackfin = host** (`make bench`): the simulator's checksums equal the host's for all 22 scenarios (default and `SUBSTEPS=4`).

The original statistical plan (distributions instead of samples) turned out unnecessary: with the float roundings emulated, the port can be compared sample by sample, a stronger check.

## 5. CPU (simulator; the device has run 1.5–1.8× the simulator)

Cycles per call (= per OP-1 block), `make bench` (8 s per scenario):

| Build | Typical average | Worst scenario average | Worst single call | At 689 calls/s (simulator) | Device estimate (×1.6) |
|---|---|---|---|---|---|
| **1 sample per call (default)** | 2,550–3,200 | 3,840 (clusters, 3 smoothed channels) | 17,546 (a clock tick drawing 4 new voltages) | **2.6 MHz** average | **~4.2 MHz** |
| 4 samples per call | | 9,736 | 23,229 | 6.7 MHz | ~10.7 MHz |
| upstream float, soft-float, 1 per call | 32,000–72,000 | 71,683 | 175,590 | 49 MHz | ~79 MHz |

- The worst single call (17.5k cycles) is about 3% of one block's budget (580k cycles at 400 MHz).
- That's for one instance (one synth slot's LFO). The glue (§6) adds the per-voice apply: an `ApplyKnob` loop as in stock random, or a 128-entry pitch multiply.
- Footprint: ~34 KB code, 78 KB tables (`bfin-elf-size`). State: 4.8 KB per instance (`TGenerator` 592 B, `XYGenerator` 4,188 B).
- The port's objects have no `.init_array` / `.ctors` sections. Undefined symbols are only libgcc's `__muldi3`, `__udivdi3`, `__divdi3` and `__ashldi3`, used at tick rate; the image already links `-lgcc`.

Optimisations already made: Blackfin `SIGNBITS` (`__builtin_clrsb`) instead of `__builtin_clz` (a libgcc call); per-tick products cached; 32-bit rounding fast path; lag filter in 32-bit products. Left on the table: an assembly lag filter, and skipping the X channels' work when their output isn't routed anywhere.

## 6. OP-1 glue design (not implemented)

### 6.1 Host and swap
- **Host on the random LFO** (`docs/lfo-interface.md` §10): a new LFO type name would make stock firmware fail at every boot; random is thematically right, and its p4..p7 are free and range-valid.
- **An orange click on the LFO page toggles Marbles / stock random** (as the MULTI engine does phase / Rings; orange is reserved for swaps in Rings and Clouds). The marker is bit 14 of `lfo_params[7]`. Every factory random preset stores 0 there, so they stay stock.
- **On stock firmware** a Marbles slot plays as the stock random LFO with whatever p0–p3 hold: harmless, provided every stored value stays within random's ranges.

### 6.2 Hooks (all plain-data words, through the image patch table)
- Random synth process `0x1910920` (stock `0x019cea70`):
  - marker 0 → tail-call stock;
  - marker 1 → tick the slot's Marbles once per block (§6.4), then apply the outputs to this voice for the current stage (§6.5).
- Random fx process `0x1910960` (stock `0x019cefb0`): the effect destination (stage 2).
- Random synth trigger `0x1910928` (stock `0x019ced74`): optional note-on actions (clock reset, or a "clock from keys" mode); otherwise stock.
- Drum process/trigger `0x1910940` / `0x1910948`: pass-through at first (drums later).
- RndLfoScreen Update / Init `0x19113a0` / `0x19113a8`: Marbles screen when the marker is set (second `SvgFile`, `document_` swapped, as MULTI does).
- SynthController OnKnob `0x19119c0` (stock `0x01a505bc`): when `page == 3` and the slot's LFO type is 2 and the marker is set, Marbles' controls; otherwise stock. Clicks: add `page == 3` cases to the MULTI engine's key hooks (`0x19119a8` / `0x19119b0` / `0x19119b8`).

### 6.3 Controls (a proposal; decision for the user)
Three pages, cycled by the white click (the Clouds convention); shift + encoder = fine adjustment, as on Rings.

| Page | Blue | Green | White | Orange | Blue click | Green click |
|---|---|---|---|---|---|---|
| **T** (rhythm) | rate | bias | jitter | deja vu | T model (shift: back) | T deja vu off / on / locked |
| **X** (voltages) | spread | bias | steps | loop length (1–16) | scale (shift: back) | X deja vu off / on / locked |
| **OUT** | depth | destination | routing | X range / pitch span | T range 0.25× / 1× / 4× | clock: free / tempo-synced |

Destinations: engine, envelope, effect (stock's three, stored in p2 so stock firmware sees a valid value), plus **pitch** and **volume**, which stock random can't do.

Routing presets: X1–X3 and Y to the destination's 4 parameters (the natural mapping: Marbles has 4 voltage outputs); X1 → pitch with each voice drawing its own X channel ("chords"); T1/T3 gates → volume (rhythmic gating), and so on.

### 6.4 lfo_params layout (all values within random's ranges: p2 1024..5120, the rest 0..32767)

| Param | Content |
|---|---|
| p0 | depth (stock meaning) |
| p1 | T rate, 15 bits (stock: speed) |
| p2 | destination engine / envelope / effect = 1024 / 3072 / 5120 (stock meaning) |
| p3 | deja vu amount, 15 bits (stock: envelope; the 12 o'clock lock zone as on Marbles) |
| p4 | T bias (7 bits), jitter (7 bits) |
| p5 | X spread (7), X bias (7) |
| p6 | X steps (7), routing (3), extended destination (pitch / volume, 2), X range (2) |
| p7 | **marker (bit 14)**, T model (3), scale (3), length − 1 (4), T range (2), T deja vu mode (2) |

The X deja vu mode, X control mode, clock source and tempo sync need 6 more bits. Either quantize something to 6 bits, or use a private JSON member (`docs/screen-interface.md` §5, option 2; not mapped). Decision for the user.

7-bit continuous values give 128 steps; the encoders move ~1/32–1/100 of a range per detent, so it's enough.

### 6.5 Ticking, state and outputs
- **One Marbles instance per synth slot** (8 × 4.8 KB = 38 KB) in the image's `.bss`, which the LDR loader zero-fills. Each slot's `Init()` runs on first use (a zero `initialised` word). No static constructors: the tables are const data; the state is zero-initialised and set up explicitly. Seed: the OP-1's LCG at `0xff802bc8`, or a per-slot constant.
- **Once per block:**
  - the per-voice process is called for each processed voice, so the first call of a block ticks Marbles: compare the Core's block counter `[0xff8016e0]` with the last value seen (to be confirmed by the probe), or the cycle counter (a gap of > 0.7 ms = new block);
  - the per-voice LFO objects don't advance when their voice isn't processed; the slot's Marbles must advance every block. If idle blocks don't call the LFO at all, catch up from the counter difference (bounded, e.g. ≤ 16 ticks), or accept a paused clock while nothing plays (decision).
- **Applying the outputs** (as stock random does, `docs/lfo-interface.md` §5):
  - engine knobs: stage 0, `Ranges::ApplyKnob(k, &delta, type, 0xff801724)` with `delta = (X_k − centre) × depth`;
  - envelope: stage 1, `0x19f69b0`;
  - effect: fx object, stage 2, FX Ranges;
  - **pitch** (new): stage 0, multiply the voice's 128 phase increments at `0xff801950` by `2^(semitones/12)` (one exp per voice per block). Quantized X gives notes;
  - **volume** (new): stage 0, scale the 128-entry gain array `0xff801d50` with a short ramp to avoid clicks.
- Marbles' voltage scale maps onto the OP-1's: 1 V = 1 octave for pitch (the depth sets the span); for knobs, ±5 V = ± the destination range × depth.

### 6.6 Tempo sync
The OP-1 tempo is `[0x1aadad4]` (`docs/lfo-interface.md` §4): a Q32 phase increment per call, 2^32 per 1/16 beat at the call rate. For tempo-synced clocking:
- set `TGenerator::set_external_frequency(inc × beats_per_cycle)`, converted to Q40 cycles per sample;
- the rate encoder then picks a beat division (1/16 … 8 beats) instead of a free rate.

Upstream's external-clock code (`RampExtractor`) isn't needed for this.

## 7. Remaining integration steps

1. **Device probe first** (no Marbles code; the FX probe's method, Test 16): a wrapper around random's synth and fx process words that measures
   - calls per second per object and the `stage` values;
   - whether idle voices' LFOs are processed;
   - what `0xff801724` holds at stages 0 and 1;
   - whether `[0xff8016e0]` advances once per block;
   - and, by writing `0xff801950` in a test mode, whether pitch modulation from the random host is heard.
   (`docs/lfo-interface.md` §11.)
2. **Restructure the image build** so Rings, Clouds and Marbles link into one image (already planned for Clouds, `docs/HANDOVER.md` §3), with shared hook dispatchers for OnKnob and the key slots.
3. **`ports/marbles/op1/`:**
   - the hooks of §6.2;
   - per-slot state and lazy init;
   - the block tick;
   - the output application per destination;
   - parameter packing (§6.4) with range checks;
   - a simulator test calling the hooks as the Core would (as `ports/rings/tests/sim_engine.cc`), with CPU measured per block.
4. **Screen:** a `marbles.svg` drawing (the user designs it, as for Rings and Clouds), swapped in on the random LFO screen.
5. **Package** through `tools/inject_app.py` / `tools/build_package.py`, diffed against stock, then device tests (procedure in `docs/device-tests.md` before the flash).

## 8. Decisions for the user

1. **Host:** the random LFO with the orange click swap (recommended), or another LFO (crank: rarer in presets, but special knob paths).
2. **Internal rate:** 1 Marbles sample per OP-1 block (689 Hz, ~4 MHz on the device; recommended) or 4 (2.76 kHz, finer timing, ~11 MHz).
3. **Controls:** the three-page layout of §6.3, and which settings get 6–7-bit storage vs which need a private JSON member (§6.4).
4. **New destinations:** pitch and volume through the random host (needs the probe's confirmation), and the routing presets.
5. **Clock while idle:** keep Marbles running when no voice sounds (catch-up from a block counter) or let it pause.
6. **Tempo sync** as a clock option (the rate becomes a beat division).

## 9. Marbles as a sequencer (2026-10-17; supersedes the LFO plan of §6–§8)

The user chose a **sequencer** host: Marbles on the **tombola** sequencer (synth only), swapped by the orange click. Firmware map: `docs/sequencer-interface.md` (probe results §10, Test 20).
- **Core:** `op1/marbles_seq.*`, platform-free. One Marbles sample per sequencer tick (96 per beat). The T clock runs in a new **synced mode** (`TGenerator::set_synced_period` / `SyncTo`): the master ramp completes exactly every `period` ticks (the rate encoder picks 1 bar … 1/32 with triplets), jitter stretches cycles by upstream's multiplier and is pulled back to the grid. Free-running mode is unchanged (bit-exact with upstream as before).
- **Notes:** T1/T2/T3 gates each play a note from X1/X2/X3 (overlapping into chords); Y → velocity 48–127. Pitch: **scale** (root = last key + X over 2 octaves, Marbles' quantizer) or **keys** (the latched held chord, X picks a key). Shift + key plays through.
- **Controls:** T page rate / bias / jitter / deja vu; X page spread / bias / steps / length; white = page, shift + white = pitch source, blue = T model or scale, green = the page's deja vu off/on/locked.
- **Glue:** `op1/marbles_op1.cc` (tombola player, controller and screen words; RAM settings; a digit readout until `marbles.svg` arrives). Image: `make op1-marbles` in `ports/rings` (Clouds + Marbles). Checks: `make sim-seq`. Device: Test 21.
