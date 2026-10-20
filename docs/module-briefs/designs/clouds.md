# CLOUDS (reverb engine 4): screen handover, aligned with the fx screens (draft)

Asset: `clouds.svg` (this folder; both pages, `page1` shown, `page2` hidden). It passes `ports/fx/op1/gen_fx_svg.py` with 0 warnings: the arcs of the old file are cubic Béziers now, and every hidden element sits in its own group. Review canvas: "OP-1 Module Screens", artboards "Clouds · page 1", "Clouds · page 2, frozen" and "Clouds next to Plate". Shared scheme: [rings.md](rings.md).

## What changed

- The three frames are gone; the scene spans the safe area (ground y 134, x 24–288).
- The top strip is plate's and hall's: `eng0..eng3` (spring, plate, hall, clouds; clouds lit), page squares, freeze star at x 112.
- The 2×2 mode buttons became four blue dots under the engine dots.
- The snowflake became the fx freeze star (the old one doubled back through its centre, which the OP-1 draws wrong).
- The CPU meter moved to the top right (x 233–289, y 14). The sun moved into `page1` (page 2 has the storm cloud there).
- Themes kept: page 1 cloud, rain, face, town; page 2 wind, puddle, rainbow, storm and thunder.

## Driven ids

| id | status |
|---|---|
| `page1`, `page2`, `page1ind`, `page2ind` | kept (indicators moved to x 77.5 / 86.5, y 11.5) |
| `freeze_off`, `freeze_on` | kept names, now the star (groups) at (112, 14) |
| `mode0..mode3` | **kept names, now circles** (were groups); r 2.5 at x 22 + 10·i, y 24 |
| `eng0..eng3` | new, static on this screen |
| `position`, `pitch`, `shadow`, `size0..size5`, `face`, `face_frozen`, `reg0..reg7`, `rnd0..rnd7` | kept, redrawn 41 px right and 5 px down; sizes, drops: now groups |
| `texture0..texture4`, `wet0..wet4`, `fb0..fb4`, `rv0..rv4` | kept, moved; `wet*`, `fb*` now groups |
| `cpu`, `cpu0..cpu5` | kept, moved |

Removed: `mode0_box..mode3_box`, `mode0_icon..mode3_icon`, `mode3_off`.

## Value → geometry

| id | mapping |
|---|---|
| `position` (translate group, drawn at pos 0.5, cloud centre x 156) | `translate(x, 0)`, **x = (pos − 0.5)·160** (was 120). Q10 per knob unit: 160·1024/32767 ≈ 5, so `(v[0] − 16384) · 5` |
| `pitch` (translate group) | `translate(0, y)`, **y = −0.5 · semitones** (±12 px for ±2 oct, was 0.625 / ±15): `-(pitch_q16 >> 7)` in Q10 |
| `size0..size5` | variant set, `k = round(size·5)` (unchanged) |
| `reg0..reg7` / `rnd0..rnd7` | count sets, `n = round(|d|·8)`, regular left of centre, random right (unchanged) |
| `face` / `face_frozen` | freeze off / on (unchanged) |
| `texture0..4`, `wet0..4` | variant sets (unchanged) |
| `fb0..fb4`, `rv0..rv4` | count sets (unchanged) |
| `mode0..mode3` | lit = current mode: fill and stroke `#638BFF`; unlit stroke `#514959`, fill `#010101`; spectral while disabled: stroke `#38354D` |
| `freeze_off` / `freeze_on` | show one (unchanged) |
| `page1ind` / `page2ind` | lit fill and stroke `#DED7FF`; unlit fill `#010101`, stroke `#514959` |
| `cpu0..cpu5` | count set, `n = ceil(load·6)`; the group `cpu` is hidden in the file (the code shows it today: hide it in a release build) |

## Default-state colours (the code reads them)

`mode0` lit, `mode1` unlit, `mode3` disabled; `page1ind` lit, `page2ind` unlit; `freeze_off` shown. Blue: shadow, wind, mode dots. Green: cloud outline, puddle, freeze when on. White: face, rainbow, page squares. Orange: rain, bolt, thunder echoes, `eng3`. Dim `#514959`: town, sun, ground, storm cloud, freeze when off.

## What clouds_screen.cc must change

1. Ids: drop `kModeBoxIds`, `kModeIconIds`, `mode3_off`; read `LineStyle` and `FillStyle` of `mode0..mode3` (on = mode0, off = mode1, disabled = mode3's line colour). Write both styles per dot; for spectral write the disabled line colour and the off fill.
2. Position: `SetTranslate(position, (v[0] − 16384) · 5, 0)` (was `· 15 / 4`).
3. Pitch: `SetTranslate(pitch, 0, -(s.pitch_q16 >> 7))` (was `-((pitch_q16 >> 6) · 5) >> 3`).
4. Everything else keeps its id and its logic. `gen_clouds_svg.py`'s arc conversion is no longer needed: ship `gen_fx_svg.py`'s output of this file.

## Implemented 2026-10-20

`ports/clouds/op1/clouds.svg` is `gen_fx_svg.py`'s output of this file (0 warnings; 60 driven ids passed with `--ids`). `clouds_screen.cc` drives `eng0..eng3` through `EngineDots` (clouds lit), the mode dots per the table (spectral's line `#38354D` while not selected), position `· 5`, pitch `>> 7`; the mode boxes, icons and `mode3_off` are gone. The CPU meter is no longer driven: `cpu` stays hidden as the file has it (`cpu0..cpu5` not looked up).
