# PLATE: Dattorro's plate in fixed point (ports/fx/src/plate)

Dattorro's figure-of-eight plate ("Effect Design Part 1", JAES 1997), as
`op1fx::PlateReverb`, at 22050 Hz, for the OP-1's reverb category
(docs/effects-research.html, card 1). It's written in the style of the
Clouds reverb: no-wrap int16 buffers with fixed offsets and mirrored
writes, the Blackfin's 16-bit MACs, and C references that the assembly
matches bit for bit.

Files:

| file | what |
|---|---|
| `ports/fx/src/plate/plate.h` | API, kernel blocks, control |
| `ports/fx/src/plate/plate.cc` | tables (constexpr), control, C reference kernels, `Process` |
| `ports/fx/src/plate/plate_layout.h` | the buffer map and kernel block offsets (shared with the assembly) |
| `ports/fx/src/plate/plate_bfin.S` | five kernels: pre-delay, input diffusers, tank stereo, tank mono, size crossfade |
| `ports/fx/tests/test_plate.cc` | simulator test: asm = C, guards, cycles, 60 s stability, checksums |
| `ports/fx/tests/plate_float_ref.cc` | double-precision reference and comparison |
| `ports/fx/Makefile` | `make test-plate`, `make compare-plate` |

## API

```cpp
struct PlateParams {      // all int32 except freeze
  int32_t decay;          // 0..32767 -> tank gain 0.2..0.98 (linear)
  int32_t damp;           // 0..32767 bright -> dark
  int32_t predelay;       // 0..32767 -> 0..200 ms (0..4410 samples)
  int32_t size;           // 0..7 -> tank lengths 0.6..1.0 (geometric steps)
  int32_t diffusion;      // 0..32767 -> input allpass gains 0..Dattorro's
  int32_t motion;         // 0..32767 -> excursion 0..2x Dattorro's (16384 = his)
  int32_t lowcut;         // 0..32767 -> 20..500 Hz (log)
  bool freeze;            // decay 1, damping off, input gated off
};

void PlateReverb::Init();
void PlateReverb::Process(const int16_t* in_l, const int16_t* in_r,
                          int16_t* wet_l, int16_t* wet_r, int32_t n,
                          const PlateParams& p);
```

- `n`: even, 2..64. The OP-1 glue passes 32 (one 64-sample effect call,
  decimated 2:1).
- `in_r == nullptr`: mono input (`in_l` only). Otherwise the tank is fed
  `(l + r) >> 1`.
- `wet_r == nullptr`: mono output, the left taps only. This uses a
  separate kernel, about 9% cheaper.
- Wet only, int16, at the input's scale, saturated. In-place calls
  (`wet_l == in_l`) are fine: the input is consumed into the pre-delay
  ring before any wet sample is written.
- `Init()` clears every buffer and state. No static constructors: every
  table is `constexpr` (the test checks that the ELF has no
  `_GLOBAL__sub_I`).
- Size of the object: about 76 KB (see the buffer map).

## Signal path

Per sample, mono sum in, stereo out:

```
(l + r) >> 1 -> pre-delay (own ring, gliding tap, 2-tap interpolation)
 -> low cut: one-pole high-pass y = x - lp, lp += k y (32-bit state)
 -> x input gain (0.25: the tank's headroom; ramps to 0 on freeze)
 -> 4 input allpasses: 142, 107 (g1 = 0.75 d), 379, 277 (g2 = 0.625 d)
 -> tank, two halves in a figure of eight; each half:
      in + decay x (the other half's last delay)
      -> modulated allpass, Dattorro's "-dd1" sign (dd1 = 0.70)
      -> delay -> damping one-pole low-pass -> x decay
      -> allpass dd2 = clamp(decay + 0.15, 0.25, 0.5)
      -> delay
 -> 7 + 7 output taps (Dattorro's table 2, his signs + + - + - - -)
 -> x 0.6 each, x 2 (the makeup) -> wet L, wet R
```

Dattorro's bandwidth filter (0.9995, essentially a pass-through) is left
out: the glue's halfband decimator already band-limits the input. The low
cut stands in its place.

Allpass forms: input diffusers and the tank's second allpasses use
`w = x - g d; y = d + g w`. The modulated tank allpasses use the other sign
(`w = x + g d; y = d - g w`), as in Dattorro's figure.

## Lengths at 22050 Hz

Dattorro's lengths at 29761 Hz, x 22050 / 29761, rounded (`Scaled()` in
plate.cc; `static_assert`s check plate_layout.h against it). Size step
`s` multiplies the tank lengths and the taps by `0.6^((7 - s) / 7)`: 0.600,
0.645, 0.694, 0.747, 0.803, 0.864, 0.930, 1.000. The input diffusers don't
scale.

| line | Dattorro | 22050, size 7 | size 0 |
|---|---|---|---|
| input AP 1 / 2 / 3 / 4 | 142 / 107 / 379 / 277 | 105 / 79 / 281 / 205 | (fixed) |
| left: mod AP A1 | 672 (+ excursion) | 498 | 299 |
| left: delay D1 | 4453 | 3299 | 1980 |
| left: AP A2 | 1800 | 1334 | 800 |
| left: delay D2 | 3720 | 2756 | 1654 |
| right: mod AP A3 | 908 (+ excursion) | 673 | 404 |
| right: delay D3 | 4217 | 3124 | 1875 |
| right: AP A4 | 2656 | 1968 | 1181 |
| right: delay D4 | 3163 | 2343 | 1406 |
| excursion (peak) | 16 | 11.85 at MOTION 16384, 23.7 at the top | (in samples, not scaled) |

Output taps at size 7 (size 0 in brackets):

- left: +D3@197 (118), +D3@2203 (1322), -A4@1417 (850), +D4@1479 (887),
  -D1@1474 (885), -A2@139 (83), -D2@790 (474)
- right: +D1@262 (157), +D1@2687 (1612), -A2@910 (546), +D2@1980 (1188),
  -D3@1564 (938), -A4@248 (149), -D4@90 (54)

The whole per-size table is `kPlateSizes` in plate.cc.

## Buffers

All three buffers live inside the object.

**Tank: one no-wrap int16 array of 32105 words (62.7 KB), as Clouds'
reverb.** Every access is an offset from a write pointer p that moves down
one sample per sample, `p in [0, SIZE)`. A line at base b with delay d is
written at `p + b` and read at `p + b + d`. Lines are packed at their
largest size with one sample between them. A write made while
`p < MAX_LINE` (the longest single line, D1 = 3299) is mirrored SIZE
further on, where it's read after p wraps. Because the longest line is
placed last, the buffer is only `SIZE + REACH` words. (Clouds mirrors
while `p < REACH` and needs about `SIZE + 2 REACH`.)

| line | base | reach (max read offset - base) |
|---|---|---|
| A1 | 0 | 523 (498 + 24 excursion + 1 interpolation) |
| A2 | 524 | 1334 |
| D2 | 1859 | 2756 |
| A3 | 4616 | 698 (673 + 24 + 1) |
| D3 | 5315 | 3124 |
| A4 | 8440 | 1968 |
| D4 | 10409 | 2343 |
| D1 | 12753 | 3299 |

REACH = 16052, SIZE = 16053, words = 32105. Smaller sizes only read nearer:
the bases are immediates in the assembly, and the 20 size-dependent read
offsets (6 tails + 14 taps) come from the kernel block, rewritten when the
size changes. Runs end where p wraps and where the mirroring starts
(`Process`), as in Clouds.

**Input diffusers: their own mirrored array of 1347 words (2.6 KB).** Lines
I1 (0, 105), I2 (106, 79), I4 (186, 205), I3 (392, 281); SIZE 674.

**Pre-delay: a plain ring of 4416 words (8.6 KB).** Writes are contiguous;
the run splits where the write wraps; the read index wraps branch-free.

Why three arrays and not one: the no-wrap scheme needs about twice the
delay memory. Diffusers plus tank are 16.7k samples and would need 33.4k
words, over the 32K-word limit, so the diffusers get their own small
array. The pre-delay is block-processed in a plain ring (spec: "the same
buffer or its own").

## Fixed-point formats

- Buffers and the signal between stages: int16 at the input's scale x
  0.25 (the input gain), which is the tank's headroom. The output makes up
  x 2, so the wet is half Dattorro's level (the same ratio as Clouds'
  reverb: 0.2 in, x 2 out).
- Arithmetic: the Blackfin's 16 x 16 fractional MACs into a 40-bit
  accumulator. The accumulator keeps full precision through each allpass
  and sum. What's written to a buffer is its high half, **rounded with the
  MAC's default unbiased (ties-to-even) rounding** and saturated. With
  truncation (Clouds' `(T)`), a frozen, lossless tank would integrate a
  -0.5 LSB bias per operation into a growing DC offset; with unbiased
  rounding the 60 s freeze test holds DC within +-5 LSB. The kernels clear
  `ASTAT.RND_MOD` on entry and restore it on exit, so the OP-1's rounding
  mode doesn't matter.
- Coefficients: Q15. Signed pairs are packed (`g | -g`) so each MAC takes
  a half register.
- Low cut state: 32-bit (`lp << 16`), so 20 Hz (k = 186 / 32768) has no
  dead band. Damping states: 16-bit, as Clouds'.
- Interpolated reads (pre-delay, modulated allpasses): two taps, weights
  `32767 - f` and `f` (f Q15), one rounded MAC pair.
- Delays and positions: Q16 samples, ramped per sample.
- No float at run time. The parameter curves are 33-point `constexpr`
  tables (evaluated by the compiler). Divisions are a reciprocal table
  and, for the usual n = 32, a shift.

## Parameter mappings (once per call: `PlateControl::Update`)

| knob | mapping | smoothing |
|---|---|---|
| decay | tank gain 0.2 + 0.78 x (Q15); freeze: 32767 | 1/8 per call (~12 ms), snaps when within 8 LSB |
| damp | damping coefficient k = 1 - exp(-2 pi fc / fs), fc = 600 Hz x 44.5^(1 - x), so 26.7 kHz (k = 0.9995: Dattorro's damping 0.0005) down to 600 Hz; mid knob about 4 kHz; freeze: 32767 (open) | as decay |
| diffusion | g1 = 0.75 x, g2 = 0.625 x (Dattorro's at the top) | as decay |
| motion | excursion 0..23.7 samples peak (Dattorro's 11.85 at 16384); a 1 Hz parabolic sine from a phase accumulator, A3 in quadrature, ramped per sample | as decay |
| lowcut | k = 1 - exp(-2 pi fc / fs), fc = 20 x 25^x Hz | as decay |
| predelay | 0..4409 samples (x 8820, Q16), rounded to whole samples so a still pre-delay doesn't interpolate | glides 1/8 of the way per call, at most 1/4 sample per sample (a brief pitch glide while turning) |
| freeze | decay 1 (32767), damping open, input gain ramped to 0 over 4 calls; dd2 follows decay (0.5) | via the decay and damp smoothers |
| size | below | |

When the knobs are still and every smoother has settled, `Update` reuses
the previous call's coefficients and only advances the modulation. This
is exactly what the full path would compute, and it takes 470 instead of
about 800 cycles per call.

## Size changes

Choice: **fade the wet out, crossfade the tank's reads while muted, fade
back in.**

1. The wet fades out over 4 calls (128 samples, 5.8 ms).
2. Muted, a crossfade kernel (`PlateXfadeRunAsm`) runs the tank with every
   size-dependent read (the 6 tails and both modulated allpasses) mixed
   from the old length to the new one over 256 samples (8 calls,
   11.6 ms). The recirculating signal never jumps. It skips the output
   taps, since the output is muted anyway.
3. The new size's offsets are installed and the wet fades in over 8 calls
   (11.6 ms).

The tail carries on at the new size, a frozen one too. A knob turned
through several steps chains the crossfades while muted.

Why not just "fade out, switch, fade in": the measurement showed that the
fade only hides the output taps' jump. The tank's own read jumps leave a
broadband burst in the tail that is the same size as a hard switch's. On a
sine's reverb, the broadband peak (third difference) against the signal:

| switch | steady before | fade + crossfade | hard switch |
|---|---|---|---|
| 7 -> 3 | -47.2 dBc | -42.6 dBc | +6.6 dBc |
| 3 -> 7 | -42.6 | -43.5 | -7.0 |
| 7 -> 6 | -47.2 | -43.0 | +6.0 |
| 0 -> 7 | -44.5 | -44.1 | +2.2 |
| noise 7 -> 0 | +11.9 | +10.9 | +10.1 |

The crossfaded switch stays within 0 to +5 dB of the steady state, about
40 to 50 dB below the hard switch (the residue left is the fade-in's edge
at +20 ms). With noise-like material there is nothing to measure either
way. A 128-sample crossfade measured up to +8 dB; 256 was kept.

## Cost (bfin-elf-run, cycles per call of 32 samples, 689 calls/s)

| configuration | cycles/call | per sample | MHz (sim) |
|---|---|---|---|
| stereo in/out, motion on | 7994 (max 8123) | 249 | **5.50** (peak 5.59) |
| stereo, motion off | 7994 | 249 | 5.50 |
| mono in/out, motion on | 7252 (max 7392) | 226 | **4.99** (peak 5.09) |
| mono, motion off | 7252 | 226 | 4.99 |
| the C kernels instead (stereo / mono) | 75932 / 68523 | | 52.3 / 47.2 |

Motion costs nothing extra: the modulated reads are always interpolated,
and at 0 the weights are just (32767, 0).

Transients:

- A size change costs about 9000 cycles per fade call and about 10500 per
  crossfade call (7.2 MHz) for its 8 crossfade calls (11.6 ms).
- A call where a knob moves runs the full control path: about +330 cycles.

Breakdown per steady call (stereo): tank kernel about 4700, input
diffusers 1400, pre-delay about 1100, control 470, glue about 300. The
tank loop is about 150 instructions per sample with the tap and tail loads
issued in parallel with the MACs. The size-dependent offsets cost 2
instructions each (a load from the block and an add to p), about 40
instructions per sample in stereo. Size-specialised kernels with immediate
offsets would save that, but at 8x the code, and the per-call I-cache
footprint is what costs on the device (Tests 30-31).

Hot code per steady call: tank 610 B (mono 504 B), input 218 B, pre-delay
144 B, `Process` and the control's fast path, under about 2.5 KB in all.
The crossfade kernel (634 B) runs only during size changes. The C
references (TankRunC 3.9 KB, XfadeRunC 6 KB, InputRunC 1.7 KB) are only
for tests. A device build can drop them (they're reached only through
`use_asm_ == false`).

Data streams per sample: the tank reads 20 (6 tails + 14 taps) plus 2
interpolated, and writes 8 (+8 mirrored near the wrap). The diffusers
take 4 + 4, the pre-delay 3. Each stream crosses a 32-byte line every 16
samples, so a call touches about 70 lines. Cold, that's the main device
excess to expect (100-190 cycles a miss). PREFETCH wasn't tried (Rings'
mode bank gained 4% from it).

## Comparison with the float reference (`make compare-plate`)

`FloatPlate` (tests/plate_float_ref.cc) is the same topology in double,
written plainly: one ring per line, no shared buffer or mirroring. It
runs from the same `PlateControl`, so coefficient values, ramps, fades and
crossfades are identical and only the arithmetic differs. Saw input:
220 Hz at 10000 (-10 dBFS); noise: 12000 peak.

| scenario | SNR L / R | tail SNR L / R | error RMS | tail octave bands, max diff | wet/in RMS | float peak: wet / tank |
|---|---|---|---|---|---|---|
| impulse, decay .5 | 30.6 / 30.6 dB | 29.0 / 29.1 | 0.89 LSB | 0.06 dB | (tail) | 1015 / 900 |
| impulse, no motion, .7 | 28.4 / 28.5 | 27.6 / 27.7 | 1.59 LSB | 0.06 dB | | 862 / 900 |
| noise burst, .85 | 50.4 / 50.3 | 48.8 / 48.8 | 2.48 LSB | 0.00 dB | 0.40 | 9855 / 7087 |
| saw, decay .5 | 61.2 / 63.2 | 52.5 / 51.5 | 1.78 LSB | 0.01 dB | 0.66 | 11677 / 5345 |
| saw, decay .95 | 62.3 / 60.6 | 61.5 / 59.2 | 3.63 LSB | 0.01 dB | 1.12 | 22898 / 10590 |
| saw .95, motion max | 61.9 / 60.3 | 61.1 / 58.8 | 3.59 LSB | 0.00 dB | 1.09 | 21920 / 9097 |
| saw .95, dark, size 0 | 66.4 / 61.7 | 64.4 / 59.7 | 2.99 LSB | 0.26 dB | 1.42 | 27414 / 16096 |
| noise, mono in/out | 53.4 | 51.8 | 2.46 LSB | 0.00 dB | 0.41 | 11715 / 7638 |
| noise, pre-delay + low cut, size 4 | 50.4 / 50.3 | 49.5 / 49.5 | 2.46 LSB | 0.01 dB | 0.21 | 9775 / 6160 |
| noise, bright, .98 | 56.2 / 56.2 | 54.4 / 54.4 | 5.06 LSB | 0.00 dB | 1.19 | 25071 / 12054 |

The error is fixed-point noise, 1 to 5 LSB RMS (-76 to -90 dBFS), and
the spectra agree to 0.06 dB (0.26 dB in the darkest case), so there is no
structural difference. The impulse cases' low SNR is only their low level
(a 900-LSB tank). WAVs of both renders go to `ports/fx/out/plate/`.

## Simulator test (`make test-plate`)

- **Kernels, asm = C bit-exact** on random runs (random buffers with
  full-scale samples, random coefficients including -32768 and 32767,
  every size, the mirrored region, n 1..64, guard words round every
  buffer): input 1500 runs, tank stereo 1500, mono 1500, crossfade 1000,
  pre-delay 1000. All equal, guards intact.
- **Whole reverb**, C instance against asm instance, guard words round
  both objects:
  - noise bursts with a size step every 200 calls (3300 calls, 6.5 trips
    round the tank buffer);
  - saw at .95 with motion at the top and pre-delay glides;
  - freeze on and off with damp and diffusion moving;
  - full-scale noise with odd n (2..64) and every knob jumping;
  - mono in / mono out;
  - mono in / stereo out.

  Outputs and buffers are equal and guards intact.
- **Checksums** of the C outputs match between the Blackfin and the host
  build (the Makefile compares them).
- **Stability, 60 s each (asm)**, peak / RMS / DC of the wet per 10 s:
  - decay at the top, -6 dBFS noise held: RMS 13465 -> 15113 (settled
    after 10 s, flat after), DC within +-4 LSB, tank mean within +-13;
  - freeze after 2 s of noise, motion 0: RMS 16050 -> 15912 over 50 s
    (-0.07 dB): the tail holds; DC within +-5;
  - freeze, motion at the top: RMS 5600 -> 3795 over 50 s (-3.4 dB): the
    moving linear interpolation is a slight low-pass on every pass; DC
    within +-1;
  - decay at the top, a held saw at -6 dBFS: RMS flat (16217 -> 16917),
    DC within +-11 after the first 10 s.

  Nothing grows. The peaks sit at full scale in the hot cases: see
  headroom below.

## For the OP-1 glue

- **Rate and scale.** Decimate the 44.1 kHz input 2:1 (Clouds'
  `HalfbandDecimator`), call `Process` with n = 32, interpolate the wet
  (`HalfbandInterpolator`), mix. Input and wet are int16 at the same scale.
  Mono positions: `in_r = wet_r = nullptr`.
- **Level.** Wet/input RMS is about 0.4 for short material at decay .85,
  0.7 at decay .5 with a held note, and 1.1 to 1.4 with a held note at
  decay .95 and above. MIX should be the usual equal-power crossfade with
  no extra wet gain.
- **Headroom.** The tank runs at 0.25 x the input, so with held material
  above about -10 dBFS at decay above .9 (or freeze), the tank and the wet
  saturate. This is stable (saturation only removes energy) but audible as
  a hard edge. If the OP-1's levels run hot, a smaller input gain
  (`kInputGain`, with `kOutGain` or the makeup shift raised to match)
  buys 6 dB of headroom for 6 dB of SNR.
- **Latency (wet onset).** The earliest taps are D3@197 (left) and D1@262
  (right) samples at 22050 Hz: 8.9 / 11.9 ms at size 7, 5.4 / 7.1 ms at
  size 0, plus the pre-delay. The halfband pair adds well under 1 ms.
- **Size switch.** About 29 ms from the knob to full level, 11.6 ms of it
  muted. The glue should pass `size` straight through; the plate handles
  the transition.
- **Freeze.** Also gates the input (4 calls). The glue shouldn't gate it
  itself.
- **CPU.** 5.5 / 5.0 MHz simulated (stereo / mono). Budget 1.5-4x on the
  device. Size changes peak at 7.2 MHz for 11.6 ms.
- **Not done here:** MIX (the glue's), the stored-value mapping of the
  eight knobs, and a device build without the C references.
