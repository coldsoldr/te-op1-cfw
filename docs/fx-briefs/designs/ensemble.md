# ENSEMBLE: screen handover (draft)

Asset: `ensemble.svg`. Shared conventions: [plate.md](plate.md).

## Modulation family layout (ensemble, shift)

- Two lanes: L on top, R below (guides `#38354D`). Ensemble lane centres y 48 / 112; shift baselines y 62 / 124.
- **Sweep dial** on the left: dim circle r 8 at (30, 80) with a blue dot `sweep` that the code moves round it every frame.
- **Spread bracket** on the centre line y 80: polyline (120,76)(120,84)(120,80)(200,80)(200,84)(200,76), scaled in x about 160.
- Engine dots `eng0..eng2` (cwo, ensemble, shift), mix bar top right.

## Driven ids

`eng0..eng2`, `sweep`, `rate`, `voiceL1..voiceL3`, `voiceR1..voiceR3`, `depth`, `mix`.

| id | Value → geometry |
|---|---|
| `sweep` (blue dot, drawn at the top of the dial, 30, 72) | per frame: `translate(8·sin φ, 8 − 8·cos φ)`, φ = LFO phase. Its speed is the rate |
| `rate` (blue bar x 22, y 94, 16 × 3, over a static track) | `sx(u, 22)`, u = rate 0..1 (0.05–8 Hz, log), keep u ≥ 0.02 |
| `voiceL1..3`, `voiceR1..3` (white sines, 2 cycles, x 60–264, amplitude 12) | **count set** per lane: n = 1 + round(2·voices). Drawn with phase offsets of 0, +5, −5 px (L) and 0, −5, +5 px (R). Per frame the code translates each group by `depth·10·sin(φ + i·2π/3)` px (rest = no transform). Mono positions may hide the R lane |
| `depth` (green bracket) | `sx(0.1 + 0.9·depth, 160)` |
| `mix` | mix bar |

## Default-state colours

Blue sweep and rate, green depth, white voices, orange mix and `eng1`. Dial `#514959`; lanes and rate track `#38354D`.
