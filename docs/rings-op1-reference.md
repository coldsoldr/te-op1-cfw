# Rings and the MULTI engine on the OP-1: technical reference

The state at the end of the Rings work (2026-10-16, `op1_246_rings_multi4.op1` on the device). `docs/HANDOVER.md` has the project-level handover; this is the detail for anyone changing the Rings / MULTI code. Design and validation history: `docs/rings-port.md`; every device test: `docs/device-tests.md` (Tests 2–15).

## 1. What's on the device

**The phase slot is the "MULTI" engine:** each slot plays **stock phase** or **Rings**, picked by `knobs[6]` (0 = phase, so every stock phase preset sounds as before).
- **Click orange** on the engine page toggles it; the change applies from the next note, and the screen swaps `pd.svg` / `rings.svg` to match.
- The engine browser (shift + T1) and the preset browser show "multi" for "phase". Display only: presets and slots keep `"type":"phase"`.

**Rings controls** (synth engine page of a MULTI slot playing Rings; on stock phase only the orange click is ours):
- encoders: blue = structure, green = brightness, white = damping, orange = position; smoothed ~50 ms; **shift + turn** = stock 1/16 fine adjustment;
- **click blue** = next model (6, Rings' order), **click green** = next polyphony (all → 1 → 2 → 4 → all), **shift + click** = back;
- **click white** = CPU meter on the six rings (ring *i* lit at (*i*+1) × 15% of a block): hidden → engine → the modal model's coefficient updates only → hidden;
- **click orange** = next engine (phase ↔ Rings).
- Engine, model and polyphony are saved per slot and in snapshots (`knobs[6]`, `knobs[4]`, `knobs[5]`, as `setting × 4096`).

**Rings' six models** (`op1rings::ResonatorModel`; screen buttons 1–6):

| # | Model | Code | "all" |
|---|---|---|---|
| 0 | Modal resonator | `src/modal_resonator.*`, `mode_bank_bfin.S` | 6 voices × 14 modes |
| 1 | Sympathetic strings | `src/string_voice.*` | 6 voices × 2 strings |
| 2 | Modulated / inharmonic string | `src/string_fixed.*` | 6 voices |
| 3 | FM voice | `src/fm_voice_fixed.*` | 6 voices |
| 4 | Quantized sympathetic strings | `src/string_voice.*` (Bryan Noll's chord table) | 6 voices × 2 strings |
| 5 | String + reverb | `src/string_voice.*` + one shared `src/reverb_fixed.*` | played as polyphony 4 |

Polyphony 1 / 2 / 4 are Rings' own (modal: 60 / 28 / 12 modes; strings: 8 / 4 / 2 per voice).

## 2. Architecture (`ports/rings/`)

**Why fixed point:** the BF524 has no FPU. Float Rings would need ~16.5 GHz per modal voice, and libgcc soft-float costs ~330 cycles per operation. Everything at run time is fixed point; a few per-block calculations use `FFloat` (`src/ffloat.h`, a software float with a 32-bit mantissa).

| File | Role |
|---|---|
| `op1/engine.cc` | Render and note-on entry points (phase voice vtable +0x14 / +0x1c). Latches the engine per voice at note-on; phase voices call the stock render / note-on (`0x1a0bc5c` / `0x1a0be82`). Rings runs at 44.1 kHz on 64 samples per call; a 4-point Lagrange `Upsampler` doubles it to 128 samples at 88.2 kHz. Holds six `RingsVoice` states behind `RingsVoice* voices[6]` (swapped on a steal), per-voice `ParamSmoother` and `Upsampler`, the `CoefficientBudget`, model 6's `ReverbBus`, the CPU meter; applies `VoiceAllocator` decisions and does fade-outs. The image header (`OP1X`) points to the patch table |
| `op1/settings.h` | `knobs[4]` model, `[5]` polyphony, `[6]` engine (`setting × 4096`); `kPolyVoices` / `kPolyModes` (`{14, 60, 28, 12}`) / `kRingsPolyphony`; `StepModel` / `StepPoly` / `StepEngine` |
| `op1/voice_allocator.h` | Rings polyphony on the OP-1's 6 voices: steal the oldest (`copy_from`: the new voice takes over its state), fade the stolen voice (512 samples, at most 2 at once), silence voices stolen before they sounded; `Forget` for voices that switch to stock phase |
| `op1/coefficient_budget.h` | The modal model's coefficient updates per OP-1 call: at most 32 modes plus one voice whatever its size, round robin; none while a voice fades after a steal; a strike always updates. Blocks end on a repeated voice or a lower index (the OP-1 renders in index order, inferred) |
| `op1/reverb_bus.h` | Model 6's shared reverb: ends a block when a voice renders twice, runs `SharedReverb` once per block, shares the wet signal among the carriers (`Transfer` on steals) |
| `op1/param_smoother.h` | ~50 ms one-pole per voice and parameter; snaps within 2^-12 and at note-on |
| `op1/cpu_meter.h` | Engine CPU share and the coefficient updates' share (`op1rings::profile_update_cycles`), for device tests |
| `op1/screen.cc`, `screen_math.h` | MULTI screen: `MultiScreenInit` runs the stock PdScreen Init on `pd.svg`, then loads `rings.svg` with the firmware's loader (`0x1a5e3b0`, `0x1a50350`, `0x1a503d8`) and finds our nodes; `MultiScreenUpdate` swaps the screen's document by the displayed slot's engine (flushing the incoming one: its vtable +0x7c, as `Screen::UpdateAll` does on screen changes), then runs the stock Update or ours. The brightness handle spans 0..29491 (the green encoder's range) |
| `op1/rings.svg`, `gen_rings_svg.py` | The package's SVG, generated from `incoming/rings.svg` |
| `op1/input.cc` | Encoder clicks (`0x3b` blue, `0x3c` green, `0x3d` white confirmed, `0x3e` orange inferred). Synth controller key-down / shifted / key-up hooks, active only on the engine page of a phase slot; they write `knobs[4..6]` and call `Patch::SetKnobs` |
| `op1/patches.cc` | The patch table: the type-list query (the engine browser shows "multi"), screen Init/Update, the 3 key hooks |
| `op1/op1.h` | Addresses and conventions, `Header` and `Patch` structs, `VoiceIndex` (no division), `Saturate16` |
| `src/op1_voice.*` | `RingsVoice`: strum → excitation filter (cutoff cached) → `ModalResonator` / `FmVoiceFixed` / `StringVoice` → `MonoLimiter` (per-model gains). `SharedReverb` for model 6. Coefficient hold and demand for the budget |
| `src/modal_resonator.*`, `mode_bank.h`, `mode_bank_bfin.S` | Chamberlin SVF bank with a closed-form state map on coefficient changes; the assembly loop: 23 instructions, 23.7 cycles per mode per sample (the bit-exact minimum here). Both outputs can be summed into one buffer (out == aux); pickup weights kept while position and gains are still; `set_hold` / `TakeChanged` |
| `src/string_fixed.*`, `string_kernel.h`, `string_kernel_bfin.S` | Fixed-point string (Hermite delay line, dispersion, FIR/IIR damping, pickup), plucker, DC blocker, upstream's LCG; the plain and stretch-only loops in C and assembly |
| `src/string_voice.*` | The string models for one voice (plucker, string 0, sympathetic strings, tuning, chords, LFOs) |
| `src/reverb_fixed.*`, `reverb_bfin.S` | Rings' reverb, mono, unwrapped 16-bit buffer (109 KB), O(1) clear; the assembly runs the settled case (it wraps the write pointer before each run) |
| `src/fm_voice_fixed.*` | Fixed-point FM voice |
| `src/limiter_bfin.S` | The mono limiter's loop |
| `src/fixed.h`, `ffloat.h`, `fixed_tables.*`, `resources/gen_fixed_tables.py` | Q-format helpers (`MulQ31Fast`, `ShlSat`), the software float, generated tables |
| `tools/inject_app.py` | Adds the image's blocks before FINAL, patches the phase voice vtable from the header and applies the patch table (32-bit word, imm16 lo/hi), checking each stock value |
| `tools/rename_preset_folder.py` | Renames a factory preset folder in an unpacked `op1_factory.db` (display only), with checks |

**Every assembly routine is bit-exact with a C reference**, checked by the make targets below.

**Hook addresses** (all plain SDRAM; detail in `docs/engine-interface.md`, `docs/screen-interface.md`):
- **Phase voice vtable** `0x191113c`: render `0x1911150` (stock `0x01a0bc5c`), note-on `0x1911158` (stock `0x01a0be82`). Note-on gets R1 = the slot's `int16 knobs[8]`.
- **Knobs:** `0xff8013c4 + slot×16`. Current slot `[0xff8016d4]`; engine type `[0xff8016e4 + slot×4]`; core slot `[0x01aaac0c]`; displayed slot `[0x01df4e38]`; synth sub-page `[0x01b4ee88]` (0 = engine page); `Patch::SetKnobs` `0x1a00d14` (patches at `0x1ddd7e8 + slot×0x38`).
- **Screen:** PdScreen vtable Init `0x1911190`, Update `0x1911188` (the filename immediates `0x1a0cd8e`/`0x1a0cd92` stay stock since multi1).
- **Keys:** synth controller vtable `0x1911994`: key down `0x19119a8`, shifted `0x19119b0`, key up `0x19119b8`.
- **Engine browser label:** the type-list query string at `0x1970866` / `0x197086a` (immediates at +2: `0x1970868` / `0x197086c`), shared by the synth, drum, fx, lfo and seq lists.

## 3. CPU (simulator; the device has run ~1.5–1.8× these)

| Case | Cost |
|---|---|
| Modal, "all", 6 voices / 3 knobs turning | ~123 / ~142 MHz (multi4) |
| Modal, polyphony 1, fast notes, knobs turning | ~97 MHz, peak ~181 |
| FM, 6 voices | ~106 MHz |
| String (model 2), "all", 6 voices | ~85 MHz |
| Sympathetic or quantized, "all", 6 voices / knobs turning | ~104 / ~114 MHz |
| String + reverb, "all" (4 voices) / knobs turning | ~73 / ~77 MHz |
| String + reverb, polyphony 4, fast notes (peak) | ~123 MHz |

Per voice and part: `make bench-models`. A per-function profile: run a small driver under `bfin-elf-run --trace-insn=on`, count instructions per address, map with `bfin-elf-nm -n` (done in multi4; the mode bank is ~70% of a modal voice).

## 4. Tests and make targets (WSL, from `ports/rings`)

| Target | What |
|---|---|
| `make op1-engine` | The engine image → `build/op1_engine.elf` |
| `make sim-engine` / `sim-engine-fm` | Entry points in the simulator, CPU per case. **Modal must print checksum `ef8618dd`** (14 modes in "all", since multi4; 16 modes was `a3ae5f5c`), **FM `567afc0c`** |
| `make sim-model M=n` | The same with model n; model 6 (`M=5`) prints `7d5e0bee` |
| `make test-alloc` | Polyphony allocator, settings encoding, click stepping, `Forget`, the coefficient budget |
| `make step-params` | Parameter-step clicks (upstream vs port; smoothed steps with updates every call, 2nd, 3rd: all below −60 dB) |
| `make test-reverb-bus` | Model 6's shared reverb on the host (16 checks) |
| `make bench-mode-bank` | The mode bank: cycles; assembly = C = host, two outputs and one |
| `make bench-models` | Cycles per voice and model, and per part; limiter C vs assembly (`092ec491`) |
| `make bench-string` | String kernels C vs assembly (`aa65f279`, `23c9c099`); the reverb over ~3.9 wraps with guard words, Blackfin = host (`c36f2ef8`) |
| `make test-string` | String models against upstream (19 scenarios) |
| `make test-reverb`, `test-fm`, `test-screen` | Reverb, FM voice, screen geometry |
| `make ref`, `port`, `compare` | Harness: upstream float Rings against the port's float `Part` |

## 5. Package recipe

```
(WSL, ports/rings) make op1-engine
.venv/Scripts/python tools/inject_app.py "op1 firmware/op1_246/OP1_vdk.ldr" OUT.ldr ports/rings/build/op1_engine.elf
cp -r "op1 firmware/op1_246" "op1 firmware/work_x"; cp OUT.ldr "op1 firmware/work_x/OP1_vdk.ldr"
cp ports/rings/op1/rings.svg "op1 firmware/work_x/content/display/rings.svg"
.venv/Scripts/python tools/rename_preset_folder.py "op1 firmware/work_x/content/op1_factory.db" phase multi --expect 16 --type phase
.venv/Scripts/python tools/build_package.py "op1 firmware/work_x" "op1 firmware/op1_246_rings_<name>.op1"
```

**Verify before asking the user to flash:**
1. `inject_app.py` prints each vtable word and patch: check every target against `bfin-elf-nm build/op1_engine.elf`.
2. Byte-diff the LDR with `%TEMP%\op1inject\check.py`: only the FIRST header, the 7 vtable/patch words, the type-list query immediates and the appended blocks may differ; the stock tail must be intact.
3. Diff the package with `pkgdiff.py`: only `OP1_vdk.ldr` and `content/op1_factory.db` change, `content/display/rings.svg` is added, `te-boot.ldr` is absent. The db's `iterdump()` against stock differs only in the 16 phase rows' folder.
4. `tools/verify_bootloader.py` passes (`build_package.py` runs it).

Also: the image ends below `0x03000000` (multi4: `0x02ca37d4`); `bfin-elf-nm` shows no new soft-float (only `__adddf3`, `__subdf3`, `__fixdfsi` from `FmVoiceFixed::Init`, harmless); no `.init_array`.

## 6. History in one line each

clicks1 (modal + FM, screen, clicks) → strings1–3 (string models, assembly loops) → strings4 (model 6 hang fixed) → strings5 (green handle) → strings6 (model 6 shared reverb) → multi1 (MULTI engine: phase or Rings, orange click, "multi" labels) → multi2 (coefficient budget) → multi3 (meter: updates reading) → multi4 (14 modes in "all", bit-identical savings). Detail per build: `docs/device-tests.md`.
