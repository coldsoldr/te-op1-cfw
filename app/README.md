# OP-1 Builder and Librarian

A local app for the OP-1 custom firmware: pick the modules you want and build a verified `.op1`; browse, back up, organise, edit and preview the presets on your OP-1 in disk mode; edit the factory presets that ship inside the firmware. Python backend (standard library plus the repo's tools and op1repacker), plain HTML/JS front end, no build step.

The screens follow the user manual's look. The design is on a Claude Design canvas: https://claude.ai/artifact/JzDmkgJX9AziWJp5NsR4Xi

## Run it

```sh
.venv/Scripts/python -m app.backend.server          # opens http://127.0.0.1:8765/ in the browser
.venv/Scripts/python -m app.backend.server --port 9000 --no-browser
```

Needs: the stock firmware `op1 firmware/op1_246.op1` (yours; never committed) and, for the factory tab and the tests, the stock unpack in `op1 firmware/op1_246/` (`op1repacker unpack`). Settings (stock file, output folder, OP-1 disk, backups) are in the gear tab and saved to `app/data/settings.json` (git-ignored, as are backups and the undo store).

## The command line

```sh
python -m app.backend.build --list
python -m app.backend.build --features rings,plate,clouds,grids --out "op1 firmware/my.op1"
python -m app.backend.build --all --out "op1 firmware/everything.op1"      # one cwo graphic (moose)
python -m app.backend.build --features ... --preset-set my-set --dry-run   # resolve only
python -m app.backend.build ... --json --keep-work                         # the full report, keep the work dir
```

The build: verify the stock package (OP1_vdk.ldr sha256 against `app/manifests/image.json`) → unpack → inject `app/image/op1_seq.elf` with the module mask and the list names (`tools/inject_app.py --set-word`, `--set-string`: a stock type reads as its category, multi / reverb / dirt / modulation, only while one of the category's modules is on; with nothing on the lists read like stock) → copy every module screen (refusing stock names) → op1repacker mods → rename the phase preset folder to multi (with Rings on) → apply the preset set → check the factory database (budget, every row through the validator, rollback-journal format, integrity) → pack without `te-boot.ldr` → `verify_bootloader` → package = work dir → diff against stock (only the LDR, the screens, the db and the chosen mods may differ). Any failure removes the package. It refuses to overwrite an existing file.

## Layout

- `backend/`: `build.py` (pipeline + CLI), `manifests.py` (features, resolver, budget), `firmware.py` (packages: decode, unpack, pack, verify, compare), `mods.py` (op1repacker options, folder rename, db format check), `factorydb.py` (factory rows, preset sets), `patch.py` (the patch JSON: canonical text, validator, readable description), `knobs.py` (names: engines, effects, categories, LFOs, drum keys, the marker, the drum unit), `aiff.py` (preset files: APPL JSON and SSND audio, round-trip exact), `device.py` (the OP-1 disk: find, scan, back up, Librarian with undo), `server.py` (the local HTTP API and static UI), `paths.py` (where things are, settings).
- `manifests/`: one JSON per feature (id, group, category, description, screens, mask bit or op1repacker option, preset rows, CPU class, requires, conflicts, warnings) and `image.json` (the ELF's sha256, the bit layout, the stock hashes, the preset limits).
- `image/op1_seq.elf`: the one image every build uses; our code only. Rebuild with `make op1-seq FX_ENGINES="plate drive ensemble hall fold shift tape tape_echo"` in `ports/rings` (WSL), then regenerate the manifests' hashes (see `docs/librarian-handover.md`).
- `ui/`: `index.html`, `app.css`, `app.js` (shell, API, helpers), `builder.js`, `librarian.js`, `editor.js` (synth and drum editors, sample preview), `factory.js`, `settings.js`.
- `presets/`: your preset sets (`NAME.json`: a list of replace / append / delete changes to the stock factory rows), reproducible inputs to a build.
- `tests/`: `python -m unittest app.tests.test_manifests app.tests.test_presets`.

## The rules it enforces

- Never writes the bootloader: `te-boot.ldr` is never packed, and every package passes `tools/verify_bootloader.py` or is deleted. No `FRC` in a file name, no `.KEY` files.
- Never writes to the OP-1 itself: you copy the package in te-boot upload mode (hold COM at power-on → 1. Upload firmware).
- In disk mode: reads freely, writes only after a backup of the whole disk in this session, one change at a time, each undoable (`app/data/undo/`).
- A preset's name is its file name (the OP-1 lists the files): a rename rewrites the name inside, an edit of the name renames the file. The user slots (`synth/user/N.aif`, `drum/user/N.aif`) keep their numbers.
- The factory tab shows the stock rows and, in an `iter` folder, the rows the iter presets feature adds at build time (read-only; copy one as a new row to edit a variant).
- Every table sorts by any column: click a header, click again to flip (text case-insensitively, numbers numerically, blanks last).
- **Copy factory presets to the OP-1** (Librarian, after a backup): every stock factory row becomes a user preset on the disk, in a folder named after its engine (`synth/digital/…`, `synth/sampler/…`, `drum/drum/…`, `drum/dbox/…`), with its factory audio for sampler and drum rows and a silent clip the length of the device's own for the others (the OP-1 writes every preset with an SSND chunk). Files already on the disk are kept; one Undo removes all of it. The firmware's own rows stay: to drop those, delete them in the Factory tab and build.
- A factory row that fails the validator, or a budget over 176 non-sampler / 42 sampler / 318 presets, stops the build: on the device it would stop the boot.

## Known limits

- The validator checks every knob against the full −32768..32767 range; the per-engine ranges of the firmware's table aren't transcribed, so an out-of-range engine value can only be caught on the device.
- The drum key pitch unit is unverified (the preview assumes 512 per semitone); start/end are verified (12 s = the int32 range, 4058 per sample at 44.1 kHz).
- Synth presets get no audio preview (no engine simulation, by design); sampler and drum presets play their stored audio.
- The desktop window option (pywebview) is a setting only; the app runs in a browser tab.
