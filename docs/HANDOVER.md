# Handover: Rings on the OP-1 (custom firmware)

Last updated 2026-10-15. Read this first in a fresh session. It is self-contained; the docs it links to have the detail.

**Next task: port Rings' string model** (§3). It unlocks four of Rings' six models.

## 1. Goal and hard rules

Build custom firmware for the **original TE OP-1**: Analog Devices Blackfin ADSP-BF524C2, app firmware 246, bootloader TE-BOOT v2.30. The owner has **one unit**. **The bootloader must never be modified or rewritten.**

The project is **Mutable Instruments Rings as an OP-1 synth engine**. It takes over the phase engine's slot (engine type 7); a separate 16th engine may come later (§8).

Non-negotiable rules (detail in `docs/brick-safety.md`):
1. **Never put `te-boot.ldr` in a package.** Build with `tools/build_package.py`, which drops it.
2. **Every package must pass `tools/verify_bootloader.py`** before flashing. `build_package.py` runs it.
3. **Never use `FRC` in a package file name** (it forces a bootloader rewrite).
4. **Never put a `.KEY` file on the OP-1 disk** (irreversible OTP key programming).
5. **Install only through the te-boot menu:** hold COM at power-on, then "1. Upload firmware".
6. **Ask the user before anything touches the device.** The user flashes; we prepare and verify. Before asking, diff every package against stock (§7): only the intended files and words may differ.
7. **Recovery:** hold COM at power-on and flash stock `op1 firmware/op1_246.op1`. This works whatever state the app is in.

How the user works:
- They test on the device and report back in numbered points.
- They stay on branch `firmware-analysis-tooling`.
- Commit and push only when they ask.

## 2. Where things stand (2026-10-15)

**On the device and confirmed working: `op1 firmware/op1_246_rings_clicks1.op1`.**
- **Sound:** Rings' **modal** model and **FM voice** at the OP-1's real voice rate. The user says it sounds "amazing".
- **Controls:**
  - blue = structure, green = brightness, white = damping, orange = position;
  - knob values are smoothed over ~50 ms; **shift + turn** keeps the stock 1/16 fine adjustment;
  - **click blue** = next model (modal ↔ FM for now), **click green** = next polyphony (all → 1 → 2 → 4 → all), **shift + click** = step back.
- **Polyphony,** Rings style:
  - 1 / 2 / 4 voices with 60 / 28 / 12 modes;
  - a new note takes over the oldest voice's ringing resonator, so legato carries the ring;
  - a chord in polyphony 1 sounds only its last note;
  - "all" = 6 OP-1 voices × 16 modes.
- **Saving:** the model and polyphony are saved with the slot and in snapshots. Old presets load as modal / "all".
- **Screen:** the user's Rings screen (`incoming/rings.svg`) replaces the phase screen. Rings, sliders, position dot and pluck follow the encoders; the model and polyphony buttons light up. Rounded corners (`rx`) draw square: the OP-1 ignores `rx`.
- No stutters, glitches or UI sluggishness reported. Device tests 1–5 and their results are in `docs/device-tests.md`.

**Proven facts to rely on** (detail in `docs/device-tests.md`, `docs/engine-interface.md`):
- **Synth voices render at 88.2 kHz,** 128 samples per call (~689 calls/s per sounding voice). Pitch arrives as per-sample phase increments `inc = f / 88200 × 2^32`.
- **CPU clock ~400 MHz.** **Free CPU ~324 MHz** (81% of a block) with one stock phase voice playing. This was measured with no FX or other tracks.
- **Only sounding voices are rendered.**
- **Injected code at `0x02c00000` runs.** The app has no integrity check. Audio overload is survivable (the OP-1 recovers).

**Git:** branch `firmware-analysis-tooling`, remote `origin` (`<public repo URL>`).
- Last pushed commit: `d981a7a` (the Rings screen).
- The model/polyphony/clicks work and this handover were written after it; check `git status` and `git log` to see whether they've been committed.
- **Never commit TE firmware:** `op1 firmware/` and `mutableinstruments/` are gitignored.
- `incoming/` belongs to the user and stays out of git. It holds `rings.svg`, `rings-handover.md` and the probe recordings.

## 3. Next task: the string model

Rings' six models, in upstream order (`op1rings::ResonatorModel`; the screen's `model0`–`model5`):

| # | Model | Status |
|---|---|---|
| 0 | Modal resonator | done (`src/modal_resonator.*`) |
| 1 | Sympathetic strings | **needs the string model** |
| 2 | Modulated / inharmonic string | **needs the string model** |
| 3 | FM voice | done (`src/fm_voice_fixed.*`) |
| 4 | Quantized sympathetic strings | **needs the string model** (plus the chord table) |
| 5 | String + reverb | **needs the string model and the reverb** |

**Upstream facts** (`mutableinstruments/rings/dsp/`; the float copies are in `ports/rings/src/string.*`, `plucker.h`, `fx/reverb.h`, `part.cc`):
- **`String`:** a delay line (`kDelayLineSize` = 2048 floats) with interpolated reads, plus a stiffness/dispersion delay (1024). It also has FIR/IIR damping, a dispersion allpass, a pickup position and a DC blocker.
- **`Plucker`** (the internal exciter): a noise burst plus a comb. It is triggered on strum with `(frequency, filter_cutoff × 8, position)`.
- **`Part::RenderStringVoice`:**
  - dispersion comes from structure, with a dead zone at 0.24–0.26;
  - sympathetic models run `num_strings = 2 × kMaxPolyphony / polyphony` strings per voice (8 / 4 / 2 for polyphony 1 / 2 / 4), tuned by `ComputeSympatheticStringsNotes`;
  - string 0 is excited and the others resonate sympathetically, with modified brightness, damping and position and an LFO on position;
  - the gain is `1/√(2 × num_strings)`.
- **Quantized sympathetic** takes its chord from a table (`chords[polyphony][11][8]` in `part.cc`), indexed by a chord derived from structure. Look at how `cv_scaler.cc` computes `performance_state.chord`.
- **String + reverb:** damping is reshaped (`damping *= 2 − damping`). Then comes a stereo reverb (`fx/reverb.h`, upstream's 16-bit buffer) with amount, time and LP from damping and brightness, and an odd/even pan from position.
- **Limiter pre-gains** (`Part::model_gains_`): modal 1.4, sympathetic 1.0, string 1.4, FM 0.7, quantized 1.0, string + reverb 1.4. `MonoLimiter` takes them per model (`LimiterGains` in `src/op1_voice.cc`).

**Suggested approach** (the same method as the modal and FM ports, which worked well):
1. **Fixed-point `String` and `Plucker`** in new files, leaving the float copies for the harness. Keep the delay line in int32 or int16; there's plenty of SDRAM (the image region is 4 MB).
2. **A host test against upstream** (like `tests/test_fm_voice.cc`, which runs upstream at 48 kHz). Watch for stmlib approximations that the port must mirror to match bit-closely: the FM port needed `SemitonesToRatio`'s 1/256-semitone floor.
3. **Wire into `RingsVoice`:**
   - `set_model` gains MODEL_STRING, then the sympathetic models (several `String`s per voice);
   - add the models to `kModelCycle` in `op1/settings.h`, and they become clickable;
   - the screen already shows any model.
4. **CPU sizing** with `make sim-engine`. Sympathetic strings with 8 strings in polyphony 1, and "all" at 6 voices, need budgets.
   - Rings' maximum polyphony is 4, so define strings per voice for "all" (e.g. 1 or 2).
   - Remember the fast-notes worst case: the allocator can have one new voice plus two fading voices.
5. **The reverb** for model 5, mixed to mono.
6. **Package, diff against stock, and ask the user to flash** (§7).

## 4. Architecture of the OP-1 build (`ports/rings/`)

**Why fixed point:** the BF524 has no FPU. Float Rings would need ~16.5 GHz per modal voice, and libgcc soft-float costs ~330 cycles per operation. Everything at run time is fixed point; a few per-block calculations use `FFloat` (`src/ffloat.h`, a software float with a 32-bit mantissa).

| File | Role |
|---|---|
| `op1/engine.cc` | Render and note-on entry points (they replace the phase voice vtable's +0x14 and +0x1c). Rings runs at 44.1 kHz on 64 samples per call; a 4-point Lagrange `Upsampler` doubles it to 128 samples at 88.2 kHz. Holds per-voice `RingsVoice`, `ParamSmoother` and `Upsampler`, applies `VoiceAllocator` decisions, and does fade-outs. The image header (`OP1X`) points to the patch table |
| `op1/settings.h` | Model in `knobs[4]` and polyphony in `knobs[5]`, as `setting × 4096` (0 = modal / "all"); `kPolyVoices`/`kPolyModes`; `kModelCycle` (models reachable by clicks); `StepModel`/`StepPoly` |
| `op1/voice_allocator.h` | Rings polyphony on the OP-1's 6 voices. Steal the oldest (`copy_from`: the new voice takes over its resonator state), fade the stolen voice (512 samples, at most 2 at once), silence voices stolen before they sounded |
| `op1/param_smoother.h` | ~50 ms one-pole per voice and parameter; snaps within 2^-12 and at note-on |
| `op1/screen.cc`, `screen_math.h` | Rings screen: `Init` finds the SVG nodes and copies colours from their default state; `Update` sets them from the displayed slot's knobs each frame, then calls `Screen::Draw` |
| `op1/rings.svg`, `gen_rings_svg.py` | The package's SVG, generated from `incoming/rings.svg` (metadata dropped, 24 pluck variants, `…box`/`…num`/`…dots` ids) |
| `op1/input.cc` | Encoder clicks (key codes `0x3b` blue, `0x3c` green, confirmed). Synth controller key-down / shifted key-down / key-up hooks, active only on the engine page of a phase slot; they write knobs[4..5] plus `Patch::SetKnobs` |
| `op1/patches.cc` | The image's patch table: screen filename immediates, screen Init/Update, the 3 key hooks |
| `op1/op1.h` | Addresses and conventions, `Header` and `Patch` structs |
| `src/op1_voice.*` | `RingsVoice`: strum → excitation filter → `ModalResonator` (or `FmVoiceFixed`) → `MonoLimiter` (per-model gains). Also `set_model`, `set_resolution`, idle sleep |
| `src/modal_resonator.*`, `mode_bank.h`, `mode_bank_bfin.S` | Fixed-point modal resonator: a Chamberlin SVF bank with a closed-form state map on coefficient changes; the assembly inner loop runs at 23.7 cycles per mode per sample |
| `src/fm_voice_fixed.*` | Fixed-point FM voice (the follower is dropped: its input is silent with the internal exciter) |
| `src/fixed_tables.*`, `resources/gen_fixed_tables.py` | Generated tables (sin/cos/tan of partials, exp2, log2, stiffness, sine, FM ratio quantizer) |
| `tools/inject_app.py` | Adds the image's blocks before FINAL and patches the phase voice vtable from the header. It also applies the header's patch table (kinds: 32-bit word, imm16 lo/hi), checking each stock value |

**Hook addresses** (all plain SDRAM; detail in `docs/engine-interface.md` and `docs/screen-interface.md`):
- **Phase voice vtable** `0x191113c`: render `0x1911150` (stock `0x01a0bc5c`), note-on `0x1911158` (stock `0x01a0be82`). Note-on gets R1 = the slot's `int16 knobs[8]`.
- **Knobs:** `0xff8013c4 + slot×16`. Current slot `[0xff8016d4]`; engine type `[0xff8016e4 + slot×4]`; core slot `[0x01aaac0c]`; synth sub-page `[0x01b4ee88]` (0 = engine page); `Patch::SetKnobs` at `0x1a00d14` (patches at `0x1ddd7e8 + slot×0x38`).
- **Screen:** the PdScreen filename immediates at `0x1a0cd8e`/`0x1a0cd92`; vtable Init `0x1911190`, Update `0x1911188`.
- **Keys:** synth controller vtable `0x1911994`: key down `0x19119a8`, shifted `0x19119b0`, key up `0x19119b8`.

**CPU** (`make sim-engine`, Blackfin simulator, all 6 voices rendered every block; free ≈ 324 MHz):

| Case | Cost |
|---|---|
| "All", 6 voices ringing | ~164 MHz |
| "All", 3 knobs turning continuously | ~226 MHz |
| Polyphony 1, a note every 17 ms (peak) | ~273 MHz |
| Six-note chord in polyphony 1 | ~80 MHz |
| Polyphony 2 / polyphony 4, fast notes (peak) | ~205 / ~185 MHz |
| FM, 6 voices | ~106 MHz |

## 5. Environment

**Windows 11:**
- Use `.venv/Scripts/python` (Python 3.11; no numpy). Run tools from the repo root.
- `ffmpeg` is on PATH.

**WSL Ubuntu 20.04:**
- The toolchain is in `~/op1-toolchain/bfin/bin/`: binutils 2.43, gdb 15.2, GCC 14.2 C/C++ with newlib (`tools/build_bfin_toolchain.sh`, `tools/build_bfin_gcc.sh`).
- Run make from `ports/rings` like this: `wsl -e bash -lc 'cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/rings && make …'`.
- **Simulator:** `bfin-elf-run`, default environment only. L1 isn't mapped there, so simulator builds use `-DOP1_SIM`.

**Gotchas:**
- **Git Bash heredocs** passed to Python mangle `\\`, `\n` and escaped quotes inside string literals. Every time this session that cost a fix. Write scripts and code with the Write/Edit tools when they contain backslashes.
- **GCC 14's `__builtin_bfin_shl_fr1x32`** miscompiles constant shifts. `ShlSat` uses `SIGNBITS` instead.
- **Blackfin symbols get a leading underscore** (C `__image_end` is `___image_end` in linker scripts).
- **objdump's `/* R0=… */` annotations can be stale;** trust the `.L`/`.H` pairs.
- **The image has no startup code or run-time initialisers.** Constant tables of addresses must be link-time constants (e.g. `Patch.value` is `const void*`). Check that no `.init_array`/`.ctors` sections appear.
- **Calling firmware functions from our C++ works** (GCC's Blackfin ABI matches VisualDSP's for R0–R2 arguments and the R0 return).

**Reverse-engineering material** (outside the repo):
- `op1 firmware/analysis/` holds the ELF and disassembly of the app (`OP1_vdk.dis`, 11M lines; grep it).
- `%TEMP%\op1re\` holds the indexes `compact.txt`, `calls.txt`, `consts.txt`, `srctags.txt` and the helpers `op1lib.py`, `show.py START END` and `vt.py BASE N`.
- `%TEMP%\op1inject\` holds `check.py` (byte-diffs an injected LDR against stock) and `pkgdiff.py` (diffs packages).

## 6. Tests and make targets (WSL, from `ports/rings`)

| Target | What |
|---|---|
| `make op1-engine` | The engine image → `build/op1_engine.elf` |
| `make sim-engine` / `sim-engine-fm` | Entry points in the Blackfin simulator: CPU for every case above. Writes `build/sim_engine.raw` (s16le, 88.2 kHz). **"All" with modal must print checksum `a3ae5f5c`** (bit-exact since the limiter fix) |
| `make test-fm` | FM voice against upstream (42–78 dB SNR) |
| `make step-params` | Parameter-step clicks, upstream vs port; checks a sustained note and smoothed steps for discontinuities |
| `make test-alloc` | Polyphony allocator, settings encoding, click stepping |
| `make test-screen` | Screen geometry against the SVG spec |
| `make host-probe`, `make op1-probe` | The CPU probe (v3) and its host simulation |
| `make ref`, `port`, `compare` | Harness: upstream float Rings against the port's float `Part` (48 kHz) |

## 7. Package workflow

```
(WSL, ports/rings) make op1-engine
.venv/Scripts/python tools/inject_app.py "op1 firmware/op1_246/OP1_vdk.ldr" OUT.ldr ports/rings/build/op1_engine.elf
cp -r "op1 firmware/op1_246" "op1 firmware/work_x"; cp OUT.ldr "op1 firmware/work_x/OP1_vdk.ldr"
cp ports/rings/op1/rings.svg "op1 firmware/work_x/content/display/rings.svg"
.venv/Scripts/python tools/build_package.py "op1 firmware/work_x" "op1 firmware/op1_246_rings_<name>.op1"
```

**Verify before asking the user to flash:**
1. **Check the patches.** `inject_app.py` prints each vtable word and patch. Check every target against the image's symbols (`bfin-elf-nm build/op1_engine.elf`).
2. **Byte-diff the LDR** with `%TEMP%\op1inject\check.py`. Only these may differ: the FIRST header, the listed vtable/patch words, the two filename immediates, and the appended blocks; the stock tail must be intact.
3. **Diff the package** with `pkgdiff.py`. Only `OP1_vdk.ldr` may change, plus the added `content/display/rings.svg`; `te-boot.ldr` must be absent.
4. **Run `tools/verify_bootloader.py`** on the package: it must pass.

`build_package.py` refuses to overwrite an existing package. Use a new name, or delete our own stale build after looking at it.

## 8. Later and open items

- **A separate 16th engine** (instead of replacing phase). Do it after Rings is complete on the phase slot, as its own read-only investigation:
  - the factory's `type < 15` check and its jump table `0x18f5a04`;
  - `instr_types` names, the engine picker, per-type screens, `Ranges`, preset type names.
  - **Safety first:** a preset or saved slot with an unknown engine type must not crash stock firmware if the user ever goes back to it, possibly at every boot. User data lives on the disk and survives reflashing.
  - Cheaper option: "stock phase" as an extra model.
- **Free CPU under real load** (FX, other tracks) is unmeasured; the 324 MHz figure is with one voice and nothing else.
- **Green encoder range:** the phase engine's stock range caps green (brightness) at 29491 (~0.9). The screen shows the true value. It could be stretched to full range if the user wants.
- **FM costs ~17 MHz per voice,** more than expected; it could be optimised.
- **The limiter** updates its gain once per 32-sample sub-block (a small zipper at −66 to −80 dB); per-sample interpolation would remove it.
- **Other user wishes:**
  - CWO → COW in the effect menu (display-only; feasible with the patch table);
  - PORTAMENTO → PORTA was dropped: don't propose it;
  - COW → POLY is moot (builds from stock show POLY).

## 9. Docs and memory

| Doc | Content |
|---|---|
| `docs/rings-port.md` | Port design, validation, measurements |
| `docs/engine-interface.md` | Engine types, factory, voice vtables, render ABI, pitch |
| `docs/screen-interface.md` | Screen class, SVG API, frame loop, input path, presets; §6 the screen hook, §7 encoder clicks |
| `docs/device-tests.md` | Every device test (probes v1–v3, Tests 2–5) with procedures and results |
| `docs/memory-map.md` | App load map; the free region `0x02c00000–0x02ffffff` |
| `docs/brick-safety.md`, `docs/firmware-246-baseline.md`, `docs/app-boot-chain.md` | Safety and groundwork |
| `ports/rings/README.md` | Port layout and harness builds |

Persistent notes: `%USERPROFILE%\.claude\projects\I--GitHub-PersonalProjects-OP1-CustomFirmware\memory\`.
