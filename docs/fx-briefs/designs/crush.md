# CRUSH: screen handover (draft)

Asset: `crush.svg`. Shared conventions: [plate.md](plate.md); dirt family layout: [drive.md](drive.md).

Static: a dim reference sine in the box (x 84–196, amplitude 32), and a dim clock baseline in the after-stage (y 86, x 236–286).

## Driven ids

`eng0..eng4`, `rate0..rate7`, `bits0..bits7`, `jit0..jit5`, `mix`.

| id | Value → geometry |
|---|---|
| `rate0..rate7` (blue sample-and-hold staircase) | **variant set**, index = round(7·r), r 0..1 along the rate range (44.1 kHz → 500 Hz, log). Step width `4·1.32^k` px: 4 → 28. Demo `rate3` |
| `bits0..bits7` (green level ticks, x 74–79 on the box's left edge) | **count set**, n = 1 + round(7·(bits − 2)/14): 16 bits → 8 ticks, 2 bits → 1. The ticks are ordered so any first n are spread out: y 78, 38, 118, 58, 98, 48, 108, 68. Demo n 5 (≈ 10 bits) |
| `jit0..jit5` (white clock ticks at x 240 + 9·i, y 70–86) | `translate(4·jitter·d_i, 0)`, d = 0, 0.375, −0.25, 0.5, −0.375, 0.25 (fixed pattern), plus optional per-frame noise of the same size. 0 = evenly spaced |
| `mix` | mix bar |

## Default-state colours

Blue staircase, green ticks, white clock, orange mix and `eng4`.
