# Clouds for OP-1: screen design brief

We're putting Mutable Instruments **Clouds** (a granular audio processor) on the OP-1 as an effect. It takes the place of the stock "phone" effect, as the master effect or a synth's effect. An **orange click** on the effect page switches between Clouds and stock phone. Phone keeps its own stock screen, so this brief covers only the Clouds screen.

**Deliverable:** one file, `clouds.svg`, plus a short handover note like the one for `rings.svg` (ids, value → geometry mappings, colours). The two "screens" are **two pages inside that one file**: groups that the code shows and hides.

## Conventions (same as `rings.svg`)

- Canvas `viewBox="0 0 320 160"`. The device supplies the black background.
- Every stroke `stroke-width="1.5"`, `stroke-linecap="square"`.
- `<g id="static">` is never touched. Everything the code changes has an `id`. Draw order = file order.
- **Encoder colours, in order:** blue `#638BFF`, green `#00EE92`, white `#DED7FF`, orange/red `#FF395A`.
- Frame `#3854A5`; inactive outline `#38354D`; dim glyph/tick `#514959`; selected/accent `#DED7FF`.
- **No text.** The device can't render it, so use icons and glyphs.

## What the code can do at run time

- **Show or hide** any element or group.
- **Move or scale** any element or group (translate x/y, scale).
- **Change the stroke or fill colour** of an element. Colours are per element, so give each one that changes colour its own style.

It **can't** edit a polyline's points or a circle's radius/centre. For shapes that change form, draw **pre-made variants** that are shown one at a time (e.g. `size0`…`size7`), or move/scale them.

**Supported:** `g`, `line`, `polyline`, `polygon`, `circle`, `ellipse`, `rect`, `path`; solid colours, `opacity`, `transform`. **Not supported:** text, gradients, rounded `rx`.

## Page 1 (the main page)

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **position** | 0..1 | where in the recorded buffer grains are taken from |
| green | **size** | 0..1 | grain size (window length) |
| white | **pitch** | **bipolar**, −2..+2 octaves, centre = no shift | transposition |
| orange | **density** | **bipolar**, centre = no grains | left of centre: regular grains, more as you turn; right: random grains, more as you turn |

**Nice to have:** a "buffer strip" showing the recording, with a marker at *position* and a window as wide as *size*. It would be drawn as a marker that moves and a window that scales, and it reads very well for granular.

## Page 2

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **texture** | 0..1 | grain envelope / smoothing (towards the top: diffusion) |
| green | **dry/wet** | 0..1 | mix |
| white | **feedback** | 0..1 | output back into the buffer |
| orange | **reverb** | 0..1 | reverb amount |

Stereo spread is fixed for now (no control).

## On both pages

| Element | States | Driven by |
|---|---|---|
| **Mode indicator** | 4 modes: granular, time stretch, looping delay, spectral. One selected. **Spectral may never ship:** design it so it can sit dimmed/disabled. | blue click = next mode (shift + click = previous) |
| **Freeze** | on / off. Freeze stops recording, so the buffer holds still. | green click toggles |
| **Page indicator** | page 1 / page 2 | white click toggles |
| *(optional)* **CPU meter** | 6 segments, hidden normally | shown for device tests, like the Rings screen's rings |

The orange click switches to phone, so it needs nothing on this screen. Shift + turn is a fine adjustment, so it doesn't need anything either.

## Suggested ids (rename freely, just list them in the note)

- Groups: `static`, `page1`, `page2`.
- Value elements: `position`, `size`, `pitch`, `density`, `texture`, `wet`, `feedback`, `reverb`, each moved or scaled. Or, for variant sets, `size0..N` etc.
- Indicators: `mode0..mode3`, `freeze` (or `freeze_on` / `freeze_off`), `page1ind`, `page2ind`, optional `cpu0..cpu5`.

For each value element, the note should say how a value maps to geometry. As for `rings.svg`, e.g. "handle moves from y=114 (0.0) to y=14 (1.0)". For the bipolar values, also give the centre.
