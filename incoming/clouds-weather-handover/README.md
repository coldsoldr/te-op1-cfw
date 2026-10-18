# Clouds for OP-1: screen SVG handover (weather theme)

Files:
- `clouds.svg`: the asset to ship (320×160). It opens on page 1 in its default state.
- `preview/page2.svg` and `preview/state-example.svg`: reference renders only, not for the device. The example state has freeze on, time stretch mode, regular rain, a big cloud, `position` at `translate(-29,0)` and `pitch` at `translate(0,-10)`.

## Concept

Page 1 is a cloud raining on a little town. The cloud is the grain window: its size is grain size, its horizontal place is buffer position, and its height is pitch. The raindrops are the grains: regular drops on one side of density, random drops on the other, and no rain in the middle. When freeze is on, the cloud has a frozen face.

Page 2 is the weather around it: wind (texture), a puddle (dry/wet), a rainbow (feedback) and thunder echoes (reverb).

The visuals are playful rather than exact. The mappings below are the only things the code has to follow.

## Conventions

- `viewBox="0 0 320 160"`. The device supplies the black background.
- Every stroke is `stroke-width="1.5"` and `stroke-linecap="square"`. There is no text, gradient or `rx`.
- `<g id="static">` holds the three frames and the sun (top right of the left frame). It is never touched.
- Draw order is file order. The cloud shapes have a `#010101` fill, which masks the sun when the cloud drifts over it. Keep them in file order.
- Hidden elements have `display="none"` in the file. To show one, remove that attribute (or map it to your own show/hide).
- **Variant sets** (`sizeN`, `textureN`, `wetN`) are "show exactly one". **Count sets** (`regN`, `rndN`, `fbN`, `rvN`) are "show the first n, hide the rest".
- The only transforms are two translates (`#position` and `#pitch`). Nothing is scaled.
- The `<metadata>` block can be stripped; it isn't used.

## Palette

| Role | Hex |
|---|---|
| Blue encoder (position / texture) | `#638BFF` |
| Green encoder (size / dry-wet) | `#00EE92` |
| White encoder (pitch / feedback), face, selected | `#DED7FF` |
| Orange encoder (density / reverb) | `#FF395A` |
| Frame | `#3854A5` |
| Inactive outline | `#38354D` |
| Dim scenery (town, sun, page-2 cloud) | `#514959` |
| Mask fill | `#010101` |

## Layout

- Left frame (x 4.5–226.5): the current page. The ground line is at y=134.
- Top-right frame: 2×2 mode buttons. The optional CPU row is at y=88.
- Bottom-right frame: freeze snowflake (left) and page indicator squares (right).

## Page 1: `#page1` (visible by default)

Nesting: `#position` › (`#shadow`, `#pitch` › (`size0..5`, `face`, `face_frozen`, `reg0..7`, `rnd0..7`)).
The cloud, face and rain move together. The shadow follows position only.

### Position (blue, 0..1): `#position`
The group is drawn at 0.5, with the cloud centred on x≈115.
- `translate(x, 0)`, `x = (pos − 0.5) * 120`
- The range is x=−60 (0.0) to +60 (1.0). The largest cloud stays inside the frame at both ends.

### Size (green, 0..1): `#size0`–`#size5`
Six cloud outlines, from small to large. Show exactly one; the file default is `size3`.
- `k = round(size * 5)`

### Pitch (white, bipolar −2..+2 oct): `#pitch`
The cloud rises when pitch goes up and sinks when it goes down. Drawn at centre (no shift).
- `translate(0, y)`, `y = −oct * 7.5` (equivalently `y = −semitones * 0.625`)
- −2 oct → y=+15 (lowest; the rain stops just above the ground). +2 oct → y=−15. Centre → 0.

### Density (orange, bipolar −1..+1): `#reg0`–`#reg7`, `#rnd0`–`#rnd7`
Raindrops under the cloud. Only one set is visible at a time.
- `n = round(|d| * 8)` (0..8)
- `d < 0`: show `reg0..reg(n−1)` (an even grid of drops). Hide all `rnd`.
- `d > 0`: show `rnd0..rnd(n−1)` (scattered, slanted drops). Hide all `reg`.
- `n = 0` (centre): hide every drop. This means no grains, so no rain.
- The file default is `rnd0..rnd4`.
- Optional: to animate the rain, cycle a small y offset (0→6px, wrap) on the visible drops, or toggle them per frame. This isn't required.

### Freeze face: `#face`, `#face_frozen`
These follow the freeze state (see "Both pages").
- Off: `face` shown, `face_frozen` hidden.
- On: the reverse. The frozen face has dash eyes and a shivering zig-zag mouth.

### Shadow: `#shadow`
A blue line on the ground under the cloud. It follows position only, so there's nothing to drive.

## Page 2: `#page2` (hidden by default)

The static scenery is the ground line (with a gap for the puddle), a dim storm cloud and a red lightning bolt.

### Texture (blue, 0..1): `#texture0`–`#texture4`
Two wind lines. 0 is sharp zig-zag gusts, the middle is nearly flat, and 1 is smooth rolling waves. Show exactly one; the file default is `texture2`.
- `k = round(texture * 4)`

### Dry/wet (green, 0..1): `#wet0`–`#wet4`
A puddle on the ground (x≈55, y=134) that grows with the wet mix. Show exactly one; the file default is `wet2`.
- `k = round(wet * 4)`
- Optional: hide all of them when `wet == 0` (fully dry).

### Feedback (white, 0..1): `#fb0`–`#fb4`
Rainbow arcs centred at x=140 on the ground, from the inner arc (`fb0`) to the outer one. This is a count set; the file default is `fb0..fb2`.
- `n = round(feedback * 5)` (0..5); show `fb0..fb(n−1)`.

### Reverb (orange, 0..1): `#rv0`–`#rv4`
Thunder echo arcs on both sides of the bolt, from the inner pair (`rv0`) outwards. This is a count set; the file default is `rv0..rv2`.
- `n = round(reverb * 5)` (0..5); show `rv0..rv(n−1)`.

## Both pages

### Page switch (white click)
- Page 1: show `page1`, hide `page2`. Set `#page1ind` fill and stroke to `#DED7FF`. Set `#page2ind` fill to `none` and stroke to `#38354D`.
- Page 2: the reverse.

### Mode (blue click / shift+click): `#mode0`–`#mode3`
Each mode has `#modeN_box` (a rect) and `#modeN_icon` (a group; recolour the stroke of **every child**).

| Index | Mode | Icon |
|---|---|---|
| 0 | granular | three grains |
| 1 | time stretch | ↔ between bars |
| 2 | looping delay | loop arrow |
| 3 | spectral | spectrum bars |

- Selected: box `#DED7FF`, icon children `#DED7FF`.
- Unselected: box `#38354D`, icon children `#514959`.
- Spectral disabled (the file default): box `#38354D`, icon `#38354D`, with the `#mode3_off` strike line shown. When spectral ships, hide `#mode3_off` and treat it like the other modes. While it's disabled, skip index 3 when cycling.
- The file default is mode 0.

### Freeze (green click)
- Off (default): `#freeze_off` shown (dim snowflake), `#freeze_on` hidden, `#face` shown, `#face_frozen` hidden.
- On: swap all four.

### CPU meter (optional): `#cpu`
The group is hidden by default. `#cpu0`–`#cpu5` are segments at y=88, left to right; 0–3 are green and 4–5 are orange.
- `n = ceil(load * 6)`; show `cpu0..cpu(n−1)`.

## Not on screen

The orange click (switch to phone) and shift+turn (fine adjust) have no visual. Stereo spread is fixed.

## Id list

`static`, `page1`, `position`, `shadow`, `pitch`, `size0`–`size5`, `face`, `face_frozen`, `reg0`–`reg7`, `rnd0`–`rnd7`, `page2`, `texture0`–`texture4`, `wet0`–`wet4`, `fb0`–`fb4`, `rv0`–`rv4`, `mode0`–`mode3`, `mode0_box`–`mode3_box`, `mode0_icon`–`mode3_icon`, `mode3_off`, `cpu`, `cpu0`–`cpu5`, `freeze_off`, `freeze_on`, `page1ind`, `page2ind`.

## Changes from the sliders version

If the code was built against the earlier `clouds.svg`, these are the differences:
- Removed: `wave`, `wf0`–`wf49`, `marker`, `markerhead`, `density` (handle), `texture`, `wet`, `feedback`, `reverb` (handles).
- `size` is now `size0`–`size5` (6 variants, previously 8).
- `position` and `pitch` are still translate-only groups, but they use the new mappings above. Pitch is now a vertical move.
- Added: `shadow`, `face`, `face_frozen`, `reg*`, `rnd*`, `texture*`, `wet*`, `fb*`, `rv*`.
- Unchanged: `mode*`, `mode3_off`, `cpu*`, `freeze_*`, `page*ind`.
