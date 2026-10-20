# RINGS (MULTI engine): screen handover, aligned with the fx screens (draft)

Asset: `rings.svg` (this folder). It passes `ports/fx/op1/gen_fx_svg.py` with 0 warnings. Review canvas: "OP-1 Module Screens", artboards "Rings · main" and "Rings · model 6, all voices".

## Shared scheme for the four Mutable Instruments screens

The fx screens' conventions apply as written in [../../fx-briefs/designs/plate.md](../../fx-briefs/designs/plate.md) (strokes, safe area, scale forms, variant and count sets). On top of them:

- **Row 1, y 14: engine dots** `eng0..engN`, r 2.5 at `x = 22 + 10·i`, orange (the orange click cycles them). Active: fill and stroke `#FF395A`; inactive: stroke `#514959`, fill `#010101`. The stock engine is `eng0`. A module's screen only shows while its own engine plays, so on these four screens the dots never change: the code can leave them alone.
- **Row 2, y 24: model / mode dots**, same size and pitch (`x = 22 + 10·i`), **blue**, because the blue click picks them on every module (Rings' model, Clouds' mode, Marbles' T model or scale, Grids' drums or euclid). Lit: fill and stroke `#638BFF`; unlit: stroke `#514959`, fill `#010101`; disabled (Clouds' spectral): stroke `#38354D`.
- **Slot x 76–92, y 10–18:** page squares (`page1ind`, `page2ind`, as plate and hall) where a module has pages, else the module's other click state (Rings' voices, Grids' resolution).
- **Slot x 100–150:** toggles (freeze star, pitch source, accent, RND, shift), in their click's colour.
- **x 232–288, y 12, 56 × 4:** the orange value as a bar over a `#38354D` track (the fx mix bar), where the module has a level-like orange value (Marbles' bias, Grids' swing / accent).
- No frames anywhere.

## Driven ids

| id | status | what it shows |
|---|---|---|
| `eng0`, `eng1` | new | phase / rings; `eng1` lit (static here) |
| `model0..model5` | **kept name, now a circle** (was a group holding `modelNbox` + `modelNnum`) | the model, blue dots |
| `voice0..voice5` | new | polyphony, green ticks |
| `ring0..ring5` | kept (now `circle`, was `ellipse`) | structure |
| `core` | kept | always blue |
| `string` | kept, moved | resting string |
| `pluck0..pluck23` | kept names, **now groups** (each holds its polyline; hidden ones `display="none"`) | position |
| `posdot` | kept, moved | position dot |
| `brightness`, `damping` | kept names, **now bars** (rects scaled in y), were handle groups translated in y | green / white values |
| `structure`, `position`, `pluck` | kept wrapper groups, not driven | |

Removed: `model0box..model5box`, `model0num..model5num`, `poly0..poly2`, `poly0box..poly2box`, `poly0dots..poly2dots`.

## Value → geometry

| id | mapping |
|---|---|
| `ring0..ring5` (blue rings, centre 100,72, r 7·(k+1)) | count by colour, as now: `k = 1 + round(structure·5)`; `ring0..ring(k−1)` stroke `#638BFF`, the rest `#38354D` |
| `pluck0..pluck23` | variant set: `i = round(position·23)`; polyline `42.5,128 (42.5+5i),123 157.5,128` |
| `posdot` (drawn at 42.5,123) | `translate(115·position, 0)` (was 92 px from x 44) |
| `brightness` (rect x 202, y 36, 4 × 80) | `sy(b, 116)` = `matrix(1,0,0,b,0,116·(1−b))`, b = the displayed brightness (the existing `BrightnessDisplayValue` / 32768), keep b ≥ 0.02 |
| `damping` (rect x 234, y 36, 4 × 80) | `sy(d, 116)`, d = damping / 32768 |
| `model0..model5` | one lit: `model` index (op1/settings.h order: modal, sympathetic, inharmonic, FM, quantized sympathetic, string + reverb) |
| `voice0..voice5` | count set by colour: lit = 1, 2, 4 or 6 (POLY_1, POLY_2, POLY_4, POLY_ALL); lit stroke `#00EE92`, unlit `#38354D` |

Static: string bridges at x 42.5 and 157.5, the two bar tracks, tick marks at y 36 / 76 / 116 beside each bar, the green crosshair under brightness and the white decay curve under damping.

## Default-state colours (the code reads them)

`ring0` lit / `ring5` dim; `model0` lit / `model1` unlit (fill and stroke); `voice0` lit / `voice5` unlit. Blue: rings, core, model dot. Green: voices, brightness bar, crosshair. White: damping bar, decay curve. Orange: `eng1`, pluck, posdot. Dim `#514959`: string, bridges, ticks, unlit dots. Track `#38354D`.

## What screen.cc must change

1. Id lists: `kModelBoxIds`/`kModelNumIds` become one list `model0..model5`; read `LineStyle` and `FillStyle` of each (the dot's fill changes too). `kPolyBoxIds`/`kPolyDotsIds` become `voice0..voice5` (line style only).
2. Polyphony: lit count = `kPolyVoices[poly]` (6, 1, 2, 4), instead of lighting 1 of 3 buttons (or all 3).
3. `brightness`, `damping`: replace `SetTranslate(0, ty)` with a y scale about y 116 (`m = {0x400,0,0, 0,s,0, 0,116·(0x400−s),0x400}`, s in Q10). `screen_math.h`: drop the translate maths, return the two values in Q10.
4. `posdot_tx` = 115 px · position, measured from x 42.5 (Q10, knob 0..32767: `(position · 115) >> 5`); `pluck` unchanged (still 24 variants, 5 px apart now).
5. `gen_rings_svg.py` no longer applies: this file is drawn in the device subset; run it through `gen_fx_svg.py` with `--ids` and ship the output as `rings.svg`.
6. The pluck variants are groups now: `SetVisible` on the group (no other change).

## Implemented 2026-10-20

`ports/rings/op1/rings.svg` is now this file through `gen_fx_svg.py --ids` (0 warnings); the Makefile rule `op1/rings.svg` builds it that way (`gen_rings_svg.py` kept, unused). `op1/screen.cc` drives `model0..5` (line + fill), `voice0..5` (count by colour, `kPolyVoices`), `ring0..5`, `pluck0..23`, `posdot`, and scales `brightness` / `damping` in y about 116; `eng0/eng1`, `core`, `string` are left as drawn. `op1/screen_math.h` returns the bar scales in Q10 (`tests/test_screen_math.cc` updated, passes). One change from the table above: damping keeps the same 0.02 minimum as brightness, so neither bar collapses to a zero-height matrix.
