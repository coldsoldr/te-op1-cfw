# TAPE ECHO: screen handover (draft)

Asset: `tape-echo.svg`. Shared conventions: [plate.md](plate.md). The lower time strip (impulse at x 24, baseline y 134) is the same as the reverbs'.

## Driven ids

`eng0`, `eng1`, `hold`, `loop`, `rechead`, `head1..head3`, `mark1..mark4`, `age`, `echo1..echo8`, `mix`.

| id | Value → geometry |
|---|---|
| `mark1..mark4` (blue ticks on the loop's top run, x 96, 126, 156, 186, y 31–37) | per frame: `translate(dx, 0)`, all four moving at a speed ∝ 1/time, wrapping within x 90–210 (spacing 30). They show the tape running |
| `echo1..echo8` (green bars, drawn at x = 24 + 20·i, y 106–134) | Δ = 8 + 24·t, t = time 0..1 (30 ms–1.5 s, log). Height scale `h_i = max(fb^(i−1), 0.02)`, fb = feedback clamped to 1 (≥ 1: all full). `transform="matrix(1,0,0,h_i,(Δ−20)·i,134·(1−h_i))"`. Demo t 0.5 (Δ 20), fb 0.6 |
| echo colours (age) | bar i is `#00EE92` while i ≤ round(8·(1 − age)), else `#514959`. Demo age 0.4 → bars 6–8 dim |
| `age` (white wow line in the loop, x 100–200, y 57–65) | `sy(max(age, 0.05), 61)`; optionally slide it a few px per frame for the flutter |
| `head1..head3` (play heads at x 130, 170, 210, under the bottom run) | lit per pattern (blue click): fill and stroke `#638BFF`; off: fill `#010101`, stroke `#514959`. Demo pattern = heads 1 + 3 |
| `mix` | mix bar |

## Hold (green click)

- `hold` (∞ glyph at x 106–118, y 14): off `#514959`, on `#00EE92`.
- `loop` stroke: `#514959` → `#00EE92` while holding.
- `rechead` stroke: `#514959` → `#38354D` (dimmed: input off).

## Default-state colours

Blue marks and lit heads, green echoes, white age line, orange mix and `eng1`. Loop, record head, hold (off) `#514959`.
