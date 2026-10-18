# Grids for OP-1: screen SVG handover (1d, "Panel + kit")

Files:
- `grids.svg`: the asset. Ships in drums mode with shift released.
- `preview/grids-euclid-shift-example.svg`: an example of the other state, for reference only (don't ship it). It shows Euclidean mode with shift held, 32nds, accent key, RND on, and SD being assigned.

The transforms and lit parts baked into `grids.svg` are example values. The code overwrites them on load.

## Conventions

- `viewBox="0 0 320 160"`, black background supplied by the device.
- Every stroke is `stroke-width="1.5"`, `stroke-linecap="square"`.
- `<g id="static">` is never touched. Draw order = file order: `static` → `map` → `dot` → parts → indicators → `playhead` → `assign` → state icons.
- **No driven element sits inside a transformed group.** All coordinates are real screen coordinates.
- A driven element's `transform` is set on the element itself, and the code replaces it.
  - Scaling about an anchor is written as a matrix:
    - in x about `ax`: `matrix(s,0,0,1,ax·(1−s),0)`;
    - uniformly about `cx,cy`: `matrix(s,0,0,s,cx·(1−s),cy·(1−s))`.
- No text, gradients, rounded rects or arcs. The frames are `path`s with cubic corners (as in `rings.svg`).
- About 70 elements; 21 ids that the code drives.

## Palette

| Role | Hex |
|---|---|
| Frames | `#3854A5` |
| Blue / green / white / orange encoders | `#638BFF` / `#00EE92` / `#DED7FF` / `#FF395A` |
| Lit part, dot, playhead, assign arrow, active icons | `#DED7FF` |
| Unlit part stroke, map nodes, beat ticks, RND off, kit stands | `#514959` |
| Unlit part fill | `#010101` |
| Map frame, indicator tracks, step ticks | `#38354D` |

## Layout

| Area | Box | Contents |
|---|---|---|
| Left panel | x 4.5–186.5 | map (x 14–104, y 12–102); kit parts (x≈133–161); step ticks and playhead (y 121–136) |
| Right top box | y 2.5–96.5 | four indicator bars |
| Right bottom box | y 102.5–144.5 | state icons |

## Map

- `map`: a group with the square (x 14–104, y 12–102) and 25 node dots at `x = 23 + 18·c`, `y = 21 + 18·r`. It's never driven; leave it shown in both modes.
- `dot`: a white crosshair path centred at **23,93**.
  - Set `transform="translate(72·X, −72·Y)"`. X runs 0 → 1 from x 23 to 95; Y runs 0 → 1 from y 93 to 21. The corners land on the corner nodes.
  - Euclidean mode: `display="none"`.

## Parts (live)

| id | Shape | Centre |
|---|---|---|
| `part_hh` | ellipse rx 14, ry 3.5 (top cymbal; the lower cymbal and stand are static) | 147,23 |
| `part_sd` | ellipse rx 13, ry 4 (drum head; the shell is static) | 147,56 |
| `part_bd` | circle r 13 (kick front; the feet are static) | 147,90 |

States:
- idle: `stroke="#514959"`, `fill="#010101"`, no transform;
- hit: `stroke` and `fill` `#DED7FF`;
- accent: as hit, plus `transform="matrix(1.25,0,0,1.25,−36.75,−0.25·cy)"` (scale 1.25 about the centre);
- fill while held: keep the part lit (hit colours) for as long as its key is down.

`assign`: a white triangle pointing at the part being assigned.
- Drawn pointing at HH (124,20 124,30 130,25).
- `transform="translate(0, dy)"`, where dy = **0** (HH), **32** (SD) or **65** (BD).
- Off (or RND selected): `display="none"`.

## Playhead (live)

- 16 static ticks at `x = 16 + 10·i` (taller and brighter at the beats, i = 0, 4, 8, 12).
- `playhead`: a 4 × 4 white square under the ticks, centred at **x=16**, y 132–136.
  - 16ths: `translate(10·step, 0)`, steps 0..15 → x 16 … 166.
  - 32nds: `translate(5·step, 0)`, steps 0..31 → x 16 … 171 (odd steps fall between ticks).

## Indicators `ind0`..`ind3`

- Filled rects at `x = 202`, `y = 12 + 21·i` (12, 33, 54, 75), 102 × 9, in encoder order and colour. A static `#38354D` track of the same size sits underneath.
- **Anchor = left end, x = 202.** Value `u` 0..1: `transform="matrix(u,0,0,1,202·(1−u),0)"`. Keep `u ≥ 0.02` so an empty bar still shows a sliver.

| | blue `ind0` | green `ind1` | white `ind2` | orange `ind3` |
|---|---|---|---|---|
| drums | map X | map Y | chaos | swing |
| drums + shift | BD fill | SD fill | HH fill | accent |
| Euclidean | BD length `u=(L−1)/31` | SD length | HH length | swing |
| Euclidean + shift | BD hits `u=hits/L` (0 if L=0) | SD hits | HH hits | accent |

## State icons (bottom-right box, left to right)

| ids | Meaning | Rule |
|---|---|---|
| `mode_drums` / `mode_euclid` | 3×3 squares / octagon | show the current mode's icon and hide the other (blue click) |
| `res16` / `res32` | two strokes / four strokes | show one (shift + blue) |
| `accent_vel` / `accent_key` | up arrow on a bar / key | show one (shift + green) |
| `rnd_on` | die | always shown. RND on: `stroke="#DED7FF"`; off: `stroke="#514959"` (shift + white) |
| `shift` | shift-key arrow | shown only while shift is held |

Optional flashes (re-roll on green click, print on shift + orange): briefly set `shift`'s or `mode_*`'s stroke to `#FF395A`. No extra element is needed.

## Full id list

`static`, `map`, `dot`,
`part_hh`, `part_sd`, `part_bd`, `assign`,
`ind0`, `ind1`, `ind2`, `ind3`, `playhead`,
`mode_drums`, `mode_euclid`, `res16`, `res32`, `accent_vel`, `accent_key`, `rnd_on`, `shift`
