# Handover: OP-1 custom firmware

Last updated 2026-10-16. Read this first in a fresh session; it's self-contained, and the docs it links to have the detail.

**Next project: Mutable Instruments Clouds as an OP-1 effect (§3).** Start with step 0: settle the open decisions with the user, then the device probe (step 1). Rings / MULTI is done for now (§2); its technical reference is `docs/rings-op1-reference.md`.

## 1. Goal and hard rules

Custom firmware for the **original TE OP-1**: Analog Devices Blackfin ADSP-BF524C2 (~400 MHz, no FPU), app firmware 246, bootloader TE-BOOT v2.30. The owner has **one unit**. **The bootloader must never be modified or rewritten.**

Non-negotiable rules (detail in `docs/brick-safety.md`):
1. **Never put `te-boot.ldr` in a package.** Build with `tools/build_package.py`, which drops it. (op1repacker's own `repack` command packs it: never use that.)
2. **Every package must pass `tools/verify_bootloader.py`** before flashing (`build_package.py` runs it).
3. **Never use `FRC` in a package file name** (it forces a bootloader rewrite).
4. **Never put a `.KEY` file on the OP-1 disk** (irreversible OTP key programming).
5. **Install only through the te-boot menu:** hold COM at power-on, then "1. Upload firmware".
6. **Ask the user before anything touches the device.** The user flashes; we prepare and verify. Before asking, diff every package against stock: only the intended files and words may differ (`docs/rings-op1-reference.md` §5 has the procedure).
7. **Recovery:** hold COM at power-on and flash stock `op1 firmware/op1_246.op1`. This works whatever state the app is in.
8. **Before ever flashing stock again, the user restores factory defaults first** (their decision), so no saved slot refers to anything stock firmware lacks.

How the user works:
- They test on the device and report back in numbered points; device tests are logged in `docs/device-tests.md` (procedure before the flash, result after).
- They stay on branch `firmware-analysis-tooling`.
- Commit and push only when they ask.
- Decisions they've made are recorded (§2, and per project); don't re-ask them.

## 2. Where things stand

**On the device: `op1 firmware/op1_246_rings_multi4.op1`** (Test 15: "everything sounds great, everything is working a treat").

**The phase slot is the "MULTI" engine:** each slot plays **stock phase** or **Mutable Instruments Rings** (all six models), switched by an orange encoder click on the engine page. The lists show "multi" for "phase" (display only). Everything about it (controls, architecture, hooks, CPU, tests, package recipe): `docs/rings-op1-reference.md`. Design and validation history: `docs/rings-port.md`.

**Known issues, to revisit at the end** (the user is happy as it is for now):
- **Model 2 (sympathetic strings) and model 6 (string + reverb) crackle somewhat in polyphony "all"; in polyphony 4 both are good** (Test 15). Read as CPU: sympathetic "all" is 6 voices × 2 strings (1.5× polyphony 4's load). **Puzzle for model 6:** the code plays "all" exactly as polyphony 4 (`op1/engine.cc` NoteOn maps it), so check whether the difference is in how it was played (more notes ringing) before optimising. Levers: the string kernel keeping state in registers (117 cycles per sample), the stiff string's general C loop, the reverb's C loop after a restart, or fewer voices in "all" (ask the user).
- Free CPU under real load (FX, other tracks) is unmeasured; the ~324 MHz free figure is one stock voice and nothing else. **This matters for Clouds** (§3).

**Decided with the user — don't re-ask:**
- sympathetic strings tune around the played note; the string models' mono mix is upstream's per-voice `out − aux`;
- model 6 plays at most 4 voices in "all";
- polyphony "all" has 14 modes per modal voice (16 crackled while turning); polyphony 1, 2 and 4 are upstream's;
- the modal and FM output must otherwise stay bit-identical (checksums: modal `ef8618dd`, FM `567afc0c`, model 6 `7d5e0bee`);
- the brightness handle spans the green encoder's range 0..29491 (display only);
- PORTAMENTO → PORTA is dropped: don't propose it.

**Proven on the device** (detail in `docs/device-tests.md`, `docs/engine-interface.md`):
- Synth voices render at 88.2 kHz, 128 samples per call (~689 calls/s per sounding voice), one call per sounding voice per block, apparently in index order. Only sounding voices are rendered.
- CPU ~400 MHz, ~324 MHz free with one stock voice. **The device runs ~1.5–1.8× the simulator's cycle counts.** Keep simulated totals well under ~200 MHz.
- Injected code at `0x02c00000` runs; the app has no integrity check. Audio overload is survivable (crackle); a memory-corrupting bug is not (power cycle; no lasting harm so far).
- Patching works through data only: vtable words, imm16 immediates, content files (SVG, `op1_factory.db`). Calling firmware functions from our C++ works.

## 3. Next project: Clouds as an effect

The spec is `docs/next-projects.md` §1 (from static analysis of firmware 246 and the Clouds source; nothing tried on the device yet). In short:

**The OP-1's effects** [C = confirmed from code]:
- 8 types (`fx_types.cpp`, table `0x18dd810`): delay, grid, punch, phone, spring, filter (hidden: no `fx_types` row in the factory db), nitro, cwo.
- Positions: synth (mono), drum (mono), master (stereo). Factories `0x1a1b5f0` (stereo, `type < 8`) and `0x1a1b6d0` (mono). Objects in the L1 scratchpad (`0xffb00600` mono, `0xffb00790` stereo): keep only a pointer there, state in our SDRAM.
- Vtable: `+0x14` process, `+0x1c` init(this, pos). **Process:** mono `(this, int16* buf, const int16* params)`, in place, **64 samples**; stereo `(this, int16* L, int16* R, [SP+12] params)`. **44.1 kHz** (inferred: delay lengths `44100 >> n`).
- Parameters: 8 int16 per position (master `0xff8016c4`), clamped by the FX Ranges table `0x1de5c50`. Saved as `fx_type` (name), `fx_params[8]`, `fx_active`; **`fx_params[4..7]` are unused by stock effects** (room for Clouds' mode and settings).
- **All effect process functions are plain SDRAM**: hookable with vtable word patches, like phase was.

**Recommended host: the master effect (stereo, as Clouds is), replacing "phone"** (visible, unused in factory presets, with its own screen):
- mono process/init words `0x1911308` / `0x1911310`; stereo `0x1911328` (stock `0x01a1c25c`) / `0x1911330` (stock `0x01a1c294`);
- screen: "ptch.svg" immediates `0x1a1c7cc` / `0x1a1c7d0`; PhoneScreen Init/Update words `0x1911358` / `0x1911350` (the PdScreen pattern we already hooked).
- "filter" (`0x1910ca8`/`0x1910cb0`, stereo `0x1910cc8`/`0x1910cd0`) only if op1repacker's filter mod isn't wanted.

**Clouds' cost** (estimates from the code): at 44.1 kHz stereo on the device, the shared chain (feedback filters, diffuser, reverb, mix) ~25 MHz; + looping delay ~35 MHz in all; granular with 32 grains ~70 MHz (16 grains ~45); time stretch (WSOLA) 45–70 MHz; spectral (phase vocoder) ~70 MHz only with an assembly FFT (>120 in C). Memory is no issue (~3.3 MB free above our image); the 16 KB data cache is (32 grains = 64 scattered streams). Upstream runs at 32 kHz: running near it saves ~30%.

**Step 0: decisions to settle with the user first:**
1. Host effect: phone (recommended) or filter? And whether op1repacker's filter mod is wanted (it decides which is free).
2. Master effect (stereo, recommended) or the synth's own effect (mono)?
3. Modes, in order: looping delay → granular → time stretch → spectral (last, or never)?
4. Controls: Clouds has 9 parameters plus freeze and trigger (`clouds/dsp/parameters.h`) for 4 encoders; mode and quality in `fx_params[4..7]`; encoder clicks on the FX page (the key hooks for that page are still to find).
5. CPU together with Rings: Rings at its heaviest (~120 MHz on the device) plus Clouds (~35–70) must fit ~324 MHz minus the OP-1's own work; expect grain and quality limits.

**Step 1: a device probe, no Clouds code yet.** A pass-through effect on the phone (master) slot that measures and reports:
- calls per second and block size (confirm 64 samples at 44.1 kHz), mono/stereo argument order;
- whether process runs while `fx_active` = 0, and pre/post the drum mix;
- its own cycles and the free headroom with 6 Rings voices plus the effect.
- Earlier probes reported numbers as audio (`tools/decode_probe_beeps.py`, `decode_probe_tones.py`; `docs/device-tests.md` Test 1–1c) or, since strings2, on the screen's rings (the CPU meter). The FX screen (PhoneScreen) can be hooked the same way to show readings.

**Then the port, Rings-style** (`docs/rings-port.md` is the model):
1. `ports/clouds/`: upstream `clouds/dsp` plus a host harness rendering float reference WAVs (from `clouds/test/clouds_test.cc`), and SNR/spectral comparison (`ports/rings/harness/compare.py`).
2. Fixed-point chassis: int16 buffers, the soft limit (Rings' table), SVFs, the diffuser, **Rings' `reverb_fixed` reused** (Rings' delay lengths are ~1.32× Clouds', i.e. already right for ~42–44 kHz), dry/wet.
3. Looping delay + pitch shifter; then granular (assembly overlap-add kernels, FFloat per-grain setup); then WSOLA (correlator in assembly with `ONES`, a search budget per call); spectral last.
4. OP-1 hookup: the FX vtable words in the patch table, a Clouds screen (`clouds.svg`), the CPU meter, device tests.

**Integration notes:**
- Everything we inject is one image (`tools/inject_app.py`: one header, one patch table, one LDR block at `0x02c00000`). Clouds goes into the same image as Rings: the build needs restructuring (e.g. a top-level image target linking `ports/rings` and `ports/clouds`, shared code such as `fixed.h`, `ffloat.h`, `reverb_fixed.*`, `cpu_meter.h` moved to a common place).
- Effects run at 44.1 kHz on 64-sample blocks: the same block size as Rings' internal rate, so Rings' fixed-point blocks and helpers fit directly.
- Every assembly kernel gets a C reference and a bit-exact check against a host build; any ring buffer driven from assembly gets a test across its wraps with guard words (the model 6 hang taught that: the simulator doesn't fault on stray writes).

## 4. Environment

**Windows 11:**
- `.venv/Scripts/python` (Python 3.11; no numpy). Run tools from the repo root.
- `ffmpeg` is on PATH.

**WSL Ubuntu 20.04:**
- Toolchain in `~/op1-toolchain/bfin/bin/`: binutils 2.43, gdb 15.2, GCC 14.2 C/C++ with newlib (`tools/build_bfin_toolchain.sh`, `tools/build_bfin_gcc.sh`).
- Run make like this: `wsl -e bash -lc 'cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/rings && make …'`.
- Simulator: `bfin-elf-run` (default environment only; L1 isn't mapped, so simulator builds use `-DOP1_SIM`). `--trace-insn=on` gives an instruction trace for profiling.

**Gotchas:**
- **Shell heredocs mangle `\n`, `\\` and quotes** inside the scripts they carry (it happened many times). Write edit scripts with the Write tool, then run them; heredocs only for text without backslashes.
- **libgcc divisions are hundreds of instructions on Blackfin** (no divide instruction): keep `/` and `%` by non-constants out of audio loops.
- **GCC 14 on Blackfin** turns selects and saturating shifts into taken branches (~5 cycles each) and spills when many values are live; `__builtin_bfin_shl_fr1x32` miscompiles constant shifts (`ShlSat` uses inline `<< n (S)`). Hot loops go to assembly with C references; check with `bfin-elf-objdump -d -C`.
- **Assembly conventions:** `MUL(d, a, b)` is the 4-instruction `MulQ31Fast`; loads/stores via P (offset ≤ 60 bytes) or I registers ride in a multiply's parallel slots (`|| … || NOP`); keep `L` registers at 0; Blackfin symbols get a leading underscore.
- **The simulator doesn't fault on stray writes and doesn't model the cache.**
- **The image has no startup code or run-time initialisers:** tables of addresses must be link-time constants; no `.init_array` / `.ctors`.
- **objdump's `/* R0=… */` annotations can be stale;** trust the `.L`/`.H` pairs. Patch-table imm16 addresses are the instruction address + 2.

**Reverse-engineering material** (outside the repo):
- `op1 firmware/analysis/`: the app's ELF and disassembly (`OP1_vdk.dis`, 11M lines; grep it).
- `%TEMP%\op1re\`: indexes `compact.txt`, `calls.txt`, `consts.txt`, `srctags.txt`, `vcalls.txt`; helpers `op1lib.py`, `show.py START END`, `vt.py BASE N`.
- `%TEMP%\op1inject\`: `check.py` (byte-diffs an injected LDR against stock), `pkgdiff.py` (diffs packages), and our built LDRs.

## 5. Other projects (specced in `docs/next-projects.md`)

- **op1repacker mods** (§5 there): tape at the top, iter and filter enabled, iter presets, CWO animals. All content-only (db rows, SVG edits): re-implement as `tools/mods.py`. **Interacts with Clouds' host choice** (filter) and with the preset budget.
- **Rings factory presets** (§6): rows in `content/op1_factory.db` as `"type":"phase"` with `knobs[4..6]`; at most 176 non-sampler factory presets (stock 160; op1repacker's iter presets would take 11).
- **More engines on the MULTI slot** (§2–§3): more engines selected by `knobs[6]`, each with its own SVG (the swap is proven); Plaits is the obvious candidate.
- **A builder app** (§4): feature manifests and a resolver over our tools, later a GUI or an on-device menu.
- **Smaller:** CWO → COW in the effect menu (display-only, patch table); the CPU meter in a "release" build (ask); green encoder's sound range (stock caps at ~0.9; ask); FM costs ~17 MHz per voice (could be optimised); the limiter's per-sub-block gain zipper (−66 to −80 dB).

## 6. Git

- Branch `firmware-analysis-tooling`, remote `origin` (`<public repo URL>`). Run `git status` first; commit only when the user asks.
- **Never commit TE firmware:** `op1 firmware/` and `mutableinstruments/` are gitignored; so is `ports/rings/build/` (scratch scripts there can be deleted).
- `incoming/` belongs to the user and stays out of git.

## 7. Docs and memory

| Doc | Content |
|---|---|
| `docs/rings-op1-reference.md` | Rings / MULTI on the device: controls, architecture, hooks, CPU, tests, package recipe and checks |
| `docs/rings-port.md` | The Rings port's design, validation and measurements |
| `docs/next-projects.md` | Specs: Clouds as an effect (the OP-1's effect interface), engines, a builder app, op1repacker mods, factory presets |
| `docs/device-tests.md` | Every device test (probes v1–v3, Tests 2–15): procedure and result |
| `docs/engine-interface.md` | Synth engine types, factory, voice vtables, render ABI, pitch |
| `docs/screen-interface.md` | Screen class, SVG API, frame loop, input path, presets, encoder clicks |
| `docs/memory-map.md` | App load map; free regions (A `0x02c00000–0x02ffffff` in use; B and C rated good, untested) |
| `docs/brick-safety.md`, `docs/firmware-246-baseline.md`, `docs/app-boot-chain.md` | Safety and groundwork |
| `ports/rings/README.md` | Port layout and harness builds |

Persistent notes: `%USERPROFILE%\.claude\projects\I--GitHub-PersonalProjects-OP1-CustomFirmware\memory\`.
