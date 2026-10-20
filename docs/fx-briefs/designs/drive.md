# DRIVE: screen handover (draft)

Asset: `drive.svg`. Shared conventions: see [plate.md](plate.md).

## Dirt family layout (drive, tape, fold, crush)

Static on all four: a dim input sine (x 20–52, y 66–90) → chevron (x 57–61) → **shaper box** (frame `#3854A5`, x 68.5–211.5, y 30.5–125.5; plot area x 84–196, y 38–118, centre 140, 78) → chevron (x 219–223) → **after-stage** area (x 232–288, y 46–96). Engine dots `eng0..eng4` (punch, drive, tape, fold, crush), mix bar top right.

## Driven ids

`eng0..eng4`, `curve0..curve7`, `bias`, `tone`, `mix`.

| id | Value → geometry |
|---|---|
| `curve0..curve7` (blue transfer curve, input x → output y) | **variant set**, index = round(7·drive). Curve k: `y = 78 − 40·tanh(g·x)/tanh(g)`, `g = 0.6·1.45^k`, x −1..1 over 84..196. Demo: `curve4` |
| `bias` (white floor line, x 84–136 at y 118) | `translate(0, −24·bias)`: odd (0) at y 118 → even (1) at y 94. Demo 0.5 → −12 |
| `tone` (green tilt line (236,90)–(284,66), pivot 260, 78) | **bipolar**, t 0..1, centre 0.5 = flat. `sy(2t − 1, 78)` = `matrix(1,0,0,2t−1,0,78·(2−2t))`. t = 1 bright (rises right), t = 0 dark. If the renderer rejects negative scales, fall back to 9 pre-drawn tilts |
| `mix` | mix bar |

## Default-state colours

Blue curves, white bias, green tone, orange mix and `eng1`. Static: axes `#38354D`, input sine / chevrons / pivot `#514959`, frame `#3854A5`.
