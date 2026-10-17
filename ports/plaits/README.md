# Plaits → OP-1 port

A fixed-point port of the Mutable Instruments Plaits DSP (the original 16 engines) for the OP-1's Blackfin BF524, which has no FPU, as a future engine on the MULTI slot (phase = 0, Rings = 1, Braids = 2, Plaits = 3). Design, measurements and status: `docs/plaits-port.md`. The Rings port (`ports/rings`, `docs/rings-port.md`) is the model; its `fixed.h`, `ffloat.h`, `compare.py`, `wav.h`, `cycles.h`, `op1/op1.h`, `op1/param_smoother.h` and `op1/link.ld` are used by relative path, unchanged.

Host and simulator work only: nothing here is linked into a device image yet.

## Layout

| Path | What |
|---|---|
| `src/` | Upstream `plaits/dsp` (without `engine2`) and `plaits/resources.*`, copied by `resources/copy_upstream.py` (namespace `op1plaits`, includes `src/…`). `voice.{h,cc}` edited by hand to drop the engine2 engines and the user-data flash. Float; the reference build's twin (`make copy-check`) |
| `fixed/` | The fixed-point port: `fixed_math.h` (formats, reciprocal, pitch, sine, BLEP, interpolator, RNG), `svf.h` (state-variable filter, one-pole, tan approximations), `dsp.h`, one `*_engine.{h,cc}` per engine, `oscillator.h` / `variable_shape_oscillator.h` / `chord_oscillators.h`, `voice.{h,cc}` (trigger, decay envelope, LPG, post-processing), `engines.{h,cc}` + `engine_list.h` (the dispatcher), `tables.{h,cc}` (generated) |
| `op1/` | The OP-1 glue: `plaits_op1.{h,cc}` (render / note-on entry points for the MULTI dispatcher, 44.1 kHz → 88.2 kHz upsampling, encoders, settings), `image_check.cc` (a freestanding image for size and initialiser checks) |
| `harness/` | `scenarios.h` (7 integer-parameter scenarios per engine), `render.cc` (engine level: upstream / copy / port), `render_voice.cc` (voice level), `report.py` (SNR vs float and double references, first 100 ms, band spectra), `segments.py` (SNR per segment), `ref64_prelude.h` (upstream built with `float` → `double`) |
| `tests/` | `bench_engines.cc` (Blackfin checksums and cycles), `test_fixed.cc` (primitives, inline assembly = reference, Blackfin = host), `test_oscillators.cc`, `test_filters.cc`, `probe_engine.cc` (one engine, constant parameters, sample dump), `sim_voice.cc` (the OP-1 glue in the simulator) |
| `resources/` | `copy_upstream.py`, `gen_tables.cc` (`make tables`: the fixed-point tables, and the speech word banks decoded by upstream's decoder) |

## Prerequisites

`mutableinstruments/` (gitignored) at the repo root, as for Rings (`ports/rings/README.md`): `plaits/` and `stmlib/`. WSL with g++ and the Blackfin toolchain in `~/op1-toolchain/bfin/bin`.

## Build (in WSL, from this directory)

```
make copy-check      # upstream and the copy render bit-identically (112 files)
make port            # the fixed-point port at 48 kHz, 12-sample blocks -> out/port
make compare         # SNR of out/port against out/ref (compare.py dirdiff)
make compare-spec    # band spectra (compare.py dirspec)
make ref64           # upstream with float -> double -> out/ref64 (E64=n for one engine)
python3 harness/report.py out [prefix]   # per scenario: SNR vs ref / first 100 ms / vs ref64, band diff
make voice-compare   # voice level (LPG, post-processing): the copy's Voice vs VoiceFixed
make bench [E=n]     # Blackfin at 44.1 kHz / 16-sample blocks: checksums = host, cycles per call
make test-fixed      # primitives on Blackfin = reference and = host
make test-oscillators test-filters   # fixed oscillators / filters vs stmlib float
make check-calls     # no soft-float and no divisions called from the port on Blackfin
make test-voice [E=n]   # the OP-1 glue in the simulator: checksums = host, pitch, cycles
make image           # the glue as a freestanding OP-1 image: sizes, no .init_array
make tables          # regenerate fixed/tables.{h,cc}
make probe; build/probe_engine ENGINE NOTE H T M [TRIG] [DUMP]
```

Every Blackfin target compares its checksums with a host build of the same code; the simulator doesn't fault on stray writes, so `render_port` was also run under AddressSanitizer (`setarch x86_64 -R` is needed for ASan under this WSL).

## License

The Plaits and stmlib code is MIT, © Emilie Gillet; its notices are kept in each copied file. Our changes and the fixed-point port are under the same terms.
