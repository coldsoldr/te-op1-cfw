# TAPE (dirt category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

A **tape machine** in a box: gentle saturation like recording hot to tape, a low "head bump", high-frequency loss, **wow and flutter** (the pitch wobble of an uneven tape transport) and optional hiss. It fits the OP-1's tape idea: run a synth or the whole mix through it and it sounds like it came off a worn cassette or a reel-to-reel.

It is engine 3 of 5 in **dirt** (punch → drive → **tape** → fold → crush). Single page.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **saturate** | 0..1 | how hot the tape is driven: clean → compressed and warm |
| green | **wobble** | 0..1, wow → flutter | amount of pitch wobble: slow, deep wow at the bottom of the range, fast flutter at the top |
| white | **age** | 0..1 | high-frequency loss plus hiss: new tape → worn tape |
| orange | **mix** | 0..1 | dry/wet |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue, green, white | (none) | |
| orange | next engine in dirt (shift: previous) | engine indicator `eng0..eng4`, tape lit |

## Visual idea

- **Two reels and the tape path** (the OP-1's own tape-reel language), in the dirt family layout.
- **Saturate:** the tape between the reels glows or thickens, or a waveform flattens.
- **Wobble:** the reels or the tape line wobble (the screen can move them per frame, following the wobble's rate). A small wow/flutter dial shows where in the range it sits.
- **Age:** the tape's colour dims, or specks (hiss) appear, as a count set (show the first n).
- **Mix:** an orange level.

## Suggested ids

- Groups: `static`.
- Values: `reelL`, `reelR`, `tapepath`, `saturate`, `wobble`, `age0..age5` (count set), `mix`.
- Indicators: `eng0..eng4`.
