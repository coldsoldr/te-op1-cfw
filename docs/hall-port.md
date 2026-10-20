# HALL: an 8-line FDN hall reverb in fixed point (ports/fx/src/hall)

Jot's feedback delay network, as `op1fx::HallReverb`, at 22050 Hz, for the
OP-1's reverb category (engine 3 of spring → plate → hall → clouds; brief:
`docs/fx-briefs/hall.md`; research: `docs/effects-research.html`, card 2).
It follows the plate (`docs/plate-port.md`) closely: one no-wrap int16
buffer with immediate offsets and mirrored writes, the Blackfin's 16-bit
MACs, assembly kernels with bit-exact C references, randomized kernel
tests with guard words, a float reference, 60 s stability runs.

Files:

| file | what |
|---|---|
| `ports/fx/src/hall/hall.h` | API, kernel block, control |
| `ports/fx/src/hall/hall.cc` | tables (constexpr), control, C reference kernels, `Process` |
| `ports/fx/src/hall/hall_layout.h` | the buffer map and the kernel block's offsets (shared with the assembly) |
| `ports/fx/src/hall/hall_bfin.S` | kernels: run stereo / mono (each with and without the mirror writes), size crossfade, pre-delay |
| `ports/fx/tests/test_hall.cc` | simulator test: asm = C, guards, cycles, 60 s stability, checksums |
| `ports/fx/tests/hall_float_ref.cc` | double-precision reference and comparison (SNR, bands, RT60, floor, onsets, size clicks) |
| `ports/fx/mk/hall.mk` | `make test-hall`, `make compare-hall` (included by `ports/fx/Makefile`) |

## API

```cpp
struct HallParams {     // 0..32767 each
  int32_t size, decay, damp, predelay, early, motion, width;
  bool freeze;
};
void HallReverb::Init();
void HallReverb::Process(const int16_t* in_l, const int16_t* in_r,
                         int16_t* wet_l, int16_t* wet_r, int32_t n,
                         const HallParams& p);
```

- `n`: even, 2..64 (the glue passes 32).
- `in_r == nullptr`: mono input. Otherwise the network is fed `(l + r) >> 1`
  (mono in, stereo out, as the plate).
- `wet_r == nullptr`: mono output (the left side: even lines + left early
  taps, no width matrix), a separate, cheaper kernel.
- Wet only, int16, at the input's scale, saturated. In place
  (`wet_l == in_l`) is fine: the input is consumed into the pre-delay ring
  first.
- `Init()` clears everything. No static constructors (every table is
  `constexpr`; the test checks the ELF has no `_GLOBAL__sub_I`), no heap,
  no float at run time, no divisions in loops.
- `sizeof(HallReverb)` = **85 716 bytes** (buffer 75.9 KB, pre-delay ring
  8.8 KB, the rest < 1 KB).

## Signal path (per sample)

```
(l + r) >> 1 -> pre-delay (own ring, gliding tap; the plate's kernel)
 -> x input gain 0.25 (headroom; ramps to 0 on freeze)       = xg
 -> early line: written with xg; 5 + 5 taps (scaled with size)
 -> 3 allpasses 131, 197, 283 (g 0.6, fixed) -> x 1/2          = d
 -> 8 lines (primes, scaled with size), each read through an interpolated
    modulated tap: two LFOs (0.61, 0.87 Hz), each used + and -, so
    lines 0 4 +A, 2 6 -A, 1 5 +B, 3 7 -B
 -> per line z = a z + b y (+ dither): the damping and the loss in one
    one-pole (below)
 -> Householder: w = z - (2/8) sum z (+ d with signs + + + - - + - -)
 -> written back to the lines
 out L = gl (y0 + y2 + y4 + y6) + early L, R = gl (odd lines) + early R,
 -> width matrix (wa, wb), x 2
```

The 8x8 matrix is a Householder reflection: one sum, one rounding
(`m = rnd(sum z / 4)`), then saturating `z - m` vector subtractions. No
multiplies. The input enters with balanced signs, orthogonal to the
all-ones vector (the reflection's -1 eigenvector).

## Lengths (22050 Hz)

Size step s (0..15) scales the largest lengths by 0.2 × 5^(s/15) (0.20,
0.22, 0.25 … 0.90, 1.00), each line rounded to the nearest prime
(`kHallSizes`, constexpr; `static_assert`s check the layout).

| line | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| size 15 (hall) | 1153 | 1361 | 1597 | 1823 | 2063 | 2381 | 2711 | 3089 |
| size 0 (room) | 229 | 271 | 317 | 367 | 409 | 479 | 541 | 617 |

52–140 ms at the top, 10–28 ms at the bottom. Early taps at size 15: left
179, 487, 857, 1291, 1777; right 263, 641, 1031, 1499, 1993 (8–90 ms),
coefficients +0.85, −0.70, +0.58, −0.47, +0.38 (both sides), scaled with
size.

## Buffers

One no-wrap array of **37 971 int16 words (74.2 KB)**, the plate's scheme:
every access is an offset from a write pointer p moving down one sample per
sample; a line at base b is written at p + b and read at p + b + d; writes
made while p < MAX_LINE (3113, the longest line's reach) are mirrored SIZE
further on. Lines are laid out at the largest size with 24 samples of
modulation reserve; the bases are immediates in the assembly, the 8 line
and 10 early read offsets come from the kernel block (rewritten when the
size changes).

| region | base | reach |
|---|---|---|
| AP1 / AP2 / AP3 | 0 / 132 / 330 | 131 / 197 / 283 |
| early line | 614 | 1993 |
| lines 0..7 | 2608, 3786, 5172, 6794, 8642, 10730, 13136, 15872 | len + 24 |

REACH 18 985, SIZE 18 986. The pre-delay is its own plain ring (4416
words), as the plate's.

## Fixed-point formats

- Buffers and the signal between stages: int16. xg = 0.25 × input; d =
  0.125 × input. Output makeup × 2.
- Arithmetic: 16 × 16 fractional MACs into 40-bit accumulators; whatever
  is written is the accumulator's high half, rounded with the MAC's
  default unbiased rounding (RND_MOD cleared on entry, restored on exit),
  saturated.
- **Dither (stochastic rounding) in the damping filters.** The first
  build had no dither and measured rounding limit cycles: with a per-line
  gain g near 1, rounding holds anything below ~0.5/(1−g) LSB, so a tail
  never fully died (3 × RT60 after a burst, up to 28 LSB RMS left at
  RT60 14 s, size 0; the fixed-point RT60 measured 57 s for 14.5).
  Now each damping MAC adds a ±½ LSB uniform dither (xorshift32 per
  sample, one value per line half) before rounding: unbiased in
  expectation, no dead band. Frozen, or at infinite decay, the dither's
  gain is 0 and the loop is exactly lossless (rnd(y × 32767/32768) = y).
- Interpolated reads: two taps, weights 32767 − f and f, one rounded MAC
  pair (lossless when f = 0). The ± groups share f: the − group reads at
  len − ip − 1 with the weights swapped, so each LFO costs its split once
  per sample and each line 2 loads + 2 MACs.
- Coefficients Q15; per-line a | b packed in pairs for the dual MACs.
- **Note for the other ports:** in the simulator, −1 × −1 accumulated into
  A0/A1 is +2^31 exactly (the 40-bit accumulator holds it); only a product
  written straight to a 32-bit register saturates. `Fr()` here models
  that. The plate's C model saturates to 0x7fffffff instead; it doesn't
  show there because its rounding hides the 1-LSB-of-accumulator
  difference, but a kernel that shifts the accumulator before rounding
  (the hall's `x 1/2`) does see it.

## Parameter mappings (per call: `HallControl::Update`)

| knob | 0..32767 means | smoothing |
|---|---|---|
| size | 16 steps (`size >> 11`), line scale 0.2 × 5^(s/15): room (10–28 ms lines) → hall (52–140 ms) | a step crossfades (below) |
| decay | RT60 = 0.3 s × (20 / 0.3)^(decay / 32512); **≥ 32512: infinite** (no loss at DC; the input still enters) | knob smoothed 1/8 per call |
| damp | HF ratio = 10^(damp / 32767): Nyquist RT60 = RT60 / ratio (1 … 10). At infinite decay the highs keep 20 s / ratio | as decay |
| predelay | 0..200 ms (0..4410 samples), whole samples when still; glides ≤ ¼ sample/sample (the plate's) | glide |
| early | early taps × early/32767 (linear); the late part full up to 16384, then down to 0 at 32767 (early only) | 1/8 per call |
| motion | excursion 0..16 samples peak (both LFOs; 0.61 and 0.87 Hz); parked to 0 while frozen | 1/8 per call |
| width | wa = (1 + w)/2, wb = (1 − w)/2: L' = wa L + wb R, R' = wb L + wa R. 0 = mono, 32767 = full. Ignored by the mono output | 1/8 per call |
| freeze | a → 0, b → 1 (no loss, no damping), input gain → 0 over 4 calls, dither off, motion parked | 4 calls |

The line coefficients (Jot): g_j = 2^(−L_j T), T = 3 log2(10) / (fs RT60);
a_j = (1 − q)/(1 + q) with q = 2^(−L_j T (ratio − 1)); b_j = g_j (1 − a_j):
DC gain g_j, Nyquist gain g_j^ratio, a + b ≤ 1 (passive by
construction). Integer tables: T (33 points), ratio − 1 (33), 2^−x (257),
a(u) (257). When the knobs are still and every smoother has settled,
`Update` reuses the previous call's coefficients (only the LFOs move).

## Size changes

Choice: **stepped (16), every change crossfaded, no mute.** The output is
made from the lines' own reads, so crossfading each line's read (and each
early tap) from the old length to the new one crossfades the output too.
`HallXfadeRunAsm` runs for 256 samples (8 calls) with a raised-cosine
weight (piecewise linear per call: a linear one's corners measured up to
+10 dB of residue); the lines' a/b step from the old size's to the new
one's with the same weight. A size reached during a crossfade starts the
next one when it ends. Measured (`compare-hall`, a held 220 Hz sine, decay
24000, broadband 3rd-difference peak against the signal, worst 10 ms):

| switch | steady before | crossfaded | hard switch |
|---|---|---|---|
| 15 → 7 | −42.0 dBc | −40.7 | −3.7 |
| 7 → 15 | −52.6 | −49.9 | −13.3 |
| 15 → 14 | −42.0 | −42.8 | +0.7 |
| 0 → 15 | −50.6 | −38.2 (at +95 ms: the new size's own steady state) | −4.4 |
| noise 15 → 0 | +12.1 | +13.4 | +12.8 |

Within 0–3 dB of the steady state (one case +12 dB vs the old size's
steady state, but −42 dBc is the new size's), 35–45 dB below a hard
switch.

## Cost (bfin-elf-run, cycles per call of 32 samples, × 689 = Hz)

| configuration | cycles/call avg (max) | per sample | MHz (sim) |
|---|---|---|---|
| stereo in/out, motion on or off | 8845 (9315) | 276 | **6.09** (peak 6.41) |
| mono in/out | 8035 (8513) | 251 | **5.53** (peak 5.86) |
| a size crossfade call (stereo or mono) | 16 731 (16 780) | 522 | **11.5** for 8 calls (11.6 ms) per step |
| the C kernels instead (stereo / mono) | 116 379 / 100 432 | | 80.2 / 69.2 |

The peaks are the calls in the mirrored region (16% of the time: every
write twice). A call where a knob moves runs the full control path (a few
hundred cycles more; 2x the line coefficients during a crossfade). The
dither costs about 14 cycles per sample (0.3 MHz).

Breakdown per stereo sample (instructions, the simulator's cycles are
close to them): dither 10, input + early line + 3 allpasses 32, two LFOs
and their pointers 28, 4 line pairs (8 interpolated reads + damping +
dither) 60, Householder and 8 writes 26 (38 mirrored), early taps 30,
output 9, pre-delay kernel ~34, control and glue ~10.

Motion costs nothing (reads are always interpolated). The C kernels are
for tests only (reached only through `use_asm_ == false`; a device build
can drop them).

Code: run kernels 0.85–1.0 KB each (one runs per call: the no-mirror one
in 84% of calls), crossfade 1.4 KB (size changes only), pre-delay 140 B.
Data streams per sample: 8 lines × 2 reads + 10 early taps + 3 allpass
tails, 12 writes (+12 mirrored), the pre-delay 3. That's about the plate's
traffic; expect the same 1.5–4× device excess from cold caches.

## Levels, latency

- **Wet/input RMS** (float ref, EARLY 16384): 0.35–0.47 for noise and saw
  at RT60 5–11 s, 0.95 for a dark small room at a long decay; early only
  0.65. Like the plate's; MIX should be the usual equal-power crossfade.
- **Early : late energy** of an impulse response (EARLY 32767 vs 0):
  early −3.2 dB, late −14.6 / −12.4 / −8.8 dB at RT60 0.9 / 2.5 / 7.2 s
  (re a full-scale sample). At EARLY 16384 the early part is 6 dB lower,
  so roughly balanced at mid decays.
- **Headroom**: lines run at 1/8 of the input scale. The largest line
  peak in the scenarios is 18 100 (a −10 dBFS saw, dark small room,
  RT60 11 s). Held hot material at very long or infinite decay saturates
  (stable: saturation only removes energy).
- **Onset (latency of the wet)**: early taps first at 1.6 / 2.4 ms (L / R,
  size 0), 3.8 / 5.6 (size 8), 8.1 / 11.9 ms (size 15); late only (EARLY
  0): 10–12 ms (size 0) to 52–62 ms (size 15). Plus the pre-delay; the
  halfband pair adds < 1 ms.

## Validation

**Float reference** (`make compare-hall`): `FloatHall` is the same network
in double (rings per line, exact Householder), driven by the same
`HallControl`.

| scenario | SNR L / R | tail SNR | error RMS | tail bands, max diff | wet/in |
|---|---|---|---|---|---|
| impulse, hall, decay 20000 | 25.1 / 25.3 dB | 18.8 / 19.2 | 1.11 LSB | 0.21 dB | (tail) |
| impulse, room (4096), motion 0 | 25.5 / 25.6 | 20.9 / 21.1 | 1.84 | 0.07 | |
| noise burst, decay 22000 | 48.3 / 48.4 | 40.6 / 40.9 | 1.60 | 0.00 | 0.38 |
| saw, short decay | 58.4 / 61.1 | 47.9 / 46.0 | 1.33 | 0.96 | 0.35 |
| saw, long (28000) | 57.4 / 55.3 | 49.8 / 49.6 | 1.82 | 0.20 | 0.42 |
| saw, long, motion max | 57.8 / 55.9 | 51.2 / 50.8 | 1.81 | 0.21 | 0.43 |
| saw, dark small room, long | 60.4 / 60.1 | 52.3 / 51.7 | 2.71 | 0.27 | 0.95 |
| noise, mono in/out | 51.7 | 44.0 | 1.55 | 0.00 | 0.39 |
| noise, pre-delay, early only | 69.8 / 69.9 | 67.9 / 68.2 | 0.22 | 0.01 | 0.65 |
| noise, late only, width 0.25 | 43.3 / 43.4 | 40.2 / 40.3 | 1.36 | 0.01 | 0.13 |
| noise, bright, RT60 ~20 s | 53.2 / 54.0 | 46.4 / 47.2 | 2.06 | 0.00 | 0.47 |

L/R correlation of the tails: -0.2 .. +0.4 at full width (0.88 at width
0.25). WAVs of both renders go to `ports/fx/out/hall/`.

The error is fixed-point noise (1–3 LSB RMS); the tails' spectra agree to
≤ 0.4 dB (1 dB in one short saw tail). The impulse cases' low SNR is only
their level (a 540-LSB network).

RT60 (noise burst, Schroeder, T30), nominal → float | fixed:

| size | 0.30 s | 0.86 | 2.49 | 7.18 | 14.46 | 18.72 |
|---|---|---|---|---|---|---|
| 0 | 0.32 \| 0.32 | 0.87 \| 0.87 | 2.49 \| 2.54 | 7.15 \| 7.57 | 14.3 \| 16.5 | 18.5 \| 22.5 |
| 32767 | 0.31 \| 0.31 | 0.85 \| 0.86 | 2.49 \| 2.54 | 7.16 \| 7.66 | 14.4 \| 17.1 | 18.6 \| 23.9 |

(The fixed point's long-decay figures carry its noise floor in the fit.)
Idle floor 3 × RT60 after a burst (the fixed point's residue, LSB RMS of
the wet): 0 at RT60 0.3 s; ≤ 2.1 at 2.5 s; ≤ 4.3 at 14.5 s (size 0,
motion 0; 0–1.5 with motion on or at large sizes).

Damp (sine-burst decays, float, decay 2.49 s): 4 kHz RT60 / 250 Hz RT60 =
0.9, 1.2, 1.9, 3.7, 5.9 at damp 0, ¼, ½, ¾, 1 (the design's ratio is at
Nyquist: 1, 1.8, 3.2, 5.6, 10). At damp 1 the 250 Hz band shortens too
(1.7 s).

**Simulator test** (`make test-hall`):

- **Kernels, asm = C bit-exact** on random runs (random buffers with
  full-scale samples, random coefficients including -32768 and 32767,
  random generator states and dither gains, every size, the mirrored
  region, n 1..64, guard words round the buffer): run stereo 1500 (mirror
  and no-mirror variants), mono 1500, crossfade 1000, pre-delay 1000. All
  equal, guards intact.
- **Whole reverb**, C instance against asm instance, guard words round
  both objects: noise bursts with a size step every 150 calls (2600
  calls, 4.4 trips round the buffer); saw with motion at the top,
  pre-delay glides, width and early moving; freeze on/off with damp moving,
  then infinite decay; full-scale noise with odd n and every knob jumping;
  mono in/out; mono in/stereo out; odd n with mono out. Outputs and
  buffers equal, guards intact.
- **Checksums** of the C outputs: Blackfin = host.
- **Stability, 60 s each (asm)**, wet RMS per 10 s:
  - decay 32511 (RT60 20 s), -6 dBFS noise held: 4608 -> 4734, flat after
    10 s; DC within +-4 LSB; lines' mean within +-34;
  - freeze after 2 s, motion at the top: 3595 -> 3424 while the motion
    parks, then flat (3425 +-2) for 50 s; DC 0;
  - freeze after 2 s, motion 0: 5767 -> 5838, flat; DC within +-1;
  - infinite decay (no freeze), 1 s of saw then silence, damp 0, motion at
    the top: 1885 -> 1553 over 60 s (-1.7 dB: the moving interpolation's
    slight low-pass, as the plate's frozen tail with motion); DC 0.

  Nothing grows; the first build's freeze with motion lost 12 dB in 50 s,
  hence the parked modulation.

## For the OP-1 glue

- **Rate and scale**: as the plate: decimate 2:1, `Process(n = 32)`,
  interpolate, mix. Mono positions: `in_r = wet_r = nullptr`.
- **Size**: pass the knob straight through; the hall crossfades. An LFO
  on size chains crossfades (no clicks), at the crossfade's cost while it
  moves.
- **Freeze**: the hall gates its own input (4 calls); don't gate it in the
  glue.
- **Decay ∞** (≥ 32512) with input still coming in accumulates until it
  saturates; that's the brief's "∞ at the top".
- **CPU**: see the cost table; budget 1.5–4× on the device. While the
  size knob moves, the crossfade kernel runs.
- **Screen**: the brief's ids map to the knobs as above; `size` is 16
  steps (an indicator can follow the raw value smoothly); `decayinf` when
  decay ≥ 32512 (`op1fx::kHallInfinite`).
- **Not done here**: MIX (the glue's), the stored-value mapping of the
  knobs, a device build without the C references, the screen.
