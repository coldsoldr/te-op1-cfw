# PLATE: screen handover (draft)

Asset: `plate.svg` (both pages; ships with `page1` shown, `page2` hidden). Review canvas: "OP-1 Effect Screens", artboards Plate p1 / p2.

## Shared conventions (all nine new screens)

- `viewBox 0 0 320 160`, every stroke `1.5`, `square` caps. Content inside x 16–296, y 8–140.
- **Top strip (y 8–20):** engine dots left, page / mode indicator at x 76–92, freeze / hold glyph at x 106–118, the orange value (usually mix) at x 232–288.
- **Engine dots** `eng0..eng3`: circles r 2.5 at `x = 22 + 10·i`, y 14. Active: `stroke` and `fill` `#FF395A`. Inactive: `stroke #514959`, `fill #010101`.
- **Mix bar** `mix`: rect x 232, y 12, 56 × 4, fill `#FF395A`, over a static `#38354D` track of the same size. Anchor = left end: `transform="matrix(u,0,0,1,232·(1−u),0)"`, keep `u ≥ 0.02`.
- **Scale forms** used below: `sx(s, a)` = `matrix(s,0,0,1,a·(1−s),0)` (x about `a`); `sy(s, a)` = `matrix(1,0,0,s,0,a·(1−s))`; uniform about `cx,cy` = `matrix(s,0,0,s,cx·(1−s),cy·(1−s))`.
- **Show/hide sets are groups** (`display="none"` only on groups). A *variant set* shows exactly one member; a *count set* shows the first n.
- Groups carry no transform in the file. Where a note says "the code translates the group", that is a run-time transform on the group.
- Stroked lines scaled in one axis get a thinner stroke in a browser; check on the device whether its renderer does the same (decay/damp envelopes, motion zigzags).
- The values baked into the file are a demo mid-state; the code overwrites them.

## Driven ids

`page1`, `page2`, `page1ind`, `page2ind`, `freeze`, `eng0..eng3`,
page 1: `ring1..ring4`, `predelay`, `decay`, `damp`, `mix`;
page 2: `size`, `motionL`, `motionR`, `tail`, `diff1..diff8`, `lowcut`.

Static: the time strip baseline (y 134, x 24–288, `#38354D`) and the input impulse (x 24, y 102–134, `#514959`).

## Tail start (shared by both pages)

`x0 = 34 + 50·pd`, pd = pre-delay 0..1 (0–200 ms). Demo pd 0.4 → x0 54.

## Page 1

| id | Value → geometry |
|---|---|
| `decay` (blue envelope) | drawn from x 34, length 200 (peak y 102). `s = 0.15 + 0.85·decay`; `transform="matrix(s,0,0,1,x0−34·s,0)"`. Demo decay 0.6: `matrix(0.66,0,0,1,31.56,0)` |
| `damp` (green highs envelope) | drawn as `decay` but peak y 110. `sd = s·(1 − 0.8·damp)`; `matrix(sd,0,0,1,x0−34·sd,0)`. Bright = as long as the tail, dark = 20 % of it |
| `ring1..ring4` (blue ellipses on the sheet) | **count set**, n = ceil(4·decay) (min 1). Demo 3 |
| `predelay` (white gap line) | line x 27–35 at y 98. `sx((x0−30)/8, 27)`: ends 3 px before the tail start |
| `mix` | mix bar, see above |

## Page 2

| id | Value → geometry |
|---|---|
| `size` (blue sheet) | step 0..7: `s = 0.6 + 0.065·step`, uniform about (150, 54): `matrix(s,0,0,s,150·(1−s),54·(1−s))`. Demo step 4 → 0.86 |
| `diff1..diff8` (green early echoes) | **count set**, n = 1 + round(7·diffusion). Drawn for x0 = 54; the code translates each group by `x0 − 54` |
| `motionL`, `motionR` (white zigzags) | `sx(motion, 56)` and `sx(motion, 244)`; 0 = straight line. Optional: animate a ±0.15 wobble on top |
| `tail` (dim envelope) | same transform as page 1 `decay`; colour stays `#514959` |
| `lowcut` (orange high-pass) | points (240,20) (252,10) (288,10), anchor x 288. `s = 1 − (2/3)·v`, v 0..1 (20–500 Hz, log): `sx(s, 288)`. Corner x = 288 − 36·s: 252 → 276 |

## Indicators

- `page1ind` / `page2ind`: 5 × 5 rects at x 77.5 / 86.5, y 11.5. Active: fill and stroke `#DED7FF`; inactive: fill `#010101`, stroke `#514959`. Show `page1` or `page2` to match.
- `freeze`: asterisk polyline at (112, 14). Off `#514959`; frozen `#00EE92`. While frozen, draw decay as 1.

## Default-state colours

Blue `#638BFF`: rings, decay, size. Green `#00EE92`: damp, diff. White `#DED7FF`: predelay, motion. Orange `#FF395A`: mix, lowcut, active engine dot. Dim `#514959`: page-1 sheet, tail, freeze (off). Track `#38354D`.
