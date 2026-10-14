# Handover: OP-1 custom firmware project

Last updated 2026-10-14 (late evening). Read this first in a fresh session, then the docs it links to.

## 1. Goal and hard rules

Build custom firmware for the **original TE OP-1** (Analog Devices Blackfin ADSP-BF524C2, firmware 246, bootloader TE-BOOT v2.30). The owner has **one unit**. **The bootloader must never be modified or rewritten.**

The current project is **Mutable Instruments Rings as an OP-1 synth engine**. It replaces the phase engine first; a separate 16th engine may come later.

Non-negotiable rules (detail in `docs/brick-safety.md`):
1. **Never put `te-boot.ldr` in a package.** Build with `tools/build_package.py`, which drops it.
2. **Every package must pass `tools/verify_bootloader.py`** before flashing. `build_package.py` runs it.
3. **Never use `FRC` in a package file name** (it forces a bootloader rewrite).
4. **Never put a `.KEY` file on the OP-1 disk** (irreversible OTP key programming).
5. **Install only through the te-boot menu:** hold COM at power-on, then "1. Upload firmware".
6. **Ask the user before anything touches the device.** The user flashes; we prepare and verify. Before asking, decode every package against stock: only the intended files may differ.
7. **Recovery:** hold COM at power-on and flash stock `op1_246.op1`. This works whatever state the app is in.

## 2. Status

**Proven on the device (2026-10-14):**
- **The safety path works.** Stock flashing and recovery through te-boot; the unit runs te-boot v2.30.
- **The app has no integrity check.** Byte patches work (the COW test; the label is back to POLY in later builds, which start from stock).
- **Injected code runs.** Probe builds placed code at `0x02c00000` (new LDR blocks inserted before FINAL) and hooked the phase engine's render through its vtable. The phase engine worked.
- **Overload is survivable.** The probe pushed the CPU into audio overload and the OP-1 recovered without crashing.
- **The free region looks safe.** No write was detected into `0x02c00000–0x02ffffff`; it was filled with a pattern and checked during the probe runs.

**Measured by probe v2** (recording `incoming/probe/probe2_report.ogg`, decoded with `tools/decode_probe_beeps.py`):
- **CPU clock: ~400 MHz** (399.6).
- **Synth voices render at 88.2 kHz.** Blocks are 128 samples, so each voice's render is called ~689 times per second. The probe's beep (a 32-sample square) came out at 2760 Hz, which means 88.3 kHz; probe v1 heard pitches and times doubled for the same reason.
  - Call intervals jitter: the shortest is 1.34 ms, the mean 1.45 ms.
  - **Not an artefact of the keyboard octave.** The recording was made with the keyboard an octave up, but the probe ignores pitch: the beep is a fixed waveform, and the call rate is timed by the cycle counter. Re-checked on 2026-10-14 (see `docs/device-tests.md`).
  - The stock phase engine steps its phase by `inc` per sample, one cycle per 2^32, so `inc = f / 88200 × 2^32`, and Rings follows the stock engine's pitch whatever the octave setting.
- **Only sounding voices are rendered.** With one key held, exactly one voice object was called, once per interval, with no other render calls.
- **Free CPU: not measured.** Probe v2 compared intervals against the *shortest* one, so normal jitter (~8%) looked like overload at once and it reported 0. Probe v3 fixes this.

**Built and verified, not flashed** (both pass `verify_bootloader.py`; each decodes to stock with only `OP1_vdk.ldr` changed):
- **`op1 firmware/op1_246_rings_probe3.op1`: probe v3.** It measures free CPU against the mean interval over 64-call windows and reports the held key's pitch. **Flash this next**; the procedure is in `docs/device-tests.md`.
- **`op1 firmware/op1_246_rings_engine.op1`: the Rings modal engine on the phase engine,** now at 88.2 kHz. Rings runs at 44.1 kHz on 64 samples per call, then a 2× upsampler fills the 128 output samples. Simulated: ~38 MHz with one voice ringing, ~121 MHz with six.

**Git:** branch `firmware-analysis-tooling` (the user wants to stay on it), pushed to `origin` (`<public repo URL>`). Latest commits:
- `2e0d097` probe v2
- `e20a050` injection pipeline
- `ae09351` fixed-point voice
- `4f2a796` port groundwork

**Never commit TE firmware.** `op1 firmware/` and `mutableinstruments/` are gitignored. `incoming/` belongs to the user; it holds `rings.svg`, `rings-handover.md` and the probe recordings.

## 3. Environment

**Windows 11:**
- Use `.venv/Scripts/python` (Python 3.11; `svg.path`, `sympy`; no numpy). Run tools from the repo root.
- `ffmpeg` is on PATH.

**WSL Ubuntu 20.04,** toolchain in `~/op1-toolchain/bfin/bin/`:
- binutils 2.43 and gdb 15.2 (`tools/build_bfin_toolchain.sh`);
- **GCC 14.2 C/C++ with newlib** (`tools/build_bfin_gcc.sh`, log `~/op1-gcc-build.log`).
- **Simulator:** `bfin-elf-run` in its *default* environment; `--environment operating` double-faults on newlib syscalls. L1 addresses aren't mapped there, so tests use `-DOP1_SIM`.
- `--trace-insn=on` plus `bfin-elf-addr2line` works as a profiler (see the trace tests).
- Windows paths are under `/mnt/i/...`.

**Gotchas:**
- **Git Bash heredocs** passed to Python mangle `\\` and `\n` inside string literals. Use the Edit tool for code containing backslashes, or write scripts to files.
- **GCC 14's `__builtin_bfin_shl_fr1x32`** miscompiles constant shifts (it emits a 16-bit shift). `ShlSat` uses `SIGNBITS` instead.
- **Blackfin symbols get a leading underscore:** C `__image_end` is `___image_end` in linker scripts.
- **objdump's `/* R0=0x... */` annotations can be stale.** Trust the `.L`/`.H` pairs.

## 4. Repo layout

| Path | What |
|---|---|
| `tools/ldr_dump.py` | Parse and rebuild Blackfin LDR files (`serialize()` recomputes checksums and the FIRST length) |
| `tools/patch_app.py` | Byte patches at runtime addresses, with expected-value checks |
| `tools/inject_app.py` | **Add an image linked at `0x02c00000` to `OP1_vdk.ldr`** (blocks before FINAL, `.bss` as FILL, optional `--fill-pattern`) and hook the phase engine's vtable from the image's `OP1X` header |
| `tools/build_package.py`, `tools/verify_bootloader.py` | Package without te-boot and the pre-flash gate |
| `tools/decode_probe_beeps.py`, `tools/decode_probe_tones.py` | Decoders for the probe recordings (v2 and v1) |
| `tools/build_bfin_toolchain.sh`, `tools/build_bfin_gcc.sh` | Toolchain builds (WSL) |
| `ports/rings/` | The Rings port. **Start with `ports/rings/README.md`** |
| `docs/rings-port.md` | Port design, measurements, remaining work |
| `docs/engine-interface.md` | Synth engine interface: factory, 6 voices, vtable, render ABI, pitch buffer, hook addresses |
| `docs/memory-map.md` | App load map, heaps, CPLBs, free regions (`0x02c00000–0x02ffffff` recommended) |
| `docs/device-tests.md` | Device test procedures and results (probe v1 and v2; the engine test) |
| `docs/firmware-246-baseline.md`, `docs/brick-safety.md`, `docs/app-boot-chain.md`, `docs/emulation-feasibility.md` | Earlier groundwork |

Gitignored files in `op1 firmware/`:
- `op1_246.op1` (stock, **the recovery file**), `op1_246/` (unpacked) and `op1_246-noboot.op1`;
- `work_*` directories and the built `op1_246_rings_*.op1` packages;
- `analysis/` (ELF and disassembly of the app and te-boot).

Throwaway analysis scripts live in `%TEMP%\op1re\` (`op1lib.py`, `show.py`, `vt.py`, the indexes), `%TEMP%\op1mem\` and `%TEMP%\op1inject\` (`check.py` byte-diffs an injected LDR against stock; `pkgdiff.py` diffs packages). They're not in the repo, and the method matters more than the code.

## 5. Rings port: where it stands

The full detail is in `docs/rings-port.md`.

**Why fixed point:** the BF524 has no FPU. Upstream float Rings needs about 16.5 GHz for one modal voice, and libgcc soft-float costs ~330 cycles per operation. The port is therefore **all fixed point**:
- **Modal resonator:**
  - upstream's TPT SVF bank becomes a Chamberlin SVF bank, mathematically equivalent;
  - a closed-form state map runs when coefficients change;
  - the inner loop is Blackfin assembly (`src/mode_bank_bfin.S`), 23.7 cycles per mode per sample, bit-exact with its C reference.
- **Coefficients:**
  - fixed point from generated tables (`resources/gen_fixed_tables.py`; upstream's `tan` approximation is built in);
  - FFloat (a software float with a 32-bit mantissa) only where a few operations are needed per block.
- **`RingsVoice`** (`src/op1_voice.*`) is one complete voice:
  - strum → excitation low-pass → resonator → limiter;
  - a mono path with a cheaper limiter for the OP-1;
  - idle-voice sleep and exciter sleep.
- **Validation:** the harness renders identical scenarios through upstream Rings and the port. Modal results are 44–66 dB SNR, with at most 0.05 dB mean spectral difference.
- **Not started:**
  - the string model;
  - sympathetic strings;
  - FM voice;
  - reverb;
  - the screen (the user's `incoming/rings.svg`).

**OP-1 integration:**
- **Images:** `ports/rings/op1/` (`engine.cc`, `probe.cc`, `link.ld`, `op1.h`), built with `make op1-engine` and `make op1-probe` in WSL from `ports/rings`.
- **Hook:** the phase engine's voice vtable at `0x191113c`:
  - `+0x14` render `0x1911150` (stock `0x01a0bc5c`);
  - `+0x1c` note-on `0x1911158` (stock `0x01a0be82`);
  - `+0x2c` note-off `0x1911168`.
- **Render ABI:** `render(this = R0, int16 out[128] = R1, int16 params[4] = R2)`.
  - The voice index is `(this - 0xffb00000) / 28`.
  - Pitch is per-sample phase increments at `0xff801950`, scaled for **88.2 kHz**: `inc = f / 88200 * 2^32`.
- **Rate:** Rings runs at 44.1 kHz on 64 samples per call; `Upsampler` in `op1/engine.cc` (4-point Lagrange) doubles it to the 128 samples.
- **Simulated cost** (`make sim-engine`, 10 modes per voice, at the real call rate): about 38 MHz with one voice ringing, 121 MHz with six.
- **Off-device checks:**
  - `make sim-engine` measures the CPU cost and writes `build/sim_engine.raw` (88.2 kHz, s16le) for pitch checks;
  - `make host-probe HOST_PROBE_ARGS="MHZ FREE% CALLS STRAY"` makes a WAV for testing the decoder.

## 6. Open questions

- **Free CPU** while the OP-1 plays. Probe v3 is built to measure it (see §7).
- **Encoder mapping:** which of `params[0..3]` is which encoder (blue, green, white, orange), and their range (assumed 0..32767).
- **Envelope and output:** whether the OP-1's ADSR and FX come after render (inferred), and the expected output level.
- **88.2 kHz:** is the rate fixed, or does it depend on settings? Does the core decimate the voice output to 44.1 kHz?
- **Screen:** the phase engine screen class looks up its own SVG element IDs (e.g. "blue"), so swapping `pd.svg` for `rings.svg` alone could break it. It needs reverse engineering.

## 7. Next steps (agreed direction: replace the phase engine with Rings; ask before every flash)

1. ~~Adapt the engine to 88.2 kHz.~~ Done (2026-10-14): Rings at 44.1 kHz plus a 2× upsampler, with the packages rebuilt.
2. **Flash probe v3** (`op1_246_rings_probe3.op1`), record, and decode. The procedure is in `docs/device-tests.md`.
   - Set the keyboard octave to 0 and hold a key the user can name.
   - Use the free-CPU result to size modes per voice (`OP1RINGS_MODES_PER_VOICE`).
   - Not done: rendering a stock voice alongside it. Ask the user to watch the screen for sluggishness during the ramp.
3. **First Rings flash.** Check the sound and its pitch against the stock engine, the encoder mapping (`kParamOrder` in `op1/engine.cc`), the level and stutters.
4. **Then:**
   - the string model, then sympathetic strings and the other Rings models;
   - the Rings screen (reverse-engineer the Pd screen class; the user's SVG and its handover are in `incoming/`);
   - possibly a separate 16th engine (needs the engine-list limit, the menu, the screen and preset handling; see the notes in `docs/engine-interface.md`).

**Other user wishes, still open:**
- **Revert COW → POLY.** Any build from stock already shows POLY.
- **CWO → COW in the effect menu,** as a display-only hook. Now feasible with the injection pipeline.
- **PORTAMENTO → PORTA was dropped.** Don't propose it.

## 8. Workflow

**Byte patch:**
```
cp -r "op1 firmware/op1_246" "op1 firmware/<work>"
.venv/Scripts/python tools/patch_app.py "op1 firmware/<work>/OP1_vdk.ldr" "op1 firmware/<work>/OP1_vdk.ldr" ADDR:OLD:NEW
.venv/Scripts/python tools/build_package.py "op1 firmware/<work>" "op1 firmware/op1_246_<name>.op1"
```

**Injected code:**
```
(WSL, ports/rings) make op1-engine
.venv/Scripts/python tools/inject_app.py "op1 firmware/op1_246/OP1_vdk.ldr" OUT.ldr ports/rings/build/op1_engine.elf
cp -r "op1 firmware/op1_246" "op1 firmware/work_x"; cp OUT.ldr "op1 firmware/work_x/OP1_vdk.ldr"
.venv/Scripts/python tools/build_package.py "op1 firmware/work_x" "op1 firmware/op1_246_<name>.op1"
```

Then byte-diff the LDR against stock (only the FIRST header, the vtable words and the appended blocks may differ) and decode-diff the package (only `OP1_vdk.ldr` may differ). Show the user, and let them flash through te-boot.

## 9. Memory

Persistent notes are in `%USERPROFILE%\.claude\projects\I--GitHub-PersonalProjects-OP1-CustomFirmware\memory\`.
