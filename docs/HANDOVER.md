# Handover: Rings on the OP-1 (custom firmware)

Last updated 2026-10-15 (end of session). Read this first in a fresh session. It is self-contained; the docs it links to have the detail.

**Next task: the user flashes `op1_246_rings_strings6.op1` (model 6's shared reverb, §3, plus strings5's green handle) and reports Test 11 (`docs/device-tests.md`).** Then pick from §9 with the user.

## 1. Goal and hard rules

Build custom firmware for the **original TE OP-1**: Analog Devices Blackfin ADSP-BF524C2, app firmware 246, bootloader TE-BOOT v2.30. The owner has **one unit**. **The bootloader must never be modified or rewritten.**

The project is **Mutable Instruments Rings as an OP-1 synth engine**. It takes over the phase engine's slot (engine type 7); a separate 16th engine may come later (§9).

Non-negotiable rules (detail in `docs/brick-safety.md`):
1. **Never put `te-boot.ldr` in a package.** Build with `tools/build_package.py`, which drops it.
2. **Every package must pass `tools/verify_bootloader.py`** before flashing. `build_package.py` runs it.
3. **Never use `FRC` in a package file name** (it forces a bootloader rewrite).
4. **Never put a `.KEY` file on the OP-1 disk** (irreversible OTP key programming).
5. **Install only through the te-boot menu:** hold COM at power-on, then "1. Upload firmware".
6. **Ask the user before anything touches the device.** The user flashes; we prepare and verify. Before asking, diff every package against stock (§8): only the intended files and words may differ.
7. **Recovery:** hold COM at power-on and flash stock `op1 firmware/op1_246.op1`. This works whatever state the app is in.

How the user works:
- They test on the device and report back in numbered points.
- They stay on branch `firmware-analysis-tooling`.
- Commit and push only when they ask. (Nothing from the string-model work is committed yet: see §2, Git.)

## 2. Where things stand (2026-10-15)

**On the device: `op1 firmware/op1_246_rings_strings4.op1`** (Test 9), or strings5 (Test 10: green handle range confirmed good). **Ready to flash: `op1_246_rings_strings6.op1`** (Test 11: shared reverb + green handle; verified per §8).
- **Models 1–5 work** (screen buttons 1–5: modal, sympathetic strings, string, FM, quantized). **Sympathetic strings "sound great, no more glitches"** (they glitched in strings1/2 from CPU overload).
- **Model 6 (string + reverb):** the strings3 hang is fixed in strings4. It "sounds great" but **glitches beyond polyphony 1** (meter 4 rings with 4 notes): one reverb per voice. strings6 shares one reverb among the voices (§3); not yet tried on the device.
- The string model at structure extremes: no glitches. Models 1–5 "all perfect".

**Controls** (all on the synth engine page of a Rings/phase slot):
- encoders: blue = structure, green = brightness, white = damping, orange = position; smoothed ~50 ms; **shift + turn** = stock 1/16 fine adjustment;
- **click blue** = next model (6 models, Rings' order), **click green** = next polyphony (all → 1 → 2 → 4 → all), **shift + click** = back;
- **click white** = toggle the **CPU meter** on the six rings (ring *i* lit at (*i*+1) × 15% of a block; engine only; `op1/cpu_meter.h`). Orange click unused.
- Model and polyphony are saved per slot and in snapshots (`knobs[4]`, `knobs[5]`); old presets load as modal / "all".

**Decided with the user — don't re-ask:**
- sympathetic strings are tuned around the played note (Rings with its FREQUENCY knob);
- the string models' mono mix is upstream's per-voice `out − aux` (modal keeps `out + aux`);
- model 6 plays at most 4 voices in "all" (decided with one reverb per voice; with the shared reverb 6 would fit, ask before changing it);
- the brightness handle spans the green encoder's range, 0..29491 (display only);
- the modal and FM models sound "amazing": their output must stay bit-identical (checksums in §7).

**Proven facts** (detail in `docs/device-tests.md`, `docs/engine-interface.md`):
- **Voices render at 88.2 kHz,** 128 samples per call (~689 calls/s per sounding voice); pitch arrives as per-sample increments `inc = f / 88200 × 2^32`. Rings runs at 44.1 kHz, 64 samples per call, upsampled 2×.
- **CPU ~400 MHz; ~324 MHz free** with one stock voice. **The device runs ~1.6× the simulator's figures** (Test 7: the modal model reads 4 rings, 60–74%, at ~41% simulated, and plays cleanly). Keep simulated totals well under ~200 MHz.
- **Only sounding voices are rendered.** Injected code at `0x02c00000` runs; the app has no integrity check. Audio overload is survivable; a memory-corrupting bug is not (power cycle needed, no lasting harm so far).

**Package history** (all in `op1 firmware/`, results in `docs/device-tests.md`): clicks1 (modal + FM, screen, clicks; liked) → strings1 (string models; sympathetic crackled) → strings2 (model 6, CPU meter; still glitching, Test 7 calibration) → strings3 (hot loops in assembly; models 1–5 clean, model 6 hangs) → strings4 (the reverb's write pointer wrapped; no hang, model 6 glitches with polyphony > 1) → strings5 (green handle spans the encoder's range; confirmed) → strings6 (model 6's reverb shared by the voices; awaiting Test 11).

**Git:** branch `firmware-analysis-tooling`, remote `origin` (`<public repo URL>`).
- Last commit (pushed): `beefe13` (model/polyphony on clicks). **Everything since — string models, reverb, assembly kernels, CPU meter, docs — is uncommitted.** Run `git status`; commit only when the user asks.
- **Never commit TE firmware:** `op1 firmware/` and `mutableinstruments/` are gitignored. `ports/rings/build/` is gitignored (it holds this session's edit scripts, `edit_*.py`, which can be deleted).
- `incoming/` belongs to the user and stays out of git.

## 3. Model 6: the hang (strings4) and the shared reverb (strings6)

**The hang (fixed in strings4, confirmed on the device, Test 9):** `ReverbFixed::ProcessAsm` passed `buffer_ + write_ptr_` to the kernel, which moves its pointer down *before* each sample, and didn't wrap: from `write_ptr_ == 0` it wrote below the buffer, over the reverb's own state and then random RAM. Now `start = write_ptr_ == 0 ? kSize : write_ptr_`. `make bench-string` runs the reverb over ~3.9 wraps inside guard words and compares with a host build (C loop); it failed at the first wrap before the fix.

**The glitches (Test 9: beyond polyphony 1, 4 rings):** each voice ran its own reverb (~12 MHz simulated each, plus a cache-heavy 109 KB buffer). **strings6 runs one reverb for every voice**, as upstream (`SharedReverb` in `src/op1_voice.*`; `op1/reverb_bus.h`; design in `docs/rings-port.md`, "String + reverb"):
- each carrier (a model 6 voice not fading after a steal) plays `dry (1 − amount)`, sends its dry signal, and mixes its share of `amount × wet` in before its limiter;
- a block ends when a voice renders a second time; the reverb runs once on the block's send; the next block's carriers share the wet equally (one block, 1.45 ms, late);
- a new carrier waits a block then ramps in (the shares always sum to 1); a steal hands the share on (`ReverbBus::Transfer`, from `NoteOn`); 50 ms idle restarts it empty;
- a sleeping voice (its string silent) still plays its share through its limiter.
- **Tests:** `make test-reverb-bus` (host, 16 checks against a second reverb fed the same sends; without `Transfer` the steal check fails). `sim-model M=5` peaks are back to strings5's (an earlier draft limited the wet separately and clipped at 32767).
- **CPU (simulated):** "all" ~73 MHz (from ~118), polyphony 4 fast notes ~123 MHz peak (from ~192).

**When the user reports Test 11:** record it. If model 6 still glitches, look at the device/simulator ratio for this model (the reverb's buffer is SDRAM-heavy: ~20 streams per sample), and at the remaining C hot spots (§9). If the sound changed for the worse, the likely causes are the one-block wet latency or the equal shares through different envelopes.

**Review the other new assembly for the same class of bug** (pointer state that the C wraps but the assembly path doesn't): `string_kernel_bfin.S` masks its write pointer itself (and has bit-exact checks over wraps of its 2048-sample line, which `bench-string` does cover); `limiter_bfin.S` has no ring buffer. The reverb was the only one with C-side pointer bookkeeping.

## 4. How the string models and the reverb work

Rings' six models, in upstream order (`op1rings::ResonatorModel`; the screen's `model0`–`model5`, buttons 1–6):

| # | Model | Code |
|---|---|---|
| 0 | Modal resonator | `src/modal_resonator.*`, `mode_bank_bfin.S` |
| 1 | Sympathetic strings | `src/string_voice.*` |
| 2 | Modulated / inharmonic string | `src/string_fixed.*` |
| 3 | FM voice | `src/fm_voice_fixed.*` |
| 4 | Quantized sympathetic strings | `src/string_voice.*` (Bryan Noll's chord table, as upstream) |
| 5 | String + reverb | `src/string_voice.*` + `src/reverb_fixed.*` |

Detail in `docs/rings-port.md` ("String models", "Assembly round", "String + reverb", "CPU meter"). In short:
- **`StringFixed`:** upstream's string loop in fixed point (Q25 signals clamped to ±7, Q19 delays); the damping SVF as two one-poles (exact at resonance 0.5); loop/pickup delays and FIR taps interpolated as products; the first block after `Init` starts settled; `SemitonesToRatio`'s floor and float rounding mirrored; noise from upstream's LCG in upstream's order. Hot loops in `src/string_kernel.h` (C reference) and `string_kernel_bfin.S`: the plain loop (every sympathetic string) and a stretch-only loop (stiff string, structure 0.24–0.81). Outside that range the general C loop runs.
- **`StringVoice`:** `Part::RenderStringVoice` for one voice, rendered per 64-sample block. Steals hand state over (the engine swaps `RingsVoice*`), and the fading voice copies only recent history. The plucker sleeps when quiet. Glides end at 0.1 cent. Brightness/damping updates are capped at 2 sympathetic strings per block.
- **`ReverbFixed`:** upstream's Griesinger reverb, mono (`dry + amount (wet_1 + wet_2 − dry)`), **one shared by every voice** (`SharedReverb`, `op1/reverb_bus.h`, §3). The buffer is indexed without wrapping (109 KB; writes within 6,312 samples of the wrap go to a second place). `Clear()` is O(1) (reads older than the samples written since return 0). The settled case runs in `reverb_bfin.S` (the C splits runs at LFO steps and the wrap zone); the C loop is the reference and runs for 6,312 samples after a Clear.
- **`MonoLimiter`:** the loop in `limiter_bfin.S`.
- **Every assembly routine is bit-exact with its C reference;** the checks are in `bench-string` and `bench-models` (§7).

**CPU** (simulator, strings3, model 6 from strings6; the device runs ~1.6× these):

| Case | Cost |
|---|---|
| Modal, "all", 6 voices / 3 knobs turning | ~153 / ~210 MHz |
| Modal, polyphony 1, a note every 17 ms (peak) | ~273 MHz (older figure; clean on the device) |
| FM, 6 voices | ~106 MHz |
| String (model 2), "all", 6 voices | ~85 MHz |
| Sympathetic or quantized, "all", 6 voices / knobs turning | ~104 / ~114 MHz |
| Sympathetic, polyphony 1, fast notes (peak) | ~173 MHz |
| String + reverb, "all" (4 voices) / knobs turning | ~73 / ~77 MHz (strings6; ~118 / ~124 with a reverb per voice) |
| String + reverb, polyphony 4, fast notes (peak) | ~123 MHz (strings6; ~192 before) |

Per voice and per part: `make bench-models`.

## 5. Architecture of the OP-1 build (`ports/rings/`)

**Why fixed point:** the BF524 has no FPU. Float Rings would need ~16.5 GHz per modal voice, and libgcc soft-float costs ~330 cycles per operation. Everything at run time is fixed point; a few per-block calculations use `FFloat` (`src/ffloat.h`, a software float with a 32-bit mantissa).

| File | Role |
|---|---|
| `op1/engine.cc` | Render and note-on entry points (they replace the phase voice vtable's +0x14 and +0x1c). Rings runs at 44.1 kHz on 64 samples per call; a 4-point Lagrange `Upsampler` doubles it to 128 samples at 88.2 kHz. Holds six `RingsVoice` states behind `RingsVoice* voices[6]` (swapped on a steal), per-voice `ParamSmoother` and `Upsampler`, model 6's `ReverbBus`, the CPU meter, applies `VoiceAllocator` decisions, and does fade-outs. The image header (`OP1X`) points to the patch table |
| `op1/settings.h` | Model in `knobs[4]` and polyphony in `knobs[5]`, as `setting × 4096` (0 = modal / "all"); `kPolyVoices`/`kPolyModes`/`kRingsPolyphony`; `kModelCycle` (models reachable by clicks); `StepModel`/`StepPoly` |
| `op1/reverb_bus.h` | Model 6's shared reverb on the OP-1: ends a block when a voice renders twice, runs `SharedReverb` once per block, shares the wet signal among the carriers (`Transfer` on steals). Host-tested (`make test-reverb-bus`) |
| `op1/voice_allocator.h` | Rings polyphony on the OP-1's 6 voices. Steal the oldest (`copy_from`: the new voice takes over its resonator state), fade the stolen voice (512 samples, at most 2 at once), silence voices stolen before they sounded |
| `op1/param_smoother.h` | ~50 ms one-pole per voice and parameter; snaps within 2^-12 and at note-on |
| `op1/screen.cc`, `screen_math.h` | Rings screen: `Init` finds the SVG nodes and copies colours from their default state; `Update` sets them from the displayed slot's knobs each frame, then calls `Screen::Draw` |
| `op1/rings.svg`, `gen_rings_svg.py` | The package's SVG, generated from `incoming/rings.svg` (metadata dropped, 24 pluck variants, `…box`/`…num`/`…dots` ids) |
| `op1/input.cc` | Encoder clicks (key codes `0x3b` blue, `0x3c` green, `0x3d` white, all confirmed). Synth controller key-down / shifted key-down / key-up hooks, active only on the engine page of a phase slot; they write knobs[4..5] plus `Patch::SetKnobs` |
| `op1/patches.cc` | The image's patch table: screen filename immediates, screen Init/Update, the 3 key hooks |
| `op1/op1.h` | Addresses and conventions, `Header` and `Patch` structs |
| `src/op1_voice.*` | `RingsVoice`: strum → excitation filter → `ModalResonator` (or `FmVoiceFixed`, or `StringVoice`) → `MonoLimiter` (per-model gains). Also `set_model`, `set_resolution`, `set_polyphony`, `CopyFrom` (copies only the strings in use), idle sleep. Model 6 plays `dry (1 − amount)`, sends to and mixes its share of `SharedReverb` (also here) before its limiter |
| `src/string_fixed.*` | Fixed-point `StringFixed` (one string: Hermite delay line, dispersion, FIR/IIR damping, pickup), `PluckerFixed`, `DcBlockerFixed`, `RandomFixed` (upstream's LCG) |
| `src/string_kernel.h`, `string_kernel_bfin.S` | The plain and stretch-only string loops in C and Blackfin assembly, bit-exact |
| `src/limiter_bfin.S`, `src/reverb_bfin.S` | The limiter's and the reverb's loops in assembly, bit-exact with their C |
| `src/reverb_fixed.*` | Rings' reverb, mono, unwrapped 16-bit buffer (109 KB), O(1) clear; `ProcessAsm` drives `reverb_bfin.S` (wraps the write pointer before each run, §3). One instance, in `SharedReverb` (`src/op1_voice.*`) |
| `op1/cpu_meter.h` | Engine CPU share for device tests, shown on the rings by a white click |
| `src/string_voice.*` | `StringVoice`: the string models for one voice (plucker, string 0, sympathetic strings, tuning, chords, LFOs) |
| `src/voice_patch.h` | `VoicePatch` and `ResonatorModel` |
| `src/modal_resonator.*`, `mode_bank.h`, `mode_bank_bfin.S` | Fixed-point modal resonator: a Chamberlin SVF bank with a closed-form state map on coefficient changes; the assembly inner loop runs at 23.7 cycles per mode per sample |
| `src/fm_voice_fixed.*` | Fixed-point FM voice (the follower is dropped: its input is silent with the internal exciter) |
| `src/fixed_tables.*`, `resources/gen_fixed_tables.py` | Generated tables (sin/cos/tan of partials, exp2, log2, stiffness, sine, FM ratio quantizer, the string's low-pass `g/(1+g)`, `svf_shift`) |
| `tools/inject_app.py` | Adds the image's blocks before FINAL and patches the phase voice vtable from the header. It also applies the header's patch table (kinds: 32-bit word, imm16 lo/hi), checking each stock value |

**Hook addresses** (all plain SDRAM; detail in `docs/engine-interface.md` and `docs/screen-interface.md`):
- **Phase voice vtable** `0x191113c`: render `0x1911150` (stock `0x01a0bc5c`), note-on `0x1911158` (stock `0x01a0be82`). Note-on gets R1 = the slot's `int16 knobs[8]`.
- **Knobs:** `0xff8013c4 + slot×16`. Current slot `[0xff8016d4]`; engine type `[0xff8016e4 + slot×4]`; core slot `[0x01aaac0c]`; synth sub-page `[0x01b4ee88]` (0 = engine page); `Patch::SetKnobs` at `0x1a00d14` (patches at `0x1ddd7e8 + slot×0x38`).
- **Screen:** the PdScreen filename immediates at `0x1a0cd8e`/`0x1a0cd92`; vtable Init `0x1911190`, Update `0x1911188`.
- **Keys:** synth controller vtable `0x1911994`: key down `0x19119a8`, shifted `0x19119b0`, key up `0x19119b8`.

## 6. Environment

**Windows 11:**
- Use `.venv/Scripts/python` (Python 3.11; no numpy). Run tools from the repo root.
- `ffmpeg` is on PATH.

**WSL Ubuntu 20.04:**
- The toolchain is in `~/op1-toolchain/bfin/bin/`: binutils 2.43, gdb 15.2, GCC 14.2 C/C++ with newlib (`tools/build_bfin_toolchain.sh`, `tools/build_bfin_gcc.sh`).
- Run make from `ports/rings` like this: `wsl -e bash -lc 'cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/rings && make …'`.
- **Simulator:** `bfin-elf-run`, default environment only. L1 isn't mapped there, so simulator builds use `-DOP1_SIM`.

**Gotchas:**
- **Git Bash heredocs** passed to Python mangle `\\`, `\n` and escaped quotes inside string literals (it happened again several times this session). Write scripts and code with the Write/Edit tools when they contain backslashes; heredocs are fine only without them.
- **GCC 14's `__builtin_bfin_shl_fr1x32`** miscompiles constant shifts. `ShlSat` uses inline `<< n (S)` for constant shifts on Blackfin and `SIGNBITS` otherwise.
- **GCC 14 on Blackfin turns selects and saturating shifts into taken branches** (~5 cycles each), and spills when a loop has many live values. Check hot loops with `bfin-elf-objdump -d -C`; `make bench-models` shows where a voice's cycles go. The hot loops are now assembly with C references.
- **The simulator doesn't fault on stray writes** and doesn't model the cache: a memory bug can pass every simulator test (the model 6 hang), and the device runs ~1.6× its cycle counts.
- **Assembly conventions here:** `MUL(d, a, b)` is the 4-instruction `MulQ31Fast`; loads/stores with offsets up to 60 bytes from a P register (and I-register accesses) can ride in a multiply's parallel slots (`|| … || NOP`); keep `L` registers at 0 (no circular addressing: interrupts may assume it).
- **Blackfin symbols get a leading underscore** (C `__image_end` is `___image_end` in linker scripts).
- **objdump's `/* R0=… */` annotations can be stale;** trust the `.L`/`.H` pairs.
- **The image has no startup code or run-time initialisers.** Constant tables of addresses must be link-time constants (e.g. `Patch.value` is `const void*`). Check that no `.init_array`/`.ctors` sections appear.
- **Calling firmware functions from our C++ works** (GCC's Blackfin ABI matches VisualDSP's for R0–R2 arguments and the R0 return).

**Reverse-engineering material** (outside the repo):
- `op1 firmware/analysis/` holds the ELF and disassembly of the app (`OP1_vdk.dis`, 11M lines; grep it).
- `%TEMP%\op1re\` holds the indexes `compact.txt`, `calls.txt`, `consts.txt`, `srctags.txt` and the helpers `op1lib.py`, `show.py START END` and `vt.py BASE N`.
- `%TEMP%\op1inject\` holds `check.py` (byte-diffs an injected LDR against stock) and `pkgdiff.py` (diffs packages).

## 7. Tests and make targets (WSL, from `ports/rings`)

| Target | What |
|---|---|
| `make op1-engine` | The engine image → `build/op1_engine.elf` |
| `make sim-engine` / `sim-engine-fm` | Entry points in the Blackfin simulator: CPU per case, note-on included. Writes `build/sim_engine.raw` (s16le, 88.2 kHz). **Modal must print checksum `a3ae5f5c`, FM `567afc0c`** (the liked sound; bit-exact through every change since) |
| `make test-fm` | FM voice against upstream (42–78 dB SNR) |
| `make test-string` | String models against upstream's float `String`/`Plucker` (19 scenarios, 46–76 dB SNR); `test_string N` = SNR over time, `test_string tune N` = string tunings |
| `make test-reverb` | Reverb against upstream's float `Reverb` |
| `make test-reverb-bus` | Model 6's shared reverb (`op1/reverb_bus.h`) on the host: one run per block in any render order, the carriers' sum against a reference reverb through joins, leaves and steals, no runs without carriers, restart after a gap |
| `make bench-string` | Cycles of the string, plucker and reverb on Blackfin; both string kernels C vs assembly (must print "identical", checksums `aa65f279`, `23c9c099`); the reverb over ~3.9 wraps with guard words; the target builds `build/bench_string_host` (C loop) and fails unless both print the same `reverb:` line (`128253 samples, guards intact, checksum c36f2ef8`) |
| `make bench-models` | Cycles per voice and model, and of their parts; the limiter C vs assembly (`092ec491`) |
| `make sim-model M=n` | `sim-engine` with model n (e.g. 1, 2, 4) |
| `make step-params` | Parameter-step clicks, upstream vs port; checks a sustained note and smoothed steps for discontinuities |
| `make test-alloc` | Polyphony allocator, settings encoding, click stepping |
| `make test-screen` | Screen geometry against the SVG spec |
| `make host-probe`, `make op1-probe` | The CPU probe (v3) and its host simulation |
| `make ref`, `port`, `compare` | Harness: upstream float Rings against the port's float `Part` (48 kHz) |

## 8. Package workflow

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

Also confirm the image ends below `0x03000000` (inject_app prints the FILL block: start + length; strings6 ends at `0x02ca30cc`), and that `bfin-elf-nm` shows no new soft-float (`__adddf3`, `__subdf3`, `__fixdfsi` come only from `FmVoiceFixed::Init`'s run-time `FFloat(220.0 / fs)`, harmless) and no `.init_array`.

## 9. Later and open items

- **A separate 16th engine** (instead of replacing phase). Do it after Rings is complete on the phase slot, as its own read-only investigation:
  - the factory's `type < 15` check and its jump table `0x18f5a04`;
  - `instr_types` names, the engine picker, per-type screens, `Ranges`, preset type names.
  - **Safety first:** a preset or saved slot with an unknown engine type must not crash stock firmware if the user ever goes back to it, possibly at every boot. User data lives on the disk and survives reflashing.
  - Cheaper option: "stock phase" as an extra model.
- **Free CPU under real load** (FX, other tracks) is unmeasured; the 324 MHz figure is with one voice and nothing else.
- **Green encoder range:** the phase engine's stock range caps green (brightness) at 29491 (~0.9). Since strings5 the screen's handle spans 0..29491 (display only, as the user asked); the engine still reads at most ~0.9. Stretching the sound's range too would need the Ranges table or a scale in the engine: ask first.
- **FM costs ~17 MHz per voice,** more than expected; it could be optimised.
- **The limiter** updates its gain once per 32-sample sub-block (a small zipper at −66 to −80 dB); per-sample interpolation would remove it.
- **Remaining C hot spots:** the stiff string's general loop (structure < 0.24 or > 0.81, ~375 cycles per sample), the reverb's C loop for 6,312 samples after a Clear, the modal model's per-block coefficient maths. The plain string kernel (117 cycles per sample) could keep state in registers.
- **The CPU meter** could stay as a feature or be removed before a "release" build; ask the user.
- **Other user wishes:**
  - CWO → COW in the effect menu (display-only; feasible with the patch table);
  - PORTAMENTO → PORTA was dropped: don't propose it;
  - COW → POLY is moot (builds from stock show POLY).

## 10. Docs and memory

| Doc | Content |
|---|---|
| `docs/rings-port.md` | Port design, validation, measurements |
| `docs/engine-interface.md` | Engine types, factory, voice vtables, render ABI, pitch |
| `docs/screen-interface.md` | Screen class, SVG API, frame loop, input path, presets; §6 the screen hook, §7 encoder clicks |
| `docs/device-tests.md` | Every device test (probes v1–v3, Tests 2–8) with procedures and results |
| `docs/memory-map.md` | App load map; the free region `0x02c00000–0x02ffffff` |
| `docs/brick-safety.md`, `docs/firmware-246-baseline.md`, `docs/app-boot-chain.md` | Safety and groundwork |
| `ports/rings/README.md` | Port layout and harness builds |

Persistent notes: `%USERPROFILE%\.claude\projects\I--GitHub-PersonalProjects-OP1-CustomFirmware\memory\`.
