# TAPE: screen handover (draft)

Asset: `tape.svg`. Shared conventions: [plate.md](plate.md); dirt family layout: [drive.md](drive.md).

Inside the shaper box: two dim reels (circles r 16 at (112,56) and (168,56)), the tape path to y 92, a dim head at (134–146, 100–106), and the wow → flutter scale (slow-wave glyph x 84–96, track x 102–178, fast-wave glyph x 184–196, all at y 118).

## Driven ids

`eng0..eng4`, `reelL`, `reelR`, `sat0..sat7`, `wobble`, `age`, `age0..age5`, `mix`.

| id | Value → geometry |
|---|---|
| `sat0..sat7` (blue wave on the tape, x 112–168, y 85–99) | **variant set**, index = round(7·saturate). `y = 92 − 7·tanh(g·sin θ)/tanh(g)`, `g = 0.6·1.45^k`, 2 cycles. Demo `sat4` |
| `wobble` (green marker, x 102, y 113–123) | `translate(76·wobble, 0)`: x 102 (wow) → 178 (flutter). Demo 0.45 |
| `reelL`, `reelR` (green 3-spoke hubs) | per frame: `translate(dx, dy)` with \|d\| ≤ 2·wobble px, at the wobble's rate (slow wow at the bottom of the range, fast flutter at the top). Rest = no transform |
| `age` (white low-pass glyph (236,70) (272,70) (284,94)) | `sx(1 − 0.6·age, 236)`: corner x 272 → 250 as the tape wears |
| `age0..age5` (white hiss specks above the glyph) | **count set**, n = round(6·age). Demo 3 |
| `mix` | mix bar |

## Default-state colours

Blue sat, green hubs and wobble marker, white age and specks, orange mix and `eng2`. Reels, tape, head, scale glyphs `#514959`; scale track `#38354D`.
