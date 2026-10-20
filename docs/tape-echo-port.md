# TAPE ECHO: a multi-head tape delay in fixed point (ports/fx/src/tape_echo)

A Space Echo / Echoplex style tape delay, as `op1fx::TapeEcho`, at 22050 Hz,
for the OP-1's delay category (engine 2 of 2: delay -> **tape echo**;
brief: `docs/fx-briefs/tape-echo.md`). It's built like the plate
(`docs/plate-port.md`): a half-rate, wet-only core; Blackfin assembly run
kernels with bit-exact C references; and randomized tests with guard words.

| file | what |
|---|---|
| `ports/fx/src/tape_echo/tape_echo.h` | API, pattern and head tables, kernel block, run kernels |
| `ports/fx/src/tape_echo/tape_echo.cc` | tables (constexpr), control, C reference kernels, run splitting, `Process` |
| `ports/fx/src/tape_echo/tape_echo_layout.h` | tape size and kernel block offsets (shared with the assembly) |
| `ports/fx/src/tape_echo/tape_echo_bfin.S` | the stereo and mono run kernels |
| `ports/fx/tests/test_tape_echo.cc` | simulator and host test |
| `ports/fx/mk/tape_echo.mk` | `make test-tape-echo` (included by `ports/fx/Makefile`) |

## API

```cpp
struct TapeEchoParams {
  int32_t time;       // 0..32767 -> 30 ms .. 1.5 s (exponential): head 3's delay
  int32_t feedback;   // 0..32767 -> 0..1.1 (linear; 1.0 at 29789)
  int32_t age;        // 0..32767: new tape -> worn
  int32_t heads;      // pattern, 0..kTapeEchoNumPatterns - 1
  bool hold;          // input off, feedback 1
};
const int32_t kTapeEchoNumPatterns = 4;
extern const uint8_t kTapeEchoPatternHeads[4];   // bit h: head h+1 active
const int32_t kTapeEchoNumHeads = 3;
extern const int32_t kTapeEchoHeadPos[3];        // Q15 fraction of TIME: 10923, 21845, 32768

class TapeEcho {
 public:
  void Init();
  void Process(const int16_t* in_l, const int16_t* in_r, int16_t* wet_l, int16_t* wet_r,
               int32_t n, const TapeEchoParams& p);
  int32_t time_samples() const;          // head 3's delay now (gliding), samples
  int32_t time_target_samples() const;
  int32_t head_level(int32_t h) const;   // 0..32767, ramps on pattern changes
};
```

- `n`: even, 2..64. The glue passes 32.
- `in_r == nullptr`: mono input. `wet_r == nullptr`: mono output, which
  runs the cheaper mono kernel on the 16-bit arrays (stereo in, mono out
  uses the mid `(l + r) >> 1`). Mono in with stereo out runs the stereo
  kernel with R = L, so both outputs are equal.
- The wet is at the input's scale, saturated. In-place calls are fine.
- `Init()` clears everything. There are no static constructors (all
  tables are `constexpr`, and the test checks the ELF for
  `_GLOBAL__sub_I`).

### Head patterns (blue click)

| index | heads | `kTapeEchoPatternHeads` | echo times (x TIME) |
|---|---|---|---|
| 0 | 3 | `0b100` | 1 |
| 1 | 2 + 3 | `0b110` | 2/3, 1 |
| 2 | 1 + 2 + 3 | `0b111` | 1/3, 2/3, 1 |
| 3 | 1 + 3 | `0b101` | 1/3, 1 |

Heads sit at 1/3, 2/3 and 1 of TIME (`kTapeEchoHeadPos`), so TIME is
always the longest head's delay. A pattern change ramps each head's
weights over 8 calls (11.6 ms) per sample, with no click.

## Signal path

Per channel (stereo is two independent loops on one tape, L and R side by
side in 32-bit pairs, so every read serves both):

```
record sum (half scale) = in x drive/2 + sum_h(head_h x fbnorm_h) x feedback/2
  -> low cut: one-pole high-pass (32-bit state), 40 .. 110 Hz with age
  -> tape loss: one-pole low-pass, 9 kHz .. 1.8 kHz with age
  -> saturation: 32767 sin(2u) for |2u| <= pi/2, then flat (a 257-point table)
  -> the tape (int16)
wet = sum_h(head_h x outnorm_h) / drive
head_h = the tape at delay d_h, linearly interpolated (2 taps)
```

- **Per repeat:** every pass goes through the low cut, the loss low-pass,
  the saturation and the interpolation, so each echo is darker, more
  saturated and (with age) more wobbly than the one before.
- **Drive** = 1 + age (1..2). It goes in at the record sum and comes out
  of the wet, so small signals return at unity at any age. The
  saturation starts earlier as the tape wears: the tape level caps at
  full scale / drive at the output.
- **Saturation curve** (`TapeEchoSaturate`, `kTapeEchoSatTable`): IronOxide's
  clamp-then-sin (Airwindows, MIT), written as a table. The input is the
  16-bit record level at half scale (-2..2 FS), and the slope is 1 at 0.
  It is self-contained here; the TAPE dirt engine can share it
  (integrator's choice).
- **Normalised weights** by the number of active heads: feedback 1, 1/2,
  1/3 (they sum to 1, so FEEDBACK 1 sustains in every pattern); out 1,
  0.6, 0.45 (between equal power and equal sum).
- **Wow and flutter** (Beads `WowAndFlutter`, MIT): a random sine
  (0.125 Hz, then 0.05..0.25 Hz, redrawn at phase 1/4) sets the wow's rate
  (`fm = 0.5 + 1.5 rm^2`) and depth (`1 - 0.4 rm`). Wow is 0.23 Hz x fm and
  flutter 1.99 Hz. The offset is (96.5 wow + 15.5 flutter) samples x age
  (Beads' 105 + 17 at 24 kHz, rescaled to 22.05 kHz). It is added to head
  3's delay, and **all heads scale with it** (`d_h = r_h (t + mod)`), as a
  tape speed change does. A first version added the same offset to every
  head instead, and a multi-head loop at feedback 1.1 and AGE 1 then died
  out, because its modes de-tuned against each other. With the scaling it
  self-oscillates as it should. The pitch wobble is then 1/3 and 2/3 as
  large on heads 1 and 2.

## Parameter mappings (once per call)

| knob | mapping | smoothing |
|---|---|---|
| time | 661.5 x 50^x samples (30 ms .. 1.5 s), rounded to a multiple of 3 samples so all three heads land on whole samples (no interpolation loss when still) | the glide below |
| feedback | loop gain 0 .. 1.1 (linear) | 1/8 per call (~12 ms), snap within 8 LSB |
| age | wow/flutter depth 0..1; loss fc 9 kHz x 0.2^age; low cut 40 x 2.75^age Hz; drive 1 + age | as feedback |
| heads | the pattern table | weights ramp 1/8 of full scale per call, per sample within the call |
| hold | input gain to 0 (ramped over at most 16 calls), loop gain exactly 1, loss at age/4, low cut 10 Hz (DC only) | via the smoothers |

**Time glide (tape motor).** Head 3's delay `t` moves with a velocity `v`
that follows `(target - t) / 2048` (a 93 ms time constant). `v` is limited
to +-0.75 sample per sample (pitch 0.25x .. 1.75x while it moves) and
changes by at most 32/32768 per sample per sample (0 to full speed in
24 ms). It lands exactly on the target. The read positions are ramped per
sample (Q15 samples), so any time change bends the pitch of everything on
the tape and never jumps. A full sweep (30 ms -> 1.5 s) takes about 2 s.

**Fast path.** When the knobs are still and every smoother has landed,
`Control` only advances the wow/flutter and the head ramps. All ramps
land exactly (`RampTo` snaps the last < n LSB), so this is the same as
the full path.

## Memory

`sizeof(TapeEcho)` is about 133.5 KB: the tape is 33217 pairs x 4 B =
132,868 B (TE_TAPE_SIZE 33216 + 1 mirror pair, which covers 1.5 s + 112
samples of wow + interpolation), plus about 600 B of state and scratch.
Mono instances use only the low halves of the tape. Two instances come to
about 267 KB.

## Fixed point

- The tape is int16 pairs, one ring written downwards. The pair at
  [N] mirrors [0] for the interpolation's second tap (copied after each
  call). Reads wrap: `Process` cuts the runs where the write wraps and
  where any head's read crosses the wrap (`TapeEchoPrepareRun`; a read
  index never rises, because every step is under 1). The assembly then adds
  a per-run constant (`k->adj`), while the C reference wraps each read
  itself, so the test checks one against the other.
- Arithmetic: the Blackfin's 16 x 16 fractional MACs into 40 bits,
  extracted with unbiased rounding and saturation (RND_MOD is cleared by
  the kernels). The low cut has a 32-bit state. The loss low-pass is
  16-bit, as a dual 16-bit vector op. The saturation is table + linear
  interpolation, floored.
- No float at run time. There are no divisions: `/3` is a multiply-shift
  (`Third`, checked exact by a `static_assert`), and the ramps divide by a
  shift (n a power of two) or a reciprocal table.

## Cost (bfin-elf-run, cycles per 32-sample call, x 689 calls/s)

| configuration | cycles/call | MHz (sim) avg | peak |
|---|---|---|---|
| stereo, 3 heads, age max, knobs still | 5032 | **3.46** | 3.76 |
| stereo, 1 head (the same: all heads are always computed) | 5032 | 3.46 | 3.76 |
| stereo, every knob moving every call (stress) | 5870 | 4.04 | 4.31 |
| mono, 3 heads, age max, still | 4100 | **2.82** | 3.13 |
| mono, every knob moving every call (stress) | 4938 | 3.40 | 3.69 |
| C kernels instead (stereo / mono) | 59312 / 73032 | 40.9 / 50.3 | |

- The kernel alone is about 4065 (stereo) / 3393 (mono) cycles per 32
  samples: 127 / 106 per sample. Each head is 16 instructions (one
  32-bit pair load serves both channels).
- The **peaks** are calls split into two runs, which happen 4 times per
  trip round the tape (the write's wrap and each head's), so at most a
  few times a second. Each split costs about 430 cycles (a second kernel
  entry and the run search).
- The stress rows change time, feedback, age, the pattern and hold every
  call. One knob turning costs a little over the still rows.
- Hot code: stereo kernel 462 B, mono 404 B, plus `Control`/`Process`. The
  C references (`TapeEchoRunC` 3.3 KB) are reached on Blackfin only when
  `use_asm_` is false; a device build can drop them, as with the plate.
- Data streams per sample: 3 heads x 2 pair loads, 1 pair store, the
  kernel block. On the device, budget 1.5-4x for cache misses: the reads
  span 132 KB of SDRAM.

## Levels and stability

- **Wet level:** an impulse at AGE 0 and FEEDBACK 0 comes back at its
  delay, spread by the loss low-pass (peak 14271 for a 16000 impulse).
  Below the loss corner the echo is at unity for one head, 0.6 each for
  two and 0.45 each for three. AGE leaves small-signal levels alone.
- **Feedback above 1 self-oscillates** and is bounded by the saturation
  (tape at most full scale, wet at most full scale / drive x the out
  weights). Measured over 60 s (test output):
  - FEEDBACK 1.1, 3 heads, AGE 1, after a 0.15 s burst: RMS 6725 ->
    8988, steady from 10 s on, peak about 13500, DC within +-1.
  - FEEDBACK 1.1, AGE 0, 1 head, a held saw: RMS 18600 flat, peak 32764.
  - FEEDBACK 1.1 with TIME sweeping end to end and full-scale noise: RMS
    about 10500-11200, flat; peak at full scale (the wet saturates).
  - At AGE 0 with 3 heads in phase the howl drives the wet into its
    saturation. That's expected: the heads add up to 1.35 x the tape.
- **Hold:** after 2 s of noise at AGE 0.5, 2 heads, 1.2 s, the RMS goes
  2847 -> 1082 -> 853 -> 720 -> 621 -> 547 per 10 s. The HF goes first (the
  quarter loss and the interpolation), then about -1.5 dB per 10 s.
- Nothing wraps: every sum saturates. Unbiased rounding keeps the DC
  within about +-10 LSB with noise. The exception is a held saw at FEEDBACK
  1.1, which carries about 600 LSB (-35 dBFS) of DC. The saturation comes
  after the low cut, and clipping an asymmetric waveform makes DC. It
  stays flat and doesn't grow. A DC blocker after the tape would remove
  it if the integrator wants one.

## Glide check (one pass, a 440 Hz sine, worst third difference)

| jump | steady | while gliding | ratio |
|---|---|---|---|
| 30% -> 70% (longer: pitch down) | 756 | 756 | 1.00 |
| 70% -> 30% (shorter: pitch up) | 756 | 1475 | 1.95 |
| 0 -> 100%, worn | 273 | 275 | 1.00 |
| 100% -> 0, worn | 275 | 637 | 2.31 |

A pitch of r gives r^3 x the third difference, so the limit of 1.75x
allows 5.4x, and a click would be far above that. The test fails above 7x.

## Tests (`make test-tape-echo`, from ports/fx in WSL)

- **Kernels:** stereo and mono assembly against the C reference over 3000
  random runs each. These use a random full-scale tape, random block
  values (including the extremes), every write position, reads near the
  wrap and the mirror pair, and n 1..64. All are equal, and the guard
  words round the tape and the block are intact.
- **Whole echo,** C instance against asm instance, with guard words round
  both objects:
  - pattern steps;
  - time sweeps at AGE 1;
  - FEEDBACK 1.1 with hold and AGE moving;
  - full-scale noise with odd n and every knob jumping;
  - mono in/mono out;
  - mono in/stereo out;
  - stereo in/mono out;
  - FEEDBACK 1.1 with time sweeps (8000 calls, more than 7 tape trips).

  Outputs and tape are equal.
- **Checksums** of the C outputs are equal on Blackfin and host.
- **Impulse response:** every pattern at mid TIME, plus 3 heads at the
  shortest time and 1 head at the longest. Each active head's echo is at
  its exact delay (1559 / 3118 / 4677 samples at mid TIME; 221 / 442 /
  663 at the shortest; 33072 at the longest), L = R.
- **Glide** and **stability** as above.

Result: `ok: 0 failure(s)` on both builds, and "Blackfin = host
(checksums)".

## For the integrator

- Rate and scale are the plate's: decimate 2:1, `Process(n = 32)`,
  interpolate, mix. Mono positions pass `in_r = wet_r = nullptr`.
- The glue's `hold` toggles; the core ramps the gate and the feedback
  itself.
- The screen: `kTapeEchoPatternHeads` for the lit heads, `time_samples()`
  for the mark speed (it glides with the pitch bend), and `head_level(h)`
  if the head lamps should fade.
- `ports/fx/src/tape_echo/*.cc` holds the C references (needed by the host
  build and for `use_asm_ = false`); they are not test-only. Tests live
  in `tests/`.
- The saturation curve (`TapeEchoSaturate`, half-scale input) can be
  unified with the TAPE dirt engine's later.
