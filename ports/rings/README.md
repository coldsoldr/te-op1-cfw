# Rings → OP-1 port

A fixed-point port of the Mutable Instruments Rings DSP for the OP-1's Blackfin BF524, which has no FPU. Design, measurements and status: `docs/rings-port.md`. Engine interface: `docs/engine-interface.md`.

## Layout

| Path | What |
|---|---|
| `src/` | Rings DSP, copied from `mutableinstruments/rings/dsp` (namespace `op1rings`), being converted to fixed point |
| `src/fixed.h` | Fixed-point primitives; host emulation is bit-exact with the Blackfin builtins |
| `src/mode_bank.h`, `src/mode_bank_bfin.S` | The modal resonator's per-sample loop, in C and in Blackfin assembly (bit-exact with each other) |
| `harness/` | Scenarios, the render driver (upstream reference or port), cycle counter, `compare.py` (stdlib only: SNR and spectral band difference) |
| `tests/` | Filter equivalence, fixed-point bit-exactness, op-cost and mode-bank benchmarks |

## Prerequisite: Mutable Instruments sources (not in git)

`mutableinstruments/` is gitignored. The reference build and the port's remaining stmlib includes need it at the repo root:

```
git clone https://github.com/pichenettes/eurorack.git mutableinstruments
git clone https://github.com/pichenettes/stmlib.git mutableinstruments/stmlib   # used: d18def8
```

The second clone fills the `stmlib` submodule; the first clone leaves that folder empty.

## Build (in WSL, from this directory)

The `bench` targets need `bfin-elf-g++` (`tools/build_bfin_gcc.sh`).

```
make ref                 # upstream float Rings, 48 kHz -> out/ref/*.wav
make port                # the port at 48 kHz (comparable with ref) -> out/port/*.wav
make port44              # the port at the OP-1 rate -> out/port44/*.wav
make compare             # waveform SNR, port vs ref
python3 harness/compare.py dirspec out/ref out/port   # spectral comparison
make bench-ref           # upstream float Rings on Blackfin (bfin-elf-run)
```

Mode-bank benchmark and bit-exactness check:

```
g++ -O2 -I. tests/bench_mode_bank.cc -o build/bench_mode_bank_host && build/bench_mode_bank_host
~/op1-toolchain/bfin/bin/bfin-elf-g++ -O2 -mcpu=bf524 -msim -I. tests/bench_mode_bank.cc src/mode_bank_bfin.S -o build/bench_mode_bank.elf
~/op1-toolchain/bfin/bin/bfin-elf-run build/bench_mode_bank.elf
```

All three builds must print the same checksum.

`OP1RINGS_RESONATOR` selects the resonator implementation:
- `0`: upstream TPT SVF, float.
- `1`: Chamberlin, float (the default).
- `2`: Chamberlin with the fixed-point mode bank.

## License

The Rings and stmlib code is MIT, © Emilie Gillet; its notices are kept in each file. Our changes are under the same terms.
