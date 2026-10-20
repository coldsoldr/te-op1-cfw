# GRIDS (drum sequencer): screen handover, aligned with the fx screens (draft)

Asset: `grids.svg` (this folder; drums mode, shift released). It passes `ports/fx/op1/gen_fx_svg.py` with 0 warnings. Review canvas: "OP-1 Module Screens", artboards "Grids · drums" and "Grids · euclidean, shift held". Shared scheme: [rings.md](rings.md).

## What changed

- The three frames are gone.
- Top strip: `eng0` (pattern), `eng1` (grids, lit); the mode is two blue dots under them (drums, euclid); the state icons moved from the bottom-right box into the strip, each in its click's colour: resolution (shift + blue) blue strokes at x 77–89, accent source (shift + green) green at x 101–111, RND die (shift + white) at x 120–129, shift arrow at x 138–150.
- `ind3` (orange: swing, or accent with shift) is the top-right mix bar.
- The other three bars are 88 × 4 and sit beside the kit piece they set in three of the four states: `ind2` (white) by the hats, `ind1` (green) by the snare, `ind0` (blue) by the kick. In drums mode they are map X (blue), map Y (green) and chaos (white) and keep their encoder colours.
- Map (80 × 80) and kit kept, kit centred at x 150; the step ticks span x 24–264, 16 px per sixteenth.

## Driven ids

| id | status |
|---|---|
| `map` | kept, not driven |
| `dot` | kept, now a **group** of two lines (was one path with two subpaths) |
| `part_hh`, `part_sd`, `part_bd` | kept, moved |
| `assign` | kept, now a **group** |
| `ind0..ind2` | kept, 88 × 4 at x 200 |
| `ind3` | kept, moved to the top-right bar |
| `playhead` | kept, moved |
| `mode_drums`, `mode_euclid` | kept names, now a lit blue dot each (show one) over static dim dots |
| `res16`, `res32`, `accent_vel`, `accent_key`, `rnd_on`, `shift` | kept, now groups, moved, recoloured |
| `eng0`, `eng1` | new, static |

Nothing removed.

## Value → geometry

| id | mapping |
|---|---|
| `dot` (crosshair centred at 32,106) | `translate(64·X, −64·Y)` (was 72) |
| `part_hh` (150,42), `part_sd` (150,74), `part_bd` (150,107) | idle `#514959` / `#010101`, hit `#DED7FF` both; accent: `matrix(1.25,0,0,1.25,−37.5,−0.25·cy)` |
| `assign` (drawn pointing at HH) | `translate(0, dy)`, dy **0 / 32 / 65** (unchanged) |
| `ind0..ind2` (x 200, 88 × 4) | `sx(u, 200)`, u ≥ 0.02 |
| `ind3` (x 232, 56 × 4) | `sx(u, 232)`, u ≥ 0.02 |
| `playhead` (4 × 4 at x 22–26, y 136) | `translate(16·step)` for sixteenths, `translate(8·step)` for thirty-seconds (were 10 / 5) |
| `mode_drums` / `mode_euclid`, `res16` / `res32`, `accent_vel` / `accent_key` | show one (unchanged) |
| `rnd_on` | stroke `#DED7FF` on, `#514959` off (unchanged rule) |
| `shift` | shown while shift is held (unchanged) |

## Default-state colours (the code reads them)

`part_hh` lit, `part_sd` idle, `rnd_on` off (unchanged). Blue: mode dot, resolution strokes, `ind0`. Green: accent glyph, `ind1`. White: `ind2`, dot, lit parts, playhead, assign, RND on, shift. Orange: `ind3`, `eng1`. Dim `#514959`: kit stands, beat ticks, map nodes; `#38354D`: map square, tracks, step ticks.

## What grids_op1.cc must change

1. `Bar()`: anchor 200 for `ind0..ind2`, 232 for `ind3` (pass the anchor).
2. Dot: `(u.x · 64) >> 6`, `−((u.y · 64) >> 6)`.
3. Accent transform: tx = −0.25·150 = −37.5 px (`−38400` Q10); `kPartCentreY = {107, 74, 42}`.
4. Playhead: `step_px` 16 / 8.
5. `dot`, `assign` and the state icons are groups now: `FindParts` finds the group and no `P2`; show, hide, translate and recolour act on the group (runtime transforms on groups work, as Rings' handle groups did).
6. Ship `gen_fx_svg.py`'s output of this file instead of `gen_grids_svg.py`'s.

## Implemented 2026-10-20

`ports/grids/op1/grids.svg` is now `gen_fx_svg.py`'s output of this file (0 warnings, `--ids` checked; no Makefile rule built the old one, so `gen_grids_svg.py` is simply no longer used). `grids_op1.cc` steps 1-5 done: bar anchors 200/232, dot 64 px, accent about (150, cy) with `kPartCentreY = {107, 74, 42}`, playhead 16/8 px; `FindParts` still tries `idP2..` but finds only the groups. `eng0`/`eng1` are left static. `make sim-grids` passes (host and bfin-elf-run); `grids_op1.cc` compiles with the op1-seq flags. Not on the device yet.
