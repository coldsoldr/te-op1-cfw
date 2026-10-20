# SHIFT: frequency shifter / ring modulator (ports/fx/src/shift)

A Bode-style single-sideband frequency shifter with barber-pole feedback and
stereo spread, plus a ring-modulator mode, as `op1fx::FreqShift`, for the
OP-1's modulation category (cwo → ensemble → **shift**,
docs/fx-briefs/shift.md). Full rate (44.1 kHz), in place, fixed point, with
two Blackfin assembly kernels that match their C references bit for bit.

The stock cwo is already a Bode shifter. SHIFT adds three things: the
spread (L and R shifted by different amounts), a feedback loop through a
delay (barber-pole sweeps), and ring modulation.

## Files

| file | what |
|---|---|
| `ports/fx/src/shift/shift.h` | API, kernel blocks, control mappings |
| `ports/fx/src/shift/shift.cc` | control (`Process`), C reference kernels |
| `ports/fx/src/shift/shift_bfin.S` | the two kernels: Hilbert pair, channel |
| `ports/fx/src/shift/shift_layout.h` | kernel block offsets and scratch map (shared with the assembly) |
| `ports/fx/src/shift/shift_tables.h` | generated: Hilbert coefficients, sin/cos table |
| `ports/fx/src/shift/gen_tables.py` | writes shift_tables.h |
| `ports/fx/src/shift/design_hilbert.py` | designs the Hilbert pair (pure Python, minimax, Q15 polish) |
| `ports/fx/tests/test_shift.cc` | the checks (below) |
| `ports/fx/mk/shift.mk` | `make test-shift` |

## API

```cpp
struct ShiftParams {
  int32_t shift;      // 0..32767, 16384 = 0 Hz
  int32_t feedback;   // 0..32767
  int32_t spread;     // 0..32767
  int32_t mix;        // 0..32767 dry -> wet
  bool ring;          // false: frequency shift, true: ring modulation
};

class FreqShift {
 public:
  void Init();
  void Process(int16_t* l, int16_t* r, int32_t n, const ShiftParams& p);
};
```

- In place. `n` **even**, 2..64 (normally 64). The Hilbert kernel works in
  sample pairs, so an odd `n` isn't supported.
- `r == nullptr`: mono. One shifter runs, with the left channel's settings
  (shift + spread), and `r` is never touched.
- `Init()` before use. There are no static constructors (the test checks
  the ELF), no heap and no float at run time. The only division (`2^16 / n`)
  is a constexpr table.
- Mix 0 returns the input bit for bit. If the mix is 0 at both ends of a
  call, the kernel doesn't write `io` at all, although the loop keeps
  running.

## Parameter mappings

**Shift** (blue): `d = 2 (shift - 16384)` (Q15, -32768..32766), and

    Hz = 2000 × (d / 32768)^3

So knob 0 gives -2000 Hz, 16384 gives 0 Hz exactly, and 32767 gives +1999.63 Hz.
The cubic gives fine control near 0. Some sample points:

| d / 32768 | ±0.05 | ±0.1 | ±0.2 | ±0.3 | ±0.5 | ±0.75 | ±1 |
|---|---|---|---|---|---|---|---|
| Hz | 0.25 | 2 | 16 | 54 | 250 | 844 | 2000 |

The phase step is `((d^3 >> 15) × 194783097) >> 30` (`ShiftIncrement`,
where 194783097 is 2000 × 2^32 / 44100). It's computed only when the
position changes. Measured output frequency error is under 0.001 Hz at every
point tested, from 0.1 Hz to 2 kHz.

**Spread** (white), following the designer's reading: the spread is added in
the knob domain, before the cubic. `s = spread × 10923 >> 15` (spread / 3, up
to 10922 = a third of the range).

    L: d + s,   R: d - s    (each clamped to -32768..32767), then the cubic

At shift 0, full spread gives L +74.06 Hz and R -74.06 Hz. At shift +20 Hz,
full spread gives L +330.5 Hz and R -3.27 Hz (both measured). Mono uses L.
In ring mode the carriers are |L| and |R|.

**Feedback** (green): loop gain `g = feedback × 30474 >> 15`, so 0..0.93.

**Mix** (orange): linear, with Q15 weights `32767 - mix` (dry) and `mix` (wet).

**Mode** (blue click): `ring`. A change crossfades over 256 samples (4
calls), with no click (see "Ring mode" below).

**Smoothing:** the phase step, feedback and mix each ramp linearly across
one call to their new values, and the steps are rounded towards zero so they
never overshoot. The oscillator's phase is continuous.

## Signal path

```
x = (l + r) / 2 (stereo) or l (mono)          32-bit, x 2^10
 -> Hilbert pair: I chain 4 allpasses, Q chain z^-1 + 4 allpasses
    (each y = a (x + y[n-2]) - x[n-2], Q15 a, 32-bit data, saturating)
 -> I, Q to int16 (rounded); Q x m (m = 1 shift, 0 ring)
 per channel (L, and R in stereo):
 -> a = (I, mQ) + g fb                 fb = the loop's complex output 256 samples ago
 -> w = a e^(j phase)                  512-entry sin/cos table, interpolated
 -> wet = Re w
 -> loop line <- lowpass(w)            one pole, lp += 0.6 (w - lp), complex
 -> out = dry (32767 - mix) + wet mix  >> 15
```

**Hilbert pair.** This is Niemitalo's polyphase IIR structure, the one Warps'
`quadrature_transform.h` uses: two chains of allpasses in z^-2, plus a
one-sample delay on one chain. The coefficients were designed for this port
by `design_hilbert.py 4 40 16000` (minimax over 40 Hz to 16 kHz at 44.1 kHz,
then rounded to Q15 and polished as integers):
`4422, 13735, 22025, 27283, 30118, 31535, 32237, 32621`, with the even ones
on the I chain. Warps' own poles are for 96 kHz and 17 sections, so they
weren't reused. The data is 32-bit (int16 × 2^10). The worst-case gain of a
chain is 5.3 (l1 norm), so this leaves about 12× headroom, and the adds
saturate anyway. A 32×16 multiply costs three instructions:
`(FU)` low × a, `>>> 15`, then a signed MAC of high × a.

**Why one Hilbert pair for stereo (mono sum in):** the Hilbert pair is about
half the cost (about 3200 of 6600 cycles a call), so a second pair would push
stereo to about 7000 cycles (4.9 MHz) or more, over the 5 MHz target. The
feedback runs in the analytic (complex) domain, so each channel still has
its own shift and its own barber-pole loop from one shared I/Q. The cost is
that the wet signal is a mono sum shifted two ways. The dry path stays true
stereo, and the spread supplies the width.

**Feedback / barber-pole.** The complex output (after the low-pass) goes into
a 256-sample line (5.8 ms) for each channel and is added back to the
analytic input. Each pass round the loop shifts by the shift again. Small
shifts give a barber-pole flanger: comb teeth 172 Hz apart that drift by
the shift frequency, endlessly up or down. Large shifts give climbing or
falling spectral smears. The one-pole low-pass (0.6) damps the high-frequency
build-up, as Warps' feedback low-pass does.

Stability: |rotation| ≤ 1, low-pass ≤ 1, and g ≤ 0.93, so the linear loop
is stable. Every stage saturates to int16, so the loop is also bounded at any
input. The feedback product is rounded **towards zero**, so the loop's own
quantisation can't sustain a limit cycle. Silence in gives exactly zero out
within a second, at the top feedback and any shift.

**Ring mode.** Q is crossfaded to 0 (m: 1 → 0 over 256 samples), so
`wet = Re((I + j·0) e^(jφ)) = I cos φ`. That is the input (allpass-dispersed,
which you can't hear) times a sine at |shift|: both sidebands, and no carrier.
Each sideband comes out at **-6 dB** relative to the input (measured: sum
-6.0 dB, difference -6.0 dB, input tone -96 dB, carrier -98 dB). Frequency
shift gives one sideband at 0 dB. The integrator may want makeup gain in ring
mode. Feedback works in ring mode too: each pass re-modulates the signal.

## Measurements (test_shift.cc)

**Sideband rejection** (mono, full wet, tone at 16000 peak, wanted over
unwanted, Hann-windowed DFT). The dashes are where the unwanted image lies
within 20 Hz of 0 Hz.

| shift \ tone (Hz) | 50 | 100 | 200 | 500 | 1k | 2k | 5k | 8k | 10k | 15k |
|---|---|---|---|---|---|---|---|---|---|---|
| +250 Hz | 48.1 | 50.8 | 49.3 | 50.6 | 50.1 | 49.5 | 51.7 | 49.9 | 54.6 | 53.5 |
| -250 Hz | - | - | - | 50.6 | 50.1 | 49.5 | 51.7 | 49.9 | 54.6 | 53.5 |
| +31.25 Hz | - | 50.8 | 49.3 | 50.6 | 50.1 | 49.5 | 51.7 | 49.9 | 54.7 | 53.4 |
| -1000 Hz | - | - | - | - | - | 49.5 | 51.7 | 49.9 | 54.6 | 53.5 |

The worst case is **49.3 dB over 100 Hz–10 kHz** (the target was 40) and
48.1 dB over every tone from 50 Hz to 15 kHz. The design predicts at least
48 dB from 50 Hz to 18 kHz, falling to 30 dB at 30 Hz and 19 dB at 20 Hz.

**Other checks:**

- **Shift accuracy:** under 0.001 Hz error at every point from ±0.1 Hz to
  ±2 kHz.
- **Feedback at the top:** shifts of 0, ±0.3, -3, 60, -700 and ±2000 Hz, in
  both modes. Each run is 2 s of full-scale noise, 2 s of a loud chord, 2 s of
  bursts, then 2 s of silence. The peak is bounded (saturation) and the last
  second is exactly 0.
- **Long runs:** 60 s of full-scale noise at the top feedback and spread,
  then silence. Bounded, and silent after. This one also runs on the
  Blackfin, through the assembly.
- **Mix 0:** bit-exact over 4000 calls of random settings (mono and stereo,
  any mode, including after wet calls), for both the assembly and the C.

**Cost:** Blackfin simulator, 64-sample calls, × 689 calls/s:

| | cycles / call | MHz |
|---|---|---|
| stereo, steady settings | 6585 | **4.54** |
| mono, steady settings | 4954 | **3.41** |
| stereo, every setting moving + mode switches (peak) | 7187 | 4.95 |
| mono, same (peak) | 5550 | 3.82 |
| C reference, stereo / mono | 93000 / 80000 | 64 / 55 |

Breakdown (steady): Hilbert kernel about 3220 cycles (8 sections × 32 pairs ×
10 instructions + fill + convert), each channel kernel about 1500
(oscillator 9, loop 9 and mix 3 cycles a sample), control about 230 (mono)
or 360 (stereo). The section loop runs at its instruction count, with no
stalls in the trace. A mode change costs about 320 more cycles that call (Q
is multiplied by m sample by sample).

**Mono misses the 3 MHz target by about 0.4 MHz.** Almost all of that is
the 4 + 4-section Hilbert pair. `design_hilbert.py 3 95 10500` gives a 3 + 3
pair (`5557, 16243, 24530, 29084, 31274, 32370`) with 42.8 dB worst-case
over 100 Hz–10 kHz, but only 17 dB at 50 Hz. It would save about 680
cycles: mono about 2.95 MHz, stereo about 4.07 MHz. The kernel is unrolled
per section (the `SECTION` list in shift_bfin.S, and `kShiftHilbert`), so
the switch is small. I kept 4 + 4 for the bass rejection. It's the
integrator's call.

**Memory:** `sizeof(FreqShift)` is about 3.9 KB: the Hilbert state (100 B);
two channels at 1064 B each (state plus a 256-word complex loop line); 1440 B
of scratch (five 72-word runs for the out-of-place sections, reused by the
channel kernel); the 256 B I/Q block; and a few words. Constant tables:
kShiftOsc 4 KB (512 sin|cos pairs, then 512 step pairs) and 16 B of
coefficients.

## Tests: `make test-shift` (ports/fx, in WSL)

It runs on the Blackfin simulator, then on the host. They check:

- **kernels:** 3000 random blocks per kernel, assembly against C: outputs,
  every state word, and guard words. The random states and coefficients go
  beyond the reachable range (saturation), m ramps and m = 32768 steady are
  both covered, as are write 0/1, every n from 2 to 64, and delay wraps.
- **instance:** an assembly FreqShift against a C one over 6000 calls of
  sweeps, mode switches, top feedback, mono and stereo, and n from 2 to 64.
- **identity, rejection, accuracy, spread, ring, feedback** (above). The
  double-precision measurements run on the host only, on the C reference,
  which the Blackfin run shows is equal to the assembly.
- **checksums:** the C output through a scenario. The Blackfin and host
  values must be equal (the make rule compares them).
- **no static initialiser** in the ELF, and **cycles**.

Result: all pass. Blackfin = host (checksum 881abe04).

## Integrator notes

- **Parallel stores** (learnt here): in the simulator, a store in parallel
  with a 32-bit ALU op that writes the same register (`R6 = R0 + R6 (S) ||
  [I0++] = R6`) stores the **new** value, while one beside a MAC extraction
  stored the old one. The kernels never store a register that the same
  instruction writes. Add this to the HANDOVER assembly rules.
- **Rounding:** the kernels clear RND_MOD (unbiased, ties to even, the MAC
  default) and restore ASTAT. The C models ties-to-even (`Rnd15`). Setting
  RND_MOD for biased rounding did *not* change the MAC extractions in the
  simulator, so don't rely on it. `Rx.L = Ry (RND)` (the 32→16 conversion)
  rounds half up.
- **Read-ahead:** the loop reads the delay word after the current one, which
  at a wrap is 4 bytes past the line, still inside the object. All the other
  reads stay inside the scratch and the tables. `l`/`r`/`io` are read
  exactly n samples, never past the end.
- **Glue:** call `Process(l, r, 64, params)` once per effect call. Map the
  encoders to 0..32767, and the blue click toggles `ring`. The screen's
  `f = (shift - 16384) / 16384` and `sp = spread / 32767` match the
  designer's geometry exactly, since the spread is a third of the knob range
  (designs/shift.md). For the `sweep` dot, `ShiftIncrement(d)` (the phase
  step) gives the true rate.
- **Cache:** the hot data is the object (3.9 KB, of which each channel's 1 KB
  line is streamed linearly) and the 4 KB table, read near-sequentially at
  the shift rate. On the device, expect some of the 2–4× SDRAM factor seen
  with Clouds if the object isn't in L1.
- Not done: packaging, the screen and glue (ports/fx/op1), and device tests.
