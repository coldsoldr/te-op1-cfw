# ENSEMBLE (modulation category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

A **stereo chorus / string ensemble**. Copies of the signal are delayed by a few milliseconds, and the delays are swept by slow LFOs, so the copies drift slightly in pitch against each other. With one copy per side it's a classic **chorus** (wide and shimmering). With three phase-spread copies per side it becomes the **ensemble** sound of 1970s string machines like the Solina: lush, thick and slowly swirling. The stock OP-1 has no chorus.

It is engine 2 of 3 in **modulation** (cwo → **ensemble** → shift). Single page. Stereo on the master, mono on a synth or drum.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **rate** | 0.05–8 Hz | LFO speed |
| green | **depth** | 0..1 | how far the delays sweep |
| white | **voices** | 0..1, chorus → ensemble | one sweeping copy per side → three phase-spread copies per side |
| orange | **mix** | 0..1 | dry/wet |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue, green, white | (none) | |
| orange | next engine in modulation (shift: previous) | engine indicator `eng0..eng2`, ensemble lit |

## Visual idea

- **Several overlapping wave lines** (one to three per side) that drift against each other; the screen can move them in time with the LFO's rate and depth.
- **Voices** shows 1, 2 or 3 copies (a count set).
- **Depth** as how far the copies spread.
- **Rate** as a small LFO dial or the speed of the drift.
- **Mix:** an orange level.
- The modulation family motif: e.g. a shared "sweep" element across ensemble and shift.

## Suggested ids

- Groups: `static`.
- Values: `rate`, `depth`, `voice1..voice3` (count set, can be moved per frame), `mix`.
- Indicators: `eng0..eng2`.
