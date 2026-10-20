# DRIVE (dirt category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

An **overdrive / distortion**: the signal is pushed into a soft clipper (a curve that rounds off the peaks), with a tone control after it and a **bias** that makes the clipping lopsided. Symmetric clipping adds odd harmonics (buzzy, like a fuzz pedal); asymmetric clipping adds even harmonics (warmer, like a valve). The level stays roughly the same as the drive goes up. It runs oversampled to keep it clean of aliasing. From gentle warmth to hard crunch.

It is engine 2 of 5 in **dirt** (punch → **drive** → tape → fold → crush). Single page.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **drive** | 0 to +36 dB | how hard the signal hits the clipper |
| green | **tone** | **bipolar**, dark ← centre → bright | tilt EQ after the clipper |
| white | **bias** | 0..1, odd → even | symmetric → lopsided clipping |
| orange | **mix** | 0..1 | dry/wet, auto-levelled |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue, green, white | (none) | |
| orange | next engine in dirt (shift: previous) | engine indicator `eng0..eng4`, drive lit |

## Visual idea

- **The transfer curve:** a sine wave going in and a clipped wave coming out, or the shaping curve itself (input on x, output on y) getting steeper and flatter-topped as drive rises (pre-drawn variants `curve0..curve7`).
- **Bias** shifts the clip point so the waveform's top and bottom clip unevenly (move one clip line, or pre-drawn variants).
- **Tone** as a tilt line about a pivot (rotate by pre-drawn variants, or move its ends).
- **Mix** as an orange level.
- The dirt family motif: e.g. the same "input wave → box → output wave" layout on all four dirt screens, with the box's contents changing per engine.

## Suggested ids

- Groups: `static`.
- Values: `curve0..curve7` (drive), `bias`, `tone`, `mix`.
- Indicators: `eng0..eng4`.
