# FOLD (dirt category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

A **wavefolder**, the West Coast synth trick. Instead of clipping the peaks off, it **folds** them back down, so a sine wave that is pushed harder grows extra wiggles in its tops and bottoms. The result is bright, metallic, vocal and very synthy, and it changes dramatically with level. Best on a synth. Oversampled and antialiased to keep it clean.

It is engine 4 of 5 in **dirt** (punch → drive → tape → **fold** → crush). Single page.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **fold** | 0..1 | gain into the folder: number of folds |
| green | **symmetry** | **bipolar**, centre = symmetric | DC offset before the folder: folds only the top or the bottom |
| white | **tone** | 0..1, dark → bright | low-pass after the folder, tames the fizz |
| orange | **mix** | 0..1 | dry/wet |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue, green, white | (none) | |
| orange | next engine in dirt (shift: previous) | engine indicator `eng0..eng4`, fold lit |

## Visual idea

- **One cycle of the output wave**, drawn as pre-made variants `wave0..wave7`: a sine that gains more and more folds as **fold** rises.
- **Symmetry** moves a horizontal offset line up or down, or picks lopsided variants.
- **Tone** as a low-pass slope icon whose corner moves.
- **Mix:** an orange level.
- Keep the dirt family layout.

## Suggested ids

- Groups: `static`.
- Values: `wave0..wave7`, `symmetry`, `tone`, `mix`.
- Indicators: `eng0..eng4`.
