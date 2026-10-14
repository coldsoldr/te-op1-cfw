# OP-1 synth engine interface (firmware 246)

How a synth engine plugs into the OP-1 app, worked out from `OP1_vdk.dis`, so we can later swap one engine's audio for our own code (the Rings port; see `docs/rings-port.md`).

How sure each fact is:
- **confirmed**: read directly from instructions or data;
- **inferred**: deduced from behaviour or naming, not proven.

Core's voice loop is in partly encrypted L1 (`0xffa00xxx`), so this doc works from the engine side, the factory and the vtables, which are all in plain SDRAM. That's enough to plan a hook.

Tools: the index scripts in `%TEMP%\op1re\` (`op1lib.py`, `show.py`, `vt.py`, `srctags.txt`) are throwaway files outside the repo. The method is in `docs/HANDOVER.md` §4.

## Engine types

`instr_types.cpp` builds a `{name, id}` table at runtime: names from `0x18df7bc`, table at `0x1dd4d98`, writer around `0x19b988c` (**confirmed**).

| id | name | module | voice getter | object size |
|---|---|---|---|---|
| 0 | fm | Fm.cpp | `0x19a3d38` (then `0x19a3d84`) | |
| 1 | digital | T10.cpp | `0x1a5222c` | |
| 2 | drwave | DrWave.cpp | `0x197ada8` | |
| 3 | string | St.cpp | `0x1a4f398` (then `0x1a4ef64`) | 32 bytes |
| 4 | cluster | Cls.cpp | `0x19607e4` | |
| 5 | pulse | Pls.cpp | `0x1a0df54` (then `0x1a0e014`) | |
| 6 | sampler | SynthSampler.cpp | `0x1a51e34` | |
| 7 | phase | Pd.cpp | `0x1a0bed8` | 28 bytes |
| 8, 9 | drum, dbox | (drum producers) | "invalid synth reset" | |
| 10 | ls | ? | `0x19d3adc` (single instance) | |
| 11 | dna | Id.cpp | `0x19b4c64` (then `0x19b4cc4`) | |
| 12 | dsynth | dsynth.cpp | `0x197ee60` (then `0x197e0b0`) | |
| 13 | voltage | Slump.cpp | `0x1a49ef8` | |
| 14 | iter | Iter.cpp | `0x1a1b99e` case | |

This settles the handover's open question: **digital = T10** and **dna = Id**.

## Factory and polyphony (confirmed)

`producer_factory.cpp` "synth reset" lives at `0x1a1b7d4`: `(R0 = type, R1 = producers[], R2)`.
- It checks `type < 15`, then jumps through the table at `0x18f5a04` (one entry per type above).
- Each case runs `for v in 0..5: producers[v] = Engine_GetVoice(v)`, so there are **6 voices** (`kSynthVoiceCount = 6`).
- Some engines then call a one-off setup function (the "then" column above).
- Invalid types assert `producer_factory.cpp:401 invalid synth reset`.
- Core keeps the array as `s_producers[instance]` (`Core.cpp:1216`, `:713`).

The per-voice getter, using Pd at `0x1a0bed8` as the example:
- It asserts `0 <= i < 6`.
- It returns `0xffb00000 + i * sizeof(voice)`, after placement-constructing the voice there.
- `0xffb00000` is the **4 KB L1 scratchpad**. Every engine's voices live there, and only one synth engine is active at a time.
- Voice size is 28 bytes for Pd and 32 bytes for St.

## Voice object and vtable (confirmed)

The constructor stores the vtable pointer at `+0`:
- **Pd:** vtable `0x191113c`, constructor `0x1a0bc14`.
- **St:** vtable `0x19118c4`, constructor `0x1a4ef38`.
- **Pls:** vtable `0x19111cc`.

VisualDSP vtable entries are 8 bytes, `{0, fn}`. The entries:

| vtable offset | method | Pd | St | Pls | notes |
|---|---|---|---|---|---|
| +0x0c | deleting destructor | `0x1a0beaa` | `0x1a4f06c` | `0x1a0df28` | resets vtable to the base (`0x191112c`), calls delete if bit 0 of R1 is set |
| +0x14 | **render** | `0x1a0bc5c` | `0x1a4f098` | `0xffa04b88` (L1) | see below |
| +0x1c | **note on** | `0x1a0be82` | `0x1a4efc8` | `0x1a0dee4` | Pd resets phase to `0x80000000` and sets its attack flag; St fills its string with noise (LCG `1664525`/`1013904223`) at full amplitude |
| +0x24 | ? | `0x1a0bbc0` | `0x1a4eee4` | `0x1a0dea4` | Pd and St return 0 |
| +0x2c | **note off** | `0x1a1b5ec` (shared `RTS`) | `0x1a4efae` | `0x1a1b5ec` | St sets release mode (`B[+0x1c] = 1`, `W[+0x18] = 0x100`); Pd relies on the OP-1 ADSR |
| +0x34 | 0 | | | | end of the voice vtable; the next class's vtable follows |

Note-on gets no arguments besides `this` (**inferred** from the Pd and St code). Pitch comes in at render time, below.

## render(this, int16_t* out, const int16_t* params) (confirmed)

Evidence: Pd `0x1a0bc5c` and St `0x1a4f098`.
- **Registers:** `R0` = this, `R1` = output buffer, `R2` = params.
- **Calling convention:** VisualDSP. Callee-saved are `R4`–`R7`, `P3`–`P5` and FP. Both engines save `(R7:4, P5:3)` and return via `JUMP (P0)` after loading `[FP+4]`. `L0`–`L3` must stay 0 (**inferred**, standard).
- **Block:** 128 samples per call (Pd `LC0 = 0x80`; St uses `0x80`).
- **Output:** mono, 16-bit fract, written with `W[P5++] = R0`. The Core's ADSR and FX come after this (**inferred** from Adsr.cpp and Core).
- **Params:** four `int16` at `+0, +2, +4, +6`. These are the 4 encoders (**inferred**); Pd uses `+6` squared and `+0` to `+4` as fract16 amounts.
- **Pitch:** a per-sample 32-bit phase increment array at **`0xff801950`** in L1 data A, 128 entries.
  - Pd reads `R5 = [I0++]` per sample and does `phase += inc`. Its wavetable at `0x101e5e8` is indexed by `phase >> 19` (8192 entries), so a full cycle is 2^32 and `inc = f / fs * 2^32`.
  - St reads only `[0xff801950]` (the pitch at the start of the block).
  - Core evidently fills this array per voice, including bend, LFO and portamento, before each render call (**inferred**; the code is in encrypted L1).
- **Global side effect:** Pd adds to a global at `0x1de4b8c` at the end of render, apparently a level or visualisation accumulator.

## Measured on the device (probe v2, 2026-10-14)

See `docs/device-tests.md`.
- **CPU clock:** ~400 MHz.
- **Voice render rate: 88.2 kHz.** Each `render` call produces 128 samples, about 689 calls per second per sounding voice.
  - So the phase increments are `f / 88200 * 2^32`.
  - Engines run 2× oversampled; the core presumably decimates (**inferred**).
- **Only sounding voices are rendered.** With one key held, only that voice object's render was called, and nothing else called the slot.
- **Call intervals jitter** between about 1.34 and 1.45 ms or more.

## Smallest hook to replace the phase engine's audio

Phase (id 7) is a good host: its voice code is small, plain SDRAM, and its vtable is plain data.

1. **Load our code:** put our code and state in a new plain LDR block in free SDRAM (see `docs/memory-map.md`, which recommends `0x02c00000–0x02ffffff`).
2. **Redirect render:** change the word at **`0x1911150`** (Pd vtable `+0x14`) from `0x01a0bc5c` to our `render`.
3. **Redirect note on:** change the word at **`0x1911158`** (`+0x1c`) from `0x01a0be82` to our `note_on`, which should strum. Optionally do the same for `0x1911168` (`+0x2c`, note off).
4. **No code changes:** steps 2 and 3 are 4-byte data patches inside an existing LDR block, which `tools/patch_app.py` already supports.

Our `render(this, out, params)` must:
- **Find its voice:** `voice = (this - 0xffb00000) / 28`. Keep our own per-voice state elsewhere: the Pd object is only 28 bytes, and the scratchpad is shared.
- **Get the pitch:** read `0xff801950[0..127]` (phase increments) and convert to a normalised frequency: `f = inc / 2^32`.
- **Read the parameters** from `params[0..3]`.
- **Produce 128 samples** of `int16` into `out`, mono.
- **Restore registers:** preserve `R4`–`R7`, `P3`–`P5` and FP, keep `L0`–`L3` at zero, and restore any hardware-loop registers it uses.
- **Stay within budget:** finish inside the per-block CPU budget. Six voices each call render every 128 samples (**inferred**; whether idle voices are called is unknown).

The phase engine's screen keeps drawing its own graphics; only the sound changes. A dedicated screen would be a separate, bigger job (SVG and UI code).

## Open questions

- **Idle voices:** does Core call render for idle voices? It decides the real CPU cost of 6 Rings voices. Find out with an on-device cycle-count probe.
- **Param scaling:** the exact scaling of `params[]` (0..0x7fff?) and whether the values are smoothed.
- **Slot `+0x24`:** what it means. Pd and St return 0.
- **Time budget:** the per-block budget. Each voice's render runs every 128 samples at 88.2 kHz (1.45 ms), With one phase voice playing and no FX, probe v3 measured ~81% of that free (~324 MHz; `docs/device-tests.md`). The cost of FX and other tracks is still unknown.
