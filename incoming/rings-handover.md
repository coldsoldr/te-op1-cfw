# Rings for OP-1: screen SVG handover

Asset: `rings.svg` (320×160, same canvas and conventions as `dsynth.svg`, `fm.svg`, `iter.svg`, `pd.svg`, `st.svg`).
Preview: `Rings Preview.dc.html`.

## Conventions

- Canvas: `viewBox="0 0 320 160"`, black background supplied by the device.
- Every stroke is `stroke-width="1.5"`, `stroke-linecap="square"`. Keep this on anything you generate.
- `<g id="static">` is never touched at runtime. Everything else has an `id` and is updated by code.
- Draw order matters. Keep elements in file order (`static` → `structure` → `string` → `position` → `brightness` → `damping` → `model*` → `poly*`). The slider handles contain a black outer rect that masks the scale line behind them.
- The `<metadata>` block can be stripped; it isn't used.

## Palette

| Role | Hex |
|---|---|
| Frame / scale spine | `#3854A5` |
| Structure (blue encoder) | `#638BFF` |
| Brightness (green encoder) | `#00EE92` |
| Damping (white encoder) | `#DED7FF` |
| Position (orange/red encoder) | `#FF395A` |
| Selected button / accent tick | `#DED7FF` |
| Inactive outline | `#38354D` |
| Inactive glyph / dim tick | `#514959` |

## Layout

- Left panel (x 4.5–186.5, y 2.5–144.5), split by a vertical line at x=120.
  - Left of the split: structure rings (centre 62,64) above the position string (y=130).
  - Right of the split: two vertical sliders. Brightness on spine x=137, damping on spine x=169. Both run from y=14 (value 1.0) to y=114 (value 0.0), with 11 ticks every 10px. Bright ticks at y=14, 64, 114 mark 1.0, 0.5, 0.0. Static icons under each slider: green crosshair (brightness) at 137,130 and a white decay curve (damping) at ~169,130.
- Right top box (y 2.5–96.5): six resonator model buttons, 2×3 grid.
- Right bottom box (y 102.5–144.5): three polyphony buttons.

## Dynamic elements

### Structure: `#ring0`–`#ring5`, `#core`
Concentric circles at 62,64, radius 8·(n+1).
- Lit count `k = 1 + round(structure * 5)` (1–6).
- `ring0..ring(k-1)` stroke `#638BFF`, the rest `#38354D`.
- `#core` is always `#638BFF`.

### Position: `#pluck`, `#posdot` (inside `#position`)
`#string` is the resting string (16,130 → 108,130), don't change it.
- `x = 16 + position * 92`, `y = 125`
- `#pluck` points = `16,130 x,125 108,130`
- `#posdot` `cx = x`, `cy = 125`
- Optional: on note-on, briefly move `y` further from 130 (e.g. 120) and ease back to 125 to show the pluck.

### Brightness: `#brightness`
Handle group drawn at y=44 (value 0.7).
- `handleY = 114 - brightness * 100`
- Apply `transform="translate(0, handleY - 44)"` to the group.

### Damping: `#damping`
Handle group drawn at y=64 (value 0.5).
- `handleY = 114 - damping * 100`
- Apply `transform="translate(0, handleY - 64)"`.

Continuous movement is fine. Snap to 10px ticks if you prefer to match the stepped look of the other engines.

### Resonator model: `#model0`–`#model5`
Each group = a rounded `rect` + a digit glyph (1–6) drawn as polylines/lines.
- Selected: rect stroke `#DED7FF`, glyph stroke `#DED7FF`.
- Unselected: rect stroke `#38354D`, glyph stroke `#514959`.
- File default: `model0` selected.

Index → Rings model:
0. Modal resonator
1. Sympathetic strings
2. Modulated / inharmonic string
3. FM voice
4. Quantized sympathetic strings
5. String + reverb

### Polyphony: `#poly0`–`#poly2`
Rect + 1, 2 or 4 dots (`circle`, fill and stroke equal).
- Selected: rect stroke `#DED7FF`, dots fill/stroke `#DED7FF`.
- Unselected: rect stroke `#38354D`, dots fill/stroke `#514959`.
- 0 → 1 voice, 1 → 2 voices, 2 → 4 voices. File default: `poly0`.

## Suggested control mapping

| Control | Parameter |
|---|---|
| Blue encoder | Structure |
| Green encoder | Brightness |
| White encoder | Damping |
| Orange encoder | Position |
| Shift + blue (or a dedicated key) | Model 0–5 |
| Shift + green (or a dedicated key) | Polyphony 1/2/4 |

The colours on screen already match these encoders. The model and polyphony bindings are a suggestion; confirm them against how the other ported engines handle secondary parameters.

## Not shown on screen

Frequency/pitch comes from the keyboard. There is no on-screen element for it, by design.
