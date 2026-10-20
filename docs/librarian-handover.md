# Builder and librarian app: handover

Written 2026-10-20 for a fresh session that builds this unattended. Read this first, then `docs/HANDOVER.md` (the top section) and `docs/fx-categories.md`.

## The goal (the user's words)

> "Could we build an app that allows you to select which of these modules you want in your firmware and then builds it for you? Also, would it be possible to build a preset librarian into that app, and make it possible to edit the factory presets as well?"
> "I want to build everything. We don't need a simulator at all, we can preview samples, but we don't need the rings and clouds engines. The app should use Claude Design/artifact and draw inspiration from the manual aesthetic."

One desktop app, three parts:
1. **Builder:** tick the modules and mods you want, press build, get a verified `.op1`.
2. **Librarian:** browse, back up, organise and edit the user presets on the OP-1 (USB disk mode), with **sample preview** (play the audio in sampler and drum presets). No sound-engine simulation: don't port or emulate Rings, Clouds or any engine for previews.
3. **Factory preset editor:** edit, replace and add the factory presets that ship inside the firmware, as part of a build.

## Where things stand

- **On the device:** `op1 firmware/op1_246_beta4.op1`. It has every module: Rings (multi), Clouds (reverb), plate, hall, drive (valve screen), tape, fold, crush, ensemble, shift, tape echo, Marbles, Grids, plus op1repacker's iter, presets-iter, filter, gfx-tape-invert and gfx-iter-lab. beta3 tested good (Test 44, "the glow is fantastic"); beta4 adds only iter's lab screen (Test 45). Test 45 itself hasn't been reported, but the user has used beta4.
- **Committed and pushed** to branch `firmware-analysis-tooling` up to `14e1ced`. beta4's docs changes (Test 45, the manual's iter screen) aren't committed yet.
- **User manual:** `manual/user-manual.src.html` is the source. `manual/user-manual.html` is generated from it with the device screens inlined as data URIs (the generator is a short Python snippet: replace `{{svg:name}}` with the base64 SVG; see "Manual regeneration" below). Published at https://claude.ai/artifact/C5nKBuHkfU8nR4RQYDRjKV.
- **Design canvases:** effect screens https://claude.ai/artifact/24THwmJh3HnPME5PV6FfyU, module screens https://claude.ai/artifact/FLWjR5T5MPnEQF6BDFPCU5.

## Standing rules (never break these)

- **Never modify or rewrite the bootloader.** Never put `te-boot.ldr` in a package. Every package must pass `tools/verify_bootloader.py`; `tools/build_package.py` drops te-boot.ldr and runs it. The app must enforce this and refuse to write a package that fails.
- Never use "FRC" in a package file name. Never put a `.KEY` file on the OP-1 disk.
- Install only through the te-boot menu (hold COM at power-on → "1. Upload firmware"). **Ask the user before anything touches the device; the user flashes.** The app must never write to the OP-1 while it's in te-boot upload mode. In disk mode (librarian), read freely, but write only after the user confirms each change set, and back up first.
- Don't use te-boot's factory reset or format (they wipe the display gamma files).
- **Never commit or distribute Teenage Engineering firmware** (`op1 firmware/`, `mutableinstruments/`). `incoming/` belongs to the user. The app takes the user's own stock `op1_246.op1` as input.
- Commit and push only when the user asks.

## Architecture (recommended)

**A local app: a Python backend plus a web UI in the browser** (a desktop window via pywebview or Tauri later, if wanted). All the existing tools are Python, and the librarian needs the filesystem (the OP-1's disk). An artifact page can't run Python or write files, so the app itself can't be an artifact.

**"Use Claude Design / artifact"** means two things:
1. **Design the app's screens on a Claude Design canvas first** (Artifact tool: quickstart `intent: "design"`, then publish with the Design type's `type_url`). Brief a design agent the way the effect screens were briefed (see the Agent prompts in this session's transcript, or follow `docs/fx-briefs/README.md`'s spirit). Get the user's review before building the UI.
2. **The aesthetic is the manual's.** Reuse its tokens exactly (from `manual/user-manual.src.html`):
   - **background** `#121214`, panels `#1a1a1e` and `#222228`, lines `#2e2c36`;
   - **text** `#dcdae4`, `#a29fae` and `#7d7a89`;
   - **the encoder colours** blue `#638bff`, green `#00ee92`, white `#ded7ff`, orange `#ff395a`;
   - **fonts:** Chakra Petch (display), IBM Plex Sans (body), IBM Plex Mono (data);
   - **the components:** cards with a black device-screen thumbnail beside the controls, encoder dots, tags, chain chips (spring → plate → …) and mono uppercase labels.

   Show the real device screens (the SVGs in `ports/*/op1/` and `docs/module-briefs/designs/`) as module thumbnails in the builder.

Optionally publish a static, non-functional preview of the app as an artifact for review. The working app runs locally.

**Layout of the code:**
- `app/` (new): `backend/` (Python package: build pipeline, preset formats, OP-1 disk access, factory db), `ui/` (HTML/CSS/JS, no build step needed; plain modules are fine), `manifests/` (feature definitions), `tests/`.
- Keep the backend usable as a CLI: `python -m app.backend.build --stock op1_246.op1 --features rings,plate,tape-top --out my.op1`. The UI is a thin layer over it.

## Part 0: the module switch (the one firmware change)

**Decision (from the discussion): compile everything in, and switch modules on and off with a table.** The user's machine then needs no Blackfin toolchain, and every combination runs the same tested code. Today the module set is fixed at compile time (`FX_ENGINES` in `ports/fx/engines.mk`, and the `OP1_CLOUDS` / `OP1_MARBLES` / `OP1_GRIDS` defines in `ports/rings/Makefile`).

1. Add one data word to the image, e.g. `extern "C" volatile uint32_t op1_module_mask` in a new `ports/rings/op1/modules.{h,cc}`, default all bits set. Use one bit per switchable module: plate, hall, clouds, drive, tape, fold, crush, ensemble, shift, tape echo, Rings, Marbles, Grids. Put it in `.data`, not `.bss`, so it has a file image to patch.
2. **Effects:** in `ports/fx/op1/fx_host.cc`, make `NumEngines` / the orange-click order / `EngineFor` skip engines whose bit is clear. A slot saved on a disabled engine must play as stock: `EngineFor` returns null, and the screen shows stock.
3. **Rings / Marbles / Grids:** their orange-click handlers (`ports/rings/op1/input.cc`, the Marbles and Grids glue) do nothing when their bit is clear, so the stock engine or sequencer stays. Check the engine render hooks fall back to stock phase / tombola / pattern if a saved slot carries the marker of a disabled module.
4. **Screens:** always ship every SVG, even for disabled modules. A missing file at boot could assert, and they're small.
5. **Patching the mask without a compiler:** build `op1_seq.elf` once with everything (`make op1-seq FX_ENGINES="plate drive ensemble hall fold shift tape tape_echo"` in `ports/rings`, via WSL; the toolchain is `~/op1-toolchain/bfin`). Ship the ELF with the app: it contains only our code, never TE's. Add `--set-word op1_module_mask=0x...` to `tools/inject_app.py`, which finds the symbol in the ELF and patches it before injecting. This keeps one injection path. Record the ELF's sha256 in the app's manifest.
6. **Device test** (log it in `docs/device-tests.md` as Test 46): all on (= beta4); all off (= stock behaviour, apart from the renamed lists); a mixed set. Also load a slot saved on a now-disabled engine: it must play stock, and must not crash.

## Part 1: the builder

**Pipeline** (all of it exists; wrap it as a library):
1. **Input:** the user's stock `op1_246.op1`. Verify it is stock 246: unpack with op1repacker (`op1repacker-master/op1repacker/op1_repack.py`), then check `OP1_vdk.ldr`'s hash against the known stock one. Compute that hash now from `op1 firmware/op1_246/OP1_vdk.ldr` and put it in the manifest.
2. **Unpack** to a temp work dir.
3. **Inject** the shipped ELF with the module mask: `tools/inject_app.py STOCK_LDR OUT_LDR op1_seq.elf` (with the new `--set-word`).
   - The ELF's patch list includes the insn16 patch at `0x1a040ac`.
   - inject_app checks every stock word it replaces; keep that check.
   - Today's builds inject into a copy of `op1 firmware/work_fx2`, which is stock content plus our earlier SVGs. The app should start from the stock unpack and add every SVG itself.
4. **Content:**
   - Copy every module SVG into `content/display/`: `ports/fx/op1/{plate,hall,drive,tapesat,fold,crush,ensemble,shift,tapeecho}.svg` and `ports/{rings,clouds,marbles,grids}/op1/*.svg`.
   - **Never overwrite a stock file name.** The tape effect's screen is `tapesat.svg` because `tape.svg` is the stock tape screen. Refuse any clash (see `tools/package_fx.sh`).
5. **op1repacker mods** as features: `iter`, `presets-iter`, `filter`, `gfx-tape-invert`, `gfx-iter-lab`, plus `subtle-fx` and the cwo graphics (`gfx-cwo-moose/dog/cat`) as options. Call `op1repacker.main modify DIR --options …`, or better, import `op1_db` / `op1_gfx` directly.
6. **Factory preset edits** (Part 3).
7. **Package:** `tools/build_package.py WORK OUT.op1`. It refuses to overwrite an existing file, so remove any old output first: a stale package once shipped that way and didn't boot (fx9).
8. **Verify:** `tools/verify_bootloader.py` (PASS required), and `tools/pkg_check.py OUT.op1 WORK` (the package's contents must equal the work dir). Diff against stock: only `OP1_vdk.ldr`, the SVGs, `op1_factory.db` and the mod files may differ. Show the user the result.

**Manifests** (`app/manifests/*.json`), one per feature:
- id, name, category;
- description (copy the manual's wording);
- screen thumbnail(s);
- mask bit or op1repacker option;
- preset rows added;
- CPU class (light / medium / heavy, from the manual's CPU table);
- conflicts and warnings.

The resolver:
- **Preset budget:** the factory db may hold at most **176 non-sampler presets** (stock has 160; iter presets add 11). It also checks 42 sampler and 318 total. Exceeding a limit **hangs the boot**.
- **Warnings, not errors:** Rings with Clouds (keep Rings at polyphony ≤ 4); "presets-iter uses filter, so enable filter".
- **Dependency:** gfx-iter-lab only makes sense with iter.

**UI:** a module grid grouped as in the manual: synths (multi: phase → Rings; iter), effects by category (reverb, dirt, modulation, delay, plus filter), sequencers (Marbles, Grids), screens and mods (tape at the top, iter lab, cwo graphics), presets. Each card shows the device screen, the one-line description, a CPU tag and a toggle. A build bar shows the chosen set, the warnings and the output name, then the verification results, then the next step: "Copy `X.op1` to the OP-1 in te-boot upload mode (hold COM at power-on → 1. Upload firmware)". Include the recovery note and the "before going back to stock" warning from the manual.

## Part 2: the librarian (user presets, OP-1 disk mode)

**The OP-1 in disk mode** shows a USB drive with `synth/`, `drum/`, `tape/` and `album/`.
- **User presets** are `.aif` files in `synth/` and `drum/`, possibly in subfolders.
- **Settings:** each holds its settings as JSON in an AIFF `APPL` chunk. The chunk data starts with `op-1`, then the JSON (see `op1repacker-master/op1repacker/op1_patches.py:read_patch`).
- **Audio:** sampler and drum presets carry their audio in the AIFF's `SSND` chunk.
- **Verify on the device:** confirm the exact layout with the user's OP-1, and whether subfolders show on the device. Ask the user to plug it in. Read only until you know the format round-trips.

**The patch JSON** (from the factory db; user files should match; check):
- **Synth:**
  - `adsr[8]`, `fx_active`, `fx_params[8]`, `fx_type`;
  - `knobs[8]`, `lfo_active`, `lfo_params[8]`, `lfo_type`;
  - `name`, `octave`, `synth_version`, `type` (the engine);
  - sampler presets add `base_freq`.
- **Drum:** `drum_version`, `dyna_env[8]`, `end[24]`, `start[24]`, `pitch[24]`, `playmode[24]`, `reverse[24]`, `volume[24]` (one entry per key), `fx_*`, `lfo_*`, `name`, `octave`, `type` (`drum` or `dbox`).
- **The validator's rules**, from boot validation `0x1a037a8`; apply them to every edit:
  - exactly 8 knobs, each within that engine's ranges;
  - octave −4..4;
  - known `fx_type` / `lfo_type`;
  - names at most 13 characters, from a–z, 0–9, space and `-`.

  The full set is in `docs/next-projects.md` §6.

**Features:**
- Browse with search, folders and tags. Back up the whole OP-1 (synth, drum, tape, album) to a dated folder before any write.
- Rename, move, duplicate and delete, with an undo log.
- Import from op1.fun or other `.aif` collections, and export.
- **A readable editor:** show each preset as the device would, with names, not raw numbers. Each value is shown in its encoder colour, and the effect and LFO by name:
  - **Engine knobs:** use the manual's tables for the knob names (`manual/modules.html` for stock, `manual/user-manual.src.html` for ours).
  - **This firmware's effects:** a preset on `spring` / `punch` / `cwo` / `delay` whose `fx_params[4]` carries the engine marker shows as reverb → plate, and so on.
  - **The marker:** bits 12-14 = 6, bits 8-11 = engine id. The engine ids are in `docs/fx-categories.md`: reverb plate 1, hall 2, clouds 3; dirt drive 1, tape 2, fold 3, crush 4; modulation ensemble 1, shift 2; delay tape echo 1.
  - **Multi:** phase presets carry Rings' model and polyphony in `knobs[4..5]` (`docs/next-projects.md` §6, `ports/rings/op1/`).
  - **Flag** presets that use this firmware's effects or engines: "needs the custom firmware; on stock, switch it to the stock effect first".
- **Sample preview:**
  - Decode the `SSND` audio of sampler and drum `.aif` files.
  - Play it in the browser (Web Audio): the whole sample, and for drum presets each key's region from `start[k]..end[k]`, with `reverse` and `pitch`.
  - Draw the waveform with the regions on it.
  - **The unit of `start`/`end` is unverified:** the values reach ~10⁹ for ~470k-sample files, so they're likely sample index × a constant. Find it from a known file: export a drum preset with a known slice, or check Teenage Engineering's drum utility documentation.
  - **No engine simulation:** synth presets get no audio preview.

## Part 3: factory presets

- **Storage:** `content/op1_factory.db` (SQLite; firmware SQLite 3.6.20, so no WAL and no new tables). The firmware reads it in place at every boot.
  - **Tables:**
    - `synth_presets(id INTEGER PRIMARY KEY, patch TEXT, folder TEXT)`: 160 stock rows, 10 engines × 16, plus 9 sampler rows; beta adds 11 iter rows;
    - `drum_presets(id, patch, folder)`: `drum` 14, `dbox` 10;
    - `synth_types`, `fx_types`, `lfo_types`, `drum_types`, `seq_types`.
  - **The patch JSON is compact, with sorted keys and a trailing newline.** Match the stock rows' formatting exactly.
  - **Folders:** the factory synth folder for phase is `multi` in the current db. Confirm whether that's stock or ours before relying on it.
- **Audio:** factory sample audio lives in `content/audio/factory_synth/*.raw`, `factory_drum/*.raw`, and so on.
  - **Format:** headerless; the first bytes look like 16-bit little-endian mono, probably 44.1 kHz. Verify by playing one.
  - **Mapping:** how a factory sampler or drum preset names its `.raw` file isn't documented here; find out (the name field, or the row order).
  - **Preview:** with that, factory sample presets can be previewed too.
- **Editor:** the same editor as Part 2. Rows can be replaced in place (keeping their id and position) or appended, within the budget:
  - **Limits:** 176 non-sampler, 42 sampler, 318 total.
  - **Names:** unique per folder.
  - **Rules:** every row passes the validator rules above.
  - **Before packaging,** run `PRAGMA integrity_check` and diff against stock. **A bad factory row hangs the boot** (recoverable by reflashing a good package), so validate hard.
- **Flow:** factory edits are a build feature: the builder writes them into the work dir's `op1_factory.db` before packaging. Save each preset set as a JSON file the user keeps (`app/presets/*.json`), so builds are reproducible.
- **This firmware's effects and engines in factory presets:** allowed, but warn that these presets need the custom firmware (on stock, a slot saved on a new effect might stop the boot).

## Order of work and checkpoints

1. Copy the manifest facts: the stock hash, and the module list with mask bits.
2. **Part 0 firmware change**, then a device test. Stop and ask the user to flash; don't continue to UI work that depends on it until it's tested. Parts 2 and 3 can proceed meanwhile.
3. **Design:** brief a design agent for the app's screens on a new Claude Design canvas:
   - builder grid, build/verify panel;
   - librarian browser, preset editor (synth, drum), sample waveform preview;
   - factory preset editor;
   - settings (stock file, OP-1 location, backups).

   Use the manual's aesthetic and real device screens. **Get the user's review.**
4. **Backend:** build pipeline library + CLI. Its tests use the stock file in `op1 firmware/`, and one end-to-end build must reproduce beta4's content exactly (same SVGs, same db, same LDR given the all-on mask).
5. **Preset formats library:** AIFF read/write (APPL and SSND), patch JSON validation and the factory db editor.
   - **Tests:** round-trip every factory row, and every user file the user provides, byte for byte.
6. **UI**, from the approved design.
7. **Librarian against the real OP-1** in disk mode: read-only first. Then writes, after the user agrees, with backup and undo.
8. Docs: an app README, plus a section in the manual or a separate app guide in the same style.

## Facts and files you'll need

- **Build the image** (WSL):

  ```sh
  cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/rings
  make op1-seq FX_ENGINES="plate drive ensemble hall fold shift tape tape_echo"
  ```

  This produces `build/op1_seq.elf`.
- **Checks on it:**
  - `bfin-elf-nm -u` must be empty;
  - no `_GLOBAL__sub_I` symbols (no static constructors);
  - the image must end below `0x03000000`;
  - every hook target in the inject log must be a symbol: 50 hooks today.
- **Package recipe:** `tools/package_fx.sh NAME "ENGINES"` builds, injects, copies the SVGs (refusing stock names), packages, verifies and checks the package's contents. It copies from `op1 firmware/work_fx2` (stock content plus older SVGs); the app should build from the stock unpack instead. `tools/pkg_check.py PKG.op1 WORKDIR` compares a package with a work dir.
- **Mods on a work dir:** from `op1repacker-master/`, run `../.venv/Scripts/python -m op1repacker.main modify "<work dir>" --options iter presets-iter filter gfx-tape-invert gfx-iter-lab`.
- **SVG rules** (memory notes `op1-svg-parser-gotchas`, and `ports/fx/op1/gen_fx_svg.py`):
  - **Boot-fatal:** `_x` ids without a closing `_`, exponents in numbers, and malformed XML.
  - **Other rules:** one M per path, no group transforms, no arcs, and no stroke that doubles back.
  - The app's screen thumbnails come from these SVGs.
- **The manual's knob tables** are the source for the preset editor's parameter names: `manual/modules.html` (stock modules) and `manual/user-manual.src.html` (this firmware's).
- **Shell gotchas** (Windows + Git Bash):
  - heredocs mangle `\n` and backslashes, and backticks in `python -c "…"` get executed by bash;
  - write Python edit scripts to files with the Write tool;
  - use `.venv/Scripts/python`;
  - the bfin printf garbles `%lld`.

## Manual regeneration

```python
import base64, re
src = open('manual/user-manual.src.html', encoding='utf-8').read()
paths = {m: f'docs/module-briefs/designs/{m}.svg' for m in ('rings', 'clouds', 'marbles', 'grids')}
paths['iter'] = 'op1repacker-master/op1repacker/assets/display/iter-lab.svg'
sub = lambda m: 'data:image/svg+xml;base64,' + base64.b64encode(
    open(paths.get(m.group(1), f'ports/fx/op1/{m.group(1)}.svg'), 'rb').read()).decode()
open('manual/user-manual.html', 'w', encoding='utf-8', newline='\n').write(re.sub(r'\{\{svg:(\w+)\}\}', sub, src))
```

Then republish the artifact with the same file path, or with `url` https://claude.ai/artifact/C5nKBuHkfU8nR4RQYDRjKV from a new session (read it first).

## Open questions for the user (ask when they come up, don't block on them)

- Desktop window (pywebview or Tauri) or a browser tab on localhost?
- Should the app also fetch op1.fun presets, or only import files the user downloaded?
- The unit of drum `start`/`end`, and how factory sample presets map to `.raw` files: verify on the device or from files.
- For Part 0, the bit layout and whether "all off" should also rename the lists back to stock (spring, punch, cwo, phase). Renaming is a separate patch (`kTypeListQuery` in `ports/rings/op1/patches.cc`); it could become a bit too.
