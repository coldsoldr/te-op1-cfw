# DRIVE: an oversampled soft-clip overdrive (ports/fx/src/drive)

The OP-1's dirt category's second engine (docs/fx-briefs/drive.md):
`op1fx::Drive`, a soft clipper run at 88.2 kHz, with a bias that makes the
clipping lopsided, a tilt EQ after it and an auto-levelled wet, at the effect
rate (44.1 kHz), in place. Fixed point throughout, written like the plate:
the Blackfin's 16-bit vector ops and MACs, with C references that the
assembly matches bit for bit.

Files:

| file | what |
|---|---|
| `ports/fx/src/drive/drive.h` | API, kernel blocks, the passes' C and assembly entry points |
| `ports/fx/src/drive/drive.cc` | tables (constexpr), control, C reference passes, `Process` |
| `ports/fx/src/drive/drive_bfin.S` | seven kernels: interpolate, shape, decimate, tilt, DC blocker, mix, mono wet |
| `ports/fx/tests/test_drive.cc` | asm = C, the effect C = asm, mix-0 identity, cycles; on the host: harmonics, level, aliasing, DC, tilt |
| `ports/fx/mk/drive.mk` | `make test-drive` (included by `ports/fx/Makefile`) |

## API

```cpp
struct DriveParams { int32_t drive, tone, bias, mix; };   // each 0..32767; tone 16384 = flat
class Drive {
 public:
  void Init();
  void Process(int16_t* l, int16_t* r, int32_t n, const DriveParams& p);
};
```

- In place, 44.1 kHz, `n` even, 2..64 (the OP-1 passes 64). `r == nullptr`:
  mono.
- `Init()` clears every state; the knobs start at drive 0, tone flat, bias
  0, mix 0 and glide to the first call's values.
- Mix 0 returns the input bit for bit (tested from `Init` and after the mix
  has been up and come back).
- No heap, no exceptions, no static constructors (the test checks the ELF
  for `_GLOBAL__sub_I`), no float at run time: every table is `constexpr`,
  evaluated by the compiler. No libgcc divisions or 64-bit multiplies on
  the run-time path.
- Size of the object: **1284 bytes** (two history buffers and a block of
  decimated wet; no delay lines).
- Tests only: `set_use_asm(false)` runs the C passes, `set_oversampling(false)`
  runs the shaper at 44.1 kHz (C only), for the aliasing comparison.

## Signal path

Per channel:

```
x -> 2x halfband interpolation (19 taps, Clouds' coefficients) at half
     scale (x' = x / 2), + beta = b / K, saturated
  -> u = sat(K x')                      the clipper's input: g x / 1.875 + b
  -> y = u (15 - 10 u^2 + 3 u^4) / 16   quintic soft clip (half scale)
  -> 2:1 halfband decimation (88.2 -> 44.1 kHz)
  -> - f(b)                             the bias's static offset
  -> tilt: Hm y + Dm lp(y)              one-pole split at 1 kHz; the
                                        auto-level gain is folded in
  -> DC blocker (10 Hz)
out = dry (1 - mix) + wet mix           (dry untouched)
```

**The clipper.** `f(u) = u (15 - 10 u^2 + 3 u^4) / 16` for |u| <= 1, flat
beyond: the quintic smoothstep's derivative, so the curve and its slope and
curvature are continuous at the clip point (it's C2: fewer high harmonics,
so less to alias, than the cubic `u - u^3 / 3`). Its slope at 0 is 15/16
(at the half scale it runs at; 1.875 at full scale) and it peaks at 0.5
(full scale 1.0). It's a polynomial, not a table: it vectorises (both
channels in one register, no gather), and it costs no cache.

Gain staging: `u = g x / 1.875 + b`, `g = 10^(dB / 20)`. At drive 0 the
small-signal gain through the clipper is exactly 1 and a full-scale peak
reaches only u = 0.53 (1.7 dB of soft compression), so 0 dB is nearly clean;
by +36 dB a -12 dBFS sine is driven to u = 8.4, nearly square.

**Bias** shifts the operating point: the clipper sees `u + b`, b = 0..0.5,
so the positive side clips first and even harmonics appear. `f(b)` (a table)
is subtracted after the decimator, so the bias's static offset never reaches
the tilt or the DC blocker as a step; the blocker removes what's left (the
signal-dependent part).

**Why the bias goes into the interpolator.** The clipper's input is formed
as `sat(K sat(x / 2 + b / K))`. The two saturations equal the single one,
`sat(K x / 2 + b)`, because K >= 1.067 always (the inner one only saturates
where the outer one would anyway). This form needs only 16-bit vector ops:
a saturating add, a fractional multiply by the gain's mantissa (Q15) and a
saturating vector shift by its exponent (1..7). The exponent is fixed per
call and the mantissa ramps per word.

**Oversampling.** Clouds' 19-tap halfband (Blackman-windowed sinc, DC gain 1),
written for this layout: the interpolator's even phase is the 10-tap sum, its
odd phase the centre sample x 0.5; the decimator is the centre x 0.5 plus 10
taps on the other phase. The wet lags the dry by **9 samples** (measured on
an impulse; chosen to be a whole number of samples).

**Tilt.** `w = Hm y + Dm lp`, lp a one-pole low-pass at 1 kHz (16-bit state),
`Hm = m hi`, `Dm = m (lo - hi)`: highs x m hi, lows x m lo. With tone flat,
hi = lo, so the low-pass drops out. m (the auto-level gain) rides along in
the same two coefficients.

**DC blocker.** `d = w - round(acc >> 16)`, `acc += 2 k d` with the low-pass
in the 40-bit accumulator (k = 47 / 32768: 10 Hz): no dead band, so it settles
on the exact DC. It sits last before the mix, so it also takes out what the
tilt's 16-bit low-pass leaves after a signal stops (a few LSB that its dead
band holds; with the blocker earlier, silence came out as a 4 LSB DC).

**Mix.** `out = (dry gd + w gw) >> 14`, rounded (the MAC's S2RND extraction),
gd = (32768 - mix) / 2 and gw = mix in Q14, with mix 32767 taken as 32768.
At mix 0 (gd = 16384, gw = 0) the product is exactly `dry << 14`: the output
is the input bit for bit. At 32767 it is the wet (within 1 LSB; the wet is
held at half scale and weighted x 2).

## Fixed point

- Signals: int16 throughout; the shaper's output and the decimated wet at
  half scale (peak 0.5), so the decimator's ringing (about 9% on a square)
  has headroom. The tilt's output is also half scale and saturates at twice
  full scale; the mix makes up the x 2.
- Products: the 16-bit fractional multiplies and MACs, rounding unbiased
  (ties to even). Every kernel clears `ASTAT.RND_MOD` on entry and restores
  ASTAT on exit. The C references model this (`Fr`, `Rnd`, `Sat16` in
  drive.cc, as in plate.cc).
- Packed: channel 0 in the low half of each 32-bit word, channel 1 in the
  high. One instruction processes both channels; the dual MACs use one
  accumulator per channel.
- Mono: the oversampled passes (interpolate, shape, decimate) have no
  feedback, so mono runs the block's two halves side by side as if they were
  two channels, each with the other's samples as history, at half the
  iterations. The wet passes (tilt, DC blocker, mix) are recursive. For mono
  they're fused into one single-channel kernel: the DC blocker in A1, the
  tilt and the mix on A0. Short mono blocks (n < 18) run through the stereo
  passes with r = l.
- Ramps: every coefficient is a packed value plus a packed step, added per
  sample (the gain per 2x word). A step rounds toward zero so a ramp never
  overshoots, and a ramp whose step rounds to 0 snaps to its target. Every
  ramp therefore settles exactly; mix 0 depends on this.

## Parameter mappings

| knob (0..32767) | mapping |
|---|---|
| **drive** | 0..+36 dB, linear in dB: g = 10^(36 x / 20), x = knob / 32768; K = 2 g / 1.875 (Q16, 33-point table, linear between) |
| **tone** | tilt: highs x 2^t, lows x 2^-t, t = (knob - 16384) / 16384 (+-6 dB each way, 12 dB end to end; 33-point table of 2^t); the split is a one-pole at 1 kHz; 16384 is exactly flat |
| **bias** | b = knob / 65536: 0..0.5 of the clipper's range (b / K into the interpolator; f(b) from a 33-point table) |
| **mix** | linear dry/wet; 32767 is all wet; 0 is the input bit for bit |

**Auto-level.** m(drive, bias) is the reference sine's RMS in over the wet's
RMS out (DC removed). The reference is a -12 dBFS sine. m is computed by the
compiler from the clipper's double-precision model on a 17 x 9 grid (drive x
bias) and read bilinearly. It runs from about 1.0 at 0 dB (1.8 with the bias
at the top) down to about 0.18 at +36 dB. At the reference level the loudness
is flat across drive and bias. Quieter material comes up as the drive rises,
and louder material goes down, which is what a clipper does (the table below).

The shelf gains m hi and m lo are each clamped to 2.0, the kernel's
coefficient range. That only happens at drive ~0 with the bias up and the
tilt far over, and there the boosted shelf stops at +6 dB.

**Smoothing.** Each knob runs through a one-pole per call (an eighth of the
way, ~12 ms, snapping within 8 LSB). The coefficients' targets are computed in
two groups on alternate calls: group A is the gain, the bias and f(b); group B
is the level and tilt and the mix. Each group ramps over two calls (2.9 ms),
so each call pays for half the control, peak as well as average. When the
block size or the stereo/mono layout changes, both groups ramp within that
call and the alternation restarts. With the knobs still and everything
settled, `Control` returns at once.

## Measurements (`make test-drive`, host analysis: double maths on the C output, which the Blackfin's equals bit for bit)

**Harmonics**: a 1 kHz sine at -12 dBFS, wet only, tone flat (dBc):

| | H2 | H3 | H4 | H5 | H7 |
|---|---|---|---|---|---|
| drive 0 dB, bias 0 | none | -50.5 | none | -103 | -106 |
| drive 12 dB, bias 0 | none | -26.3 | none | -58.8 | -101 |
| drive 12 dB, bias half | -17.2 | -27.3 | -44.3 | -57.8 | -96 |
| drive 12 dB, bias max | -10.4 | -38.3 | -35.1 | -54.7 | -104 |
| drive 24 dB, bias 0 | none | -10.7 | none | -17.6 | -24.4 |
| drive 24 dB, bias max | -12.8 | -13.1 | -15.8 | -26.4 | -46.6 |
| drive 36 dB, bias 0 | none | -9.6 | none | -14.2 | -17.3 |
| drive 36 dB, bias max | -24.5 | -9.7 | -24.7 | -14.6 | -18.1 |

With bias 0 the clipper is exactly odd-symmetric in fixed point too: the even
harmonics are absent (below the measurement floor). The bias brings them
in. At high drive the bias matters less: the input swings far past it, so the
waveform is nearly square either way and only the duty cycle shifts.

**Level**: wet RMS over input RMS (dB), 1 kHz sine, tone flat:

| drive | -24 dBFS, bias 0 | bias max | -12 dBFS, bias 0 | bias max | -6 dBFS, bias 0 | bias max |
|---|---|---|---|---|---|---|
| 0 dB | +0.08 | +0.01 | +0.01 | +0.01 | -0.23 | 0.00 |
| 12 dB | +1.22 | +0.07 | +0.07 | +0.06 | -3.09 | -0.56 |
| 24 dB | +8.36 | +4.70 | -0.03 | -0.02 | -5.71 | -5.59 |
| 36 dB | +11.50 | +11.36 | -0.05 | -0.05 | -6.00 | -6.00 |

At the reference level (-12 dBFS) the loudness stays within +-0.08 dB at
every drive and bias.

**Aliasing**: power on the non-harmonic bins (folded harmonics and anything
else) over the fundamental, wet only, bias 0. "No oversampling" is the same
chain with the shaper at 44.1 kHz:

| sine | drive | no oversampling | 2x | less |
|---|---|---|---|---|
| 5.0 kHz, -6 dBFS | 12 dB | -31.4 dB | -43.9 dB | 12.5 dB |
| 5.0 kHz, -6 dBFS | 24 dB | -12.3 | -25.3 | 12.9 |
| 5.0 kHz, -6 dBFS | 36 dB | -9.9 | -18.0 | 8.2 |
| 8.7 kHz, -6 dBFS | 12 dB | -14.7 | -31.0 | 16.3 |
| 8.7 kHz, -6 dBFS | 24 dB | -7.9 | -19.2 | 11.3 |
| 8.7 kHz, -6 dBFS | 36 dB | -6.7 | -15.3 | 8.6 |
| 12.1 kHz, -12 dBFS | 24 dB | -9.7 | -24.1 | 14.4 |

2x buys 8-16 dB. At +36 dB the clipper is nearly a hard clip whose harmonics
fall off only as 1/k, so a lot still folds back from above 44.1 kHz. More
would take 4x (twice the oversampled passes' cost) or ADAA on top
(docs/effects-research.html).

**DC** (output mean, wet only, after 1 s): 0.14 LSB (200 Hz, -6 dBFS, 12 dB,
bias max), 0.00 at 36 dB, 0.00 (1 kHz, -20 dBFS, drive 0, bias max), 0.00
(bias half, 24 dB). Silence in with the bias at the top, at every drive:
exactly 0 out after 1.5 s.

**Tilt** (drive 0, -24 dBFS, bias 0; gain at 100 Hz / 1 kHz / 8 kHz, dB):

| tone | 100 Hz | 1 kHz | 8 kHz |
|---|---|---|---|
| 0 (dark) | +5.9 | +3.4 | -3.8 |
| 8192 | +3.0 | +1.2 | -2.2 |
| 16384 | 0.0 | +0.1 | +0.1 |
| 24576 | -2.9 | +0.8 | +2.7 |
| 32767 (bright) | -5.5 | +2.9 | +5.5 |

The one-pole split is gentle: the shelves are only fully reached about two
octaves either side of 1 kHz.

## Simulator test (`make test-drive`)

- **Passes, asm = C bit-exact** on random runs, 1500 each: random packed
  samples including full scale, random coefficients and ramps (including
  wrapping ones), every n 1..64, guard words round every buffer. The tilt
  and mix are also checked in place; the mono wet kernel is checked with one
  run and with two.
- **The effect**, a C instance against an asm instance, 1500 calls each:
  stereo n = 64; mono (split) n = 64; stereo with n jumping 2..64; mono with
  n jumping 2..64 (both the split and the short-block path, and the layout
  changes between them). The knobs jump at random every 37 calls (the drive
  every 5), with sines, noise and full-scale random input. Outputs are equal.
- **Checksums** of the C outputs match between the Blackfin and the host.
- **Identity**: mix 0 for 400 calls from `Init`, then the mix up for 200,
  then back at 0. Every mix-0 call after it settles (1200 calls, stereo and
  mono) equals the input bit for bit, with the other knobs jumping at random.
- No static initialiser in the ELF.

## Cost (bfin-elf-run, cycles per 64-sample call; MHz = cycles x 689 / 10^6)

| configuration | cycles/call | MHz |
|---|---|---|
| stereo, knobs still | 4651 | **3.20** |
| stereo, knobs moving (an LFO on drive, or three knobs at once) | 5009 (max 5029) | **3.45** (peak 3.46) |
| mono, knobs still | 3235 | **2.22** |
| mono, knobs moving | 3605 (max 3636) | **2.48** (peak 2.50) |
| the C passes instead (stereo / mono) | 219556 / 110845 | 151 / 76 |

Targets: <= 4 MHz stereo, <= 2.5 MHz mono. Mono with knobs moving is right
at the limit.

Per pass (stereo, n = 64): interpolate ~990, shape ~1230 (128 words), decimate
~850, tilt ~590, DC ~270, mix ~340; control ~360 (one group) when moving,
~50 settled; the history copies and glue ~300. Mono: interpolate ~540 and
decimate ~460 (32 steps each), shape ~650 (64 words), the fused wet ~1020
(64 samples), history moves ~260. The inner loops are 14 (interpolate), 9
(shape, per word), 12 (decimate), 8 (tilt), 3 (DC), 4 (mix) and 14 (mono wet)
bundles per step. Measured, they run about 1-1.5 cycles per step over that,
in stalls.

Code: the assembly is 1.2 KB. drive.cc is 11.5 KB with the C references, or
5.5 KB built with `-DOP1FX_DRIVE_ASM_ONLY`: control and tables only, with the
C passes and the tests' no-oversampling path left out. The tables are about
1.1 KB of rodata.

Per call the kernels touch about 2.5 KB of data: the object's buffers and the
caller's block. There are no delay lines, so the cold-cache excess seen on the
device with Clouds (Tests 30-31) should be small. The hot code is about 1.2 KB
plus `Process` and the control.

## For the integrator

- **Call it directly at 44.1 kHz** with the effect's 64-sample block, in
  place; no decimation by the glue (DRIVE does its own 2x oversampling
  inside). Mono: `r = nullptr`.
- **Mix is inside the core.** The glue should pass MIX straight through and
  not crossfade again. Mix 0 is a bit-exact bypass, but it still costs the
  full CPU. If the glue wants to save the CPU at mix 0 it can skip `Process`;
  then call `Init()` (or keep calling) before bringing the mix up again, or
  the first few milliseconds will come from stale history.
- **The wet lags the dry by 9 samples (0.2 ms).** At partial mix and low
  drive, where the wet still resembles the dry, the sum combs (first notch
  ~2.45 kHz). It's milder as the drive rises and the two decorrelate. The
  brief asks for mix 0 to be the input bit for bit, so the dry isn't delayed.
  If the comb matters more than that, the dry can be delayed by 9 samples
  before the mix pass (a 9-sample ring per channel; mix 0 then becomes a
  9-sample delay rather than the identity).
- **Level.** At a -12 dBFS sine the output matches the input at every drive.
  Hotter material loses up to 6 dB at full drive and quieter material gains
  up to 11.5 dB, as a clipper does. If the OP-1's synth level sits elsewhere,
  move `kRefLevel` in drive.cc: the table rebuilds at compile time.
- **Headroom.** The wet is computed at half scale. The tilt can boost to twice
  full scale before saturating, and the mix saturates at full scale.
- **Device build:** `-DOP1FX_DRIVE_ASM_ONLY` (and the usual
  `-ffunction-sections`/`--gc-sections` if the glue wants the unused
  `DriveShapeOne` dropped too).
- **Switching mono and stereo** between calls is handled: the layout change
  makes both control groups ramp within the call, and the mono path leaves
  channel 1's histories and states equal to channel 0's. R starts from L's
  state for the first few samples after a switch to stereo.
- **The screen** (docs/fx-briefs/drive.md) has no core values to read beyond
  the knobs. The transfer-curve variants follow drive, and the bias's clip
  asymmetry follows b = knob / 65536.
- **Not done here:** the OP-1 glue (op1/), the screen, packaging, the device.
