# New OP-1 effects: screen design briefs

Nine new effect modules for the OP-1, grouped into **categories**. A stock effect slot becomes a category: it keeps its stock effect, and an **orange click** on the effect page cycles through the category's engines (shift + orange click goes back).

| Category (list name) | Stock slot | Engines, in orange-click order |
|---|---|---|
| **reverb** | spring | spring → **plate** → **hall** → clouds |
| **modulation** | cwo | cwo → **ensemble** → **shift** |
| **dirt** | punch | punch → **drive** → **tape** → **fold** → **crush** |
| **delay** | delay | delay → **tape echo** |

New engines are in bold. The stock engines keep their stock screens, and Clouds keeps the screen it has.

One brief per new engine:

| Brief | Category | Pages |
|---|---|---|
| [plate.md](plate.md) | reverb | 2 |
| [hall.md](hall.md) | reverb | 2 |
| [drive.md](drive.md) | dirt | 1 |
| [tape.md](tape.md) | dirt | 1 |
| [fold.md](fold.md) | dirt | 1 |
| [crush.md](crush.md) | dirt | 1 |
| [ensemble.md](ensemble.md) | modulation | 1 |
| [shift.md](shift.md) | modulation | 1 |
| [tape-echo.md](tape-echo.md) | delay | 1 |

**Deliverable per engine:** one `<name>.svg` plus a short handover note (ids, value → geometry mappings, colours), as for `clouds.svg` and `grids.svg`. A two-page engine has both pages in one file, as groups `page1` / `page2` that the code shows and hides.

## Conventions (every screen)

- **Canvas:** `viewBox="0 0 320 160"`, origin top-left, y grows down. The device draws the black background.
- **Strokes:** `stroke-width="1.5"`, `stroke-linecap="square"`.
- **Ids:** `<g id="static">` is never touched. Everything the code changes has an `id`. Draw order = file order.
- **Encoder colours, in order:**

  | Encoder | Hex |
  |---|---|
  | blue | `#638BFF` |
  | green | `#00EE92` |
  | white | `#DED7FF` |
  | orange/red | `#FF395A` |

- **Other colours:** frame `#3854A5`; inactive outline `#38354D`; dim glyph/tick `#514959`.
- **Colour coherence:** an element's colour is the colour of the encoder whose turn or click controls it. Keep frames to a minimum, and only where they tie to a control.
- **No text.** The device can't render it, so use icons and glyphs.
- **Follow the LFO:** an LFO can modulate any encoder, and the screen redraws every frame, so every value element must move smoothly through its range.

### Safe area: the viewing angle

The OP-1's screen is viewed at an angle, which makes the **bottom and right edges** hard to see. Stock screens rarely draw past x ≈ 296–311 or y ≈ 142–147.

- **Keep all content inside x 16–296, y 8–140.**
- Keep anything you need to read (values, indicators) well away from the bottom and right edges. Put the important things top-left and centre.

### Category identity and the engine indicator

- Each category's engines should feel like a family, e.g. a shared motif or framing for all the dirt screens.
- Every new screen shows **which engine of its category is active**: a small row of dots or glyphs, one per engine including the stock one, with the active one lit. It is **orange**, because the orange click cycles it. Place it top-left or top-centre (not bottom-right). The ids are `eng0..engN`.

### What the code can do at run time

- **Show or hide** any element or group.
- **Move or scale** any element or group (translate x/y, scale).
- **Change the stroke or fill colour** of an element that has its own style. A fill can only be recoloured where the file sets one, so use a fill of `#010101`, not `none`, if it will change.
- **It can't** edit polyline points or a circle's radius or centre. For shapes that change form, draw **pre-made variants** shown one at a time (`curve0..curveN`), or move and scale them.
- **Supported:** `g`, `line`, `polyline`, `polygon`, `circle`, `ellipse`, `rect`, `path`; solid colours, opacity. **Not supported:** text, gradients, rounded `rx`.

### Parser limits (a bad file stops the boot; our converter script enforces these)

- Ids: ASCII, **no `_x`**, at most one `_`.
- **One `M` per path:** split subpaths into separate paths.
- **No transforms on groups;** draw driven elements in real screen coordinates.
- **No arcs (`A`)**, no exponents, at most 2 decimals.
- `display="none"` only on groups.

### Budget

Aim for about 25 driven ids and about 80 elements per screen. Each screen is redrawn every frame next to the audio, so lighter screens are better.
