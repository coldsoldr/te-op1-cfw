# Handover: OP-1 custom firmware project

Last updated 2026-10-14. Read this first in a fresh session, then the docs it links to.

## 1. Goal and hard rules
Build custom firmware for the **original TE OP-1** (Analog Devices Blackfin ADSP-BF524C2, firmware 246, bootloader TE-BOOT v2.30). The owner has **one unit**. **The bootloader must never be modified or rewritten.**

Non-negotiable rules (detail and evidence in `docs/brick-safety.md`):
1. **Never put `te-boot.ldr` in a package.** Build with `tools/build_package.py`, which drops it.
2. **Every package must pass `tools/verify_bootloader.py`** before flashing.
3. **Never use `FRC` in a package file name** (forces a bootloader rewrite). **Never put a `.KEY` file on the OP-1 disk** (irreversible OTP key programming).
4. **Install only through the te-boot menu:** hold COM at power-on, then "1. Upload firmware". The app-side update path forces a bootloader rewrite when te-boot.ldr is present.
5. **Ask the user before anything touches the device.** The user flashes; we prepare and verify.
6. **Recovery:** hold COM at power-on and flash stock `op1_246.op1`. This works whatever state the app is in, because the init code checks COM before any app code loads.

## 2. Status

**Proven on the device (2026-10-14):**
- **Stock flash through te-boot:** the updater printed "te_boot skipped", which proves the unit runs v2.30 and that recovery works.
- **Patched app** `op1_246_cow.op1`: 3 bytes changed (play-mode label `poly` → `cow` at `0x18f2f8c`). It boots and shows **COW**. So there's **no integrity check on the app**, and the patch → package → flash pipeline works.
- **The unit currently runs the COW build.**

**Git:** branch `firmware-analysis-tooling` (the user wants to stay on it), commits `a3ce4e9` and `dea6f49`, pushed to `origin` (`<public repo URL>`).
- **Never commit TE firmware.** `op1 firmware/` is gitignored.
- The user has uncommitted local changes I didn't touch: `.gitignore` is modified and there's a new `mutableinstruments/` folder.

## 3. Environment
- **Windows 11.** Use `.venv/Scripts/python` (Python 3.11 with `svg.path`; there's no `python3` alias). Run tools from the repo root.
- **WSL Ubuntu 20.04** has the Blackfin toolchain at `~/op1-toolchain/bfin/bin/`: `bfin-elf-objdump`, `bfin-elf-run` (GDB simulator, `--model bf524 --environment operating`) and `bfin-elf-gdb`. It was built by `tools/build_bfin_toolchain.sh`, log in `~/op1-toolchain-build.log`. Windows paths are visible as `/mnt/i/...`.
- **Gotchas:**
  - Git Bash heredocs passed to Python mangle `\\`. Write analysis scripts to a file instead, or use `chr(92)`.
  - `/tmp` in Git Bash is not the same as WSL's `/tmp`.
  - objdump's `/* R0=0x... */` value annotations can be stale across branches. Trust the `.L`/`.H` pairs instead.

## 4. Repo layout
| Path | What |
|---|---|
| `tools/ldr_dump.py` | Parse Blackfin LDR files; `--blocks`, `--dump DIR`, `--elf OUT [--dxe N]`; `serialize()` rebuilds byte-identically |
| `tools/patch_app.py` | `IN.ldr OUT.ldr ADDR:OLDHEX:NEWHEX ...`, patches at runtime addresses; checks the expected bytes; refuses CALLBACK, FILL and IGNORE blocks |
| `tools/build_package.py` | `UNPACKED_DIR OUT.op1`: packs without te-boot.ldr (reusing op1repacker), then runs the guard |
| `tools/verify_bootloader.py` | Pre-flash gate: te-boot absent (or `--allow-stock-bootloader`), CRC, ≤15 MB package, app ≤ NAND blocks 4–127, valid LDR, no FRC or .KEY names |
| `tools/build_bfin_toolchain.sh` | Builds binutils 2.43 and gdb 15.2 for bfin-elf in WSL |
| `op1repacker-master/` | Vendored community tool (MIT): unpack, repack, DB and SVG mods |
| `docs/firmware-246-baseline.md` | Hashes, package format, LDR memory map, DB schemas, scrambled L1 notes |
| `docs/brick-safety.md` | Update pipeline, te-boot write logic, rules |
| `docs/app-boot-chain.md` | Boot chain, XTEA L1 encryption, NAND layout, on-device verification |
| `docs/emulation-feasibility.md` | Simulator results and the work needed |

Gitignored working files in `op1 firmware/`:
- `op1_246.op1`: stock firmware (**the recovery file**)
- `op1_246/`: unpacked stock
- `op1_246-noboot.op1`: stock without te-boot
- `test_cow/` and `op1_246_cow.op1`: the current device build
- `analysis/`: `te-boot.elf`, `te-boot-init.elf`, `OP1_vdk.elf`, the matching `.dis` files from `bfin-elf-objdump -d -z` (huge: te-boot.dis is 16 M lines because it includes data sections), and `ldr/` memory dumps

The analysis scripts from the last session were throwaway files in `%TEMP%` (`eng_measure.py`, `cwo_refs.py`, `fx_table.py`, `fx_callers.py`, `font_callers.py`). They're not in the repo. The approach matters more than the code:
- Tag each function with the source files whose assert strings it references.
- Resolve strings through the `memory_runs` image.
- Find references through the `.H` lines' `R?=0x...` annotations.

## 5. Key technical facts

**Package and image formats**
- **`.op1`:** CRC-32 (4 bytes, LE), then LZMA-alone (`lc3 lp0 pb2`, 8 MB dictionary), then a GNU tar with `OP1_vdk.ldr`, `te-boot.ldr` and `content/` (SVGs, `.raw` audio, SQLite DBs).
- **LDR (BF52x):** 16-byte headers, `0xAD` signature, byte-XOR = 0. A FIRST block's argument = offset to the next DXE. Flags: CALLBACK `0x400`, FILL `0x100`, INIT `0x800`, IGNORE `0x1000`, FIRST `0x4000`, FINAL `0x8000`.

**Boot chain** (`docs/app-boot-chain.md`)
1. The ROM boots te-boot.
2. The **init DXE** (L1 at `0xffa00000`):
   - reads the **XTEA key from OTP page 0xD0** (`0xffa00514`);
   - checks COM via an on-board device at address `0x25`, bit 12 (`0xffa0042c`);
   - in app mode, points the ROM at **NAND block 4** and installs a callback at `0xffa1007e`, which uses the XTEA routine at `0xffa10000`.
3. The callback decrypts **8 bytes of every 24** in the app's CALLBACK blocks: 45 blocks, 80 KB, all of it L1.
4. **SDRAM code (~23 MB) is plain, and there's no authentication.**
5. te-boot `main` = `0xf7c6`. It reboots through the watchdog at `0x169ec`.

**Memory and storage**
- **Memory map:** 64 MB SDRAM. te-boot lives in low SDRAM. The app's code/data runs from `0x01000000` to `0x01dfff33`, plus regions at `0x02000000`–`0x0303ffff`. L1 instruction memory (48 KB) is effectively full.
- **NAND:** 4096 blocks × 128 KB = 512 MB. The boot area is 128 blocks (16 MB): te-boot from block 0, the app written raw from block 4 (room ≈ 15.5 MB). YAFFS2 (≈ 496 MB) follows.

**Update code (te-boot)**
- `0xefca`: update routine. CRC check, unlink the old `.ldr` files, untar, then write te-boot **only if the file exists**, then write the app.
- `0x1068`: te-boot writer. Skips if the new image contains `te_boot_version:2.30` and force is off. Merges per-unit gamma calibration. Refuses versions below 2.3. The write is not atomic.
- `0x1388`: app writer.
- The file name is checked for `.KEY`, `.OP1`, `RBT` and `FRC` (force).

**Update code (app)**
- `0x1a5dedc(buf, len, force)`.
- Called from OP_Main at `0x19f0dd0` with **force = 1**, and from the PC-sync path at `0x1a0a242` with force = 0.

**App addresses useful for next steps**
- **FX name table:** `{name, id}` pairs at `0x18dd810` (delay, grid, punch, phone, spring, filter, nitro, cwo → 0–7). Converters `0x19aac38`, `0x19aadf8`, `0x19aae44` are called from `patch.cpp`, `MasterController` and `main.c`.
- **FX SQL:** `SELECT type FROM fx_types WHERE id=%i` at `0x18f16d8` and `0x18e33ac`. Also `SELECT id FROM fx_types WHERE type='%s'` at `0x18e3120` and `0x18f18b0`.
- **Play-mode screen** (`0x1a0daf0`):
  - switch table at `0x18f2fbc`, string base `0x18f2fac`;
  - labels `poly` (`0x18f2f8c`, now `cow`), `mono` `…94`, `legato` `…9c`, `unison` `…a4`; also `off`, `max`, `%d`;
  - text pattern: `CALL 0x1a9ce04(buf, 16, str)` (copy/format into a buffer), then `CALL 0x1a61aac(node, buf)` (probably "set node text").
- **Tape:**
  - `kTape_length = 0xF23FA0` (6:00 at 44.1 kHz, compared at `0x195c114`); 4 mono tracks; 16-bit;
  - one file `/user/tape_c.raw` (chunked);
  - "4 tracks" is hard-coded throughout;
  - producer_factory: "mono fx only allowed in synth, drum" (stereo FX variants exist for the master).
- **Engine modules** (SDRAM, approximate):

  | Engine | Module | Size | Range |
  |---|---|---|---|
  | phase | Pd | 5.3 KB | `0x1a0bafc` |
  | pulse | Pls | 6.1 KB | `0x1a0d1e0` |
  | string | St | 5.3 KB | `0x1a4eec0` |
  | iter | Iter | 4.5 KB | `0x19b996e` |
  | cluster | Cls | 10 KB | |
  | voltage | Slump | 20 KB | |
  | dsynth | dsynth | 27 KB | |
  | digital/dna | Id / T10 | 16 KB / 7 KB | which is which not confirmed |

  fm and drwave code wasn't found in plain SDRAM. The user **declined** porting an engine to Python.

## 6. Open questions
- **Free memory for new code:** can a new plain LDR block safely go into unused SDRAM (for example the gap `0x01e00000`–`0x01ffffff`)? Is it really unused, or heap or buffers at runtime? This must be established before the first code hook.
- **The COM-key reading** of `0xffa0042c` and the meaning of OTP page 0xD8 are inferred. Behaviour matches.
- **Album side length** (assumed 6:00 stereo, ≈64 MB per side) isn't confirmed. Real free flash space on the device is unknown.
- **The emulator** needs: an SDRAM-idle bit in the sim, Boot ROM high-level stubs (OTP calls), the OTP key for the app's L1, then NAND and peripherals.

## 7. Suggested next steps (agreed direction; confirm with the user)
1. **Effect menu CWO → COW as a display-only code hook.** This would be the first patch that adds code.
   - Find the FX menu code: callers of the `SELECT type FROM fx_types WHERE id=%i` strings, then follow the resulting string to the text-drawing call (probably the same `0x1a9ce04` / `0x1a61aac` pattern as the play-mode screen).
   - Patch that call to a small stub: if the string is "cwo", use "cow"; otherwise pass it through.
   - Needs: a safe code location (see open questions) and branch-encoding helpers for Blackfin `CALL`/`JUMP.L`.
   - **Do not** rename the identifier "cwo": it's in the DB and in user presets.
2. **PORTAMENTO → PORTA:** remove the M-E-N-T-O letter paths from the `static` group in `content/display/playmode.svg`. A graphics-only edit.
3. Later:
   - MIDI tools (scale quantize, chords) via the sequencer/player modules;
   - stereo synth/drum FX, then linked stereo tape pairs (1+2, 3+4);
   - new effects or engines.

## 8. Workflow for any app change
```
cp -r "op1 firmware/op1_246" "op1 firmware/<work>"
.venv/Scripts/python tools/patch_app.py "op1 firmware/<work>/OP1_vdk.ldr" "op1 firmware/<work>/OP1_vdk.ldr" ADDR:OLD:NEW
.venv/Scripts/python tools/build_package.py "op1 firmware/<work>" "op1 firmware/op1_246_<name>.op1"   # runs the guard
```
Then diff the package against stock (decode both, compare files: only the intended bytes may differ). Show the user the diff, then let them flash through the te-boot menu.

## 9. Memory
The persistent notes in `%USERPROFILE%\.claude\projects\I--GitHub-PersonalProjects-OP1-CustomFirmware\memory\` cover the goal, the boot chain and the current next steps.
