# CRUSH (dirt category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

A **bit crusher**: it lowers the **sample rate** (holding each sample for longer, so the sound turns gritty, with metallic aliasing) and the **bit depth** (rounding to fewer levels, so it gets noisy and steppy). **Jitter** makes the sample clock unsteady, like a dying sampler. From subtle 12-bit grit to 2-bit destruction.

It is engine 5 of 5 in **dirt** (punch → drive → tape → fold → **crush**). Single page.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **rate** | 44.1 kHz → 500 Hz | sample rate |
| green | **bits** | 16 → 2 (stepped) | bit depth |
| white | **jitter** | 0..1 | randomness of the sample clock |
| orange | **mix** | 0..1 | dry/wet |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue, green, white | (none) | |
| orange | next engine in dirt (shift: previous) | engine indicator `eng0..eng4`, crush lit |

## Visual idea

- **A staircase waveform:** a smooth wave over a stepped version of it. **Rate** sets how wide the steps are (scale x, or variants); **bits** sets how many levels there are (horizontal grid lines as a count set, or variants `bits0..bits7`).
- **Jitter** makes the step widths irregular (a few pre-drawn irregular variants, or offsets wobbling per frame).
- **Mix:** an orange level.
- Keep the dirt family layout.

## Suggested ids

- Groups: `static`.
- Values: `rate` (or `rate0..rate7`), `bits0..bits7`, `jitter`, `mix`.
- Indicators: `eng0..eng4`.
