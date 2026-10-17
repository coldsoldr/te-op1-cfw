# Grids for OP-1: screen design brief

We're putting Mutable Instruments **Grids** (a topographic drum sequencer) on the OP-1 as the **drum sequencer**. It takes the place of the stock **pattern** sequencer, which will show as **"MULTI"** in the sequencer list. An **orange click** on the sequencer screen switches between Grids and stock pattern. Pattern keeps its own stock screen, so this brief covers only the Grids screen.

**Deliverable:** one file, `grids.svg`, plus a short handover note: the id list, how each value maps to geometry, and the colours.

## Keep it simple (please read first)

The first drafts were too complicated. The screen is 320 × 160 pixels and is read at a glance while playing. Aim for the feel of the stock OP-1 screens: a few bold shapes, one colour per encoder, nothing decorative that has to be driven.

- **One picture plus four value indicators.** Each indicator belongs to one encoder, in its colour, in a fixed place.
- **Shift shows the second set of controls in the same places**: the four indicators change meaning, not position or shape.
- **Prefer moving or scaling one element** over sets of pre-drawn variants.
- **No step-by-step bar graphs** and no per-step levels: 3 parts × 32 steps is far too much to drive and read.
- **Budget:** at most about **25 ids** that the code drives and about **80 elements** in total.
- **Live activity** is limited to three hit lights and a playhead (below). Nothing else animates.

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

- Grids plays three drum parts: **kick (BD)**, **snare (SD)** and **hi-hat (HH)**, each on a drum key you choose.
- The rhythm comes from a **map**: an X/Y position on a 5 × 5 grid of drum patterns, blended between the nearest ones. Moving X/Y morphs the groove.
- Each part's **fill** sets how busy it is. **Chaos** adds random variation each bar. **Accents** play louder (or on a second key, if one is set).
- **Holding a part's key** makes that part fill up while you hold it.
- A second mode, **Euclidean**, replaces the map: each part spreads N hits evenly over a loop of L steps.
- Default resolution is **16ths** (a bar = 16 steps); shift + blue switches to 32nds.

## Main controls (drums mode)

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **map X** | 0..1 | position on the map |
| green | **map Y** | 0..1 | position on the map |
| white | **chaos** | 0..1 | random variation per bar |
| orange | **swing** | 0..1 | off-beat delay |

**Suggested picture:** a square for the map with **one dot** at (X, Y). The dot is one element moved in x and y; the 5 × 5 grid behind it is static.

## Shift held: fills

| Encoder | Parameter | Range |
|---|---|---|
| blue | **BD fill** | 0..1 |
| green | **SD fill** | 0..1 |
| white | **HH fill** | 0..1 |
| orange | **accent** (how much louder accents are) | 0..1 |

The same four indicators show these values while shift is held. A small "shift" marker or a change of the indicators' icon is enough to tell the pages apart.

## Euclidean mode (blue click)

| | Blue | Green | White | Orange |
|---|---|---|---|---|
| plain | BD length (1..32) | SD length | HH length | swing |
| shift | BD hits (0..length) | SD hits | HH hits | accent |

The map picture can stay; the dot isn't used in this mode (hide it, or dim the map).

## Clicks and states (shown on the screen)

| Click | Plain | With shift | On screen |
|---|---|---|---|
| blue | mode: drums ↔ Euclidean | resolution: 16ths ↔ 32nds | a mode indicator (2 states), a resolution indicator (2 states) |
| green | **re-roll**: a new chaos draw at the next bar | accent mode: louder ↔ second key | a short flash on re-roll (optional); accent mode (2 states) |
| white | **assign**: select BD → SD → HH → off; while selected, the next key pressed becomes that part's sound | random voice on/off | which part is selected (highlight one of the three part markers) |
| orange | swap Grids ↔ stock pattern | **print**: copy the current bar into stock pattern | nothing (print could flash briefly, optional) |

## On the screen, always

| Element | States | Driven by |
|---|---|---|
| **Three part markers** (BD, SD, HH) | each lights on its hit (brighter for an accent); one can be highlighted while assigning | live, white click |
| **Playhead** | the step in the bar: 16 positions (32 in 32nds mode) | live |
| **Fill boost** | a part is being filled by its held key | held keys (can share the part marker: e.g. it stays lit) |

## Suggested ids (rename freely, list them in the note)

- Groups: `static`, `map` (the picture), `shift` (marker for the fills page).
- Encoder indicators: `ind0`..`ind3` (one shape each, moved or scaled; reused for every page and mode).
- Map: `dot`.
- States: `mode_drums` / `mode_euclid`, `res16` / `res32`, `accent_vel` / `accent_key`, `rnd_on`.
- Parts: `part_bd`, `part_sd`, `part_hh` (recoloured or shown/hidden for hit, accent, assign, fill).
- Playhead: `playhead` (moved in x by step; say the x positions of step 0 and step 15/31).

For each value element, the note should say how a value maps to geometry, e.g. "dot moves from x=20 (X 0.0) to x=120 (X 1.0)". For lengths and hits, say how the counts map.
