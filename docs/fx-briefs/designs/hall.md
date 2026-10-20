# HALL: screen handover (draft)

Asset: `hall.svg` (both pages; ships with `page1` shown). Shared conventions (top strip, engine dots, mix bar, scale forms, set rules): see [plate.md](plate.md). Same time strip as the plate: plate = an object, hall = a space.

## Driven ids

`page1`, `page2`, `page1ind`, `page2ind`, `freeze`, `eng0..eng3`, `refl`,
page 1: `size`, `decay`, `decayinf`, `damp`, `mix`;
page 2: `room`, `predelay`, `early`, `late`, `motionL`, `motionR`, `widthL`, `widthR`.

Static: time strip baseline and impulse (as plate), floor line y 84 x 30–270 (`#38354D`).

## Hall section (size)

- `size` (blue, page 1), `room` (dim, page 2) and `refl` (dim reflection path, both pages) share one transform: uniform scale about the floor centre (150, 84). `s = 0.4 + 0.6·size`; `matrix(s,0,0,s,150·(1−s),84·(1−s))`. Demo size 0.6 → `matrix(0.76,0,0,0.76,36,20.16)`. At s = 1 the walls are at x 46 / 254 and the roof apex at y 26.

## Tail start

`x0 = 34 + 50·pd` (pre-delay, page 2). Demo pd 0.3 → x0 49.

## Page 1

| id | Value → geometry |
|---|---|
| `decay` (green envelope) | drawn from x 34, length 200. `s = 0.15 + 0.85·d`, d = decay 0..1 (RT60 0.3–20 s, log); `matrix(s,0,0,1,x0−34·s,0)`. At ∞ use s = 1 |
| `decayinf` (green ∞ glyph at the tail end, x 270–282, y 100) | show only when decay = ∞ (top of range, or freeze) |
| `damp` (white highs envelope) | as `decay`, peak y 110; `sd = s·(1 − 0.8·damp)` |
| `mix` | mix bar |

## Page 2

| id | Value → geometry |
|---|---|
| `predelay` (blue gap line) | line x 27–35, y 98: `sx((x0−30)/8, 27)` |
| `early` (green comb, 4 spikes) | drawn for x0 = 34. `se = 0.2 + 0.8·early`; `matrix(1,0,0,se,x0−34,134·(1−se))` |
| `late` (green envelope from x 70, length 180) | x scale = the decay `s` from page 1, `sl = 1 − 0.7·early`; `matrix(s,0,0,sl,x0−34+70·(1−s),134·(1−sl))` |
| `motionL`, `motionR` (white zigzags at x 30 / 270) | `sx(motion, 30)`, `sx(motion, 270)` |
| `widthL`, `widthR` (orange arrowheads either side of x 260, y 14) | `translate(−14·w, 0)` and `translate(14·w, 0)`; mono positions: keep w = 0 |

## Indicators

`page1ind` / `page2ind`, `freeze`: as plate. Freeze also shows `decayinf`.

## Default-state colours

Blue: size, predelay. Green: decay, decayinf, early, late. White: damp, motion. Orange: mix, width, active engine dot. Dim `#514959`: room, refl, freeze (off).
