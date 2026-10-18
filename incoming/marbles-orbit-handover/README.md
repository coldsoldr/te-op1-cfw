# Marbles for OP-1: screen SVG handover (Orbit)

Files:
- `marbles.svg`: the asset. Ships showing page T.
- `preview/marbles-pageX-example.svg`: the same file with page X shown and example values. For reference only, don't ship it.

The values baked into `marbles.svg` (indicator transforms, lit gates, selected model) are only example values. The code overwrites them all on load.

## Conventions

- `viewBox="0 0 320 160"`, black background supplied by the device.
- Every stroke is `stroke-width="1.5"`, `stroke-linecap="square"`.
- `<g id="static">` is never touched. Everything else the code drives has an `id`.
- Draw order = file order: `static` → `pageT` → `pageX` → indicators → gates → right column. The gates come after the blue core so they are never covered.
- No text, gradients, rounded rects or arcs. The three frame outlines are `path`s with cubic corners (same as `rings.svg`).
- Totals: 19 driven ids, about 60 elements.

### Transforms
Each encoder indicator sits inside an un-id'd wrapper `<g transform="translate(ox,oy)">`. The indicator itself is drawn around local `0,0`. The code only sets the `transform` on the **indicator element**; the wrapper stays fixed. All mappings below are in local units.

If your renderer can't nest transforms, add the wrapper offset to the indicator's coordinates once at load. For scaled shapes, also scale about that point.

## Palette

| Role | Hex |
|---|---|
| Frames | `#3854A5` |
| Blue encoder | `#638BFF` |
| Green encoder | `#00EE92` |
| White encoder | `#DED7FF` |
| Orange encoder | `#FF395A` |
| Active icon / lit gate | `#DED7FF` |
| Inactive icon / unlit gate stroke | `#514959` |
| Orbit rings, column-centre tick, white track | `#38354D` |
| Unlit gate fill | `#010101` |

## Layout

- **Left panel** (x 4.5–186.5): the orbit, centred at **70,74**. Three rings, r 22 / 36 / 50, are the lanes for T1 / T2 / T3, with crosshair axes through the centre.
  - The white indicator track is at x=138 (y 24–124).
  - The orange column is at x 146–164, y 20–128, with a centre tick at y=74.
- **Right top box:** model or scale row (y≈18), deja vu icons (y≈52), pitch source icons (y≈80).
- **Right bottom box:** page indicator.

## Pages

| id | Contents | Show when |
|---|---|---|
| `pageT` | 8 white clock ticks around the orbit (r 55–60) + the model row | page T |
| `pageX` | 12 white dots around the orbit (r 58, one per semitone) + the scale row | page X |

Toggle them with `display="none"`. Everything else is shared by both pages.

## Encoder indicators (same shapes on both pages)

First normalise each parameter to `u` in 0..1:

| Encoder | Page T | Page X |
|---|---|---|
| blue | rate step `k` 0..9 (1 bar … 1/32), `u = k/9` | spread, `u = spread` |
| green | bias, `u = bias` (0.5 = even) | bias, `u = bias` (0.5 = middle) |
| white | jitter, `u = jitter` | steps, `u = steps` |
| orange | deja vu, `u = dejavu` (0.5 = locked loop) | length `n` 1..16, `u = (n-1)/15` |

Then:

| id | Shape | Wrapper | Transform | Range |
|---|---|---|---|---|
| `ind0` | blue filled circle r12 (the core) | translate(70,74) | `scale(s)`, `s = 0.15 + 0.85·u` | radius 1.8 → 12 px |
| `ind1` | green filled diamond (±4.5) | translate(70,74) | `translate((u-0.5)·100, 0)` | x 20 → 120 along the horizontal axis; centre x=70 |
| `ind2` | white filled triangle | translate(138,124) | `translate(0, -u·100)` | y 124 (0) → 24 (1) on the white track |
| `ind3` | orange filled rect, 14 × 104, drawn from y −104 to 0 | translate(148,126) | `scale(1, max(u, 0.02))` | fills upward from y 126; full at y 22; the centre tick (y 74) = 0.5 |

For rate and length, either snap to the steps or move continuously. Steps: blue gives 10 core sizes; orange gives 16 heights of 6.5 px each.

## Gates (live)

| id | Position | Lane |
|---|---|---|
| `gate1` | circle r3.5 at 70,52 | inner ring, T1 |
| `gate2` | circle r3.5 at 70,38 | middle ring, T2 (clock) |
| `gate3` | circle r3.5 at 70,24 | outer ring, T3 |

- High: `stroke` and `fill` `#DED7FF`.
- Low: `stroke="#514959"`, `fill="#010101"`.

They're the only animated elements; overlapping gates simply light together. (The optional "gates follow X1–X3" isn't used here: they sit on fixed lanes.)

## State indicators (right column)

All of these are stroke-only. To select one, set its `stroke` to `#DED7FF`; for the others in its group, set `#514959`.

| ids | Meaning | Driven by |
|---|---|---|
| `model_mk` | white triangle under 7 dim dots (x 205 + 17·i). `translate(17·m, 0)`, m = 0..6: coin toss, clusters, drums, independent, divider, three states, Markov. Inside `pageT`. Its fill is already white; it moves, it doesn't recolour. | blue click / shift + click |
| `scale_mk` | same triangle under 6 dim bars of rising height (x 205 + 20.4·i). `translate(20.4·s, 0)`, s = 0..5 = Marbles' preset scales in firmware order. Inside `pageX`. | blue click / shift + click |
| `dv_off`, `dv_on`, `dv_locked` | zigzag = off, open octagon = on, closed octagon with a square = locked. They show the **current page's** deja vu state: T state on page T, X state on page X. | green click |
| `src_scale`, `src_keys` | staircase = scale, keyboard = held keys | shift + white |
| `pageTind`, `pageXind` | pulse train = T, bell = X | white click |

## Full id list

`static`, `pageT`, `pageX`, `ind0`, `ind1`, `ind2`, `ind3`, `gate1`, `gate2`, `gate3`, `model_mk`, `scale_mk`, `dv_off`, `dv_on`, `dv_locked`, `src_scale`, `src_keys`, `pageTind`, `pageXind`

## Not on screen

The orange click (switch to tombola) and shift + turn (fine adjust) need nothing on this screen.
