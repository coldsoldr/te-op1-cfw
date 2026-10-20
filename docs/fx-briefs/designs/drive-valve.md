# DRIVE: valve bias (proposal, 2026-10-20)

Asset: `drive-valve.svg` (the whole drive screen, default state bias 0.5). It replaces only the white `bias` line of [drive.md](drive.md); every other element, id and mapping is unchanged. Canvas: "OP-1 Effect Screens", row "DRIVE · valve bias (proposal)", artboards `DriveValveLow` (bias 0.15, cool) and `DriveValveHigh` (bias 0.9, hot).

## What it is

A white vacuum tube standing on the zero axis in the shaper box's upper-left quadrant (x 96–124, y 37–76). That quadrant is always empty: every transfer curve has y ≥ 78 for x < 140. The tube sits at the left of the plot, roughly where the bias line was, so it reads as part of the shaper.

- Static group `valve`: the white envelope (`#DED7FF` path: straight sides, dome top), a dim plate/anode outline (`#514959` rect x 103–117, y 56–70) and a dim filled base (`#514959` rect x 96–124, y 73–76).
- `glow` (circle r 5 at 110, 63.5, fill only): the heat inside the plate, behind the filament.
- `fil` (polyline "M" zigzag, stroke only): the filament.
- `heat0..heat2` (groups, each one S-shaped path): heat shimmer rising off the dome. heat0 is the centre (x 110, y 37–45), heat1 the left (x 103), heat2 the right (x 117).

## Ids

- Removed: `bias`.
- New, driven: `glow`, `fil`, `heat0`, `heat1`, `heat2`.
- New, static: `valve` (group, no transform).

## Mapping, bias b = 0..1 (odd → even)

| id | kind | b → value |
|---|---|---|
| `fil` | line colour, continuous | lerp per channel from `#38354D` (b 0, cold) to `#DED7FF` (b 1, white hot). Alpha byte kept as read |
| `glow` | fill colour, continuous | lerp per channel from `#010101` (b 0, invisible on black) to `#514959` (b 1) |
| `heat0..heat2` | **count set** | count = min(3, floor(4·b)): b < 0.25 none; 0.25–0.5 heat0; 0.5–0.75 heat0+1; ≥ 0.75 all three |

The envelope stays full white at every b (it's the white encoder's element). The plate and base stay dim.

Colours the drawings use:

| b | `fil` | `glow` | heat lines |
|---|---|---|---|
| 0.15 (canvas, cool) | `#514D68` | `#0D0C0E` | 0 |
| 0.5 (drive-valve.svg) | `#8B86A6` | `#29252D` | 2 |
| 0.9 (canvas, hot) | `#CDC7ED` | `#494250` | 3 |

## drive_fx.cc changes

- Drop `bias_node`, its `Find(document, "bias")` and the `SetTranslate(bias_node, ...)` line (and the header comment `bias translate(0, -24 x bias)`).
- Init: find `heat0..heat2` into `void* heat_nodes[3]` (`FindAll`); `fil_line = LineStyle(self, "fil")`, `glow_fill = FillStyle(self, "glow")`; check `Find(document, "fil")` and `Find(document, "glow")` like the others so `ready` stays false if they're missing. Read both styles once with `ReadColour` to keep each alpha byte.
- Update, with `p.bias` in 0..32767:
  - `ShowFirst(heat_nodes, 3, min(3, (p.bias * 4) >> 15))` (`p.bias * 4` fits in int32).
  - For each channel i of 0..2: `fil.rgba[i] = cold[i] + ((hot[i] - cold[i]) * p.bias >> 15)` with cold `38 35 4D`, hot `DE D7 FF`; `glow.rgba[i]` the same with `01 01 01` → `51 49 59`. `WriteColour(fil_line, fil)` and `WriteColour(glow_fill, glow)`.
- Device file: replace `drive.svg`'s `bias` line with the valve elements (or ship `drive-valve.svg` through gen_fx_svg.py as `drive.svg`). The `--ids` list drops `bias` and gains `glow,fil,heat0,heat1,heat2`.
- Not proven yet: writing arbitrary in-between RGB values each frame (so far only swaps between two read colours: engine dots, the hall/plate page dots). If the device shows something wrong, fall back to 5 fixed steps `Steps(p.bias, 4)` with colours from a small table: `fil` `#38354D #514959 #807890 #AFA7C8 #DED7FF`, `glow` `#010101 #151317 #29252D #3D3743 #514959`.

## Check

`gen_fx_svg.py docs/fx-briefs/designs/drive-valve.svg <out> --ids eng0,eng1,eng2,eng3,eng4,curve0,curve1,curve2,curve3,curve4,curve5,curve6,curve7,glow,fil,heat0,heat1,heat2,tone,mix`: 0 warnings (same for the 0.15 and 0.9 variants). All polyline turns are 41° or more. One M per path, C curves only, at most 2 decimals, `display="none"` only on the `heat` groups.
