# FOLD: a sine wavefolder with ADAA (ports/fx/src/fold)

The FOLD engine of the dirt category (docs/fx-briefs/fold.md): a wavefolder
with first-order antiderivative anti-aliasing (ADAA), symmetry, a tone
low-pass and a dry/wet mix. It's `op1fx::Fold`, at the full effect rate
(44.1 kHz, in place), fixed point, with Blackfin assembly kernels that
match C references bit for bit.

Files:

| file | what |
|---|---|
| `ports/fx/src/fold/fold.h` | API, kernel blocks, C reference declarations, the MAC rounding model |
| `ports/fx/src/fold/fold.cc` | control (smoothing, ramps), C reference kernels, `Process` |
| `ports/fx/src/fold/fold_bfin.S` | three kernels: shape (folder), tone mono, tone stereo |
| `ports/fx/src/fold/fold_layout.h` | kernel block offsets and EXTRACT patterns (shared with the assembly) |
| `ports/fx/src/fold/fold_tables.h` | const tables (generated; include it only from fold.cc) |
| `ports/fx/src/fold/gen_fold_tables.py` | the generator: `.venv/Scripts/python ports/fx/src/fold/gen_fold_tables.py` |
| `ports/fx/tests/test_fold.cc` | simulator + host test |
| `ports/fx/mk/fold.mk` | `make test-fold` |

## API

```cpp
namespace op1fx {
struct FoldParams { int32_t fold, symmetry, tone, mix; };   // each 0..32767
class Fold {
 public:
  void Init();
  void Process(int16_t* l, int16_t* r, int32_t n, const FoldParams& p);
};
}
```

- In place, 44.1 kHz. `n` even, 2..64 (normally 64). An odd `n` works but
  takes the C kernels (14 MHz mono); `n > 64` is clamped to 64.
- `r == nullptr`: mono (left only; a separate, cheaper tone kernel).
- `Init()` clears everything. No constructor, no heap, no static
  initialisers (the test checks the ELF for `_GLOBAL__sub_I`). The first
  `Process` after `Init` jumps straight to its parameters (no fade-in).
- Values outside 0..32767 are clamped.
- No alignment needs on `l` / `r` (half-word access only).

## Signal path

Per sample and channel:

```
x (int16)
 -> u = x G + c                      Q27 turns. G: fold gain; c: symmetry offset
 -> w = (F(u) - F(u')) / (u - u')    first-order ADAA of f(u) = sin(2 pi u), u' = previous u
      = sin(pi (u + u')) sinc(pi (u - u'))           (exact; see below)
 -> w - sin(2 pi c)                  the offset's DC, as the folder makes it for silence
 -> two one-pole low-passes (tone)   12 dB/oct, no resonance
 -> DC blocker, 10 Hz                one-pole high-pass
 -> x level compensation x mix  +  dry x (1 - mix)
```

### The folder and its ADAA

The folder is a sine of the scaled input (a "sine folder", as in Buchla-
style and Warps-like folders): `f(u) = sin(2 pi u)` with u in turns. At the
lowest fold a full-scale sine just reaches the sine's peak (soft
saturation); turning fold up wraps it round the sine more and more times,
so each peak grows folds.

First-order ADAA replaces `f(u[n])` with the mean of f over the segment
from `u[n-1]` to `u[n]`: `(F(u) - F(u')) / (u - u')`, F the
antiderivative. For this f, `F(u) = -cos(2 pi u) / (2 pi)`, and

    cos a - cos b = 2 sin((a + b) / 2) sin((b - a) / 2)

turns the quotient into, exactly,

    w = sin(pi (u + u')) x sinc(pi (u - u')),    sinc(t) = sin(t) / t.

So there is **no division and no ill-conditioning**: when `u = u'` the
sinc is 1 and w is just `f(u)`; there is no `0 / 0` branch and no
threshold. Two table lookups (a sine at the midpoint, a sinc of the step)
and one multiply. That's why the ADAA is cheap enough to run at 1x.

### Symmetry and DC

The symmetry offset c (up to +-1/8 turn) shifts the operating point, so
the top folds before the bottom (or the reverse) and even harmonics appear.
A shifted folder makes DC. Two parts remove it:

- `sin(2 pi c)`, which is exactly what the folder outputs for silence at
  that offset, is subtracted (ramped with c): silence in gives zero out,
  and a symmetry move doesn't step the DC.
- The remaining, signal-dependent DC (`sin(2 pi c) E[cos(2 pi G x)]`
  differs from `sin(2 pi c)`) is removed by the 10 Hz DC blocker.

### Choice: ADAA at 1x, no oversampling

The budget (4 MHz stereo, 2.5 mono) doesn't fit 2x oversampling: the
Clouds halfband pair alone costs about 28 cycles per sample per channel in
assembly, about the whole folder again, which would put stereo at 6-7 MHz.
ADAA at 1x costs about 23 cycles per sample, and the measured aliasing
(below) shows what 2x would add. ADAA's known side effect at 1x: the wet
has ADAA's half-sample average in it, `|cos(pi f / fs)|`: -0.6 dB at
5 kHz, -2.4 dB at 10 kHz, -6.4 dB at 15 kHz, -17 dB at 20 kHz. The tone
low-pass sits after it anyway, and the dry path is untouched.

## Parameter mappings

Each knob is smoothed per call (1/4 of the way, snapping within 4 LSB: a
time constant of about 3.5 calls, 5 ms), mapped once per call, and every
mapped value is ramped linearly per sample inside the kernels.

| knob | mapping |
|---|---|
| fold (blue) | G = 0.25 x 16^k turns per full scale (k = knob / 32767): 0.25, 1 at mid, 4 at the top. A -6 dBFS sine reaches 0.125 .. 2 turns at its peak: no fold at 0, four folds in each quarter of its cycle at the top. 33-point table, linear in between. |
| symmetry (green) | c = (knob - 16384) / 16384 x 1/8 turn: -1/8 .. +1/8 turn (+-45 degrees of the sine), linear; 16384 = symmetric. |
| tone (white) | one-pole coefficient `1 - exp(-2 pi fc / 44100)`, fc = 300 Hz x 2^(7 k): 300 Hz, 3.4 kHz at mid, 38 kHz; the top is 32767 (open). Two stages in series. |
| mix (orange) | linear dry/wet, Q15. Out = dry x (32767 - mix) + wet x mix, / 2^15. |
| level | from fold: `kFoldLevel`, the gain that makes a sine's wet RMS equal its dry RMS, averaged in dB over -9..-3 dBFS peaks (0.69 at fold 0, about 0.5 above mid). |

The level compensation is "rough" by nature: a folder's output level
hardly depends on its input level (it's bounded by the sine), so it
compresses. Measured wet/dry RMS (220 Hz, tone open, symmetric):

| fold | -15 dBFS in | -6 dBFS in | -1 dBFS in |
|---|---|---|---|
| 0 | 1.07 | 1.00 | 0.84 |
| 8192 | 1.37 | 1.03 | 0.50 |
| 16384 | 2.76 | 0.91 | 0.61 |
| 24576 | 3.18 | 0.90 | 0.59 |
| 32767 | 2.93 | 0.94 | 0.52 |

So at -6 dBFS the level holds within 1 dB over the whole knob; quiet input
comes up by up to +10 dB at high fold and hot input goes down by up to
-6 dB.

## Mix 0 is bit-exact

When the smoothed mix has settled at 0 (start and end of the call's ramp
both 0), `Process` leaves `l` / `r` untouched and the tone kernel writes
into an internal sink instead, so the folder's previous u, the tone
states and the DC blocker keep running (no transient when the mix comes
back up). The test checks it both from `Init` and after full wet and back.
While the mix ramps through 0 the output is `dry x 32767 / 32768`
(rounded), within 1 LSB of dry.

## Fixed-point formats

- u: Q27 turns (int32). `|x G| <= 4` turns, `|c| <= 1/8`, so
  `|u| < 2^29 + 2^24` and the sum and the difference of two u fit.
- Sine: 256 segments of `value | delta << 16` (Q15), one 32-bit load per
  lookup, linear interpolation with the MAC's rounded fractional multiply.
  Segment = bits 20..27 of `u + u'`, fraction = bits 5..19 (one EXTRACT
  each).
- Sinc: 1064 segments over `|u - u'|` = 0..8.3 turns (128 per turn), Q14
  capped at 16383; `|u - u'|` can't reach the end (at most segment 1056),
  so there's no clamp. Interpolation error below -80 dB.
- w: Q14 (`sin x sinc`, one fractional multiply). w minus the DC term
  stays within +-27968, so every filter difference fits 16 bits.
- Tone states: int16, Q14 (rounded updates; two stages packed in one
  register, or both channels' stage in one register in stereo). With
  16-bit states, the darkest setting has a dead band of about 12 LSB of
  Q14 (-63 dB below full scale): a tiny static offset, which the DC
  blocker after it removes.
- DC blocker: 32-bit state (Q30), coefficient 47 / 32768 (10.0 Hz).
- Mix: dry x (32767 - mix) as an integer product plus 2 e (level x mix)
  as a fractional one in the 40-bit accumulator, extracted with S2RND
  (x 2, rounded, saturated).
- Ramps: g (gain), c, the DC term as 32-bit ramps in I registers (step
  then use); level x mix and 32767 - mix share one 32-bit ramp (high and
  low halves; the low half stays in 0..32767 so no carry crosses).
- Rounding: the kernels clear `ASTAT.RND_MOD` (unbiased rounding) on
  entry and restore it on exit; `FoldMulFrac` and `S2Rnd` model it.
- No float, no division at run time: the per-call ramp steps use a 1/n
  table in two 16-bit halves (no 64-bit multiply); a settled call skips
  the mapping entirely.

## Cost (bfin-elf-run, cycles per 64-sample call, x 689 calls/s)

| configuration | cycles/call | MHz (sim) |
|---|---|---|
| stereo | 4637 (max 4746) | **3.19** (peak 3.26) |
| mono | 3059 (max 3168) | **2.10** (peak 2.18) |
| stereo, every knob moving every call (LFO) | 4752 (max 4776) | 3.27 (3.29) |
| mono, every knob moving every call | 3174 (max 3198) | 2.18 (2.20) |
| C kernels instead (stereo / mono) | 41251 / 20630 | 28.4 / 14.2 |

Kernels alone, per 64 samples: shape 1441 (one channel, 22.5 per
sample), tone mono 1107 (17.3), tone stereo 1253 (both channels, 19.6 per
stereo pair). Control and call overhead about 450-550 cycles per call
(the settled fast path), about 120 more with knobs moving.

Device: Clouds and Rings ran 1.5-4x the simulator with cold caches (Tests
30-31). FOLD has no buffers; per call it touches the sine table (1 KB,
random lines), the low part of the sinc table (4.2 KB in all; quiet or
low-frequency material stays in its first lines), w_ (256 B) and about
1 KB of hot code (shape 278 B, tone 150 / 200 B, Process 406 B, the fast
part of Control). Budget 1.5-2.5x the simulator.

## Memory

- `sizeof(Fold)` = 636 bytes: the kernel blocks, two channel states (12 B
  each), the smoothers, a 256 B scratch for the folder's output and a
  256 B sink for the bypass.
- Const tables, shared: 5936 bytes (sine 1024, sinc 4256, the three knob
  curves 3 x 132, 1/n 260).
- Stack: each kernel uses 13 words (LINK, saved registers, loop registers, ASTAT); `Process` has no large locals.

## Aliasing (host, `make test-fold`)

A -6 dBFS sine on an FFT bin (8192 points), tone open, symmetric, full
wet, after 1 s. "naive" is the same folder without ADAA (`sin(2 pi u[n])`,
double precision); "ADAA" is this effect (fixed point; "float" is the same
ADAA in double); "2x" is, for context, ADAA in double at 88.2 kHz between
Clouds' 19-tap halfbands. Figures are alias power against harmonic power
(dB), all aliases and only those below 5 kHz (the inharmonic tones under
the note, the audible ones).

| sine | turns at the peak (fold) | all: naive / ADAA / 2x | below 5 kHz: naive / ADAA / 2x |
|---|---|---|---|
| 1997 Hz | 0.25 (8192) | -94.0 / -85.5 / -67.8 | -100.4 / -91.9 / -75.6 |
| 5001 Hz | 0.25 | -48.1 / -55.5 / -60.6 | -100.7 / -90.4 / -80.8 |
| 8005 Hz | 0.25 | -18.3 / -26.1 / -29.8 | -48.0 / **-68.9** / -92.1 |
| 1997 Hz | 0.5 (16384) | -83.4 / -81.8 / -59.7 | -89.9 / -87.4 / -70.5 |
| 5001 Hz | 0.5 | -18.5 / -25.5 / -32.1 | -70.8 / **-83.8** / -69.1 |
| 8005 Hz | 0.5 | +1.5 / -8.7 / -10.9 | -14.7 / **-39.7** / -55.9 |
| 1997 Hz | 2.0 (32767) | -8.5 / -12.9 / -23.2 | -67.8 / **-78.4** / -48.1 |
| 5001 Hz | 2.0 | +7.5 / -9.1 / -20.5 | -2.0 / **-27.4** / -39.2 |
| 8005 Hz | 2.0 | +9.2 / 0.0 / -8.7 | +5.6 / **-8.3** / -27.6 |

- Where there is aliasing to speak of, ADAA takes 10 to 25 dB off the
  audible (below 5 kHz) aliases and 4 to 17 dB off the total. The fixed
  point matches the double-precision ADAA to 0.1 dB in every case with
  real aliasing.
- At low fold and low notes both sit at the noise floor: the fixed point's
  floor is about -85 to -92 dB (Q14 output, table interpolation); the 2x
  reference's floor, -68 to -80 dB, is the 19-tap halfband's leakage, not
  folding.
- 2x adds another 8 to 20 dB at the highest folds and notes, at about
  twice the cost.
- The test fails if ADAA isn't at least 6 dB better than naive below 5 kHz
  (or under -80 dB).

## DC (`make test-fold`, the assembly)

Mean of the output over 2 s after 1 s, and the worst 250 ms mean:

| case | DC | worst 250 ms | RMS |
|---|---|---|---|
| symmetry 0, fold max, 220 Hz -6 dB | -1 | 0 | 11476 |
| symmetry max, fold max, 220 Hz -6 dB | 0 | 0 | 11475 |
| symmetry 0, fold 0 | 0 | 0 | 8423 |
| symmetry max, fold mid, 1 kHz -12 dB | 1 | 0 | 10522 |
| silence, symmetry 0, fold 20000 | 0 | 0 | 0 |
| symmetry swept end to end at 2 Hz, fold mid | 0 | 1673 | 11026 |

Static DC is gone (at most 1 LSB). With the symmetry swept by an LFO the
DC follows the sweep faster than the 10 Hz blocker, leaving a 2 Hz
wobble about 16 dB under the signal (-26 dBFS here): subsonic, but it
takes headroom. A faster blocker (20 Hz) would halve it at the cost of
about 1 dB at 40 Hz in the wet; left at 10 Hz.

## Tests (`make test-fold`)

Blackfin simulator:

- **Kernels, asm = C bit-exact**: 3000 random runs (random states, ramps,
  coefficients, full-scale input, n 2..64, stride 1 and 2) of the shape
  kernel, the mono tone kernel and the stereo tone kernel, with guard
  words. All equal, guards intact.
- **Effect, asm = C**: four scenarios, outputs and channel states equal on
  every call: knob jumps (stereo), fold/symmetry sweeps at LFO rate,
  full-scale noise with n 2..64 and out-of-range knob jumps, mono.
- **Mix 0 identity**, from Init and after full wet and back, C and asm,
  mono and stereo: bit-exact.
- **DC** and **level** (tables above), **cycles** (above).

Host: the same C checks, the aliasing measurement, and the C output's
checksums, which must equal the Blackfin's (the Makefile compares them).

## For the integrator

- Call `Process(l, r, 64, params)` in place; `r = nullptr` for mono. The
  four knobs are the brief's: blue fold, green symmetry (16384 =
  centre), white tone, orange mix, each 0..32767.
- The mix is inside; mix 0 is a bit-exact bypass. The mix law is linear
  (like CRUSH); the wet is roughly level-matched to the dry at -6 dBFS.
- Latency: the wet is half a sample late (ADAA's midpoint), plus the tone
  filters' phase. Nothing to compensate.
- The wet can exceed full scale (level x the folded peak, up to about
  1.4 x before the mix) and is saturated by the mix's extraction.
- CPU: 3.2 MHz stereo, 2.1 mono in the simulator, nearly constant (no
  data-dependent branches; mix 0 costs the same).
- Screen hints for the SVG: `fold` picks `wave0..wave7` (turns at the
  peak of a -6 dBFS input: 0.125 x 16^k, so wave i ~ k = i / 7);
  `symmetry` is bipolar around 16384; `tone` fc = 300 x 2^(7 k) Hz.
- Not done here: the OP-1 glue (`ports/fx/op1/*`), the stored-value
  mapping of the knobs, a device build without the C references (they are
  only reached when `use_asm_` is false or n is odd).
