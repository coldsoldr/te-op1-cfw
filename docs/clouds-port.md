# Clouds port: design, validation and state

Supersedes `docs/clouds-handover.md` (the snapshot from when the port was started). Project context and the user's decisions: `docs/HANDOVER.md` §3. The OP-1 effect interface: `docs/next-projects.md` §1 and `docs/device-tests.md` Test 16.

## 1. State (2026-10-16)

| Part | State |
|---|---|
| Upstream copy + harness (`src/dsp`, `harness/`) | Done; the copy is bit-identical to upstream (`make copy-check`) |
| Fixed-point core (`src/core/clouds_core.*`) | **Looping delay and granular done and verified** (§3); time stretch and spectral output silence |
| CPU | **Assembly kernels for the grains, reverb and diffuser; a CPU governor** (§4); the looper's path (pitch shifter, filters) is still C |
| OP-1 hookup | **Done, not yet on the device:** `op1/clouds_fx.cc`, `op1/clouds_settings.h`, built into the Rings image (`make op1-clouds` in `ports/rings`); package `op1_246_rings_clouds1.op1`, Test 17. Design: `docs/HANDOVER.md` §3 |

Nothing has been committed for Clouds; `op1_246_rings_clouds1.op1` is waiting for the device (Test 17).

## 2. Layout (`ports/clouds/`)

| Path | What |
|---|---|
| `src/dsp/`, `src/resources.*` | Upstream Clouds, copied (namespace `op1clouds`). Two diagnostic switches, both off by default: `OP1CLOUDS_COPY_REAL=double` (the looper's and pitch shifter's phases in double, `make copyd`), `OP1CLOUDS_GRAIN_LOG` (one stderr line per scheduled grain) |
| `src/core/config.h` | Rate profile: 32000 (upstream's constants, for comparison) or 44100 (the OP-1; time constants scaled by 44100/32000) |
| `src/core/dsp_fixed.h` | Q formats (audio Q27, params Q16, coefficients Q31), soft limit, LCG, `SemitonesToRatio` (FFloat) |
| `src/core/record_buffer.h` | Upstream's 16-bit circular buffer with interpolation tail and freeze crossfade |
| `src/core/looping_player.h` | Looping delay (delay Q15, loop phases Q16) |
| `src/core/granular_player.*` | Granular: upstream's scheduling and random sequence; each grain as runs (envelope a linear ramp between its kinks, no buffer wrap) for the kernels |
| `src/core/grain_kernel.h`, `grain_kernels_bfin.S` | Grain overlap-add kernels (linear / Hermite, mono / stereo): 16-bit dual-MAC arithmetic, C reference + Blackfin assembly, bit-exact |
| `src/core/governor.h` | CPU governor: the grain cap from the engines' measured load (feed-forward) and late calls (a learned ceiling) |
| `src/core/svf_fixed.h`, `pitch_shifter_fixed.h` | stmlib SVF, pitch shifter (C) |
| `src/core/diffuser_fixed.*`, `diffuser_layout.h`, `diffuser_bfin.S` | Diffuser: 16-bit buffer (Q14), run kernel in C and assembly, bit-exact |
| `src/core/reverb_stereo.*`, `reverb_layout.h`, `reverb_stereo_bfin.S` | Clouds' reverb, stereo: 16-bit MAC run kernel in C and assembly, bit-exact; Rings-style no-wrap buffer with mirrored writes |
| `src/core/clouds_core.*` | The processor: feedback, record, players, diffuser, pitch shifter, filters, reverb, dry/wet; mono or stereo; all memory inside the object |
| `tests/profile_core.cc` | Cycle costs per configuration on Blackfin (64-sample calls, cheap input): `make profile [S=prefix]` |
| `tests/test_kernels.cc` | Every assembly kernel against its C (random runs; the reverb and diffuser over more than a trip round their buffers, stereo and mono; guard words): `make test` |
| `tests/test_governor.cc` | The governor against a simulated OP-1 (measured load, sudden load, infeasible load, hidden load): `make test-governor` |
| `harness/profile.py`, `profile_lines.py` | Instructions per function / per source line from a `bfin-elf-run --trace-insn=on` trace |

Make targets (WSL, from `ports/clouds`): `ref`, `copy`, `copy-check`, `copyd`, `port`, `port44`, `compare`, `spec`. `bench` works but is very slow (the harness's input generator is soft-float on Blackfin); use the profile driver instead:

```
B=~/op1-toolchain/bfin/bin
$B/bfin-elf-g++ -O2 -g -std=c++17 -mcpu=bf524 -msim -fno-exceptions -fno-rtti -DOP1_SIM -I. -I.. -I../rings \
  tests/profile_core.cc src/core/clouds_core.cc src/core/granular_player.cc src/core/reverb_stereo.cc \
  src/core/clouds_tables.cc ../rings/src/fixed_tables.cc -o build/profile_core.elf
$B/bfin-elf-run build/profile_core.elf [config-prefix] [blocks]
```

## 3. Validation (port at 32 kHz constants vs upstream float, 21 scenarios)

- **Looping delay:** plain delay, filters, diffuser and dry/wet at 64–65 dB SNR (16-bit rounding).
  - The pitched and frozen scenarios differ from upstream mostly by **upstream's own float rounding**: its delay and loop phases are single-precision floats, which round by ~0.0005 sample per step at thousands of samples.
  - The double-precision diagnostic copy (`make copyd`) differs from upstream float just as much: 0.7 dB on the frozen loop. The port tracks the double copy: freeze 32 dB, sine at −7 74 dB, mono 61 dB, high-pass 43–51 dB.
  - The pitch shifter at +12 drifts slowly against the reference (a ~5 ppm phase-increment difference from the per-block size smoothing). Only the crossfade timing moves, which is inaudible.
  - **Spectra match** (`dirspec` against copyd): mean band difference 0.01–0.26 dB, 0.44 dB with reverb + feedback.
- **Granular:** deterministic 54 dB, dense −12 57 dB, frozen sine 55 dB, mono 58 dB, stereo spread 56 dB, mode switch 64 dB.
  - **The grain logs are identical to upstream** (`OP1CLOUDS_GRAIN_LOG`): every grain starts at the same sample, with the same size, pitch, pan and quality.
  - `gr_rand_p12` / `gr_diffuse` read 18 dB only because the harness's Q16 position (0.2 → 0.199997) moves every grain start by one sample. With the position rounded the other way they read 59.5 / 55.3 dB.
- **Stages skipped at 0** (feedback, diffuser, reverb): the output is unchanged, since upstream's contribution is exactly 0. When a stage comes back it restarts cleared. Only `gr_knobs` (the diffuser switching on mid-sweep) and `ld_mono_music` (an LSB from the mono reverb's mix, now exact) changed.

## 4. CPU (simulator, one instance, 44.1 kHz; the device runs ~1.6x this)

| Config (`make profile`) | C only | Now |
|---|---|---|
| looping delay, stereo, plain (-7, no diffusion/feedback/reverb) | 35 | 31 |
| looping delay, stereo, diffusion 0.5 + feedback 0.3 + reverb 0.5 | 75 | 49 |
| looping delay, mono, plain | 23 | 20 |
| granular, stereo, random, ~7 grains | 68 | 28-30 |
| granular, stereo, dense, ~20 grains | 138 | 51 |
| granular, stereo, ~7 grains + reverb + feedback + diffusion + spread | 108 | 49 |
| granular, mono, random, ~8 grains | 57 | 28 |

What changed:
- **Grain kernels in assembly:** ~35 instructions per linear grain-sample, ~46 Hermite (stereo), from ~190 in C. Not yet scheduled into parallel slots (~30% more to gain).
- **Reverb:** ~470 instructions per sample in C, now a 16-bit-MAC assembly kernel (~-15 MHz with reverb on).
- **Diffuser:** likewise (~-7 MHz with diffusion on).
- Dry/wet's crossfade is computed once per block while the knob is still; the grain-gain smoothing stops once settled; idle stages (feedback, diffuser, reverb at 0) are skipped.

Still C, by size (plain looping delay, per stereo sample): the pitch shifter ~120 instructions, the two texture SVFs ~110 (32-bit states needed for the 20 Hz high-pass), the Hermite delay reads ~60, dry/wet + soft limit ~80. Granular with no grains costs ~18 MHz (scheduling, dry/wet, recording).

**The budget (Test 16):** ~300 MHz free with nothing playing; with Rings model 6 x 4 voices only ~40-75 MHz (device) for Clouds, phone's share included.

**The governor** (`src/core/governor.h`, `make test-governor`): sets the grain cap per call.
- Feed-forward: 0.75 of the period (Test 16's free time includes the UI's) minus our engines' measured cycles. Over it, the cap drops in proportion at once; well under, it grows one grain per ~0.1 s.
- Feedback for load it can't see (stock engines, drums, tape): an overload (a call later than 5/4 of the period, or the mean interval over period + 1/64, as probe v3 found) sets a ceiling at 7/8 of Clouds' cycles then, which relaxes ~1%/s and goes once past the overload point.
- Simulated: no late calls with measured load (heavy Rings: 9 grains at 89% of a period); hidden load 72%: 46 late calls while it learns, then none; recovers to the full count when the load goes.
- Looping delay has no grains: its cost is fixed (plain ~31 MHz simulator, ~50 device), so with heavy Rings it can still overload. Levers if needed: the pitch shifter in assembly, a lower internal rate.

## 5. OP-1 hookup: what's known

- **Effect calls** (Test 16): 64 samples at 44.1 kHz; stereo (L, R) with params at `[SP+12]`; not called while switched off. Phone's process words: mono `0x1911308`, stereo `0x1911328`.
- **Phone's encoder ranges** (the FX Ranges table, built at boot; read by running its initialiser `0x1a1e914` in `bfin-elf-gdb`'s simulator; entries `{min, max, x}` at `0x1de5c50 + type × 48 + knob × 6`):

  | Encoder | Range |
  |---|---|
  | blue | 204..20480 |
  | green | 3072..17408 |
  | white | 1536..16896 |
  | orange | 0..32767 |
  | knobs 4..7 | 0..32767 (no encoder reaches them: shift + turn is a fine adjustment) |

  Clouds normalises each encoder from the table at run time.
- **Synth FX page:** the synth controller (vtable `0x1911994`, already hooked for Rings) handles it as sub-page 2 of `[0x1b4ee88]`. Its knob case is `0x1a50690`:
  - the type is `[0xff801734 + slot × 4]`;
  - the values are at `0xff801444 + slot × 16`;
  - they're saved by `0x19ff664(patch, values)`, the FX counterpart of `SetKnobs`. So Clouds' mode and page can live in `fx_params[4..7]`, set from the click hook.
- **Master FX page:** `MasterController`. Its primary vtable is `0x1910aac` (a second vptr at `+0xc` uses `0x1910a94`):
  - `+0x0c` destructor `0x19d99a0`;
  - `+0x14` key down `0x19db7c4` (handles `0x2d–0x2f`);
  - `+0x1c` shift + key down `0x19d987e`;
  - `+0x24` key up `0x19db708`;
  - `+0x2c` knob `0x19da2bc` (works on `0xff8016c4`).
  
  Still to find: which master subscreen is showing (`subscreen_ <= kCompressor` asserts; state near `0x1aad5d8`), and how the master FX params are saved.
- **Master FX page, found in the integration:** `subscreen_` is `[0x1aad5d8]` (the knob case's subscreen 2 copies the FX values `0xff8016c4`). Stock key down handles keys `0x05..0x14` and `0x2d..0x2f`: no encoder click. The master effect object is `[0x1aaad68]` (the producers per position: synth `0x1aaad60`, drum `0x1aaad64`, master `0x1aaad68`); phone's stereo object has vtable `0x1911314`.
- **Still to do:** the Clouds screen (the user's SVG; brief in `docs/clouds-screen-brief.md`); the master's save path, if Test 17 shows the settings don't persist.
