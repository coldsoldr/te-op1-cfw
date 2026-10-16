# Next projects: Clouds, more engines, a builder app, op1repacker mods, Rings presets

Specced 2026-10-16 from static analysis of firmware 246, the Mutable Instruments sources and op1repacker. Nothing here has been tried on the device. Tags:
- **[C]** confirmed from code or data;
- **[I]** inferred.

Addresses are runtime addresses in `OP1_vdk.ldr`. The hard rules in `docs/brick-safety.md` and `docs/HANDOVER.md` §1 apply to all of it.

**Status (2026-10-16):** §2 is done (the MULTI engine, `op1_246_rings_multi4.op1`). **Clouds (§1) is the next project**: `docs/HANDOVER.md` §3 has the plan and the decisions to settle first.

## Summary

| # | Question | Answer | Size |
|---|---|---|---|
| 1 | Clouds as an effect | **Yes, plausible.** Effects are patchable like phase was. Three of Clouds' four modes should fit in ~35–70 MHz on the device; the spectral mode needs an assembly FFT (last, or never) | Large: bigger than Rings |
| 2 | Add Rings instead of replacing phase | **Yes, but not as a new engine type.** A new type name in a saved slot hangs stock firmware at every boot. Host it on an existing type instead (§2) | Medium |
| 3 | How many engines | Real new types: hard (tables to move, the screen table is full) and unsafe. Hosted on an existing type with a marker: **as many as CPU and memory allow**, one playing at a time | — |
| 4 | Companion app | Feasible. Space isn't the limit (~13 MB free for code); slots, the preset count (176) and conflicts are. A build app with a feature checklist, or a runtime menu | Medium (later) |
| 5 | op1repacker mods | **Easy and safe:** content-only (SQLite rows, SVG edits), no LDR patches, so no overlap with ours. Re-implement in our own `tools/mods.py` | Small |
| 6 | Rings factory presets | Rows in the package's `content/op1_factory.db`, stored as `"type":"phase"` with model and polyphony in `knobs[4..5]`. At most **176** non-sampler factory presets (stock 160); one more hangs the boot | Small (plus sound design) |

**Decisions the user needs to make** (they interact):
- **iter.** op1repacker's main mod makes the hidden iter engine playable, and iter is also the best spare host for Rings. Choose: iter as a stock engine, or as Rings' home (§2).
- **filter.** Same for effects: op1repacker enables the hidden filter effect, which is also a candidate host for Clouds. If the filter mod goes in, Clouds should replace **phone** instead (§1).
- **Preset budget.** Only 16 preset places are spare: op1repacker's iter presets take 11, leaving 5 new Rings presets beyond the 16 phase ones that can be replaced (§6).

## 1. Clouds as an effect

### The OP-1's effects [C unless marked]
- **8 effect types** (`fx_types.cpp`; table `0x18dd810` of `{name, id}`):

| id | name | module | screen | process (mono / stereo) |
|---|---|---|---|---|
| 0 | delay | Dssd.cpp | 8, delay.svg | `0x197d8c4` / `0x197dc42` |
| 1 | grid | Grid.cpp | 9, grid.svg | |
| 2 | punch | Mllp.cpp | 10, mllp.svg | |
| 3 | phone | Ptch.cpp | 14, ptch.svg | `0x1a1bb74` / `0x1a1c25c` |
| 4 | spring | Rymd.cpp | 15, rymd.svg | `0x1a326e8` / `0x1a32ffc` |
| 5 | filter | Mmmf.cpp | 11, mmmf.svg | `0x19e7934` / wrapper |
| 6 | nitro | Ftwo.cpp | 12, ftwo.svg | |
| 7 | cwo | BodeAudioEngine.cpp | 13, bode.svg | `0x194480c` / `0x19450fc` |

- **filter is hidden:** the factory db's `fx_types` table has no row for it, so the menu doesn't offer it [I, strong]. op1repacker's `filter` mod adds the row. phone appears in 3 factory presets, always inactive.
- **Positions:** 0 = synth, 1 = drum (both mono, sharing one object at `0xffb00600`, so only the live instrument's effect exists), 2 = master (stereo, at `0xffb00790`).
- **Factories:** stereo `0x1a1b5f0` (`type < 8`), mono `0x1a1b6d0`. The object's vtable has `+0x14` process and `+0x1c` init(this, pos).
- **Process ABI:**
  - mono: `process(this, int16* buf, const int16* params)`, **in place, 64 samples**;
  - stereo: `process(this, int16* L, int16* R, [SP+12] params)`.
  - **44.1 kHz:** delay lengths are `44100 >> n`, and its buffer is 1 s at 44.1 kHz. 64 samples at 44.1 kHz match one 128-sample voice block at 88.2 kHz [I].
- **Parameters:** 8 int16 per position (synth `0xff801444 + slot × 16`, drum `0xff8015c4 + inst × 16`, master `0xff8016c4`), clamped by the FX Ranges table `0x1de5c50` (48 bytes per type). Saved in presets as `fx_type` (by name), `fx_params[8]` and `fx_active`; master FX in op1.db's `master` table. **`fx_params[4..7]` are stored but unused by stock effects:** room for Clouds' mode and settings [I].
- **All 16 process functions are plain SDRAM** and none calls into L1: every effect can be redirected with vtable word patches, like phase.
- **Buffers:** 3 × 176 KB per-position buffers (`0x1ab8bb0` synth, `0x1ae3cfc` drum, `0x1b0ee48` master), plus phone's private 187 KB array at `0x27d1fe0`. Our own region at `0x02c00000` has more room and is safer.
- **CPU:** unmeasured. The engine's 81% free (probe v3) excludes FX.

### Clouds' cost (estimates from the code, not measured)
Clouds runs at 32 kHz in 32-sample blocks; its correlator and FFT frames run in the idle loop (`clouds.cc:133`). On the OP-1 that work has to be split into slices in each call.

| Mode | Upstream work per stereo sample | Blackfin estimate (device, 44.1 kHz) | Fixed-point difficulty |
|---|---|---|---|
| Shared chain (feedback filters, diffuser, reverb, mix) | ~175 float ops, 4 divisions | ~25 MHz | Easy: the reverb is Rings' (`reverb_fixed`), the soft limit is Rings' table |
| Looping delay + pitch shifter | ~100–150 | ~35 MHz in total | Easy |
| Granular (32 grains; the first quarter Hermite, the rest linear) | ~1,000 | ~70 MHz (16 grains: ~45) | Moderate: phases are already 16.16, buffers int16; assembly overlap-add kernels |
| Time stretch (WSOLA) | ~65, plus an integer sign correlator | 45–70 MHz (search can be capped) | Moderate: the correlator is already bit operations (Blackfin `ONES`) |
| Spectral (4,096-point FFT phase vocoder) | ~700, in bursts | ~70 MHz with an assembly FFT; >120 MHz in C | Hard: fixed-point FFT, atan2, huge dynamic range |

At ~29.4 kHz (3:1 from 88.2) or 32 kHz everything costs ~0.7× as much. Grain count and correlator search are graceful quality knobs.

**Memory:** upstream uses 184 KB (1 s of stereo at 32 kHz). Free SDRAM is not a constraint (2 × 256 KB gives ~3 s at 44.1 kHz). The 16 KB data cache is: 32 grains read 64 scattered streams, so the device/simulator ratio may be worse than Rings' 1.6×. Keep the diffuser and block accumulators in L1.

### Recommendation
- **Host:** the **master FX** (stereo, as Clouds is), replacing **phone** (visible, unused in factory presets, has its own screen and buffer):
  - mono process/init `0x1911308` / `0x1911310`;
  - stereo `0x1911328` / `0x1911330`;
  - screen: "ptch.svg" immediates at `0x1a1c7cc`/`0x1a1c7d0`, PhoneScreen Init/Update words `0x1911358`/`0x1911350`.
  - Use **filter** (`0x1910ca8`/`0x1910cb0`, stereo `0x1910cc8`/`0x1910cd0`) only if op1repacker's filter mod isn't wanted.
- **Order:**
  1. Shared chain and looping delay.
  2. Granular (the mode Clouds is known for).
  3. Time stretch.
  4. Spectral last or never.
- **Controls:** Clouds has 9 parameters plus freeze and trigger (`parameters.h:36`). Four encoders, shift fine-adjust and encoder clicks as for Rings (FX page key hooks still to find). Mode, quality and blend in `fx_params[4..7]`.
- **Work outline** (as for Rings):
  1. `ports/clouds/` with a bit-exact host baseline from `clouds/test/`.
  2. Fixed-point chassis (buffers, soft limit, filters, diffuser, `reverb_fixed` reused).
  3. Looping delay, then grain kernels in assembly, then the correlator in assembly.
  4. The OP-1 hookup, the CPU meter, device tests.
- **First device probe** (no Clouds code): a pass-through effect on phone that counts calls per second and measures its own cycles. It answers:
  - the rate and block size;
  - whether process runs while `fx_active` = 0;
  - pre- or post-mix with the drums;
  - how much CPU is left with 6 voices plus an effect.
- **CPU together:** Rings model 6 with 4 voices (~73 MHz simulated, ~120 MHz on the device) plus Clouds (~35–70 MHz on the device) is ~155–190 MHz of the ~324 MHz free, before the OP-1's own effects and tape. Expect to need grain and quality limits.

## 2. Adding Rings without replacing phase

**Done (2026-10-16, `op1_246_rings_multi1.op1`, Test 12):** route 2 below, with an orange click toggling phase and Rings and the drawing swapped per engine; "phase" shows as "multi" in the engine browser (a patched list query) and the preset browser (the factory folder). See `docs/HANDOVER.md` §2.

### A real 16th engine type [C]
Everything indexed by engine type would need extending:
- **Factory:** `0x1a1b7d4`, `R0 = 0xf` at `0x1a1b7dc` (a 16-bit opcode: `inject_app.py` needs a halfword patch kind), jump table `0x18f5a04` (15 words, followed by a string: must move to our image, imm16s at `0x1a1b7ea`/`0x1a1b7ee`).
- **Type names:** `{name, id}` table `0x1dd4d98` (15 entries, iter state right after it: relocate). Read by `IsValidType` `0x19b97c4` and `NameToId` `0x19b9810` (loop counts `0xf`).
- **Screens:** `ScreenId` `0x1a3bf44` (table `0x18f803c`, 15 entries). **The screen table `s_screens` (`0x1df4e54`, 68 entries) is full:** a new screen must share an existing id (swap the SVG document inside a hooked Update) or extend the table (patch `Screen::Screen`, `GetScreens`, `LoadAll`, `UpdateAll`).
- **Ranges:** `0x1de5890 + type × 64`; type 15 would read the FX Ranges table (`0x1de5c50`). Relocate and patch 7 readers.
- **Picker:** no fixed table. The engine list is the `synth_types` table in the package's `op1_factory.db` (10 rows; sampler separate; iter has none). A new engine needs a row.
- Per-type byte tables are safe up to type 25; the compare immediates allow up to 63.

**Why not:** presets and slots store the engine as a **string** (`"type":"phase"`). Stock firmware validates the 8 saved slots at boot (`0x1a05454` → `0x1a037a8`), and an unknown name fails `patch.cpp:390` with a **fatal assert** (screen message, then an IDLE loop) **at every boot**. That happens if the user ever goes back to stock firmware with a Rings slot saved. Recovery would be reflashing our firmware or a te-boot factory reset or format (losing user data). User presets imported over USB are only rejected, not fatal.

### Safe routes: host on a type stock firmware knows
1. **Rings on iter (type 14).** iter is a complete hidden engine:
   - factory case `0x1a1b99e`, screen 20 `iter.svg`, voice 36 bytes;
   - patch its vtable render `0x19103e0` (stock `0x019b99d4`) and note-on `0x19103e8` (stock `0x019b9b8a`);
   - screen: imm16s at `0x19ba338`/`0x19ba33c`; IterScreen Init/Update words `0x1910420`/`0x1910418`;
   - the key hooks then check type 14;
   - add a `synth_types` row (as op1repacker's `iter` mod does).
   
   Stock phase comes back. On stock firmware a Rings slot plays as iter: harmless. **This conflicts with op1repacker's iter mod.**
2. **A multi-engine host on phase** (recommended if iter stays a stock engine):
   - our hook keeps the phase slot, and a marker in `knobs[6]` (stock phase never writes it; range 0..32767) picks the engine: 0 = **stock phase** (call its own render `0x1a0bc5c` and note-on `0x1a0be82`: our voice objects are real Pd objects), 1 = Rings, later 2 = Plaits, 3 = Elements, and so on;
   - the screen hook switches between `pd.svg` (stock PdScreen Init/Update) and our SVGs;
   - selection by an encoder click on the engine page, or by presets in their own folder;
   - on stock firmware, every one of these slots plays as phase: safe.
   
   The picker still says "phase". Renaming its label is a small content/patch change to find.
3. **Replace another engine** the same way as phase. Every engine's render is plain SDRAM except pulse (`0xffa04b88`, L1), and every vtable is plain data, so even pulse can be redirected. It costs a stock engine each time.

## 3. How many engines

| Limit | Real new types | Hosted with a marker |
|---|---|---|
| Tables | Relocate ~6 tables; screens full; boot-crash hazard on stock | None |
| Engine ids | up to 25 without more work (byte tables), 63 by immediates | One host type carries many |
| Voice objects | L1 scratchpad `0xffb00000`: 6 × ≤ 108 B used; keep ≤ 256 B (our 28 B; state in SDRAM) | Same |
| CPU | One synth engine is active at a time (one `producers` array, `0x1aaad18`) [C]: each engine has the whole budget | Same |
| Code space | App LDR room on NAND ≈ 15.5 MB (`docs/app-boot-chain.md`), stock 2.2 MB: **~13 MB** free. Rings' code and tables are ~90 KB | Same |
| SDRAM | ~3.3 MB left in region A (`0x02c00000`–`0x02ffffff`, in use since probe v1) above our image; regions B (`0x02800000`–`0x02bfffff`, 4 MB) and C (`0x01e00000`–`0x01ffffff`, 2 MB) rated "good" in `docs/memory-map.md` §6 but untested | Same; Rings uses ~670 KB |
| Presets | 176 non-sampler factory presets in total (§6) | Same |

**Practical answer:** the number of engines is bounded by memory (3.3 MB proven free, ~9 MB with regions B and C once tested: several Mutable Instruments engines fit) and by the effort to port each one, not by the firmware, if they're hosted on phase with a marker. Good candidates: **Plaits** (16 models, the most OP-1-like), **Elements** (heavy: modal + exciter + reverb, like Rings ×2), **Braids**, Tides as an LFO source. Each costs a fixed-point port like Rings'.

## 4. A companion app (later project)

Space is not the real constraint (~13 MB of code room; Rings is ~90 KB). The constraints are:
- **slots:** one host per engine type, one per effect type, and the iter/filter conflicts above;
- **the preset count:** 176;
- **the screen table** (full);
- **CPU:** heavy combinations, e.g. Clouds plus model 6, may need limits;
- **safety:** every build must pass the bootloader checks and the stock diffs.

Two shapes, not exclusive:
1. **Build app (desktop):** a checklist of features (op1repacker mods, engines, effects, preset packs) → a verified `.op1`.
   - Each feature is a manifest: LDR patches with stock values, content edits, the slots it occupies, preset count, conflicts, CPU class.
   - A resolver rejects impossible combinations, then runs our existing pipeline: `inject_app.py`, `mods.py`, the preset tool, `build_package.py` and `verify_bootloader.py`, and the stock diffs.
   - The user still flashes through te-boot. A small local web UI or Python GUI over the same tools.
2. **Runtime menu:** since space isn't scarce, ship everything and let the user pick on the OP-1. The engine marker of §2 already does this for engines, and effects could take a mode in `fx_params[4..7]`. Needs a settings page or an encoder-click menu; no reflash to change.

Start with the manifests and the resolver as a CLI (`tools/build_firmware.py --features rings,clouds,tape-top`); the GUI is a thin layer later.

## 5. op1repacker mods

op1repacker 0.2.6 (`op1repacker-master/`, MIT, op1hacks) **never patches `OP1_vdk.ldr`**. Every mod edits content [C]:

| Mod | Change |
|---|---|
| `iter` | `INSERT INTO synth_types VALUES (11,'iter','[1516,16704,0,15168,0,0,0,0]')` in `op1_factory.db` |
| `filter` | `INSERT INTO fx_types VALUES (2,'filter','[7548,0,8272,19572,8000,…]')` |
| `subtle-fx` | `UPDATE fx_types SET default_params` for 8 effects (gentler defaults) |
| `presets-iter` | 11 community iter presets into `synth_presets` (folder `iter`); 5 use the filter effect |
| `gfx-iter-lab` | replaces `content/display/iter.svg` |
| `gfx-tape-invert` ("tape at the top") | regex edit of `tape.svg`: moves the tape tracks to the top |
| `gfx-cwo-moose` / `-cat` / `-dog` | regex edit of `bode.svg` (the CWO animal) |

- **Firmware 246:** no version checks in the tool (tested on 235). A dry run on a copy of stock 246 content applied every mod cleanly, and a package built from it with `build_package.py` passed `verify_bootloader.py`. iter and filter exist in the 246 app (names, `Iter.cpp`, `iter.svg`).
- **Safety:** `op1repacker modify` is safe. **`op1repacker repack` is not:** it packs everything, including `te-boot.ldr`, with no checks. Never use it for our packages. The packing format is the one `build_package.py` already uses (it imports op1repacker's functions).
- **No overlap** with our patches (LDR words, `rings.svg`) [C].
- **Reverting to stock:** a slot using iter or the filter effect on stock firmware (whose db has no row for them) takes a lookup fallback (`0x1a02384` → `0x1a02030`) that hasn't been traced [I]. Move slots off iter/filter before flashing stock.
- **Licensing:** the tool and graphics are MIT and credited, but **the iter presets ("community presets from op1.fun") carry no licence:** fine for personal builds; credit and ask before publishing. The patched SVGs derive from TE files: generate them at build time, don't commit them.

**Plan: `tools/mods.py`** (our own, staged like `build_package.py`):
1. Copy the stock tree to a work tree.
2. Check each input's SHA-256 against stock 246 and refuse otherwise.
3. Apply the db edits as explicit SQL (ids asserted free first, presets in sorted order).
4. Apply the SVG edits through op1repacker's `op1_gfx.apply_patch` with svg.path pinned, checking pinned output hashes.
5. Then `rings.svg`, `inject_app.py`, `build_package.py`.

The stock-diff allow-list grows to: `OP1_vdk.ldr`, `rings.svg`, `op1_factory.db`, `tape.svg`, `bode.svg`, `iter.svg`.

**Oracle test:** `op1repacker modify` on a temp copy; SVGs byte-identical, db rows equal (`iterdump`).

**Device test:** iter appears and plays; filter works; iter presets load; tape at the top; the CWO animal; Rings still on phase.

## 6. Rings factory presets

- **Storage** [C]: factory presets are rows in `content/op1_factory.db`, table `synth_presets(id, patch TEXT, folder TEXT)`. There are 169 rows: 10 engines × 16 (ids 1–160; phase is 97–112) plus 9 sampler rows. The firmware reads this file **in place at every boot** (`Patch::LoadAll` `0x1a059d0`, rowid order) and never copies it.
- **User data** (`/yaffs2/user/op1.db`: `synth_user`, `synth_slots`, …) is never overwritten by a firmware update. `.aif` files are only the USB view of user presets.
- **The patch JSON:** compact, sorted keys, trailing newline:
  - keys: `adsr[8]`, `fx_active`, `fx_params[8]`, `fx_type`, `knobs[8]`, `lfo_active`, `lfo_params[8]`, `lfo_type`, `name`, `octave`, `synth_version`, `"type":"phase"`;
  - knobs[4..7] are saved and restored, which carries our model and polyphony.
- **Boot validation** (`0x1a037a8`) checks, among other things:
  - exactly 8 knobs, each within the Ranges table (phase: 0..32767, **brightness ≤ 29491**);
  - octave −4..4;
  - known fx and lfo types;
  - names at most 13 characters (a–z, 0–9, space, `-`).
- **Limits (fatal asserts, `patch.cpp:613`):** **176 non-sampler factory presets**, 100 user presets, 42 sampler presets, 318 patches in total. A bad or extra factory row **hangs the boot** (recoverable by reflashing a good package).
- **The browser groups by folder string**, factory folders first: a `rings` folder shows as its own group [I: position to confirm].
- **No db checksum** [C]. SQLite 3.6.20 in the firmware reads the files Python writes (format 4, rollback journal; no WAL, no new tables).

**Plan: `tools/add_rings_presets.py`**, driven by a JSON list (name, structure, brightness, damping, position, model, polyphony, envelope, fx, lfo, octave):
- start each row from a stock phase row;
- write `knobs = [s, min(b, 29491), d, p, model × 4096, poly × 4096, 0, 0]`;
- refuse anything outside the validator's rules;
- either replace ids 97–112 (16 presets, keeping phase's place in the list) or insert up to 16 more in folder `rings`, asserting ≤ 176;
- run `integrity_check` and diff the rows against stock.

Optionally also put one Rings sound in `content/op1.db` slot 6 (used only on a wiped disk) and change phase's `synth_types` default to a nicer starting sound.

**With op1repacker's iter presets** (11 rows), only 5 new rows remain: replace the 16 phase rows instead, or drop some iter presets.

**Sound design:** the presets need sounds. Either the user designs them on the device and exports them (USB disk mode: `synth/` `.aif` files, from which the tool can read the JSON), or we start from a set of named starting points per model and the user tunes them.

**Device test:**
- it boots with the new rows (try 16 added, then the replace variant);
- where the `rings` folder shows;
- loading a preset sets the model and polyphony on the next note;
- saving one as a user preset and rebooting keeps knobs[4..5];
- after flashing stock, user-saved Rings presets load as phase.

## Suggested order

1. **op1repacker mods** (`tools/mods.py`) and **Rings presets** (`tools/add_rings_presets.py`): small, content-only, and they settle the preset budget. Decide iter/filter first.
2. **Multi-engine host on phase** (stock phase as engine 0, Rings as engine 1): gives phase back without a new type. Rings presets then carry the marker.
3. **Clouds probe** (a pass-through effect on phone measuring rate and CPU), then the Clouds port in stages.
4. **Build tool** with feature manifests; a GUI later.
