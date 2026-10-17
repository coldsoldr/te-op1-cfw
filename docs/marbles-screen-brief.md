# Marbles for OP-1: screen design brief

We're putting Mutable Instruments **Marbles** (a random melody and rhythm generator) on the OP-1 as a **synth sequencer**. It takes the place of the stock **tombola** sequencer. An **orange click** on the sequencer screen switches between Marbles and stock tombola. Tombola keeps its own stock screen, so this brief covers only the Marbles screen.

**Deliverable:** one file, `marbles.svg`, plus a short handover note: the id list, how each value maps to geometry, and the colours. The two pages live **inside that one file** as groups the code shows and hides.

## Keep it simple (please read first)

The first drafts were too complicated. The screen is 320 × 160 pixels and is read at a glance while playing. Aim for the feel of the stock OP-1 screens: a few bold shapes, one colour per encoder, nothing decorative that has to be driven.

- **One picture per page, plus four value indicators.** Each indicator belongs to one encoder, in its colour, in a fixed place.
- **Same layout on both pages.** Only what the four indicators mean changes; the indicators themselves can be identical shapes.
- **Prefer moving or scaling one element** over sets of pre-drawn variants. Use a variant set only for a choice between a few named things (a model or scale icon, at most 7).
- **Budget:** at most about **25 ids** that the code drives and about **80 elements** in total.
- **No scenery** that has to change with the values: no landscapes, no per-value redraws. A metaphor is welcome if it is one simple shape.
- **Live activity** is limited to the three gate lights (below). Nothing else animates.

## Conventions (same as `rings.svg` and `clouds.svg`)

- Canvas `viewBox="0 0 320 160"`. The device supplies the black background.
- Every stroke `stroke-width="1.5"`, `stroke-linecap="square"`.
- `<g id="static">` is never touched. Everything the code changes has an `id`. Draw order = file order.
- **Encoder colours, in order:** blue `#638BFF`, green `#00EE92`, white `#DED7FF`, orange/red `#FF395A`.
- Frame `#3854A5`; inactive outline `#38354D`; dim glyph `#514959`; selected/accent `#DED7FF`.
- **No text.** Use icons and glyphs.
- The code can: **show/hide** any element or group; **move** (translate) or **scale** it; **change the stroke or fill colour** of an element (colours are per element, so give each recoloured element its own style). It can't edit points or radii: use transforms or a few variants.
- Supported: `g`, `line`, `polyline`, `polygon`, `circle`, `ellipse`, `rect`, `path` (lines and curves; **no arcs**, we convert them, but avoid them if you can). Not supported: text, gradients, rounded corners.

## How it plays (what the screen should communicate)

- Marbles has a clock and three gates, **T1, T2, T3**. T2 is the steady clock; T1 and T3 fire randomly around it. **Each gate plays a note**, so gates can overlap into chords.
- Each gate's pitch is a random value (**X1, X2, X3**). The pitch comes either from a **scale** around the last key pressed, or from the **keys** you hold (a latched chord).
- **Deja vu** makes the randomness repeat: off = pure random, on = loops sometimes, locked = a fixed loop of *length* steps.

## Page T (rhythm)

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **rate** | 10 steps: 1 bar, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 | the clock's beat division |
| green | **bias** | 0..1, centre = even | which of T1 / T3 fires more |
| white | **jitter** | 0..1 | timing looseness |
| orange | **deja vu** | 0..1, centre = locked loop | how much the randomness repeats (shared by both pages) |

Click states on this page:
- **T model** (blue click, shift + click = back): one of **7**: coin toss, clusters, drums, independent, divider, three states, Markov. A small icon or a 7-position marker.
- **T deja vu** (green click): off / on / locked.

## Page X (pitch)

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **spread** | 0..1 | how widely the pitches vary |
| green | **bias** | 0..1, centre = middle | whether pitches lean low or high |
| white | **steps** | 0..1 | smooth (left) → snapped to the scale (right) |
| orange | **length** | 1..16 | loop length for deja vu |

Click states on this page:
- **Scale** (blue click, shift + click = back): one of **6** (Marbles' preset scales). An icon or 6-position marker.
- **X deja vu** (green click): off / on / locked.

## On both pages

| Element | States | Driven by |
|---|---|---|
| **Gate lights** | three lights, T1 / T2 / T3, each on while its gate is high (they flash with the notes) | live |
| **Page indicator** | T / X | white click |
| **Pitch source** | scale / keys | shift + white |
| **Deja vu state** | off / on / locked, for T and for X (two small indicators, or one that shows the current page's) | green click |

The orange click switches to tombola, so it needs nothing on this screen. Shift + turn is a fine adjustment and needs nothing either.

**Optional, only if it stays simple:** the three gate lights could move up and down with X1–X3 (one translate each), so the screen shows the melody as three dots. Skip it if it adds clutter.

## Suggested ids (rename freely, list them in the note)

- Groups: `static`, `pageT`, `pageX`.
- Encoder indicators: `ind0`..`ind3` (one shape each, moved or scaled; the code reuses them on both pages), or `t_rate`, `t_bias`, `t_jitter`, `dejavu`, `x_spread`, `x_bias`, `x_steps`, `x_length`.
- States: `model0`..`model6`, `scale0`..`scale5`, `dv_off` / `dv_on` / `dv_locked` (or per page), `src_scale` / `src_keys`, `pageTind` / `pageXind`.
- Live: `gate1`, `gate2`, `gate3`.

For each value element, the note should say how a value maps to geometry, e.g. "bar scales from 0 (0.0) to 40 px (1.0)" or "marker moves from x=20 (0.0) to x=120 (1.0)". For the bipolar values (bias, deja vu), give the centre. For rate and length, say how the 10 or 16 steps map.
