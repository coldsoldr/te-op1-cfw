# Marbles → OP-1 port

A fixed-point port of Mutable Instruments Marbles' generators (the T clock/rhythm generator, the X/Y random voltage generator, the deja vu machinery and the ramps) for the OP-1's Blackfin BF524, which has no FPU. It is meant to run as an OP-1 LFO (hosted on the "random" LFO), one Marbles sample per LFO call (689.0625 Hz).

- Design, verification, CPU cost, the OP-1 glue design and the integration plan: `docs/marbles-port.md`.
- The OP-1 LFO interface it plugs into: `docs/lfo-interface.md`.
- As a sequencer: `op1/marbles_seq.*` (the core, `docs/marbles-port.md` §9) and, since 2026-10-28, the kennel plugin `ports/knl/marbles/` for hundekraft (`docs/kennel-plugins.md` "Sequencer features"); the firmware glue `op1/marbles_op1.cc` stays as the lab.

## Layout

| Path | What |
|---|---|
| `upstream/` | Marbles' `random/`, `ramp/`, `resources.*` copied by `scripts/copy_upstream.py` (namespace `op1marbles`, include paths `upstream/...`; nothing else changed), plus the preset scales extracted from `settings.cc` |
| `src/` | The fixed-point port (namespace `op1marbles::fx`): `random_fx.h` (LCG, stream, deja vu sequence), `distributions_fx.h`, `ramps_fx.h`, `t_generator_fx.*`, `x_y_generator_fx.*`, `output_channel_fx.*` (with the lag processor), `quantizer_fx.*`, `fx.h` (integer helpers and float-rounding emulation), `tables_fx.*` (generated) |
| `harness/` | Scenarios (`scenarios.h`), drivers for the float code (`float_driver.h`) and the port (`fixed_driver.h`), the renderer (`render.cc`), cycle counter |
| `tests/` | `test_components.cc` (each part of the port against the float copy) and `compare.cc` (the whole generator, side by side, many seeds) |
| `scripts/` | `copy_upstream.py`, `gen_tables.py` (Q formats from upstream's float tables), `inspect_bin.py`, `compare_bin.py`, `check_bench.py`, `profile_trace.py` |

## Prerequisite

`mutableinstruments/` (gitignored) at the repo root, with `stmlib`, as for the Rings port (`ports/rings/README.md`). Only the reference, copy and test builds need it; the port in `src/` doesn't.

## Build (WSL, from this directory)

```
make copy-check   # upstream and the copy render byte-identical outputs; the copy is upstream + renames
make port         # the port -> out/port/*.bin
python3 scripts/compare_bin.py out/ref out/port   # sample by sample, port vs upstream
make test         # component tests (exact / close bounds)
make compare      # 928 side-by-side runs -> out/compare.txt
make bench        # Blackfin simulator: checksums must equal the host's; cycles per tick
make tables       # regenerate src/tables_fx.cc
```

`SUBSTEPS=4 make ...` runs 4 Marbles samples per OP-1 call (2756.25 Hz); `make clean` first.

Output files (`*.bin`): per Marbles sample, `uint8` gates (T1, T2, T3 in bits 0..2) then X1, X2, X3, Y as `float32` volts. `scripts/inspect_bin.py FILE --csv N` prints a summary and the first N records as CSV.

## License

The Marbles and stmlib code is MIT, © Emilie Gillet; its notices are kept in each copied file. The port is derived from it under the same terms. Upstream's `marbles/test/` is GPL and is not used.
