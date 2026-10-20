# TAPE ECHO (delay category): screen brief

Read [README.md](README.md) first for the shared conventions, the safe area and the engine indicator.

## What it is

A **tape echo** in the style of a Roland Space Echo or an Echoplex. The sound is recorded onto a loop of tape and played back by one to three heads at different distances, so the echoes repeat in a rhythm. Each repeat goes back through the tape, so it gets a little **darker, more saturated and more wobbly** than the one before. Turned up, the **feedback** self-oscillates into a dub-style howl. Changing the time while it repeats bends the pitch of the echoes, just like speeding up a real tape. The motion comes from the Mutable Instruments Beads wow-and-flutter model, and the saturation is the same as the TAPE dirt engine.

It is engine 2 of 2 in **delay** (delay → **tape echo**). Single page. Stereo on the master, mono on a synth or drum.

## Page 1

| Encoder | Parameter | Range | Meaning |
|---|---|---|---|
| blue | **time** | 30 ms – 1.5 s (shift: fine) | tape speed, i.e. the spacing of the repeats |
| green | **feedback** | 0..1.1 (top end self-oscillates) | how many repeats |
| white | **age** | 0..1 | wow and flutter, darkening and saturation per repeat: new tape → worn tape |
| orange | **mix** | 0..1 | dry/wet |

## Clicks

| Click | Action | On screen |
|---|---|---|
| blue | **heads**: cycles the playback-head patterns (1 head; 2 heads; 3 heads; heads 1+3 …) | head indicator, blue |
| green | **hold**: input off and feedback at 1, so the tape loops what it holds | a hold indicator, green |
| white | (none) | |
| orange | next engine in delay (shift: previous) | engine indicator `eng0..eng1`, tape echo lit |

## Visual idea

- **The tape loop:** an oval path past a record head and three playback heads. The active heads are lit (blue) per pattern.
- **Time:** the loop's motion, e.g. a few marks travelling around the loop at a speed set by time (the code moves them per frame).
- **Repeats:** a row of echo bars decreasing in height (count set) as feedback sets how many and how tall; the bars darken with age.
- **Age:** the tape line or the bars wobble.
- **Hold:** the record head dims and the loop glows green.
- **Mix:** an orange level.

## Suggested ids

- Groups: `static`.
- Values: `time` (or `mark1..mark4` moved per frame), `echo1..echo8` (count set / scaled), `age`, `mix`.
- Indicators: `head1..head3`, `hold`, `eng0..eng1`.
