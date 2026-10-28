# Roadmap: our modules as kennel plugins

Decided 2026-10-26. The user will flash **hundekraft 3.0.3** (`incoming/hundekraft/hundekraft-3.0.3.op1`)
on the OP-1 and we deliver our work as `.knl` plugin files for its kennel core, instead of our own
injected firmware. Our firmware branch (`ports/*/op1`, `tools/inject_app.py`, the module mask) stays
as the measurement lab and as the fallback; nothing of it is deleted. Licensing: whatever the SDK
requires is inherited (GPL-3.0-or-later with the OP-1 firmware permission). The builder/librarian
app and the click-based UX are parked until the user has used hundekraft's own UX.

Background and the comparison that led here: the 2026-10-26 session summary (the kennel assessment)
and `third_party/kennel/PLUGIN_GUIDE.md`.

## Facts that shape the plan

- **Hosting.** A kennel synth runs in the hidden iter slot (type 14), a kennel effect in the hidden
  filter slot (type 5); the saved sound selects it by name (`type_name`) and a legacy order word.
  Every plugin is a named entry in the stock list. No orange-click swap, no category hosts.
- **ABI.** Synth: `op1_engine_t` = `note_on(self, params)`, `render(self, out[128], params)`,
  `note_off(self)` on the voice object (0x24 bytes, 32 ours), stock T2 envelope applied after.
  The same vtable shape we hook on the phase voice today, so Rings' `RingsRender`/`RingsNoteOn` map
  1:1. Effect: `op1_fx_t` = `init(pos)`, `render(pos, buf[64], params)`; with `stereo: true` an
  extra `render_stereo(pos, l, r, params)` for the master. Positions 0 synth, 1 drum, 2 master,
  state per position. Features (sequencers, LFOs, pages, tape): `op1_feature.h` and the
  `op1_feature_*.h` services, dispatched by the core.
- **Clicks.** Only `OP1_PRESS_PAGE` (knob edits words 5..7 instead) and `OP1_PRESS_LATCH` (an unsaved
  toggle). Mode selectors become page-2 words or a companion feature.
- **Voice rate.** Device tests (`docs/device-tests.md` Tests 2-4: the 32-sample square at 2760 Hz,
  F3 at 0 cents with `inc / 2^32 × 88200`) show a voice render call produces 128 samples per
  64-sample effect block, i.e. 88.2 kHz, decimated by the core (kennel's own docs call the 64-sample
  bus "post-decimator"). The SDK header's `OP1_SAMPLE_RATE 44100` is wrong for synth voices; our
  Rings port already runs at 44.1 kHz and upsamples ×2 with a Lagrange filter, which is what we ship.
- **Memory.** Plugin arena 0x02980000..0x02C00000 (2.5 MB shared by every file, ~45 of hundekraft's
  own), 512 KB per file, files read into the top 512 KB at boot. FX scratch 176,460 bytes per
  position (shared with whichever effect is active there), `op1_alloc` for more, BSS is resident.
  Our full image is 404 KB of code/tables and 1.97 MB of buffers, so Clouds and Rings must be
  sized for the arena, not just for the file limit.
- **Toolchain.** Their packer compiles C with gcc 13.3.0 and `ld -r` into four sections; no
  `.init_array`, no libstdc++. Ours is gcc/g++ 14.2.0 in WSL (`~/op1-toolchain/bfin/bin`).
  `tools/knl_pack.py` compiles our C++ and `.S` with our flags and hands the relocatable object to
  their unmodified `pack.pack()`; `reloc.py` re-checks the file.
- **Delivery.** Plugins go to the OP-1 over USB MIDI SysEx (`third_party/kennel/tools/op1push.py`,
  or the workshop's "Your plugins → Update OP-1" in Chrome). Installed files survive a factory reset.
  Recovery is unchanged: a bad file is skipped at boot; a boot that didn't finish skips all files
  on the next one.

## Phases

### 0. Spike: one effect as a `.knl` (this session)
- Vendor the SDK subset (`third_party/kennel/`, done).
- `tools/knl_pack.py`: our packer driver (C++/.S, import check against `kn_exports.c`, self-check).
- `ports/knl/cs_crush/`: crush as a stereo FX plugin with its screen (our `crush.svg`, our screen
  helpers). Pure DSP, no scratch buffer, no clicks: the smallest thing that exercises compile,
  pack, load, audio on all three positions, and the screen.
- **Device (Test 50):** flash hundekraft 3.0.3; push `crush.knl`; it appears in the FX list as
  "crush", plays on synth, drum and master, screen animates, settings save and survive a restart;
  the OP-1 boots with the file removed.
- Decide from the result whether anything in the toolchain path needs changing.

### 1. The nine effects and Clouds
- Each `ports/fx/src/<name>` core gets a thin `ports/knl/<name>/` adapter: position state, our
  encoder normalisation replaced by `op1_fx_param_q15`, page 2 via `OP1_PRESS_PAGE` where we had a
  white click (drive's valve, the two-page effects), `OP1_PRESS_LATCH` for freeze.
- Half-rate effects (plate, hall, ensemble, tape echo) keep `half_rate.h`; delay lines move to
  `op1_fx_scratch(pos)` when they fit, else `op1_alloc` once.
- Clouds: replace hundekraft's own `clouds` by id (same saved name, their order 2 is theirs; ours
  stays order 0 and name-based). Looping delay + granular, the governor fed from the core's own
  load counters (`sm_aud_busy` is exported) instead of our `measured_cycles`.
- Screens: regenerate the SVGs without the category dots (`eng0..`), keep the 8 KB target.
- Factory presets: our `app/presets` entries as `.op1` preset files for the workshop's preset zip.

### 2. Rings as a synth plugin
- `op1_engine_t` over `RingsVoice`; model and polyphony as page-2 words (blue press → word 5
  model, green press → word 6 polyphony) instead of orange clicks; the screen as is, driven by
  `op1_synth_knobs_live`.
- Six voices at 88.2 kHz output: the upsampler stays. Memory: the six voice states, the mode bank
  and model 6's reverb sized against the arena; `OP1RINGS_L1_SCRATCH` stays (absolute L1 addresses
  are plain immediates to the packer).
- CPU re-measured under the kennel core (its `tick` and dispatch cost per block are unknown);
  the Rings + Clouds limit re-stated in the manual.

### 3. Marbles and Grids as sequencer features
- `op1_feature_t` with `.seq` = `op1_seq_type_t` (player object with the R246 ok_player vtable
  shape, screen in slot 0x2D, key/press/turn callbacks, `op1_seq_note_on/off`).
- The key transport (hold to run, shift latches) re-expressed through `key()`; settings in their
  own SQLite table through `sq_store_*` (what we never did on our firmware: RAM only).

### 4. Publish
- A `.knl` per module, source, control map, claims, notices, changelog; manual pages in
  hundekraft's manual format; offer them to the workshop catalogue.

## Open questions (answered by Test 50 and the hundekraft session)
1. Does the gcc 14 output pack and load on the 13.3-built core (relocation types, libgcc)?
2. The plugin screen: does the core draw after `update`, or does `update` call `op1_screen_draw`?
   (Checked in the plate reference plugin before writing crush's screen.)
3. What the master's mono-sum routing sounds like for a mono `.knl`, versus `stereo: true`.
4. The core's per-block cost with nothing of ours loaded (their sysmon page shows audio load).
