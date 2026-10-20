# PLATE (reverb category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

A classic **plate reverb** (Dattorro, 1997). The input is band-limited and smeared by four allpass diffusers, then fed into a **figure-of-eight tank**: two halves that feed each other. Each half has a slowly modulated allpass, a delay, a damping low-pass and a decay allpass. Seven taps per side read the tank at different points, which gives a wide, dense, bright, slightly metallic stereo tail. It sounds like a big steel sheet ringing: shiny, smooth and immediate.

It is engine 2 of 4 in **reverb** (spring → **plate** → hall → clouds). In the master position it is stereo; on a synth or drum it is mono.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **decay** | 0..1 (tank gain 0.2–0.98) | how long the tail rings |
| green | **damp** | 0..1, bright → dark | high-frequency loss in the tank; darker tails |
| white | **pre-delay** | 0–200 ms | gap before the tail starts |
| orange | **mix** | 0..1 | dry/wet, equal power |

## Page 2

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **size** | 8 steps | tank delay scale: small plate → huge plate (stepped, so no sweep artefacts) |
| green | **diffusion** | 0..1, sparse → dense | input diffuser gains: early echoes audible → smooth wash |
| white | **motion** | 0..1 | tank modulation depth: still → chorused, drifting tail |
| orange | **low cut** | 20–500 Hz | high-pass on the input, keeps the tail clean |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue | (none) | |
| green | **freeze**: decay goes to 1 and input stops, so the tail holds forever | a freeze indicator, green |
| white | **page** 1 ↔ 2 | page indicator, white |
| orange | next engine in reverb (shift: previous) | engine indicator `eng0..eng3`, plate lit |

## Visual idea

- **The plate itself:** a rectangle seen in perspective, or a flat sheet, which "rings". Decay could stretch a set of concentric ripple outlines or a fading tail drawn as decreasing bars; damp could shorten or darken the upper ripples.
- **Pre-delay** as a gap (white) between an input impulse and the start of the tail.
- **Mix** as a simple orange level.
- **Page 2:** size changes the plate's drawn size (8 pre-drawn variants, or a scale); diffusion turns a few distinct echo lines into many; motion wobbles the sheet's edges (a scale or offset); low cut as a small high-pass slope icon.

## Suggested ids

- Groups: `static`, `page1`, `page2`.
- Values: `decay`, `damp`, `predelay`, `mix`, `size0..size7` (or `size` scaled), `diffusion`, `motion`, `lowcut`.
- Indicators: `freeze`, `page1ind`, `page2ind`, `eng0..eng3`.

For each value element, the note should say how a value maps to geometry, e.g. "handle moves from x=40 (0.0) to x=200 (1.0)".
