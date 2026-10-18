# Marbles for OP-1: screen SVG handover (v2, "Dots"; colour pass 2026-10-17)

Files:
- `marbles.svg`: the asset. Ships showing page T.
- `preview/marbles-pageX-example.svg`: the same file with page X shown. For reference only, don't ship it.

The marks, brackets, bars and chip positions baked into `marbles.svg` are an example (an 8-step locked loop). The code overwrites them on load. The **colours** are drawn in the default states the brief asked for:
- `h00` shown;
- `loop_t` green (locked) and `loop_x` dim green (on);
- `src_scale` selected and `src_keys` not.

## Conventions

- `viewBox="0 0 320 160"`, black background supplied by the device.
- Every stroke is `stroke-width="1.5"`, `stroke-linecap="square"`.
- `<g id="static">` is never touched. Everything else the code drives has an `id`.
- **No driven element sits inside a translated or scaled group.** All coordinates below are real screen coordinates.
  - The only groups outside `static` are the legend icons and the two chip rows. They have no transform; the code only shows and hides them.
- A driven element's `transform` attribute is set directly on the element; the code replaces it.
  - Scaling about an anchor point `a` is written `matrix(s,0,0,1,a·(1-s),0)`, which is the same as `translate(a,0) scale(s,1) translate(-a,0)`.
- No text, gradients, rounded rects or arcs. The frame outlines are `path`s with cubic corners.
- Driven ids: 48 marks + 2 brackets + 8 icons + 4 bars + 2 chip markers + 2 chip rows + 2 source icons = **68**.

## Palette

| Role | Hex |
|---|---|
| Frames: chip box only (history and legend have none; the pitch source box is white `#DED7FF`) | `#3854A5` |
| Blue encoder | `#638BFF` |
| Green encoder | `#00EE92` |
| White encoder | `#DED7FF` |
| Orange encoder | `#FF395A` |
| Marks, selected source icon | `#DED7FF` |
| Unselected source icon, bar centre ticks | `#514959` |
| Brackets: locked / on (deja vu is green throughout) | `#00EE92` / `#13704D` |
| Chip markers / chip dots and bars (the blue click) | `#638BFF` / `#3854A5` |
| Lane guide lines, bar tracks | `#38354D` |

## Layout

| Area | Box |
|---|---|
| History | x 4.5–252.5, y 2.5–102.5 |
| Chip (model / scale) | x 258.5–316.5, y 2.5–50.5 |
| Pitch source | x 258.5–316.5, y 56.5–102.5 |
| Legend | x 4.5–316.5, y 108.5–156.5 |

## 1. History marks `h<lane><col>`

- `lane` 0 = T1 (bottom), 1 = T2 (middle), 2 = T3 (top).
- `col` 0..15; **15 is the newest** (right). Ids: `h00`..`h015`, `h10`..`h115`, `h20`..`h215`.
- Every mark is a `circle` r=2.5, `fill` and `stroke` `#DED7FF`.
- **Column x:** `x = 22 + 14·col` (22, 36, … 232).
- **Lanes** (each mark is drawn at its lane's baseline; 18 px of travel):

| Lane | Baseline y (lowest note) | Top y (highest note) | Guide line |
|---|---|---|---|
| T1 (`h0*`) | 88 | 70 | y 92.5 |
| T2 (`h1*`) | 62 | 44 | y 66.5 |
| T3 (`h2*`) | 36 | 18 | y 40.5 |

- **Pitch:** normalise the note to `p` 0..1 within the current spread range. Set `transform="translate(0, -p·18)"`.
- **Gate:** show the mark when that gate fired on that step; otherwise `display="none"`.
- **Scrolling:** on each clock step (T2), shift every lane's state one column left and write the new step into col 15. Only visibility and translate-y change; x never moves.

## 2. Loop brackets

| id | Points (full width) | Position |
|---|---|---|
| `loop_t` | `15,11 15,6 239,6 239,11` | above the lanes (rhythm deja vu) |
| `loop_x` | `15,95 15,100 239,100 239,95` | below the lanes (pitch deja vu) |

- Full width = 16 columns (224 px, 14 px per column). **Anchor = right end, x = 239.**
- Length `n` 1..16: `s = n/16`, `transform="matrix(s,0,0,1,239·(1-s),0)"`. The left end lands at `239 − 14·n`, so the bracket covers the last `n` columns.
- State (per bracket, from that page's deja vu click):
  - off: `display="none"`;
  - on: `stroke="#13704D"` (dim green);
  - locked: `stroke="#00EE92"` (green).

## 3. Legend (4 slots)

Slot `i` = 0..3 in encoder order (blue, green, white, orange). Slot left edge `x0 = 4.5 + 78·i`, so x0 = 4.5, 82.5, 160.5, 238.5.

**Icons:** groups `leg<i>_t` (page T) and `leg<i>_x` (page X), centred at `x0+39, 124`. Show the current page's group and hide the other. This is also the page indicator.

| Slot | Colour | `leg<i>_t` | `leg<i>_x` |
|---|---|---|---|
| 0 | `#638BFF` | rate: metronome | spread: arrows apart |
| 1 | `#00EE92` | deja vu: loop arrow | length: bracket |
| 2 | `#DED7FF` | jitter: wobbly line | steps: staircase |
| 3 | `#FF395A` | bias: seesaw | bias: up/down arrows |

**Bars:** `bar<i>` is a `rect` at `x = x0+14`, y 140, 50 × 4, filled with the slot colour. **Anchor = left end, x = x0+14** (18.5, 96.5, 174.5, 252.5).
- Value `u` 0..1: `transform="matrix(u,0,0,1,ax·(1-u),0)"`, where `ax` is the anchor.
- A static track (`#38354D`, same rect) sits underneath.
- Slots 1 and 3 have a centre tick at `x0+39` (y 147–149). Slot 1 (deja vu): the locked-loop point on page T; no special meaning for length. Slot 3 (bias): even on page T, middle on page X.
- Value mapping:
  - Page T:
    - blue: rate step `k` 0..9, `u = k/9`
    - green: deja vu
    - white: jitter
    - orange: bias
  - Page X:
    - blue: spread
    - green: length `n`, `u = (n−1)/15`
    - white: steps
    - orange: bias

## 4. Chips

| id | Contents | Show when |
|---|---|---|
| `chipT` | 7 dim dots at `x = 268 + 6.67·i`, y=20, plus the marker `chip_model` | page T |
| `chipX` | 6 dim bars of rising height at `x = 268 + 8·i` (bottoms at y=23), plus the marker `chip_scale` | page X |

- `chip_model`: a blue triangle (264.5,34 271.5,34 268,29). `transform="translate(6.67·m, 0)"`. The order of m 0..6: coin toss, clusters, drums, independent, divider, three states, Markov.
- `chip_scale`: same triangle. `transform="translate(8·s, 0)"`. The order of s 0..5: major, minor, pentatonic, pelog, raag Bhairav, raag Shri.
- `src_scale` (staircase) / `src_keys` (keyboard), stroke-only. Selected: `stroke="#DED7FF"`; other: `stroke="#514959"`. Toggled with the white click (shift + white turns the page).

## Full id list

`static`;
`h00`..`h015`, `h10`..`h115`, `h20`..`h215`;
`loop_t`, `loop_x`;
`leg0_t`, `leg0_x`, `leg1_t`, `leg1_x`, `leg2_t`, `leg2_x`, `leg3_t`, `leg3_x`;
`bar0`, `bar1`, `bar2`, `bar3`;
`chipT`, `chipX`, `chip_model`, `chip_scale`;
`src_scale`, `src_keys`.

## Not on screen

The orange click (switch to tombola) and shift + turn (fine adjust) need nothing on this screen.
