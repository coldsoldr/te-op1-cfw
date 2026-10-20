# ENSEMBLE: chorus to string ensemble in fixed point (ports/fx/src/ensemble)

This is a stereo chorus that morphs into a string ensemble. It's written as `op1fx::Ensemble`, runs at
22050 Hz and sits in the OP-1's modulation category (cwo → **ensemble** → shift;
docs/fx-briefs/ensemble.md, docs/effects-research.html). The design follows
Rings' and Plaits' `fx/ensemble.h` (Emilie Gillet, MIT): three delay taps per
side, phase-spread at 120°, a slow LFO plus a fast vibrato, and the third
voice of each side read from the other side's input. The parts that differ
are listed under "Changes from Rings" below. The code style is the plate's
(docs/plate-port.md): int16 no-wrap lines with mirrored writes, the
Blackfin's 16-bit MACs, and C references that the assembly matches bit for
bit.

Files:

| file | what |
|---|---|
| `ports/fx/src/ensemble/ensemble.h` | API, side-kernel block, control |
| `ports/fx/src/ensemble/ensemble.cc` | tables (constexpr), control, C reference kernel, `Process` |
| `ports/fx/src/ensemble/ensemble_layout.h` | line sizes, delays, kernel block offsets (shared with the assembly) |
| `ports/fx/src/ensemble/ensemble_bfin.S` | two kernels: one side with 3 voices, one side with 1 voice |
| `ports/fx/tests/test_ensemble.cc` | asm = C, guards, scenarios, cycles, width, latency, 60 s stability, float reference (host) |
| `ports/fx/mk/ensemble.mk` | `make test-ensemble` (included by ports/fx/Makefile) |

## API

```cpp
struct EnsembleParams { int32_t rate, depth, voices; };   // each 0..32767

class Ensemble {
 public:
  void Init();
  void Process(const int16_t* in_l, const int16_t* in_r,
               int16_t* wet_l, int16_t* wet_r, int32_t n,
               const EnsembleParams& p);
};
```

- `n`: even, 2..32 (up to 64 works). The glue passes 32.
- `in_r == nullptr`: mono input. Only the left line is written; every voice
  reads it.
- `wet_r == nullptr`: mono output. Only the left side is computed, which
  costs about half.
- With a mono input and a stereo output, the right side's sweeps are the
  left's inverted, so the image is wide (see "Width").
- Wet only. The output is int16 at the input's scale, rounded and saturated.
  In-place calls (`wet == in`) are fine: each run's input goes into the
  line before any wet sample of it is written (tested).
- `Init()` clears everything. There are no static constructors: every table is
  `constexpr`, and the make target checks the ELF for `_GLOBAL__sub_I`.
- **Size: 9336 bytes per instance** (two lines of 2288 int16, plus about 180
  bytes of state). Shared rodata is 2.5 KB (a 1025-point sine table and
  three 33-point tables).

## Signal path

Per sample, per side, with the knobs smoothed and the delays ramped:

```
line L <- in_l          line R <- in_r (mono input: no line R; all reads use line L)

m_k = A sin(phi + k/3) + F sin(psi + k/3),    k = 0, 1, 2
      phi: the RATE LFO (0.05..8 Hz); psi: a 6.57 Hz vibrato (Plaits')

wet_l = g1 L(150 + m_0) + g2 L(150 + m_1) + g3 R(150 + m_2)
wet_r = g1 R(150 - m_0) + g2 R(150 - m_1) + g3 L(150 - m_2)
        X(d): line X read d samples back, linear interpolation
```

- **Chorus** (VOICES 0): one copy per side, gain 1, no vibrato. Left is
  delayed by `150 + A sin`, right by `150 - A sin`. This is the classic stereo
  chorus with inverted LFOs. Mixed with the dry signal, each side beats
  against it in opposite directions.
- **Ensemble** (VOICES 1): three copies per side at 1/3 each, with sweeps 120°
  apart and the vibrato on. This is Rings' ensemble.
- In between, the copies' gains always sum to 1 (see VOICES below).

### Changes from Rings

- **The right side's sweeps are the left's inverted.** Rings uses the same
  `mod_1..3` on both sides, so a mono input comes out mono. Inverting the
  right side's sweeps makes it wide.
- **A RATE knob** (Rings' LFOs are fixed at 0.75 Hz and 6.6 Hz). The slow
  LFO follows RATE. The vibrato stays at 6.57 Hz (Plaits' value).
- **A VOICES morph** from chorus (one tap per side) to ensemble.
- **Delays for 22050 Hz:** rest 150 samples (6.8 ms), sweep up to ±80
  (3.6 ms), vibrato up to ±7 (0.32 ms). Plaits' 192 ± 160 ± 16 at 48 kHz
  becomes 88 ± 73 ± 7 at this rate. The rest delay here is longer, so at
  full depth the shortest delay is 2.9 ms rather than 0.7 ms, which keeps it
  out of flanger territory.
- **A pitch cap:** the sweep's peak is limited so that its pitch deviation
  stays at or below 2 % (34 cents). See RATE.

## Parameter mappings (once per call: `EnsembleControl::Update`)

| knob | 0..32767 means | notes |
|---|---|---|
| **rate** (blue) | slow LFO f = 0.05 × 160^x Hz: 0 → 0.05, 8192 → 0.18, 16384 → 0.63, 24576 → 2.25, 32767 → 8.0 Hz | 33-point increment table, linear between points |
| **depth** (green) | sweep peak A = depth × min(80 samples, 70.2 / f) | So 3.63 ms peak at f ≤ 0.88 Hz, then shrinking: 1.41 ms at 2.25 Hz and 0.40 ms at 8 Hz. The cap keeps the pitch deviation 2πfA/fs ≤ 2 %. Below 0.88 Hz the deviation is lower: 1.4 % at 0.63 Hz and 0.11 % at 0.05 Hz. Depth 0 gives a static 6.8 ms delay. |
| **voices** (white) | 0: one copy per side (1, 0, 0); 16384: two (1/2, 1/2, 0); 32767: three (1/3, 1/3, 1/3); linear between. The vibrato's peak is F = 7 × depth × voices samples (up to 0.32 ms, about 1.3 % / 22 cents at 6.57 Hz). | The gains always sum to 1, so low frequencies keep their level. |
| mix (orange) | the glue's dry/wet | |

Smoothing: rate, depth and voices each move an eighth of the way per call
(about 11.6 ms time constant) and snap when within 8 LSB. The six delays
(Q16 samples) are computed at the end of each call and ramped linearly per
sample. The three gains are ramped per sample (steps rounded toward zero,
remainders under one LSB per sample snap). The LFOs are free-running phase
accumulators and are never reset. When the knobs are still and everything
has settled, the control skips the smoothing, the table lookups and the
gains, and only advances the LFOs.

## Buffers

There are two no-wrap int16 lines of 2288 words each (4.5 KB each), as in the plate.
The write pointer `p` moves down one sample per sample, `p in [0, 2048)`.
The newest sample is at `p`, the one `d` samples older at `p + d`. Writes made
while `p < 240` (the reach) are mirrored 2048 further on. Every read is a
plain positive offset from `p`, the L registers stay 0, and nothing wraps
inside a run. `Process` splits a call into runs where `p` wraps (one split
every 64 calls at n = 32). The input is written into the lines in C; the
kernels only read.

The longest read is 150 + 80 + 7 + 1 = 238 < 240 and the shortest is 62
samples (static_asserts).

## Arithmetic

- Interpolated read: `rnd(s0 × (32767 − f) + s1 × f)`, with f = the delay's
  fraction in Q15. This is one rounded MAC pair, the same as the plate's
  modulated taps.
- Sum: `rnd(g1 v1 + g2 v2 + g3 v3)` in the 40-bit accumulator, gains in
  Q15, with the MAC's default unbiased rounding and saturation. The kernels
  clear `ASTAT.RND_MOD` on entry and restore it on exit.
- Delays are Q16 samples, ramped per sample. Gains are Q15, packed `g1 | g2`
  and ramped with a 16-bit vector add, plus `g3 | step` in a second register.
- No float at run time. All curves are `constexpr` tables. The only
  division is a shift for n = 32; other n use a reciprocal table and a 64-bit
  multiply (no libgcc division).

Kernels (`ensemble_bfin.S`):

- `EnsembleSide3Asm`: one side, 3 voices, 38 instructions per sample. Each
  voice is 8 instructions of address and weights, then 3 for the read. The
  previous voice's gain MAC is issued in parallel with the next voice's
  first load, and the delay step in parallel with the interpolation MAC.
- `EnsembleSide1Asm`: voice 1 only, 14 instructions per sample. It is used
  when g2 = g3 = 0 for the whole call (VOICES at 0 and settled). Because
  `Fr(x, 0) = 0`, it gives exactly the same output as the 3-voice kernel.
- Code: 232 + 142 bytes. The C reference `EnsembleSideRunC` (1.1 KB) is only
  for tests; a device build can drop it (it's reached only through
  `use_asm_ == false`).

## Cost (bfin-elf-run, cycles per call of 32 samples, × 689 calls/s)

| configuration | cycles/call | MHz (sim) |
|---|---|---|
| stereo in/out, ensemble (3 voices) | 3759 (max 3879) | **2.59** (peak 2.67) |
| stereo, 2 voices (3-voice kernel) | 3759 | 2.59 |
| stereo, chorus (1-voice kernel) | 2231 | 1.53 |
| mono in, stereo out, ensemble | 3636 | 2.50 |
| mono in/out, ensemble | 2233 (max 2296) | **1.53** (peak 1.58) |
| mono, chorus | 1469 | 1.01 |
| C kernels instead (stereo / mono) | 27867 / 14296 | 19.2 / 9.85 |

Targets were ≤ 4 MHz stereo and ≤ 3 MHz mono. Breakdown of a steady stereo
ensemble call: the two side kernels about 2 × 1250, control about 520 (more
in a call where a knob moves), and the line writes and `Process` glue about
700. Data per call: 2 × 32 input writes, and 6 interpolated reads per sample
spread over about 240 words per line, so about 15 cache lines per line are
touched. That is much lighter than the plate (about 70 lines).

## Tests (`make test-ensemble`, about 2 minutes, mostly the simulator)

- **Side kernels, asm = C bit-exact**: 3000 random runs each for the 3-voice
  and 1-voice kernels. Inputs: random lines with full-scale samples, delays
  anywhere in [0, 238] samples with steps up to ±2 samples per sample, gains
  and gain steps random including −32768 and 32767 (16-bit wrap), and n 1..64.
  Guard words after the outputs are intact.
- **Whole effect**, a C instance against an asm instance, with guard words
  round both objects. Scenarios: stereo noise with all knobs sweeping; a saw
  with everything at the top; full-scale noise and ±full-scale with odd n
  (2..64) and every knob jumping (including VOICES to 0); chorus ↔ ensemble
  every 100 calls (the kernel switch); mono in/mono out; mono in/stereo out;
  stereo in/mono out. Each runs 1200 to 1500 calls, about 20 trips round the
  lines. Outputs and lines are equal, and the guards are intact. In-place
  calls equal out-of-place calls.
- **Checksums** of the C outputs are equal on the Blackfin and on the host.
- **Width** (mono saw at 220 Hz, or noise, into a stereo output; L/R
  correlation and wet/in RMS):

  | setting | L/R correlation | wet/in RMS |
  |---|---|---|
  | depth 0 (static delay) | 1.000 | 1.00 |
  | chorus, depth .5, saw | 0.31 | 1.00 |
  | chorus, depth 1, saw | −0.11 | 1.00 |
  | two voices, depth .5, saw | 0.72 | 0.84 |
  | ensemble, depth .5, saw | 0.90 | 0.78 |
  | ensemble, depth 1, saw | 0.63 | 0.60 |
  | chorus, depth .5, noise | 0.01 | 0.82 |
  | ensemble, depth .5, noise | 0.05 | 0.48 |
  | ensemble, rate 8 Hz, depth 1, saw | 0.98 | 0.94 |

  At 8 Hz the pitch cap leaves only a 0.4 ms sweep, so the image is narrow.
  The width is in the slow settings. On a harmonic signal, the ensemble's
  three overlapping voices per side make the two sides more alike than a
  single-voice chorus does. The image sounds wide; the correlation figure
  just doesn't show it.
- **Latency**: an impulse at depth 0 comes out exactly 150 samples later
  (6.8 ms at 22050 Hz), at 19998/20000.
- **Stability**, 60 s each: (1) everything at the top with a held saw;
  (2) stereo noise with every knob sweeping. Wet RMS is flat, peaks stay
  below the input's, and wet DC follows input DC within ±10 LSB. There is
  no feedback path, so nothing can grow.
- **Float reference** (host only; a double-precision model with one plain
  ring per input, exact linear interpolation and exact gains). Input: a
  220 Hz saw on the left, noise plus half the saw on the right.

  | case | exact sine LFOs: SNR L / R, error RMS | same delays and gains: SNR L / R, error RMS |
  |---|---|---|
  | ensemble, rate .5, depth 1 | 50.0 / 46.5 dB, 11.6 LSB | 79.1 / 78.1 dB, 0.35 LSB |
  | ensemble, 8 Hz, depth 1 | 47.3 / 43.0 dB, 24.2 LSB | 82.2 / 80.8 dB, 0.36 LSB |
  | chorus, rate .5, depth .5 | 70.6 / 54.6 dB, 5.5 LSB | 86.0 / 82.6 dB, 0.29 LSB |
  | two voices, rate .75, depth 1 | 62.8 / 45.2 dB, 12.8 LSB | 82.0 / 78.3 dB, 0.40 LSB |
  | ensemble, mono in | 60.3 / 60.6 dB, 3.3 LSB | 81.7 / 81.9 dB, 0.28 LSB |

  With the fixed point's own delay trajectories, the arithmetic agrees to
  0.3-0.4 LSB RMS with a 1 LSB peak. Against exact sine LFOs, the delays
  differ by 0.006 samples RMS (0.015 peak) at 0.63 Hz and 0.012 (0.03) at
  8 Hz. The causes are the 1024-point sine table, the per-call linear ramps
  and the rate table's interpolation. That is a slight difference in the
  LFO's shape and timing, smooth and deterministic, not noise. It shows up
  as error mainly on the noisy right input, because the error grows with
  frequency.

## For the OP-1 glue

- **Rate and scale.** Decimate 2:1 (Clouds' `HalfbandDecimator`), call
  `Process` with n = 32, interpolate the wet (`HalfbandInterpolator`), and
  mix. Input and wet are int16 at the same scale. Mono positions:
  `in_r = wet_r = nullptr`.
- **MIX.** The chorus sound is the dry + wet sum. MIX at 50 % is the classic
  chorus, and 100 % wet is pure vibrato. An equal-power crossfade is fine.
  Rings uses `dry = 1 − amount/2, wet = amount`, which keeps the dry
  louder; that's worth trying if equal-power sounds thin at the top.
- **Level.** In chorus mode the wet is at unity (white noise reads 0.82
  only because linear interpolation is a slight low-pass on average). In
  ensemble mode the three voices add coherently at low frequencies (unity)
  and partly cancel higher up. Wet/in RMS is about 0.6 to 0.8 on a saw and
  0.48 on noise, so turning VOICES up is 2 to 4 dB quieter on the wet.
  If that's noticeable, the glue can add up to +3 dB wet makeup scaled by
  VOICES. The core doesn't, so that it never clips: its gains sum to 1.
- **Latency.** The wet is delayed 6.8 ms at rest (2.9 to 10.7 ms while
  sweeping). The halfband pair adds well under 1 ms. That's inherent to a
  chorus, not something to compensate.
- **CPU.** 2.6 MHz stereo and 1.5 MHz mono (simulated). Chorus mode is 1.5
  and 1.0. Budget 1.5-4× on the device for cold caches. The hot code is about
  2.3 KB (kernels 374 B, `Update` 1.5 KB, `Process` 0.45 KB).
- **Screen.** For the brief's moving copies, `coefs()` exposes this call's
  six delays (`pos0`, Q16 samples, EN_BASE ± the sweep), the sweep and
  vibrato amplitudes (`slow_amp`, `fast_amp`, Q8 samples) and the LFO
  phases. The smoothed VOICES maps to the visible copy count 1 → 3.
- **Not done here:** MIX, the stored-value mapping of the knobs, a device
  build without the C reference, and the screen.
