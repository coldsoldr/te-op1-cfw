# MARBLES (synth sequencer): screen handover, aligned with the fx screens (draft)

Asset: `marbles.svg` (this folder; page T shown). Ids are already in their device form (`loopX`, `leg0X`: no `_x`). It passes `ports/fx/op1/gen_fx_svg.py` with 0 warnings. Review canvas: "OP-1 Module Screens", artboards "Marbles · page T" and "Marbles · page X". Shared scheme: [rings.md](rings.md).

## What changed

- Frames gone (chip box, pitch-source box).
- Top strip: `eng0` (tombola), `eng1` (marbles, lit); page squares for T / X (new; the legend no longer has to be the page indicator); pitch source glyphs at x 106–134; **bias** (orange, both pages) is the top-right bar with a centre tick at x 260.
- Row 2: the model / scale chip is a row of blue dots under the engine dots (7 for page T, 6 for page X), with a lit dot that moves.
- History kept (16 columns × 3 lanes), wider: columns 16 px apart, lanes 24 px apart, 14 px of pitch travel.
- Legend: three slots (blue, green, white) along the bottom, each an icon and a 56 × 4 bar, the same bar as the fx mix bar.

## Driven ids

| id | status |
|---|---|
| `h00..h015`, `h10..h115`, `h20..h215` | kept names, **now groups** (each holds its circle; hidden ones `display="none"`) |
| `loop_t`, `loopX` | kept, moved |
| `leg0_t`, `leg1_t`, `leg2_t`, `leg0X`, `leg1X`, `leg2X` | kept, redrawn smaller, moved |
| `bar0`, `bar1`, `bar2` | kept, 56 × 4, moved |
| `bar3` | kept, moved to the top-right bar |
| `chipT`, `chipX` | kept (now rows of dim dots) |
| `chip_model`, `chip_scale` | kept, now a blue dot r 2.5 drawn at (22, 24) |
| `src_scale` | kept, moved (one polyline) |
| `src_keys` | kept, now a **group** (rect + two key lines, one subpath each; no `src_keysP2..P4`) |
| `eng0`, `eng1` | new, static |
| `page1ind`, `page2ind` | new: page T / page X |

Removed: `leg3_t`, `leg3X` (bias needs no icon in the mix-bar place).

## Value → geometry

| id | mapping |
|---|---|
| marks `h<lane><col>` | column x = **28 + 16·col**; baselines **T1 102, T2 78, T3 54**; `translate(0, −14·p)` (was 18), p = height 0..1 |
| `loop_t` (`20,36 20,32 276,32 276,36`), `loopX` (`20,110 20,114 276,114 276,110`) | anchor **x 276**, `s = n/16`: `matrix(s,0,0,1,276·(1−s),0)`; off hidden, on `#13704D`, locked `#00EE92` |
| `bar0`, `bar1`, `bar2` (x 44 / 132 / 220, y 127, 56 × 4) | `sx(u, anchor)`, anchors **44, 132, 220** |
| `bar3` (x 232, y 12, 56 × 4) | `sx(u, 232)` |
| `chip_model` | `translate(10·m, 0)`, m 0..6 (was 6.67) |
| `chip_scale` | `translate(10·s, 0)`, s 0..5 (was 8) |
| `leg*_t` / `leg*X`, `chipT` / `chipX` | page T / page X |
| `page1ind` / `page2ind` | page T / X: lit fill and stroke `#DED7FF`, unlit fill `#010101`, stroke `#514959` |
| `src_scale` / `src_keys` | selected stroke `#DED7FF`, other `#514959` |

The bar values per page are as now (page T: rate, deja vu, jitter, bias; page X: spread, length, steps, bias).

## Default-state colours (the code reads them)

`h00` shown; `loop_t` `#00EE92` (locked), `loopX` `#13704D` (on); `src_scale` selected, `src_keys` not; `page1ind` lit, `page2ind` unlit. `#13704D` (dim green) is the one colour outside the fx palette; it stays because deja vu is green throughout.

## What marbles_op1.cc must change

1. Marks: `SetTransform(mark, …, −height·14·1024/255)` (was 18). The marks are groups: no other change.
2. Brackets: anchor 276 instead of 239 (`276·(0x400 − n·64)`).
3. Bars: `kBarAnchor = {44, 132, 220, 232}` px in Q10.
4. Chips: `t_model · 10240`, `scale · 10240` (10 px).
5. Legend: loop over 3 icon pairs instead of 4 (drop `leg3_t`, `leg3X` from `kNodeIds`).
6. New: drive `page1ind` / `page2ind` like Clouds (line and fill styles, read from the file's default).
7. `src_keys` is one group: `FindParts`-style lookup finds no `P2`, so it recolours the group once (as Rings' `model0num` groups did).
8. Ship `gen_fx_svg.py`'s output of this file instead of `gen_marbles_svg.py`'s.

## Implemented 2026-10-20

`ports/marbles/op1/marbles.svg` is now `gen_fx_svg.py`'s output of this file (0 warnings; rule `op1/marbles.svg` in `ports/marbles/Makefile`, ids checked: the 22 named ids + 48 marks). `marbles_op1.cc` follows steps 1–7: travel 14 px, bracket anchor 276, bar anchors 44/132/220/232, chips 10 px, three legend pairs (`leg3_t`/`leg3X` gone), `page1ind`/`page2ind` driven from the file's defaults, `src_keys` recoloured as one group (the `P2..` part lookup is gone). `eng0`/`eng1` stay static. Not yet on the device.
