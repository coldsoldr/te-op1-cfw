# Marbles for OP-1: screen design brief, v2

We're putting Mutable Instruments **Marbles** (a random melody and rhythm generator) on the OP-1 as the **synth sequencer**, in place of the stock **tombola** sequencer. An **orange click** switches between Marbles and stock tombola, which keeps its own screen.

**Deliverable:** one file, `marbles.svg`, plus a short handover note listing the ids, how each value maps to geometry, and the colours.

## Why a v2

The first design (Orbit) is well drawn and kept to the budget, but on the device it was **very hard to read without the description**. It shows *where the knobs are* (a dot grows, a diamond slides, a column fills), and that doesn't connect to what you hear. Marbles makes sense when you **watch what it plays**: on the hardware module, people learn it from the lights, not the knobs. The model, bias, jitter and deja vu all describe *patterns over time*, which one frame of knob positions can't show.

**So v2 shows the music, not the knobs:**
1. a **history** of the last 16 steps (what was played, and how high);
2. a **loop bracket** that makes deja vu visible;
3. a **legend row** that says what each encoder does on this page, with a small level bar.

## Layout (a suggestion; proportions are yours)

```
+--------------------------------------------------+----------------+
|  [rhythm loop bracket .............. ]           |  model/scale   |
|  T3  .   o       .   o   o           o           |  chip          |
|  T2  o   o   o   o   o   o   o   o   o   ...     |                |
|  T1      o   .       o       o   o               |  scale/keys    |
|  [pitch loop bracket ........ ]                  |  chip          |
+--------------------------------------------------+----------------+
|  (blue icon)   (green icon)   (white icon)   (orange icon)        |
|  [==== ]       [===  ]        [======]       [==   ]              |
+--------------------------------------------------------------------+
```

- **History area** (the big part, left): 16 columns × 3 lanes. The newest step is at the **right**; older steps move left.
- **Right column:** two small "chips".
- **Bottom row:** the legend, four slots in encoder order: blue, green, white, orange.

## 1. History (16 steps × 3 lanes)

- **Columns = steps of the clock** (T2, the steady clock; its speed is the rate encoder). Each column shows what happened on that step.
- **Lanes = the three gates**, T1 (bottom), T2 (middle), T3 (top). A mark appears in a lane when that gate played a note on that step. Gates can play together (chords), so a column can have 0–3 marks.
- **Pitch = the mark's height within its lane.** Each mark moves up or down by the note it played: low notes at the bottom of the lane, high notes at the top. So a melody reads as a little contour in each lane.
- Each cell is **one mark element**, `h<lane><col>`: `h00`..`h015` for T1, `h10`..`h115` for T2, `h20`..`h215` for T3 (48 in all). They're all the **same shape**: draw one and repeat it. The code shows or hides each mark and moves it vertically (translate y).
- Give, in the note: each lane's baseline y (the lowest note), its height in px (the highest note), and each column's x.
- **What this makes visible:** rate (marks scroll faster or slower), bias (one outer lane busier than the other), jitter (irregular gaps), the T model (its typical pattern), spread and steps (how far marks jump, how few heights they use), and deja vu (see below).

## 2. Loop brackets (deja vu)

Marbles remembers its last *length* steps (1–16). Deja vu replays them. Rhythm and pitch each have their own deja vu, so there are **two brackets**:
- `loop_t` **above the lanes** (the rhythm loop) and `loop_x` **below** (the pitch loop).
- Each spans the **last `length` columns**, ending at the newest column (the right edge). The code scales it in x from its **right end**: draw it at full width (16 columns) and say where its right end is (the anchor).
- States, per bracket:
  - **off:** hidden;
  - **on** (repeats sometimes, set by the deja vu encoder): dim;
  - **locked** (a perfect loop): bright.

  The code changes the bracket's stroke colour and shows or hides it.
- When locked, the marks under the bracket visibly repeat every *length* steps.

## 3. Legend row (4 slots)

One slot per encoder, in encoder order and colour, always in the same place. Each slot has:
- an **icon** for what the encoder does **on the current page**: two pre-drawn icons per slot (page T, page X), shown one at a time;
- a **level bar** under it that the code scales in x from its **left end** (draw it full length; give the anchor).

| Slot (colour) | Page T: icon idea | Page X: icon idea |
|---|---|---|
| blue `#638BFF` | **rate**: a clock, or a metronome | **spread**: arrows apart |
| green `#00EE92` | **bias**: a seesaw | **bias**: a seesaw, or up/down arrows |
| white `#DED7FF` | **jitter**: a wobbly line | **notes** (steps): a staircase. Left = all 12 notes, right = the root only |
| orange `#FF395A` | **deja vu**: a loop arrow | **length**: a bracket |

Bars run 0..1. For **bias** and **deja vu**, the middle matters (bias: even; deja vu: the perfect loop), so give those bars a small centre tick.

**The legend is also the page indicator:** the icons change with the page (white click), so no separate page indicator is needed.

## 4. Chips (right column)

Small, simple, stroke-only:
- `chip_model` (page T) / `chip_scale` (page X), each a marker under a row of dots or bars: page T shows the **T model** (7: coin toss, clusters, drums, independent, divider, three states, Markov); page X shows the **scale** (6: major, minor, pentatonic, pelog, raag Bhairav, raag Shri). The code moves the marker (translate x); give the step per position. The history shows the model's behaviour anyway, so this only needs to say which one. Blue click changes it.
- `src_scale` / `src_keys`: whether notes come from the **scale** around the last key pressed, or from the **held keys** (shift + white). Two small icons; the code highlights one (stroke colour).

## Conventions

- Canvas `viewBox="0 0 320 160"`. The device supplies the black background.
- Every stroke `stroke-width="1.5"`, `stroke-linecap="square"`.
- `<g id="static">` is never touched. Everything the code changes has an `id`. Draw order = file order.
- Colours: blue `#638BFF`, green `#00EE92`, white `#DED7FF`, orange/red `#FF395A`; frame `#3854A5`; inactive `#38354D`; dim `#514959`; selected `#DED7FF`.
- **No text.** Supported: `g`, `line`, `polyline`, `polygon`, `circle`, `ellipse`, `rect`, `path` (no arcs). Not supported: text, gradients, rounded corners.
- The code can: show/hide; translate and scale an element; change an element's stroke or fill colour (give each recoloured element its own colour attributes).
- **Learned from Orbit:** the OP-1 folds a group's `transform` into its children, so a driven element inside a translated group loses that offset when the code moves it. **Draw driven elements at their real coordinates, not inside translated or scaled groups**, and give the anchor point of anything that scales.
- Draw each element's **default state** as follows, because the code reads colours from it: the first mark `h00` shown; `loop_t` bright (locked) and `loop_x` dim (on); `src_scale` selected and `src_keys` not.

## Budget

- About **75 driven ids**: 48 history marks (one shape, repeated), 2 brackets, 8 legend icons, 4 bars, about 12 chip elements. The marks are one template, so this is still a simple drawing.
- **Nothing decorative** that has to be driven. Static decoration (frames, lane guide lines, the bracket's end ticks) is fine, kept dim.

## Id list (rename freely, list them in the note)

`static`; `h00`..`h015`, `h10`..`h115`, `h20`..`h215`; `loop_t`, `loop_x`; `leg0_t`, `leg0_x`, `leg1_t`, `leg1_x`, `leg2_t`, `leg2_x`, `leg3_t`, `leg3_x` (icons); `bar0`..`bar3`; `chip_model`, `chip_scale` (the marker), `src_scale`, `src_keys`.
