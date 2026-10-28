# Our plugins for hundekraft's kennel core

Built by `make` in `ports/knl/` (WSL) into `build/knl/<id>.knl`; installed through the workshop
(Your plugins → Update OP-1 → T1). `docs/kennel-roadmap.md` has the plan, `ports/knl/common/knl_fx.h`
what the effects share, `tools/knl_pack.py` the packer driver, `tools/knl_svg.py` the screens.

Every id is prefixed `cs_` (hundekraft has its own crush, plate, warble, …; a same-id file would
replace theirs), shown with a space: **cs crush**. The id is the saved name: it never changes.

## What differs from the firmware build

Kennel has two kinds of knob press, and no click of its own: **page** (the knob edits a page-2
word, 5..7, instead of its knob; blue 5, green 6, white 7, orange shares 7 so it's never a page
knob) and **latch** (an unsaved toggle). So:

- Two-page effects (plate, hall) have three page-2 parameters, not four; the fourth is fixed at
  its old default. Pressing a knob switches that knob alone; the screen shows page 2 while any of
  the three is pressed.
- Freeze and hold are latches (orange for the reverbs, green for the tape echo).
- A click that cycled a setting (tape echo's head pattern, shift's ring mode) is now a page-2
  word turned with the blue knob: one step per encoder click, cycling (`knl::ClickMode`, as
  Rings' model and polyphony; kennel moves a word ~1536 per click, so the full range would be
  21 clicks otherwise).
- No category dots: a plugin is its own list entry (`tools/knl_svg.py` strips them).
- **Shift + a page-2 knob = fine steps** (2026-10-27, Test 55, the user's ask: the stock knobs'
  shift fine control on every module). Kennel's dispatcher moves a page-2 word a click (~1536)
  with or without shift, so the effect does it itself (`knl::Fine`, `common/knl_fx.h`): each
  render block it compares the words it follows with what it saw last, and a change of up to
  three clicks while shift is held (the stock flag `0x01dd7f78`) is rewritten in the stored words
  as a fine step a click; a bigger jump (a sound loading) passes. Quarter clicks for plate, hall
  and both Clouds; a tenth of a dB (273 words) for the bitcrusher's gain, so its readout moves one
  digit a click. The mode words (freq_shift's, on green; tape echo's heads; Rings' model and polyphony) stay a
  click a step.
- Each effect instance lives in the position's 176 KB scratch buffer (synth, drum, master), cleared
  on init; nothing of a core's state is resident. The half-rate effects keep their restart on a
  pause. Clouds' core and Rings' voices are too big for that and for kennel's arena: they sit at
  fixed SDRAM addresses above it (`ports/knl/common/cs_sdram.h`, below).

## The plugins (all stereo on the master, mono on synth and drum)

| id | knobs (blue, green, white, orange) | presses | fixed |
|---|---|---|---|
| bitcrusher (was cs_crush, renamed 2026-10-27) | rate, bits, jitter, mix (blue and green clockwise = more: a higher rate, more bits; the core's crush amounts are the words mirrored, defaults mirrored too, 2026-10-27) | blue → page 2: input gain -6..+6 dB (word 5, 0 dB at the centre; drives the crusher only) | |
| drive (was cs_drive, renamed 2026-10-27) | drive, tone, bias, mix | blue → page 2: input gain -6..+6 dB (word 5, 0 dB at the centre; scales the clipper's gain only, `DriveParams::gain`), shift for tenths | screen: the schematic (`gen_drive_knl_svg.py`, canvas "Drive screen" design A, 2026-10-27) |
| tape_drive (was tapesat, renamed 2026-10-28; cs_tape before that: hundekraft has a `tape`) | saturate, wobble, age, mix (white clockwise = a newer tape: the core's wear is the word mirrored, default mirrored too, 2026-10-27) | blue → page 2: input gain -6..+6 dB (word 5, 0 dB at the centre; adds to the saturation drive's exponent only, `TapeParams::gain_oct`), shift for tenths | screen: the reels, aligned (`gen_tape_drive_knl_svg.py`, canvas "Tape screen" design A, 2026-10-27) |
| wavefolder (was cs_fold, renamed 2026-10-27) | fold, symmetry, tone, mix | blue → page 2: input gain -6..+6 dB (word 5, 0 dB at the centre; scales the folder's gain only, `FoldParams::gain`), shift for tenths | screen: the wave, aligned (`gen_fold_knl_svg.py`, canvas "Fold screen" design A, 2026-10-27) |
| freq_shift (was cs_shift, renamed 2026-10-27) | shift, feedback, spread (master), mix | blue → page 2: input gain -6..+6 dB (word 5; the wet path's analytic signal, `ShiftParams::gain`), shift for tenths; green → page 2: mode (word 6, one click flips shifter / ring) | screen: the dial (`gen_shift_knl_svg.py`, canvas "Freq shift screen" design B, 2026-10-27) |
| ensemble (was cs_ensemble, renamed 2026-10-27) | rate, depth, voices, mix | blue → page 2: input gain -6..+6 dB (word 5, 0 dB at the centre; scales the wet path's input only), shift for tenths | screen: the drift (`gen_ensemble_knl_svg.py`, canvas "Ensemble screen" design C, 2026-10-27) |
| cs_plate | decay, damp, pre-delay, mix | blue → size, green → diffusion, white → motion; orange latches freeze | low cut .1 |
| cs_hall | size, decay, damp, mix | blue → pre-delay, green → early, white → width; orange latches freeze | motion .3 |
| tape_echo (was cs_tape_echo, renamed 2026-10-28) | time, feedback, age, mix (shift + blue on page 1: the time locks to the project tempo's divisions, 1/16T .. 1/2, a division a click, `TimeLock`, plugin state not saved with the sound; the tempo from `knl::TempoIncrement`, proven on the device 2026-10-28) | blue → page 2: input gain -6..+6 dB (word 5; scales what reaches the record head), shift for tenths; green latches hold; white → page 2: head pattern (word 7, one click per pattern: 3; 2+3; 1+2+3; 1+3; was blue's) | screen: the schematic (`gen_tape_echo_knl_svg.py`, canvas "Tape echo screen" design B, 2026-10-28) |
| cs_clouds (granular) | position, size, pitch (centre 0), density (centre none) | blue → texture, green → dry/wet, white → reverb; orange latches freeze | feedback 0 |
| cs_clouds_delay (looping delay) | position, size, pitch, density | blue → dry/wet, green → feedback, white → reverb; orange latches freeze | texture 50 % |

**rings** (a synth, in the engine list; plain `rings`, no `cs_` prefix: hundekraft has no rings, the user's choice 2026-10-27): blue structure, green brightness, white damping, orange
position. Blue press → page 2, where blue picks the model (six steps: modal, sympathetic strings,
string, FM, quantized sympathetic strings, string + reverb); green press → page 2, where green
picks the polyphony (four steps: all, 1, 2, 4). **One step per encoder click**, cycling: kennel
moves a page-2 word by about 1536 per click (21 clicks over the range, Test 52), and
`ports/rings/op1/settings.h` reads the word in clicks (rounded to the nearest). A new sound is
modal, "all". It is the 22.05 kHz build (Test 51), upsampled 4x; the model-6 reverb is shared as
before. Its screen is its own (`ports/knl/rings/gen_rings_knl_svg.py`, 2026-10-27, Test 53):
rings lit by structure, rays for brightness, a pad lifting off the rings for damping, the strike dot for
position, a ripple per voice and a glowing core (the engine exports each voice's level and age,
`RingsScreenVoices`), MODEL and POLY rows with stroked labels and a bracket while the knob is on
page 2 (`op1_eng_page`). Labels are polylines from a 5 x 7 stroke font in the generator (the
parser has no text).

Clouds is two plugins because kennel has three page-2 words and Clouds has five more settings
(mode, texture, dry/wet, feedback, reverb): each plugin fixes the mode and the setting that
matters least in it. The firmware build's blue click (mode) is the choice between the two list
entries now.

Page-2 words and the knobs are saved with the sound (plugin.json `defaults`, `page2_defaults`);
latches aren't.

## Sizes (2026-10-26)

| id | file | resident (bss) |
|---|---|---|
| bitcrusher | 37 KB | 356 B (its screen 29 KB: 8 staircase variants, 113 clock ticks, the readout glyphs) |
| drive (was cs_drive) | 31 KB | 200 B |
| tapesat (was cs_tape) | 32 KB | 236 B |
| wavefolder (was cs_fold) | 30 KB | 160 B |
| freq_shift (was cs_shift) | 31 KB | 204 B |
| ensemble (was cs_ensemble) | 27 KB | 1.2 KB |
| cs_plate | 32 KB | 1.2 KB |
| cs_hall | 36 KB | 1.1 KB |
| tape_echo | 24 KB | 1.5 KB |
| cs_clouds | 100 KB | 4.2 KB (the core, ~360 KB per position, is in the fixed region) |
| cs_clouds_delay | 100 KB | 4.2 KB (the same slots) |
| rings | 112 KB | 1.3 KB (the voices and the reverb, 572 KB, are in the fixed region); its screen 13 KB |

All twelve take 392 KB of the arena (the loader's own bookkeeping aside).

Kennel's plugin arena is 2 MB usable (0x02980000..0x02B80000; the top 512 KB is the file staging
buffer) for every plugin, hundekraft's own included, and the loader fills it with the user's files
(plugins/, alphabetical) before the shipped ones (content/). Hundekraft 3.0.3's shipped set takes
about 1.14 MB (Gin alone 452 KB). The first builds kept Rings' 573 KB of state and a Clouds core
per position in that arena: all twelve, or Rings plus one Clouds, pushed the last shipped files out
with "plugin memory full" (the MIDI sequencer among them), and the inventory text, built in the
same arena on the workshop's first LIST, failed with "io", which blocks the workshop's own Remove
(2026-10-26). The way out of that state is kennel's safe start: switch the OP-1 off within a few
seconds of the tape screen appearing (the crash marker `plugins/.boot` is cleared only after the UI
has run ~7-10 s); the next boot skips every plugin file, the inventory works, and the workshop can
remove or replace files. **Replacing a file in place** (the same id, Update OP-1 → T1) hung the boot on
the "kennel: updated" screen once (tape_echo, 2026-10-28); the same file added after a Remove and a
restart booted fine. So replace in two steps: remove, restart, add. Installed plugin files live in NAND `/yaffs2/plugins`: a firmware
package, a stock reflash and the factory reset leave them alone, and the browser's "Your plugins"
list re-sends them with every USB update.

Since then the big state is outside the arena, in SDRAM 0x02C00000..0x02DB0000 (region A of
`docs/memory-map.md`, where our own firmware ran; `ports/knl/common/cs_sdram.h`): Rings' six
voices and reverb at 0x02C00000 (576 KB, zeroed by its first note), then one 384 KB Clouds slot
per effect position, shared by both Clouds plugins and cleared by whichever takes the position.
Nothing in hundekraft 3.0.3 uses that page, but its docs call 0x02980000..0x03000000 the planned
plugin block: a core whose arena grows past 0x02C00000 needs these three plugins moved (check
`KN_ARENA_HI` in its `kn_boot.c`). Rings in the iter slot: its per-call buffers sit in the L1
scratchpad at 0xffb000e0, just past the six 36-byte iter voices (kennel's code uses nothing else
there).

The packer links with `--gc-sections` rooted at the two descriptors and `--force-group-allocation`
(C++ inline variables and templates would otherwise stay in COMDAT groups their packer rejects), so
a plugin carries only what its callbacks reach.

Still to port: Marbles and Grids (phase 3, sequencer features).

## Own screens (2026-10-27)

Two plugins no longer take the firmware's screen: **rings** (`ports/knl/rings/gen_rings_knl_svg.py`)
and **bitcrusher** (`ports/knl/bitcrusher/gen_crush_knl_svg.py`), both designed on Claude Design
canvases after the user's beta feedback and both using the shared 5 x 7 stroke font
`ports/knl/common/knl_glyphs.py` for labels and readouts (the parser has no text). Their
generators enforce the parser's rules and hundekraft's `svg_check.py` passes. The SquareWave
op1-design skill (`.claude/skills/op1-design`) is installed for the designs; its own `check_svg.py`
is stricter than the parser (it rejects `display`, line caps and the root's px units) and is only
an advisory lint.
