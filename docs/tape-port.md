# TAPE: tape saturation, wobble and age in fixed point (ports/fx/src/tape)

`op1fx::TapeSat`, engine 3 of the dirt category (docs/fx-briefs/tape.md):
tape saturation, wow and flutter (Beads' `WowAndFlutter`, MIT, rewritten
in fixed point), and age (head bump and HF loss, no hiss). Full rate
(44.1 kHz), in place, with the mix inside the core.

| file | what |
|---|---|
| `ports/fx/src/tape/tape.h` | API, kernel blocks, kernel prototypes |
| `ports/fx/src/tape/tape.cc` | control, C reference kernels, `Process` |
| `ports/fx/src/tape/tape_layout.h` | kernel block offsets (shared with the assembly) |
| `ports/fx/src/tape/tape_bfin.S` | three kernels: stereo delay, mono delay, lane (up, sat, down, tone, mix) |
| `ports/fx/src/tape/tape_tables.h` | const tables (generated) |
| `ports/fx/src/tape/gen_tape_tables.py` | the table generator (offline, plain Python) |
| `ports/fx/tests/test_tape.cc` | checks, cost and analysis |
| `ports/fx/mk/tape.mk` | `make test-tape` |

## API

```cpp
struct TapeParams { int32_t saturate, wobble, age, mix; };   // each 0..32767
class TapeSat {
 public:
  void Init();
  void Process(int16_t* l, int16_t* r, int32_t n, const TapeParams& p);
};
```

- In place, n even 2..64 (normally 64). `r == nullptr`: mono.
- `mix == 0` gives the input back bit for bit, whatever the other knobs
  are. A ramp down to 0 lands exactly (about 35 calls from full wet).
- The first call after `Init()` snaps every smoother to the knobs.
- Mono to stereo: the left lane's state is copied to the right on the
  first stereo call, so there's no click.
- No heap, no static constructors (the test checks the ELF), no float at
  run time, no divisions. Object size **3108 bytes**.

## Signal path (per channel; the wobble is shared)

```
in -> wobble delay (0..245 samples, linear interpolation, out at half scale)
   -> 2x up: polyphase IIR halfband
   -> quintic soft clip with drive and makeup
   -> 2x down: the same halfband (back to full scale)
   -> head bump: y = x + gb (lp180(x) - lp45(x))
   -> HF loss: two one-poles
   -> mix: dry - dry m + wet m       (the dry is not delayed)
```

- **Halfband:** hiir's design (Laurent de Soras, WTFPL) with 4 coefficients
  and a 0.1 transition. That gives a passband to 17.6 kHz, -70 dB images
  and aliases, and a group delay of about 2.3 base samples for the up/down
  pair. That is far less than the 9 samples of Clouds' 19-tap FIR, so the
  wet against the dry combs only above about 10 kHz at mid mix. Two
  first-order allpasses per path, with both paths packed in one register's
  halves. The second section rounds towards zero. Its coefficients (0.55,
  0.83) would otherwise sustain limit cycles of up to 3 LSB in silence.
- **Saturation:** S(v) = v - 2/3 v^3 + 1/5 v^5 for |v| <= 1, and +-8/15
  beyond. It is odd, so it makes no DC, and C2 at the knee. It is computed
  as out = z T(z^2): z is the drive-scaled input, clipped by a saturating
  shift, and T is a quadratic in z^2. Full input precision survives at low
  drive (z is scaled by 2^-c).
- **Rounding:** RND_MOD is cleared (unbiased, ties to even). Measured DC
  made by the effect is under 0.2 LSB.
- **All filter states are 32-bit.** The bump's low-passes run in the
  accumulators. The HF loss poles are Q16 in data registers, offset by
  0x8000 so that the high half is the rounded value. There is no dead
  band, so silence in comes back to exact 0 within about 20 calls (the
  delay ring plus the bump's 45 Hz pole).

## Parameter mappings

| knob | mapping |
|---|---|
| saturate | drive g = 0.125 x 64^s (0.125 .. 8, i.e. -18 .. +18 dB around the knee at full scale); makeup M(g) from a 33-point table so a -12 dBFS sine keeps its RMS |
| wobble | 0 .. 0.5: wow depth rises (squared) to 118 samples peak; 0.5 .. 1: wow falls to 0.4 of that while flutter rises to 4.7 samples. Wow is 0.5 Hz x (0.5..2), driven by Beads' drift (a 0.05..0.25 Hz sine whose rate is redrawn each cycle, which also scales the wow by 1 - 0.4 drift). Flutter is 7 Hz x (0.9..1.1). Delay = wow (1 + sin) x amount + flutter (1 + sin), clamped to 250 samples |
| age | HF loss: one-pole k for fc = 20 kHz x (2.2k/20k)^a, exactly open at 0, used twice in cascade. Bump gain gb = a (2 - a), up to +4 dB at 90 Hz (lp 180 Hz minus lp 45 Hz) |
| mix | 0..32767 = dry .. wet, linear (not equal power) |

**Smoothing.** The control runs on alternate calls: the knobs on one, the
wobble LFOs on the other, so no call pays for both.

- saturate, age and wobble: 1/8 per update (every 2 calls, about 23 ms),
  snapping within 8 LSB.
- Wobble depths: also slew-limited to 0.2 samples per 64. A full turn of
  the knob therefore glides the pitch by under about 11 cents instead of
  jumping (the depth reaches full in about 0.8 s).
- The delay is ramped per sample over each pair of calls.
- mix: 1/8 per call, ramped per sample.

## Measurements (`make test-tape`)

**Cost** (bfin-elf-run, cycles per 64-sample call x 689):

| configuration | avg | peak |
|---|---|---|
| stereo | 7026 cycles, **4.84 MHz** | 4.97 MHz |
| mono | 4128 cycles, **2.84 MHz** | 2.98 MHz |
| stereo, every knob moving every call | 4.90 MHz | 4.98 MHz |
| mono, every knob moving every call | 2.90 MHz | 2.99 MHz |
| the C path (stereo / mono) | 251 / 127 MHz | |

Breakdown per stereo call:

- stereo delay kernel: 744 cycles (mono: 729);
- lane kernel: 2836 cycles per channel (up 9, sat 9, down 10, tone 12,
  mix 2.5 instructions per sample);
- control: about 200 cycles (LFO call) or up to about 400 (knob call);
- glue: the rest.

Budget the usual 1.5-4x on the device for cold caches. Hot code is the
three kernels (0.95 KB) plus Process/control. The C reference kernels are
linked but only reached with `set_use_asm(false)` (tests).

**Harmonics and level** (1 kHz sine, mix 1). Each cell is H3 dBc / level
change in dB:

| sat | -24 dBFS | -12 dBFS | 0 dBFS |
|---|---|---|---|
| 0 | -73 / 0.0 | -76 / 0.0 | -52 / -0.1 |
| 0.25 | -73 / 0.0 | -58 / 0.0 | -34 / -0.5 |
| 0.5 | -66 / +0.3 | -40 / 0.0 | -16 / -3.7 |
| 0.75 | -46 / +2.0 | -21 / 0.0 | -10 / -9.8 |
| 1 | -27 / +8.1 | -11 / 0.0 | -10 / -11.5 |

- Level is exact at -12 dBFS by design. High drive compresses: quiet gets
  louder and loud gets quieter.
- Mostly odd harmonics (H3, then H5).
- Aliasing, 7 kHz sine at 0 dBFS: -91 dBc at sat 0.5. At sat 1 it is
  -22 dBc at 11.2 kHz, because the hard-clipped 11th harmonic folds at
  the 2x rate. That is 2x oversampling's limit with a nearly square
  output; for content below about 4 kHz the folds land outside the band.

**Wobble** (peak pitch deviation of a 1 kHz sine over 10 s, cents):

| wobble | peak | rms |
|---|---|---|
| 0 | 0.1 | 0.07 |
| 0.125 | 1.3 | 0.5 |
| 0.25 | 4.5 | 2.1 |
| 0.375 | 10 | 4.6 |
| 0.5 | 17.8 | 8.2 |
| 0.625 | 17.2 | 7.1 |
| 0.75 | 16.8 | 6.4 |
| 0.875 | 16.4 | 6.2 |
| 1 | 16.1 | 6.6 |

The top half keeps the depth while it turns from slow wow into fast
flutter.

**Age** (dB, -12 dBFS sine, sat 0):

| age | 60 Hz | 90 Hz | 300 Hz | 1 kHz | 3 kHz | 6 kHz | 10 kHz | 15 kHz |
|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0.5 | +3.0 | +3.2 | +1.6 | +0.1 | -1.4 | -4.6 | -8.8 | -12.4 |
| 1 | +3.8 | +4.1 | +2.0 | -1.2 | -8.9 | -17.9 | -25.1 | -30.0 |

**Checks:**

- **Kernels:** assembly = C bit for bit. The delay (stereo and mono) gets
  1500 random runs. The lane kernel gets 2000 random runs with random
  states and coefficients, full-scale samples, n 2..64, and unaligned io.
  Guard words are checked in both.
- **Effect:** C = assembly through 4 scenarios: knobs jumping, gliding,
  mono to stereo with odd even-n, and everything at the top. The C
  checksums are equal on the Blackfin and the host.
- **Mix 0** is the identity (random knobs, random n, stereo and mono).
- **Silence** comes back to exact 0 after loud noise. The host also ran
  200 seeds x 3 settings with no failure.

## For the integrator

- `Process(l, r, 64, p)` in place, as `ports/fx/op1/tape_fx.cc` does. n
  must be even. The `w`/`u` work buffers are inside the object.
- `io` (l, r) needn't be aligned. It is read and written only within
  [0, n).
- Latency of the wet: about 2-3 samples (halfband pair plus the HF poles)
  plus the wobble delay (0 at wobble 0). The dry is never delayed.
- Hot input: the core saturates gracefully (the soft clip, then saturating
  adds). With saturate high, full-scale input comes out about 11 dB
  quieter (the makeup targets -12 dBFS material).
- Not done: hiss (the task said hiss-free), pre-emphasis around the clip,
  even harmonics or bias (left out: they would make DC), and a device
  build without the C references.
- Gotcha found here: on bfin-elf-run, two half-register loads into the
  same register in one instruction (`R0.L = W[I0++] || R0.H = W[I1++]`)
  keep only one of them.
