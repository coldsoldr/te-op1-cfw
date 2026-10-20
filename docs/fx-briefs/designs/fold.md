# FOLD: screen handover (draft)

Asset: `fold.svg`. Shared conventions: [plate.md](plate.md); dirt family layout: [drive.md](drive.md).

Static inside the box: two fold lines (`#38354D`) at y 50 and y 106, x 84–196.

## Driven ids

`eng0..eng4`, `symmetry`, `wave0..wave7`, `tone`, `mix`.

| id | Value → geometry |
|---|---|
| `wave0..wave7` (blue, one output cycle x 84–196) | **variant set**, index = round(7·fold). `y = 78 − 28·sin(g·π/2·sin θ)`, `g = 1 + 0.6·k`. Demo `wave4` |
| `symmetry` (green centre line, x 84–196 at y 78) | **bipolar**, v 0..1, centre 0.5 = symmetric: `translate(0, −24·(2v − 1))` → y 102 (bottom) … 54 (top). Demo v 0.6 → −4.8 |
| `tone` (white low-pass glyph (236,70) (268,70) (282,94)) | `sx(0.3 + 0.7·tone, 236)`: corner x 245.6 (dark) → 268 (bright) |
| `mix` | mix bar |

Optional later: lopsided variants of the waves for large symmetry offsets; the current file only moves the line.

## Default-state colours

Blue waves, green symmetry, white tone, orange mix and `eng3`.
