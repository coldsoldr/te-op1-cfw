# SHIFT (modulation category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

Two modes, chosen with the blue click:

- **Frequency shifter** (Bode style, from Mutable Instruments Warps): it moves every frequency in the sound up or down by the same number of hertz. Unlike pitch shifting, harmonics stop lining up, so small shifts give a phasing/beating shimmer and large ones turn sounds clangorous and bell-like. Shifting **left up and right down** gives instant stereo width. **Feedback** sends the output back in, so the sound climbs or falls endlessly, like a barber-pole.
- **Ring modulator** (Warps' diode model): it multiplies the sound by a sine wave at the shift frequency, giving the sum and difference frequencies: robotic, metallic, Dalek voices.

The stock **cwo** is also a frequency shifter. This one adds the stereo spread, barber-pole feedback and the ring-mod mode.

It is engine 3 of 3 in **modulation** (cwo → ensemble → **shift**). Single page.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **shift** | **bipolar**, −2 kHz ← 0 → +2 kHz, fine near 0 | frequency shift (ring mod: carrier frequency, 0–2 kHz) |
| green | **feedback** | 0..1 | output back into the input: barber-pole sweeps |
| white | **spread** | 0..1 | left shifted up / right down by this proportion (master only) |
| orange | **mix** | 0..1 | dry/wet |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue | **mode**: frequency shift ↔ ring mod | mode indicator, blue |
| green, white | (none) | |
| orange | next engine in modulation (shift: previous) | engine indicator `eng0..eng2`, shift lit |

## Visual idea

- **A spectrum strip:** a few harmonic lines that slide left or right as a group with **shift** (one translate). In ring mod, show a mirrored pair of groups (sum and difference) instead (show/hide).
- **Spread** splits the group into two coloured copies (left up, right down) that move apart.
- **Feedback** as repeated, fading copies of the group stepping onwards (a count set), suggesting the barber-pole.
- **Mix:** an orange level.
- Keep the modulation family motif.

## Suggested ids

- Groups: `static`, `modeshift`, `modering`.
- Values: `shift`, `spreadL`, `spreadR`, `fb1..fb4` (count set), `mix`.
- Indicators: `mode0`, `mode1`, `eng0..eng2`.
