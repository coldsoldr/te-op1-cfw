# SHIFT: screen handover (draft)

Asset: `shift.svg` (ships in frequency-shift mode). Shared conventions: [plate.md](plate.md); modulation family layout: [ensemble.md](ensemble.md).

Static: lane baselines y 62 (L) and y 124 (R), x 48–276; the dim original spectrum in each lane (6 harmonics at x = 112 + 18·k, heights 24, 18, 14, 11, 8, 6).

## Driven ids

`eng0..eng2`, `mode0`, `mode1`, `sweep`, `modeshift`, `modering`, `shiftL`, `shiftR`, `spread`, `ringupL`, `ringdnL`, `ringupR`, `ringdnR`, `fb1..fb4`, `mix`.

| id | Value → geometry |
|---|---|
| `shiftL`, `shiftR` (blue combs, drawn on the originals) | f = shift, bipolar −1..+1 (centre 0; fine near 0 as the encoder curve already is). `shiftL: translate(60·f + 20·sp, 0)`, `shiftR: translate(60·f − 20·sp, 0)`, sp = spread 0..1 (L up, R down; 60 px = 2 kHz). Demo f 0.3, sp 0.5 → +28 / +8 |
| `spread` (white bracket) | `sx(max(sp, 0.05), 160)`. Mono positions: sp = 0 |
| `sweep` (blue dot on the dial) | per frame: rotates at a rate ∝ \|f\| (scaled down to be visible), direction = sign of f. Drawn at 40° |
| `ringupL/R`, `ringdnL/R` (ring mode combs, in hidden group `modering`) | `translate(+60·f, 0)` (sum) and `translate(−60·f, 0)` (difference), f = carrier 0..1 |
| `fb1..fb4` (green chevrons, x 216–250 on y 80) | **count set**, n = round(4·feedback). When f < 0 mirror them: `matrix(−1,0,0,1,466,0)` on each group (about x 233) |
| `mix` | mix bar |

## Mode (blue click)

- `mode0` (arrow, x 76–88, y 14) = frequency shift; `mode1` (ring, r 4 at 99, 14) = ring mod. Active stroke `#638BFF`, inactive `#514959`.
- Show `modeshift` or `modering` to match.

## Default-state colours

Blue combs, sweep, mode0. Green chevrons. White spread. Orange mix and `eng2`. Original combs and dial `#514959`.
