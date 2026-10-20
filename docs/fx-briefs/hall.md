# HALL (reverb category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

A **hall reverb** built as an **8-line feedback delay network** (Jot's FDN). A few diffuser steps scramble the input into early density. Eight delay lines of different lengths then feed back into each other through a mixing matrix, each with its own damping filter. Even lines feed the left channel and odd lines the right. Unlike the plate, the line lengths scale cleanly, so **size goes from a small room to a huge hall**. The sound is smoother, softer and less "ringy" than the plate: a space rather than an object.

It is engine 3 of 4 in **reverb** (spring → plate → **hall** → clouds). In the master position it is stereo; on a synth or drum it is mono.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **size** | 0..1, room → hall | the size of the space (line lengths) |
| green | **decay** | RT60 0.3–20 s, **∞ at the top** | how long the tail lasts; the top is infinite |
| white | **damp** | 0..1 | how much faster high frequencies die than lows |
| orange | **mix** | 0..1 | dry/wet |

## Page 2

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **pre-delay** | 0–200 ms | gap before the reverb |
| green | **early** | 0..1 | early reflections vs late tail balance |
| white | **motion** | 0..1 | line modulation: a chorused, moving tail |
| orange | **width** | 0..1, mono → wide | stereo width (master only; mono positions ignore it) |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue | (none) | |
| green | **freeze** (decay ∞, input off) | a freeze indicator, green |
| white | **page** 1 ↔ 2 | page indicator, white |
| orange | next engine in reverb (shift: previous) | engine indicator `eng0..eng3`, hall lit |

## Visual idea

- **A room/hall drawn in plan or section:** walls that move apart as size grows (move or scale, or pre-drawn variants). Reflections as lines bouncing between the walls.
- **Decay** as the length of a fading tail; at ∞ the tail becomes a closed loop or flat line.
- **Damp** as the tail's top edge falling off, or a high-shelf icon.
- **Page 2:** pre-delay as a gap before the first reflection; early vs late as two weights; motion as wavy walls; width as two speakers or arrows spreading apart.
- Distinct from the plate: plate = an object, hall = a space.

## Suggested ids

- Groups: `static`, `page1`, `page2`.
- Values: `size` (or `size0..size7`), `decay`, `decayinf`, `damp`, `mix`, `predelay`, `early`, `motion`, `width`.
- Indicators: `freeze`, `page1ind`, `page2ind`, `eng0..eng3`.
