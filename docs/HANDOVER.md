# Handover: OP-1 custom firmware

**Latest (2026-10-28, later): Marbles and Grids built as kennel sequencer features (Test 62 awaiting
the install).** `ports/knl/marbles/` and `ports/knl/grids/` → `build/knl/marbles.knl` (142 KB) and
`build/knl/grids.knl` (28 KB): `make marbles grids` and `make host-test` in `ports/knl` (WSL). Each is
a kind-3 feature with `.seq` set (`op1_feature_seq.h`): a named entry in both instruments' sequencer
lists, its own player (`common/knl_seq_player.c`, the ok_player vtable in C), the firmware build's
screen (`marbles.svg` / `grids.svg`, category dots stripped) in finger's slot, settings in the op1.db
tables `marblesseq` / `gridsseq` through `sq_store_*` (`seq/seq_text.h` vendored into
`third_party/kennel/synth_sdk/seq/`). The cores are the firmware build's (`ports/marbles/op1/marbles_seq.*`,
`ports/grids/op1/grids_seq.*`), one per mode (so Marbles also runs a kit and Grids also plays notes);
the key transport of Test 36 is unchanged; the clicks became the type's presses (orange is free;
shift + orange on grids = print). Host tests (`ports/knl/tests/`, the core's services stubbed) pass:
the vtable, the latch and take-over, presses, turns, the settings text round trip. The control map
and what to check: `docs/kennel-plugins.md` "Sequencer features", `docs/device-tests.md` Test 62.
Unverified on the device: the finger-slot tick counter's phase, the key_down shift flag, print under
the grids type. **After Test 62: better graphics for some of hundekraft's own plugins** (the user's
ask; which ones is open). Nothing committed since 10b642d.

**Latest (2026-10-28): tape_echo rebuilt (Test 61 awaiting install), tape_drive renamed.** The user chose
design B "Schematic" from the canvas "Tape echo screen" (https://claude.ai/artifact/KAu8e82XAZyk9QAYPJ6sdK):
the Space Echo's signal path with the heads sliding with the time. **Control map:** blue press = the
input gain (word 5), white press = the head pattern (word 7, was blue's), green latches hold; **new:
shift + blue on the time page locks the time to the project tempo's divisions** (1/16T .. 1/4, a click
each; `TimeLock` in `ports/knl/tape_echo/tape_echo_knl.cc`; the tempo from `knl::TempoIncrement()` in
`knl_fx.h`, the stock LFO tempo word `[0x1aadad4]`, 120 BPM if it reads nonsense). **Round 1 found:
a page-1 word can't be rewritten** (the stock knob keeps its own copy and writes it back on the next
click; page-2 words can, `knl::Fine`), so round 2 keeps the lock in the plugin (the core gets the
division's time in place of the word; not saved with the sound). **Round 2 on the device: "sounding
great, the tempo syncs nicely"**, so the stock tempo address holds on hundekraft's core. Round 3 adds
1/2T, 1/4. and 1/2 (skipped where they exceed 1.5 s): **on the device, "great" (Test 61 done)**. Install
lesson: replacing a plugin in place hung the boot ("kennel: updated", black; safe start = power off within
seconds of the first screen); remove, restart, then add. The core gained `TapeEchoTimeSamplesQ4` / `TapeEchoTimeWord`
(the time table and its inverse); `knl_glyphs.py` gained '/'. Renamed plain `tape_echo` (was cs_tape_echo)
and `tape_drive` (was tapesat), folders, Makefile, docs; `build/knl/tape_echo.knl`, `tape_drive.knl` built.
`docs/fx-briefs/designs/tape-echo-schematic.md`. **The UI round is done (decided 2026-10-28):** plate, hall
and the two Clouds plugins stay as they are, uninstalled (hundekraft ships its own plate, hall and
Clouds); their folders keep the `cs_` ids. **Next: Marbles and Grids as kennel sequencer features**
(roadmap phase 3; `docs/op1-sequencer-decisions` in memory, `docs/marbles-port.md`, `docs/grids-port.md`;
the tempo word `knl::TempoIncrement()` is proven on the device). **After that: better graphics for some of
hundekraft's own plugins** (the user's ask; which ones is open). Committed 2026-10-28.

**Latest (2026-10-27, end of session): freq_shift done (Test 60, "looks great").** The user chose
design B "Dial" from the canvas "Freq shift screen" (https://claude.ai/artifact/4JwECGQ5XeJKoNRp7mLViS):
shift as a needle on a bipolar arc spanning the width. **Control map changed:** blue press = the gain
(word 5, the wet path's analytic signal, `ShiftParams::gain`), green press = the mode (word 6, a green
bracket round the mode marks while pressed). Renamed `freq_shift` (was cs_shift). **Proven on the
device: the renderer's transform is a full affine** (the needles rotate, `RotateAbout` in
`shift_knl.cc`); rotation is available to every screen now. Also: the spread arc shows on every
position and in ring mode; **sonar ripples** on sound (the render meters the input, four arcs scaled
from the hub to the rim and faded, `rp1..rp4`), the user's ask. `ports/knl/freq_shift/`,
`docs/fx-briefs/designs/freq-shift-dial.md`.

**The UI round so far (all on the device, all good):** rings (Test 53), bitcrusher (54-55), drive (56),
tapesat (57), wavefolder (58), ensemble (59), freq_shift (60). Each has: its own screen from a
generator in its plugin folder (design chosen on a canvas), the family frame (blue gain gauge x 16,
orange mix gauge x 304, rows 28..124, readout top left), an input gain on blue's page 2 with
`knl::Fine` tenths, and a plain id (hundekraft's own ids avoided). **Remaining:** tape echo (installed,
the firmware screen still), then plate, hall, cs_clouds, cs_clouds_delay (rebuilt with the shift fine
steps, not yet installed: Test 55 points 5-6). Nothing committed since f534fe4: commit before the next
big piece.

**Earlier (2026-10-27, late): cs_ensemble rebuilt (Test 59).** The user chose design C "Drift" from the
canvas "Ensemble screen" (https://claude.ai/artifact/DzykGtr3epWihZ7qJEUfoM): each voice a string
swaying about its home mark, depth ticks, the dial at the right, the gain on blue's page 2 (the wet
path's input, scaled in the plugin). `ports/knl/ensemble/gen_ensemble_knl_svg.py`, `ensemble_knl.cc`,
`docs/fx-briefs/designs/ensemble-drift.md`; renamed plain `ensemble`, on the device, **Test 59 good**. Next: shift,
tape echo.

**Earlier (2026-10-27, late): cs_fold rebuilt (Test 58).** The user chose design A "Wave, aligned" from
the canvas "Fold screen" (https://claude.ai/artifact/CscKQMR3VBtoGF1GQaTpNN): the folded wave without
the box, the family gauges, the gain on blue's page 2 (`FoldParams::gain`, the fifth smoothed knob).
`ports/knl/wavefolder/gen_fold_knl_svg.py`, `fold_knl.cc`, `docs/fx-briefs/designs/fold-aligned.md`;
renamed `wavefolder` (was cs_fold), on the device, **Test 58 good**. The dirt family is done. Next: ensemble, shift, tape echo.

**Earlier (2026-10-27, late): tapesat built (Test 57).** The user chose design A "Reels, aligned" from
the canvas "Tape screen" (https://claude.ai/artifact/TKsEMfeTbLg34RCNroqFkJ): the firmware's reels
without the box, spread over the width, the gain gauge (blue) at the far left as the bitcrusher's.
Renamed `tapesat` (hundekraft has `tape`). `ports/knl/tapesat/gen_tapesat_knl_svg.py`, `tape_knl.cc`,
`TapeParams::gain_oct`, `docs/fx-briefs/designs/tapesat.md`; on the device, **Test 57 good** (white mirrored: clockwise = a newer tape).
Next: fold, ensemble, shift, tape echo.

**Earlier (2026-10-27, night): drive redesigned (Test 56).** The user chose design A "Schematic" from
the canvas "Drive screen" (https://claude.ai/artifact/KAb4QAzStA5Eo9vvq4nztk; B "Waveform" and C
"Harmonics" stay there): the circuit as the screen, with the input gain as a pot (new: blue's page 2,
`DriveParams::gain` scales the clipper's K only) and the tilt EQ as an S curve.
`ports/knl/drive/gen_drive_knl_svg.py`, `drive_knl.cc`, `docs/fx-briefs/designs/drive-schematic.md`;
renamed to plain `drive` (was cs_drive), on the device, **Test 56 "perfect"**. Next: tape, fold, ensemble, shift, tape echo, one by one, the same way (canvas, then build). The gain table is shared now (`knl::GainQ14`).

**Earlier (2026-10-27, evening): bitcrusher done (Tests 54-55, "perfect").** On the device: the new
screen, the input gain, the jitter shake (was scaled to 0.016 px, fixed), blue and green turning the
stock way (clockwise = higher rate, more bits; the core's words mirrored, defaults 24767), and
**shift + a page-2 knob = fine steps on every module** (`knl::Fine`, `ports/knl/common/knl_fx.h`:
kennel's dispatcher steps a click regardless of shift, so the plugin rewrites the stored word; proven
on the gain, a tenth of a dB a click). Plate, hall and both Clouds rebuilt with quarter-click shift
steps, not yet installed (Test 55 points 5-6). Next: the remaining installed plugins' UI round (drive,
ensemble, fold, shift, tape, tape echo), then install plate/hall/Clouds, then Marbles and Grids as
kennel sequencer features (roadmap phase 3), then publish (phase 4). Nothing committed since f534fe4.

**Earlier (2026-10-27, afternoon): the beta's first findings, cs_rings rebuilt (Test 53).** The user
installed eight of the twelve (rings, crush, drive, ensemble, fold, shift, tape, tape echo) and is
going through their UIs one by one; Rings first. Its presses work (kennel: a press is a page
toggle, then the knob turns the word) but the screen gave no sign and six models took 21 clicks:
fixed (one step per click, `settings.h`; `docs/kennel-plugins.md`). The screen was then redesigned
on the Claude Design canvas https://claude.ai/artifact/UbRYUMAzUUc4bYNs6jJjRE (after the user
rejected three "too literal" scenes: bell, zither, chimes): concentric rings with light (rays),
pressure (a pad) and ripples per voice, the valve's glow on the core, MODEL/POLY rows with stroked
labels, page-2 brackets, 8 px left and 12 px up for the viewing angle. Built:
`ports/knl/rings/gen_rings_knl_svg.py` -> `rings.svg` (13.5 KB; the plugin is plain `rings` now, no `cs_` prefix: hundekraft has no rings), `rings_screen_knl.cc`, the
engine exports per-voice level/age (`RingsScreenVoices`, `-DOP1_KENNEL`). **`build/knl/rings.knl`
awaits install (Test 53; rounds 2-3 done on the device: larger, pad reversed, rows larger, sounding-voice ticks, the user is happy).** Then **bitcrusher** (was cs_crush): renamed, an input gain on blue's page 2, and its own screen (canvas option C "Ladder", https://claude.ai/artifact/MMa1PBoYe8Q3XbPvxUdx9V): `build/knl/bitcrusher.knl` awaits install (**Test 54**). Shift and tape echo rebuilt with one-step-per-click modes (`knl::ClickMode`). SquareWave's op1-design skill installed in `.claude/skills/`. Unverified on the device: whether a matrix scale thickens a stroke
(the ripple). Next: the other seven plugins' UI findings, one by one. Nothing committed.

**Latest (2026-10-27): the plugins' big state moved out of kennel's arena (Test 52 round 1 ran out).**
All twelve installed filled kennel's 2 MB plugin arena (hundekraft's own files take 1.14 MB; Rings'
573 KB of voices and reverb and a 360 KB Clouds core per position were in it), so hundekraft's
MIDI sequencer failed to load and the workshop's inventory, built in the same arena, answered "io"
and blocked Remove; the files survive reflashes and factory resets (NAND `plugins/`). Kennel's
safe start (power off within seconds of the tape screen → next boot skips all plugin files) got
the user out. Fix: `ports/knl/common/cs_sdram.h` puts Rings' state at 0x02C00000 (zeroed at the
first note) and one 384 KB Clouds slot per position (shared by both Clouds plugins, cleared on
creation) above it, region A where our firmware ran; the twelve now take 392 KB of the arena.
Tied to cores whose arena ends at 0x02C00000 (3.0.3 does). Test 52 round 2 awaiting install. Also:
hundekraft's author guide `incoming/hundekraft/llms.txt` read and checked (all conform; run
`synth_sdk/harness/svg_check.py` on screens).

**Earlier (2026-10-26, night): Clouds and Rings are kennel plugins too; twelve files, Test 52 = the beta.**
`build/knl/`: cs_crush, cs_drive, cs_tape, cs_fold, cs_shift, cs_ensemble, cs_plate, cs_hall,
cs_tape_echo, cs_clouds (granular), cs_clouds_delay (looping delay), cs_rings (synth, the 22.05 kHz
build). `docs/kennel-plugins.md` is the control map and what changed; `docs/device-tests.md` Test 52
the beta procedure. Rings runs in kennel's iter slot with `-DOP1_KENNEL` (36-byte voices, scratch at
0xffb000e0, model/polyphony from page-2 words); the firmware image is still byte-identical. Clouds
allocates ~360 KB per position on first use (`op1_alloc`); its governor runs on lateness alone.
Next: the user's beta feedback; then Marbles and Grids as sequencer features (phase 3). Nothing committed.

**Latest (2026-10-26, evening): hundekraft on the device, all nine effects ported to kennel.** The user
flashed hundekraft 3.0.3 (the repack without te-boot.ldr) and `cs_crush.knl` worked ("ok it works,
amazing!"). Test 51's answer first: Rings at 22.05 kHz is not audibly different and a little lighter,
so that build carries forward. Then every effect became a plugin: `ports/knl/cs_*/` (`make` in WSL →
`build/knl/cs_*.knl`), prefixed `cs_` because hundekraft ships its own crush/plate; **read
`docs/kennel-plugins.md`** (ids, knobs, presses, what changed: three page-2 knobs, freeze/hold as
latches, modes as page-2 words, no category dots, instances in the positions' scratch buffers).
**Test 50 round 2 (installing the eight) awaits the user.** Next: Clouds as a plugin, then Rings
(phase 2, from the 22.05 kHz build), then Marbles/Grids. Nothing committed yet.

**Latest (2026-10-26, later): Rings at 22.05 kHz test build, Test 51 awaiting the flash.** Before
moving to hundekraft the user asked for a firmware with Rings rendering at half its rate (32 samples
a call, upsampled 4x) to hear whether it matters and whether Rings + Clouds then fit.
`op1 firmware/op1_246_beta8_r22.op1` = beta8 with only the app image changed (checked file by file).
`ports/rings/src/rate_config.h` is where the rate enters; `make op1-seq-r22`; `docs/rings-port.md`
(last section) lists what had to change to keep the sound (reverb lengths, limiter coefficients,
the modes' Q, the string FIR) and what didn't, with the simulator's A/B numbers. The 44.1 kHz build
is byte-identical to the shipped image. `app.backend.build --image ELF` injects a test image.

**Latest (2026-10-26): direction change, our modules become kennel plugins.** The user found
hundekraft 3.0.3 (`incoming/hundekraft/`: the firmware, its full source and plugin SDK, the browser
workshop), JoeMod 1.0 and SilverTape beta1, and decided to align with hundekraft's kennel plugin
system: our DSP and screens ship as `.knl` files for its core, delivered over USB MIDI without a
reflash, each a named entry in the stock lists. Licensing is inherited (GPL-3.0-or-later with the
OP-1 firmware permission). The builder/librarian app and the click-based UX are parked until the
user has used hundekraft's UX. **Read `docs/kennel-roadmap.md`** (facts, phases, open questions).
Done this session: the SDK subset vendored unchanged in `third_party/kennel/` (`README-vendored.md`
says what and why); `tools/knl_pack.py` (runs in WSL; compiles our C++/.S with our flags, hands the
`ld -r` object to their unmodified `pack.pack()`, checks imports against `kn_exports.c`, re-reads the
file with their `reloc.py`); `ports/knl/cs_crush/` packs to `build/knl/cs_crush.knl` (11.6 KB, stereo, with
its screen). **Test 50 (`docs/device-tests.md`) awaits the flash:** `op1 firmware/hundekraft-3.0.3-noboot.op1`,
the hundekraft package repacked **without `te-boot.ldr`** (their file carries the stock bootloader and
fails `verify_bootloader.py`; never flash it as downloaded), then `crush.knl` through the workshop.
Our firmware branch stays as the lab and the fallback (`op1_246_beta7.op1` known good). A kennel
caveat recorded in the roadmap: their header says synth voices render at 44.1 kHz; our Tests 2-4
measured 88.2 kHz (128 samples per 64-sample effect block, decimated by the core), which is how our
Rings port already works. Nothing committed yet.

**Latest (2026-10-20, later): the builder and librarian app is built** (`app/`, `app/README.md`; run `python -m app.backend.server`). The one firmware change (Part 0 of `docs/librarian-handover.md`) is in: a module mask word in the image switches every module, patched by the builder; **Test 46 (`op1_246_beta5.op1`, `_off`, `_mix`): all good (2026-10-20), no issues.** The user then asked that "all off" be the stock experience, so the list names (multi, reverb, dirt, modulation) now follow the mask too: the builder rewrites the engine browser's query string in the image (`op1_type_list_query`, `tools/inject_app.py --set-string`) and renames the phase folder only with Rings on; Test 47 found a boot hang: a slot saved on **hall** (and any half-rate engine: plate, ensemble, tape echo) live at power-on looped the restart logic (`fx/op1/half_rate.h`, the pause measured from the call's start; Clouds measures from its end) and starved the boot. Fixed and **confirmed: beta7 boots with the hall slot that hung beta6** (Test 48, 2026-10-20). Then, from the user's first Librarian session: a preset's name is its file name (a rename rewrites the name inside, an edit of the name renames the file, the selection follows), the factory tab shows the iter presets feature's rows (read-only), and Clouds' entry defaults are the user's snapshot (`clouds_settings.h` `kEnterDefaults`: looping delay, the knobs as saved). **Test 49 (`op1_246_beta8.op1`, `_off`, `_mix`) awaits the flash**, with the rest of Test 48's checks folded in. The Librarian's backup worked on the real OP-1 (91 files). The app's screens are on the Claude Design canvas https://claude.ai/artifact/JzDmkgJX9AziWJp5NsR4Xi (awaiting review). Nothing committed yet. Status and open questions: the end of `docs/librarian-handover.md`. On the device: `op1_246_beta5.op1` (everything on, = beta4).

**Latest (2026-10-19, later): effect categories.** The user decided that spring becomes **reverb**, cwo **modulation** and punch **dirt**, delay stays **delay**, and phone is plain **phone** again; Clouds moved to reverb. Nine new engines are planned. Read `docs/fx-categories.md` (the host, the marker, the firmware words). The screen briefs are in `docs/fx-briefs/`, and the draft designs are in `docs/fx-briefs/designs/` and on the Claude Design canvas https://claude.ai/artifact/24THwmJh3HnPME5PV6FfyU, awaiting the user's review. fx1 (Test 37) worked, but its negative marker made a saved Clouds slot fail the synth patch check (error 24): fx2 fixed it (`op1_246_rings_fx2.op1`, **Test 38 all good, on the device**: the check's effect-value test patched away, an in-range marker, fx1's values migrated, LFO into Clouds). Batch 1 is built: **plate** (reverb, `ports/fx/src/plate/`, `docs/plate-port.md`) and **crush** (dirt, `ports/fx/src/crush/`); the dirt host is hooked. The package is `op1_246_rings_fx3.op1`, **Test 39, not flashed yet**. The plate costs 11.4 MHz stereo and 8.3 MHz mono in the simulator; 5.3 MHz of that is the half-rate resampling and the mix, as for Clouds. Then, while the user was away (they asked for one firmware per effect): **fx4 … fx10** add drive, ensemble, hall, fold, shift, tape and tape echo, one per build (Test 40, not flashed); fx10 has all nine engines. **Test 40 (2026-10-20):** fx3–fx8 good (sound all good), but strokes that double back render wrong, fx9 didn't boot (a stale package that replaced the stock tape.svg), and engines forgot their settings across orange clicks. Fixed in **fx11/fx12 (Test 41, not flashed)**: per-owner engine memory in `fx_host.cc`, redrawn shapes, `gen_fx_svg.py` refuses doubled-back strokes. Test 41: all good. **`op1_246_beta.op1` (Test 42, not flashed)** = fx12 + op1repacker's iter, presets-iter, filter and gfx-tape-invert. **`op1_246_beta4.op1` (Test 45, not flashed)** = beta3 + op1repacker's gfx-iter-lab. **`op1_246_beta3.op1` (Test 44, not flashed)** = beta2 + drive's valve (`docs/fx-briefs/designs/drive-valve.md`). **`op1_246_beta2.op1` (Test 43, not flashed)** = beta + the aligned Rings/Clouds/Marbles/Grids screens (`docs/module-briefs/designs/`, canvas https://claude.ai/artifact/FLWjR5T5MPnEQF6BDFPCU5). User manual: `manual/user-manual.src.html` → `manual/user-manual.html` (screens inlined), published https://claude.ai/artifact/C5nKBuHkfU8nR4RQYDRjKV. cwo and delay are now hosted (8 more hooks), stock when no engine is chosen. Build with `FX_ENGINES` (`ports/fx/engines.mk`). Plan: `%USERPROFILE%\.claude\plans\we-re-going-to-move-floating-lamport.md`.

Last updated 2026-10-19. Read this first in a fresh session; it's self-contained, and the docs it links to have the detail. **The 2026-10-19 session's findings and decisions are in `docs/handover-2026-10-19.md`** (Rings + Clouds CPU question closed: polyphony 4 works, the manual advises against the combination; seq14 = Marbles/Grids key transport, Test 36 awaiting; `manual/modules.html`). seq13 is on the device.

**Where things stand (2026-10-17):**
- **On the device:** whatever the user last flashed. Known good: `op1_246_rings_clouds4.op1` (Rings MULTI + Clouds on phone/"multi", Tests 17–19). Recovery is always COM at power-on + flash clouds4.
- **seq3 crashed on a key press on the Grids screen** (Test 23 result: stock pattern's controller looked up pattern.svg nodes in grids.svg). Fixed in seq4 (Test 23b). seq5 (SVG subpath split) didn't boot: its split left unbalanced `</path>` tags. **seq6 (Test 23c) is the corrected split. seq7 (Test 24) adds the Marbles colour pass (green = deja vu, swapped encoders and white clicks). seq8 (Test 25) adds Rings: the screen follows the LFO, no CPU meter, no main-window frame, a green polyphony frame (Test 25 all good). seq9 (Test 26) is the first beta: Clouds' main frame white, bottom-right frame removed. Test 26 good: seq9 is the first beta. seq10 (Test 27) aims at the user's next two items (Rings tape animation choppy with more than 3 voices while recording; Clouds and Rings together): the OP-1 runs without a data cache, so Rings' per-call buffers moved to the L1 scratchpad (`src/scratch.h`), bit-identical in the simulator. Test 27: animation still choppy, Rings + Clouds unchanged, slightly better at 4 voices: not the main cost. Next: measure on the device: `op1_246_rings_cpuprobe1.op1` (Test 28: 399 MHz, data cache present, Rings ~25 MHz a voice on the device ≈ the simulator, Clouds ~81 MHz, the screen starves above ~40% load). Test 29 (mode budget, -72 dB sleep, Clouds share 0.40) worsened the sound: reverted, seq10 back on the device, committed `3f4919b`. **Priority: Rings + Clouds (granular, master) together at full quality.** cpuprobe3 (Test 30) splits Clouds and Rings into stages and tests the instruction cache Test 30 results: both caches 16 KB, a miss 100-190 cycles; every Clouds stage 2-4× the simulator (cold caches each call: the per-call code of Rings + Clouds exceeds the 16 KB instruction cache); the governor's cap reads 1 (its cut assumes cycles ∝ grains, but ~85 MHz is fixed); the master was probably in looping delay mode. Test 31 (granular): cap 1 next to Rings confirmed; Rings 182 + Clouds 85 MHz. cpuprobe4 / seq11 (Test 32): governor rewritten (fixed vs per-grain cost, fast recovery, learnt period), Clouds' parameter maths cached (bit-identical; per-call code 310 to 226 lines, `make hot-set`). Test 32: the governor's interval-based lateness fired on jitter (cap still 1), onset pops = real overruns at ~90% load. cpuprobe5 / seq12 (Test 33): overruns detected by the firmware's block counter, share 0.70. Test 33: the counter can't see dropped blocks (the probe's clock read 460 for 399 MHz: 13% of blocks dropped, late count 0); a synth Clouds instance (52 MHz) was on in every case; Clouds alone behaves (cap 32, 24 grains at density high). ADI EE-197: no pipeline latencies in the mode bank's chain, so its device excess is cache lines. cpuprobe6 / seq13 (Test 34): mode bank prefetches two modes ahead + aligned loop tops (bit-identical), governor also counts a mean call interval 2% over nominal as an overrun, probe page 2 benches the kernel warm/cold. Batching is out (it doubles the per-block peak). Test 34: the prefetch buys 4% of the mode bank only (and the user's model is a string one); the interval detector works (9% dropped read, no false trips); Rings six voices = 50% of a period per block, Clouds master 12%, so with the firmware's own work blocks drop (pops, pumping, cut tails = the dropped audio). **Conclusion: Rings polyphony 'all' + Clouds doesn't fit; sound-identical savings left are single-digit %.** Test 35 proposal: find the polyphony that fits (4, else 2) and document it as the limit. See device-tests.md Tests 30-35. **The manual must advise against Rings and Clouds together.** `manual/modules.html` (2026-10-19): every engine, drum module, effect, LFO and sequencer with its technique and knob functions, stock and added. seq14 (Test 36): Marbles and Grids run from the keys (hold to run, shift to latch, next key takes over, last key up stops; the run starts on the press for tape sync).**
- **Test 23's build: `op1_246_rings_seq3.op1` (also Test 22's Marbles steps):** clouds4 + **Marbles as the synth sequencer** (on tombola, Dots screen) + **Grids as the drum sequencer** (on pattern, "Panel + kit" screen). Build: `make op1-seq` in `ports/rings`; packaging recipe below.
- **Committed and pushed** up to `eff35fa` (Marbles Dots screen). **Not committed yet:** the Grids glue and screen (`ports/grids/op1/`, `tests/sim_grids.cc`, Makefile), the `kCall24` patch kind (`tools/inject_app.py`, `op1.h`), the `_x`/display/transform fixes in `ports/marbles/op1/gen_marbles_svg.py` and the `loopX`/`legNX` ids in `marbles_op1.cc`, `docs/sequencer-interface.md` §12, Tests 22–23 notes, `docs/grids-port.md` §9. Commit only when the user asks.

**Next steps:**
1. The user's Test 23 results (numbered points). Fix what they find.
2. **Show pattern as "MULTI"** (the user's decision): the chooser looks the current type up by its shown name, so the query rename alone would crash it. `GridsSeqName` exists (`-DOP1_GRIDS_RENAME`, off) but the chooser's CALL at `0x1970bea` can't reach region A (pcrel24 = ±16 MB, ~19.6 MB away). Needs a trampoline within 16 MB (e.g. region C `0x01e00000`, untested): its own small test.
3. Later: Grids/Marbles settings in a private op1.db table (RAM only now); the knob speed if the user wants it changed; Rings + 2 Clouds CPU strain (accepted for now).

**SVG lessons (a bad screen file stops the boot until COM recovery):** ids must not contain `_x` (the parser decodes `_xHH_` escapes: `Parser.cpp:231`, marbles5/6); the OP-1 folds group transforms into children (draw driven elements in screen coordinates); no arcs, no exponents, ≤ 2 decimals; `display` only proven on g/line/polyline/rect/path/ellipse. Every design goes through its `ports/*/op1/gen_*_svg.py`, which enforces all of this. Designer briefs: `docs/marbles-screen-brief.md` (v2), `docs/grids-screen-brief.md`.

**Packaging recipe** (from the repo root, `.venv/Scripts/python.exe`): copy a known `op1 firmware/work_*` dir (e.g. `work_seq3`) to a new one; regenerate `content/display/*.svg` with the gen scripts; `tools/inject_app.py "op1 firmware/op1_246/OP1_vdk.ldr" <work>/OP1_vdk.ldr.new ports/rings/build/<image>.elf`, move it over `OP1_vdk.ldr`; `tools/build_package.py <work> "op1 firmware/op1_246_rings_<name>.op1"`; `tools/verify_bootloader.py` must PASS; check every hook target against `bfin-elf-nm`.

**Key docs:** `docs/device-tests.md` (Tests 17–23 and results), `docs/sequencer-interface.md` (sequencers, probe results §10, pattern data/print and the rename §12), `docs/marbles-port.md` §9, `docs/grids-port.md` (Decisions, §9), `docs/clouds-port.md`. Simulator checks: `make sim-seq` (ports/marbles), `make sim-grids` / `make compare` (ports/grids), `make sim-fx` / `make test` (ports/clouds).

**Older (Clouds integration, 2026-10-16):** §3 below; the Clouds sections are history now that clouds4 is on the device.

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
- Decisions they've made are recorded (§2, §3); don't re-ask them.

## 2. Where things stand

**On the device: `op1 firmware/op1_246_rings_fxprobe1.op1`.** This is multi4 (Test 15: "everything sounds great, everything is working a treat") plus the FX probe on phone (Test 16). Phone sounds normal, but turning its blue encoder starts a CPU measurement (stutter, then beeps); white resets it. Reflash `op1_246_rings_multi4.op1` for plain phone.

**The phase slot is the "MULTI" engine:** each slot plays **stock phase** or **Mutable Instruments Rings** (all six models), switched by an orange encoder click on the engine page. The lists show "multi" for "phase" (display only). Everything about it (controls, architecture, hooks, CPU, tests, package recipe): `docs/rings-op1-reference.md`. Design and validation history: `docs/rings-port.md`.

**Known Rings issues, to revisit at the end** (the user is happy as it is):
- **Model 2 and model 6 crackle somewhat in polyphony "all"; polyphony 4 is fine** (Test 15).
- **Rings model 6 × 4 voices costs ~255–290 MHz on the device** (Test 16): ~2× the simulator estimate, most likely the data cache, since its reverb and strings live in SDRAM. That's the main lever if Rings needs to be lighter.

**Decided with the user (Rings), don't re-ask:**
- sympathetic strings tune around the played note; the string models' mono mix is upstream's per-voice `out − aux`;
- model 6 plays at most 4 voices in "all";
- "all" has 14 modes per modal voice; polyphony 1, 2 and 4 are upstream's;
- the modal and FM output stay bit-identical (checksums: modal `ef8618dd`, FM `567afc0c`, model 6 `7d5e0bee`);
- the brightness handle spans 0..29491 (display only);
- PORTAMENTO → PORTA is dropped: don't propose it.

**Proven on the device** (detail in `docs/device-tests.md`, `docs/engine-interface.md`):
- **Synth voices:** rendered at 88.2 kHz, 128 samples per call, one call per sounding voice per block.
- **Effects:** called at 44.1 kHz, 64 samples per call, ~690 calls/s (Test 16).
- **CPU:**
  - ~300 MHz free with nothing playing;
  - the OP-1's own audio work is ~15% of the period;
  - **the device runs ~1.5–1.8× the simulator's cycle counts** (Rings; Clouds is unmeasured).
- **Injected code at `0x02c00000` runs;** the app has no integrity check. An audio overload is survivable (crackle); a memory-corrupting bug is not (power cycle).
- **Patching works through data only:** vtable words, imm16 immediates, content files. Calling firmware functions from our C++ works.

## 3. Clouds as an effect

The detailed design, validation and numbers are in **`docs/clouds-port.md`**. The effect interface (static analysis) is in `docs/next-projects.md` §1, and the device measurements in `docs/device-tests.md` Test 16. The screen brief for the user's designer is `docs/clouds-screen-brief.md`.

**Decided with the user (2026-10-16), don't re-ask:**
1. **Host: phone.** **The orange click on the FX page swaps each effect between Clouds and stock phone** (as MULTI does for phase/Rings). "Filter" stays free for op1repacker's filter mod.
2. **Both positions:** the master effect (stereo) and the synth effect (mono). One core class, an instance per position.
3. **Modes in order:** looping delay → granular → time stretch → spectral (last, or never).
4. **Controls:**
   - page 1 encoders: position, size, pitch, density;
   - page 2 encoders: texture, dry/wet, feedback, reverb;
   - **stereo spread fixed for now**;
   - clicks: **blue = next mode** (shift = back), **green = freeze**, **white = page**, **orange = Clouds/phone**;
   - shift + turn = fine adjustment;
   - mode and page are saved in `fx_params[4..7]`.
5. **CPU trade-offs are left to us** (quality limits, the governor).
6. The user is drawing **`clouds.svg`** (two pages; brief in `docs/clouds-screen-brief.md`). Until it arrives, Clouds can run with phone's stock screen.

**State of `ports/clouds/`:**
- **Looping delay and granular are done and verified** against upstream (21 scenarios: 52–65 dB SNR, spectra within ~0.5 dB). The remaining gaps are upstream's own float rounding, proven with a double-precision diagnostic copy, or the harness's parameter quantisation. Time stretch and spectral output silence.
- **Assembly kernels**, each bit-exact with a C reference (`make test`; the reverb and diffuser are tested over more than a trip round their buffers, with guard words):
  - grain overlap-add (linear/Hermite × mono/stereo, 16-bit dual-MAC);
  - the stereo reverb;
  - the diffuser.
- **CPU (simulator, one stereo instance):**

  | Config | MHz |
  |---|---|
  | granular, ~7 grains | ~28–30 |
  | granular, ~20 grains | ~51 |
  | granular with reverb, feedback and diffusion | ~49 |
  | looping delay, plain | ~31 |
  | looping delay with diffusion, feedback and reverb | ~49 |
  | mono, roughly | ~0.6× stereo |

  Still in C: the pitch shifter, the texture SVFs, the looper's Hermite reads, dry/wet with the soft limiter.
- **Governor** (`src/core/governor.h`, `make test-governor`): sets the grain cap per call.
  - It takes the call interval, Clouds' own cycles and our engines' cycles.
  - Feed-forward: 0.75 of the period minus the engines.
  - Feedback: overload is detected by the mean interval or a late call. It learns a ceiling on Clouds' cycles for load it can't see.
  - Simulated: no late calls under measured load, settles under hidden load, recovers. The looping delay has no grains, so its fixed cost (~50 MHz on the device, estimated) can still overload next to heavy Rings.

**Firmware facts found for the hookup** (detail in `docs/clouds-port.md` §5):
- **Phone's process vtable words:**
  - mono `0x1911308` (stock `0x01a1bb74`);
  - stereo `0x1911328` (stock `0x01a1c25c`);
  - the stereo process runs the mono one on two sub-objects;
  - the stereo params arrive at `[SP+12]`;
  - a switched-off effect isn't called (treat a gap > 5 ms as a restart).
- **Phone's encoder ranges** (the FX Ranges table, built at boot; obtained by running its initialiser `0x1a1e914` in `bfin-elf-gdb`'s simulator; entries `{min, max, x}` at `0x1de5c50 + type × 48 + knob × 6`, phone = type 3):

  | Encoder | Range |
  |---|---|
  | blue | 204..20480 |
  | green | 3072..17408 |
  | white | 1536..16896 |
  | orange | 0..32767 |
  | knobs 4..7 | 0..32767 |

  Normalise from the table at run time.
- **Synth FX page:** sub-page 2 of `[0x1b4ee88]` in the synth controller (vtable `0x1911994`, whose key words Rings already hooks: `op1/input.cc`).
  - Knob case `0x1a50690`;
  - FX type `[0xff801734 + slot × 4]`;
  - values `0xff801444 + slot × 16`;
  - saved by **`0x19ff664(patch, values)`** (like `SetKnobs` `0x1a00d14`; patches `0x1ddd7e8 + slot × 0x38`).
- **Master FX page:** `MasterController`, primary vtable **`0x1910aac`**:
  - `+0x0c` destructor `0x19d99a0`;
  - `+0x14` key down `0x19db7c4` (cases `0x2d–0x2f`);
  - `+0x1c` shift key down `0x19d987e`;
  - `+0x24` key up `0x19db708`;
  - `+0x2c` knob `0x19da2bc` (uses `0xff8016c4`).
  - **Still to find:** which master subscreen is showing (asserts `subscreen_ <= kCompressor`, state near `0x1aad5d8`), the master FX type variable, and how master FX params are saved (op1.db's `master` table).
- **Phone's screen:** `ptch.svg` immediates `0x1a1c7cc` / `0x1a1c7d0`; PhoneScreen Init/Update words `0x1911358` / `0x1911350` (the PdScreen swap pattern Rings uses).

**Integration done (2026-10-16), awaiting Test 17:**
- **Build:** `ports/rings/Makefile` target `op1-clouds` (`-DOP1_CLOUDS`, `-I../clouds -I..`): the engine sources plus `ports/clouds/op1/clouds_fx.cc` and the Clouds core. `op1-engine` still builds multi4 byte-identical.
- **Glue:** `ports/clouds/op1/clouds_fx.cc`.
  - The mono and stereo process entries replace phone's two words. Each position plays Clouds or calls stock phone.
  - Position: master = the stereo call (values at the passed pointer, `0xff8016c4`). Synth = a mono call whose `self` is the synth effect object (`[0x1aaad60]`; producers per position at `0x1aaad60/64/68`); the drum effect stays phone.
  - The synth's values are read from `0xff801444 + [0x1aaac0c] × 16`, not the passed pointer (Test 16: that is elsewhere).
  - Restart on a gap > 5 ms, after phone, or on a slot change. `Init` clears ~350 KB, ~115k cycles in the simulator; the restarting call passes the input through.
  - The governor is fed with `op1fx::measured_cycles`, a running total that `RingsRender`/`RingsNoteOn` and both Clouds instances add to. Its first 16 calls after a restart are skipped.
- **Settings** (`ports/clouds/op1/clouds_settings.h`):
  - `values[0..3]` are the shown page (the encoders);
  - `values[4]` holds the engine bit (**0 = Clouds: phone is Clouds by default**), the mode index (2 bits, in the user's order), the page bit and the hidden page's first value (11 bits);
  - `values[5..7]` hold the hidden page's other values;
  - hidden values are stored as offsets from defaults: dry/wet 50%, texture 50%, feedback/reverb 0;
  - the white click swaps the encoders' values with the hidden page's, so both pages persist.
  - **Verified in the firmware:** the synth FX knob case (`0x1a50690`) writes `0xff801444 + slot × 16`; shift + turn writes `[k − 4]` with delta / 16, so it never touches `[4..7]`.
- **Clicks:**
  - synth FX page through `op1/input.cc` (sub-page `[0x1b4ee88] == 2`, slot FX type `[0xff801734 + slot × 4] == 3`), saved with `0x19ff664`;
  - **master FX page through MasterController's key words** `0x1910ac0/ac8/ad0` (stock `0x19db7c4/0x19d987e/0x19db708`; stock handles no encoder click there). The page is active when `subscreen_ [0x1aad5d8] == 2`; the knob case's subscreen 2 copies `0xff8016c4`. The master effect counts as phone when the master producer's vtable is `0x1911314`.
  - **No master save call is known.** Clicks write the live `0xff8016c4` values; whether they persist is a Test 17 question.
- **Simulator** (`ports/clouds`: `make sim-fx`, `tests/sim_fx.cc`):

  | Case | MHz |
  |---|---|
  | master looping delay | ~31 |
  | master granular, ~7 grains | ~31 |
  | master granular, dense | ~55 |
  | synth looping delay | ~20 |
  | synth granular | ~29 |

  The settings and click round trips are checked too.
- **Choices made here** (the user may revisit):
  - phone = Clouds by default;
  - stereo spread fixed at 50%;
  - pitch ±24 semitones with a small centre dead zone;
  - freeze isn't saved;
  - the blue click cycles only the implemented modes (looping delay, granular).

**Next steps:**
1. **Test 17 on the device** (the user flashes `op1_246_rings_clouds1.op1`). From its results:
   - Clouds' device/simulator ratio, and the governor's share;
   - whether master settings persist (if not, find the master save: op1.db's `master` table);
   - whether synth/drum detection via the producers holds.
2. **The screen:** the user's `clouds.svg` in place of `ptch.svg`, swapped by the setting (as `pd.svg`/`rings.svg`). Show the mode, page, freeze and governor state. Phone's screen words: §3 facts above.
3. **Later:**
   - the pitch shifter in assembly (~4 MHz);
   - kernel scheduling into parallel slots (~30% of grain cost);
   - `GranularPlayer::OverlapAdd`'s 64-bit division per grain and block (`__divdi3`);
   - time stretch (WSOLA);
   - the screen polish.

**The original plan, for reference (steps 1–3 are done above):**
1. **One image with Rings and Clouds.**
   - Today `ports/rings/Makefile` builds the image (`op1-engine`; the FX probe variant `op1-fxprobe` with `-DOP1_FX_PROBE`).
   - Add Clouds' `src/core/*.cc` and `*.S` with `-I../clouds`. The include roots work: `src/core/...` exists only in clouds, `src/...` in rings.
   - Or make a top-level image target; restructuring the build is in scope.
   - Mind the image size: each `CloudsCore` holds ~0.5 MB at 44.1 kHz (record buffers, reverb 142 KB, diffuser, pitch shifter). Two instances fit region A `0x02c00000–0x02ffffff` with Rings (check `bfin-elf-size`; regions B/C in `docs/memory-map.md` are rated good but untested).
   - No static constructors; no soft-float.
2. **The glue, `ports/clouds/op1/clouds_fx.cc`:**
   - mono and stereo process entries in place of phone's two words;
   - per position: Clouds or stock phone (the orange-click setting), with a call-through to stock phone's process;
   - restart on a > 5 ms gap;
   - encoders normalised from phone's ranges;
   - the governor fed with `Cycles()` around the core and our engines' render cycles (add a counter to `RingsRender`, as the FX probe's `render_calls`);
   - mode and page from `fx_params[4..7]`;
   - a simulator check (`make sim-fx`, Makefile target already named).
3. **Click hooks:**
   - synth FX page: extend `op1/input.cc` (sub-page 2, the slot's FX type = phone 3);
   - master page: hook `MasterController`'s key words. Find the subscreen state first, or start with the synth position only.
   - Persist with `0x19ff664` on the synth side; find the master equivalent.
4. **The screen:** the user's `clouds.svg` in place of `ptch.svg`, swapped by the setting (as `pd.svg`/`rings.svg`). Show the mode, page, freeze and governor state.
5. **Package and device test** (Test 17): the recipe and checks as in `docs/rings-op1-reference.md` §5, plus `ports/clouds` in the image. Measure Clouds' device/simulator ratio (its large buffers may cache worse than Rings') and tune the governor's share.
6. **Later:** the pitch shifter in assembly (~4 MHz), kernel scheduling into parallel slots (~30% of grain cost), time stretch (WSOLA), the screen polish.

## 4. Environment

**Windows 11:**
- `.venv/Scripts/python` (Python 3.11; no numpy). Run tools from the repo root.
- `ffmpeg` is on PATH.

**WSL Ubuntu 20.04:**
- Toolchain in `~/op1-toolchain/bfin/bin/`: binutils 2.43, gdb 15.2, GCC 14.2 C/C++ with newlib.
- Run make like this: `wsl -e bash -lc 'cd /mnt/<drive>/<repo>/ports/clouds && make …'`.
- `ports/clouds` targets:
  - comparing with upstream: `ref`, `copy`, `copy-check`, `copyd` (double-precision diagnostic), `port`, `port44`, `compare`, `spec`;
  - checks: `test` (kernels: assembly = C), `test-governor`;
  - cost: `profile [S=prefix PROFILE_BLOCKS=n]` (MHz per configuration on Blackfin).
  - `bench` works but is very slow: the harness's float input generator runs as soft-float on Blackfin.
- Simulator: `bfin-elf-run` (L1 isn't mapped; simulator builds use `-DOP1_SIM`). `--trace-insn=on` gives an instruction trace; `ports/clouds/harness/profile.py` / `profile_lines.py` turn it into instructions per function / per source line.
- **Running firmware code in the simulator** (how the FX ranges were obtained):
  1. wrap `op1 firmware/analysis/ldr/OP1_vdk_dxe0_01000000.bin` as an ELF: `bfin-elf-objcopy -I binary -O elf32-bfin -B bfin --change-section-address .data=0x01000000 --rename-section .data=.text,alloc,load,code,contents --set-start ADDR`;
  2. in `bfin-elf-gdb`: `target sim --environment operating`, `load`, break at ADDR, `run`;
  3. set SP, FP and RETS (to a breakpoint), `continue`, `dump binary memory`.

**Gotchas:**
- **Shell heredocs mangle `\n`, `\\` and line continuations** (it happened again this session). Write edit scripts with the Write tool (e.g. into `%TEMP%\clouds_tools\`), then run them; heredocs only for text without backslashes.
- **libgcc divisions are hundreds of instructions on Blackfin**: keep `/` and `%` by non-constants out of audio loops. **`FFloat(double)` at run time is soft-float**: make such constants `static constexpr`.
- **GCC 14 on Blackfin:** selects and saturating shifts become taken branches; hot loops go to assembly with C references.
- **Blackfin assembly rules learnt with the Clouds kernels:**
  - half-register loads (`R0.L = W[..]`) only through **I registers**;
  - a dual MAC's two halves must use **the same two source registers** (pack operands accordingly);
  - `R3.H = R4.L` isn't a move: use `R3 = PACK (R4.L, R3.L)`;
  - only a low half zero-extends (`R4 = R2.L (Z)`; the high: `R5 = R2 >> 16`);
  - a 32-bit extraction from A0 goes to an **even** register (`R2 = A0`), from A1 to an odd one;
  - the `LSETUP` end label must sit on the loop's **last** instruction;
  - immediate load/store offsets up to 65534 bytes are fine outside parallel slots.
  - Keep the `L` registers at 0 (no circular buffers: use Rings' no-wrap buffer with mirrored writes).
- **Use the cycle-accurate profile driver, not guesses**: the first Clouds C build was 2× slower than estimated.
- **The simulator doesn't fault on stray writes and doesn't model the cache.**
- **The image has no startup code or run-time initialisers:** link-time constants only; no `.init_array` / `.ctors`.
- **objdump's `/* R0=… */` annotations can be stale;** trust the `.L`/`.H` pairs. Patch-table imm16 addresses are the instruction address + 2.

**Reverse-engineering material** (outside the repo):
- `op1 firmware/analysis/`: the app's ELF, disassembly (`OP1_vdk.dis`) and the extracted LDR memory (`ldr/`).
- `%TEMP%\op1re\`: indexes `compact.txt`, `calls.txt`, `consts.txt`, `srctags.txt`, `vcalls.txt`; helpers `op1lib.py` (`rd`, `u32`, `cstr`), `show.py START END` (annotated disassembly). The L1 code at `0xffa0xxxx` doesn't disassemble cleanly.
- `%TEMP%\op1inject\`: `check.py` (byte-diffs an injected LDR against stock), `pkgdiff.py` (diffs packages), built LDRs.
- `%TEMP%\clouds_tools\`: this session's scratch scripts (`segsnr.py`: SNR per time segment; `ranges.gdb`). Disposable.

## 5. Other ports (parallel agents) and projects

**Other agents port modules; this session integrates them** (user, 2026-10-16). Their folders and docs, untracked:
- `ports/braids` (MULTI engine; `docs/braids-port.md` when written);
- `ports/plaits` (MULTI engine);
- `ports/streams` (**an effect**: it will need its own slot or a place in the orange-click cycle; settle with the user);
- `ports/marbles` (an LFO; `docs/lfo-interface.md` maps the OP-1's LFO interface);
- `docs/streams-port.md`, `docs/marbles-port.md`.

The announced default, **not yet confirmed by the user:** with 3+ engines, the orange click cycles phase → Rings → Braids → Plaits. Every module shares the CPU budget (§2).

Specced earlier (`docs/next-projects.md`):
- op1repacker mods as `tools/mods.py` (content only; interacts with the preset budget);
- Rings factory presets;
- a builder app;
- small items: CWO → COW display rename, the CPU meter in release builds, the green encoder's range, FM optimisation.

## 6. Git

- Branch `firmware-analysis-tooling`, remote `origin` (`<public repo URL>`). Commit only when the user asks.
- **Uncommitted from 2026-10-16 (this session's work):**
  - modified: `docs/HANDOVER.md`, `docs/device-tests.md` (Test 16), `docs/next-projects.md`, `tools/decode_probe_beeps.py` (v4), `ports/rings/Makefile`, `ports/rings/op1/{engine.cc, patches.cc, patches.h}` (FX probe, under `OP1_FX_PROBE`; `make op1-engine` still builds multi4 byte-identical);
  - new: `ports/rings/op1/fx_probe.*`, `ports/rings/tests/host_fx_probe.cc`, `ports/clouds/`, `docs/clouds-port.md`, `docs/clouds-handover.md` (superseded), `docs/clouds-screen-brief.md`;
  - integration (later session): `ports/clouds/op1/`, `ports/clouds/tests/sim_fx.cc`, `ports/rings/{Makefile, op1/input.cc, op1/input.h}` and more `OP1_CLOUDS` blocks in `ports/rings/op1/{engine,patches}.*`, Test 17 in `docs/device-tests.md`;
  - **other agents' folders:** `ports/braids`, `plaits`, `streams`, `marbles` and their docs.
- **Before committing:** gitignore `ports/clouds/build/` and `ports/clouds/out/` (renders); check the other ports likewise.
- **Never commit TE firmware:** `op1 firmware/` and `mutableinstruments/` are gitignored. `incoming/` belongs to the user and stays out of git.

## 7. Docs and memory

| Doc | Content |
|---|---|
| `docs/clouds-port.md` | **Clouds:** state, layout, validation, CPU, governor, OP-1 hookup facts |
| `docs/clouds-screen-brief.md` | The Clouds screen brief (two pages, controls, SVG constraints) |
| `docs/rings-op1-reference.md` | Rings / MULTI on the device: controls, architecture, hooks, CPU, tests, **package recipe and checks** |
| `docs/rings-port.md` | The Rings port's design, validation and measurements |
| `docs/next-projects.md` | Specs: the OP-1's effect interface (§1), engines, builder app, op1repacker mods, presets |
| `docs/device-tests.md` | Every device test (probes v1–v4, Tests 2–16): procedure and result |
| `docs/engine-interface.md` | Synth engine types, factory, voice vtables, render ABI, pitch |
| `docs/screen-interface.md` | Screen class, SVG API (what can change at run time), input path, presets, encoder clicks |
| `docs/memory-map.md` | App load map; free regions (A `0x02c00000–0x02ffffff` in use; B and C rated good, untested) |
| `docs/brick-safety.md`, `docs/firmware-246-baseline.md`, `docs/app-boot-chain.md` | Safety and groundwork |
| `ports/rings/README.md` | Rings port layout and harness builds |

Persistent notes: `%USERPROFILE%\.claude\projects\I--GitHub-PersonalProjects-OP1-CustomFirmware\memory\` (index `MEMORY.md`).
