# Plugin numbers

Each synth and FX plugin has an `order` in its `plugin.json`. Saved sounds store it (`knobs[4] = 0x5300 + order`),
so a number means the same plugin forever: never renumber or reuse one after a release. Gaps are fine; a sound
whose number has no plugin in a build plays the stock iter (synth) or filter (FX). Orders run 1..31 per kind
(`OP1_MAX_ORDER`). Claim a number here before you ship. The table shows saved names; ids and the naming rule:
`docs/plugin-format.md` "Names" (`in a room` is the folder `in_a_room`).

| order | synth | FX |
|---|---|---|
| 1 | supersaw (op1r) | tape (op1r) |
| 2 | fmbell (op1r) | clouds (op1r) |
| 3 | granular (op1r) | reverb (op1r) |
| 4 | wavetable (hundekraft) | washer (hundekraft) |
| 5 | taffy (hundekraft) | snowglobe (hundekraft) |
| 6 | pew (hundekraft) | palme (Musica Obscura, #1) |
| 7 | elg (hundekraft) | in a room (Musica Obscura, #1) |
| 8 | mixtur (Musica Obscura, #1) | |
| 9 | arc (Musica Obscura, #1) | |

| 10 | aether (op1r 33–40; native kennel file) | |

Amp is a loadable input engine without a fixed order (allocated by saved name); it does not reserve or renumber any released order. Its `input_engine` flag requires the new input-host core.
