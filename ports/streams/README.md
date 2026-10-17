# Streams → OP-1 effect port

Mutable Instruments Streams (dual dynamics gate) as an OP-1 insert effect, host and simulator work only. Design, measurements and the OP-1 glue design: `docs/streams-port.md`.

Streams' audio path is analog (a 2164 VCA and a voltage-controlled filter); its firmware only computes two control signals per channel (gain and cutoff) from the EXCITE input. The port runs upstream's integer processors on the effect's own audio (self-sidechain) and applies their outputs to a digital VCA and a zero-delay-feedback SVF. One core serves mono (synth/drum effect) and stereo (master effect, linked detection).

## Layout

| Path | What |
|---|---|
| `src/dsp/` | Upstream processors copied from `mutableinstruments/streams` (namespace `op1streams`): envelope, vactrol, follower, compressor, filter controller, Lorenz, the follower's SVF, tables. Additions are marked: `retime.h` (sample-rate retiming, exact speed-ups), `resources_44k.cc` / `resources_22k.cc` (generated tables), `Processor::ProcessBlock` |
| `src/core/` | The effect core: detector, processor, VCA (2164 model), SVF (LP/BP), trim, dry/wet; `core_tables.cc` (SVF coefficients) |
| `op1/streams_fx.*` | OP-1 entry points (mono/stereo process, init), parameter encoding in `fx_params[4..7]`, per-position state |
| `harness/` | Test signals (integer, host = Blackfin), scenarios, float reference model, render/compare driver, `ctl_diff.py` |
| `tests/` | `copy_check.cc` (upstream vs copy), `bench.cc` (checksums and cycles), `sim_fx.cc` (entry points in the simulator) |
| `resources/` | `gen_tables.py` (upstream's rate-dependent tables for any rate; checked against upstream at 31089 Hz), `gen_core_tables.py` (SVF coefficients); upstream's `lookup_tables.py` and `waveforms.py` for reference |

## Prerequisites

`mutableinstruments/` (gitignored) at the repo root, with `streams/` and `stmlib/` (see `ports/rings/README.md`). Rings files used by relative path, unmodified: `src/fixed.h`, `harness/wav.h`, `harness/cycles.h`, `harness/compare.py`, `op1/link.ld`, `op1/support.cc`.

## Build (WSL, from this directory)

```
make tables       # table generator vs upstream (31089 Hz), regenerate 44.1/22.05 kHz tables
make copy-check   # upstream vs copy at 31089 Hz, per sample and in blocks: bit-identical
make ref          # reference: upstream processors at 31089 Hz + float VCA/SVF  -> out/ref/
make port         # fixed-point port, processors at 44.1 kHz                     -> out/port/
make compare      # SNR, level, VCA gain and cutoff tracking vs the reference
make port22 compare22   # the cheap profile (processors at 22.05 kHz)           -> out/port22/
make input        # the dry test inputs                                          -> out/input/
make spec         # phase-blind band spectra (Rings' compare.py; slow)
make bench-check  # host vs Blackfin checksums: unmodified processors, port, port22
make bench        # simulated cycles per 64-sample call (device ~1.6x)
make sim-fx       # the OP-1 entry points in bfin-elf-run vs host (PROFILE=22 for the cheap profile)
make op1-size     # freestanding link at 0x02c00000 (no package): sizes, no constructors
make test         # copy-check + bench-check + sim-fx
```

`bench-check` and `bench` take several minutes in the simulator (`BENCH_SECONDS`, default 1).

## Configurations (`src/dsp/retime.h`)

| Name | Flags | Use |
|---|---|---|
| unmodified | `OP1STREAMS_SAMPLE_RATE=31089 FAST=0 CENTROID_STRIDE=1` | upstream's code and tables: copy check, Blackfin bit-exactness |
| exact | `31089 FAST=1 STRIDE=1` | the exact speed-ups, proven bit-identical to upstream |
| port | `44100 FAST=1 STRIDE=8` | processors retimed to 44.1 kHz (default) |
| port22 | `22050 FAST=1 STRIDE=4` | processors on every other sample (about half the processor CPU) |

## License

Streams and stmlib are MIT-licensed, © Emilie Gillet (checked: every Streams source file carries the MIT notice; the task brief said GPLv3, which doesn't match the files). The notices are kept in each copied file. Our additions are under the same terms.
