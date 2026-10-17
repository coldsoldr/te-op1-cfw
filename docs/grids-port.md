# Grids on the OP-1: port and drum-sequencer design

Mutable Instruments Grids (topographic drum sequencer) for the OP-1's Blackfin, to become the OP-1's **drum sequencer**. Marbles is the synth sequencer (tombola host, `docs/marbles-port.md`); Grids takes the drums.

- Port: `ports/grids/` (layout and commands in its `README.md`).
- Sequencer interface: `docs/sequencer-interface.md`.

**Status (2026-10-17): host and simulator work only.**
- The generator is ported and bit-exact with upstream (≈15 M checked calls, 0 mismatches).
- The OP-1 layer (clock → steps → drum notes) is written and checked against upstream.
- Blackfin build equals the host; CPU measured in the simulator.
- The OP-1 glue (hooks, screen, controls, storage) is designed (§6) but not written; nothing has run on the device.

Tags as in the other docs: **[C]** confirmed from code or data, **[I]** inferred.

## 1. What Grids does

Grids holds a **5×5 map of 25 drum patterns** ("nodes"), each 3 parts (BD, SD, HH) × 32 steps. Each step stores a 0–255 *level*: how essential that hit is to the groove. Upstream data: `node_0..24` in `resources.cc`.

| Control | Upstream | Effect |
|---|---|---|
| **Map X, Y** | knobs + CV | position on the map. The 4 surrounding nodes are bilinearly mixed (`U8Mix`), so moving morphs smoothly between grooves |
| **Fill BD / SD / HH** | knobs + CV | density per part: a step fires when its level > `~density`. Low fill keeps only the strongest hits; high fill adds ghost notes |
| **Chaos** | knob + CV | at each pattern start, a random perturbation (0..chaos/4, per part) is added to every level for that bar: more and different hits |
| **Accent** | outputs | a fired step whose level > 192 also raises its part's accent output |
| **Tempo / clock** | knob, tap, clock in (4/8/24 ppqn), MIDI clock | 8 steps per quarter: a step is a 32nd note and a pattern is one 4/4 bar. At 4 ppqn only the even steps (16ths) are evaluated |
| **Swing** | settings mode: the chaos knob becomes swing | lengthens the first sixteenth of every eighth by up to 42/128 (about triplet swing). Perturbation is off while swing is on |
| **Euclidean mode** | settings mode | X, Y and chaos become the 3 parts' lengths (1–32), fills become hit counts. Patterns from a 32 × 32 table; the accent outputs give each cycle's start |
| **RND bit** | output bit 7 | one LFSR bit per clock: a random gate |

Musically, the maps only populate 16ths: of 25 × 96 levels, only 49 odd (32nd-note) entries are non-zero [C, `make tables`]. With chaos, odd steps can fire at the 32nd resolution.

Examples from `make patterns` (one bar; `|` = quarter; `#` = accent, `x` = hit):

```
x=128 y=128 density=160          x=255 y=255 density=160          x= 64 y=192 density=224
BD |#.......|x...x...|#...x...|........|   |#.......|#.....#.|x.......|#...x...|   |#...#.x.|....x.x.|....#.#.|....x.x.|
SD |........|#...x...|....x...|#.#.x...|   |#.......|#.....#.|..x.....|x.....x.|   |..x.x...|#.....#.|..x.x.x.|#...x.x.|
HH |....#.x.|x...#.x.|....#.x.|x...x.x.|   |x...#...|....#...|..x...x.|....#...|   |#.x.#.x.|#.x.x.x.|x...x...|x.x.x.x.|
```

The centre of the map is a plain backbeat: SD on steps 8 and 24 (beats 2 and 4), hats on the off-beat eighths.

## 2. What's ported

| Upstream | Port | Notes |
|---|---|---|
| `pattern_generator.{h,cc}` | `src/pattern_generator.*` (`op1grids::PatternGenerator`) | Everything:<ul><li>drum-map read and interpolation;</li><li>perturbation with its clip rule;</li><li>thresholds and accents;</li><li>Euclidean mode;</li><li>output-clock and gate-mode bit layouts, `swing_amount()`;</li><li>step, pulse and beat counters, Reset/Retrigger, the pulse-duration counter.</li></ul>Arithmetic and order of random draws unchanged. **Bit-exact** |
| avrlib `Random` | in the class | same 16-bit Galois LFSR (x^16+x^14+x^13+x^11, `0xb400`), seed `0x21`; per instance instead of global |
| avrlib `U8Mix`, `U8U8MulShift8` | C versions | identical to the AVR asm (checked against pichenettes/avril `op.h`) |
| `resources.cc` (nodes, Euclidean LUT) | `src/grids_tables.cc` (generated) | plain `const` arrays. `drum_map[5][5]` becomes a table of node indices: no pointers, nothing to relocate |
| — | `src/drum_sequencer.*` (`op1grids::DrumSequencer`) | the OP-1 layer (§3) |

**Changes for the OP-1 image** (`ports/rings/op1/link.ld` constraints):
- no static constructors (`Init()` sets everything; checked by `make size`);
- no PROGMEM or EEPROM;
- the state is an instance, not class statics;
- integer only, no division anywhere;
- `make size` shows **no undefined symbols** (no libgcc helpers).

**Not ported, deliberately:**
- the internal clock and its tempo table, tap tempo, the clock and reset inputs, MIDI clock: the OP-1 sequencer clock replaces them;
- the EEPROM options and the factory-testing retrigger hack;
- the LED and shift-register code.

**OP-1 extension: swing is a separate setting.** Upstream reuses the chaos knob for swing and turns perturbation off. The layer keeps the generator's swing flag off, so chaos and swing work together. Swing timing is computed the way `clock.Wrap()` does it (§3).

## 3. The OP-1 layer (`DrumSequencer`)

- **Clock:** the host calls `Tick(t)` once per sequencer tick (96 per beat [I]), with `t = tick mod 48`. That is the position inside an eighth note (`docs/sequencer-interface.md` §4).
  - A Grids step is a 32nd = 12 ticks; a pattern is one bar = 384 ticks.
  - Steps happen at four onset ticks per eighth: `0, 12(128+v)/128, 24(128+v)/128, +12(128−v)/128`, rounded, with `v = swing·43>>8` (0..42 as upstream). At v = 42 the onsets are 0, 16, 32, 40: about triplet swing.
  - Because `t` is the host's own counter, Grids stays phase-locked to the OP-1 clock.
- **Resolution:**
  - **32nds** (default): every step, `TickClock(3)`, as Grids' internal clock and 8/24 ppqn;
  - **16ths**: even steps only, `TickClock(6)`, as Grids at 4 ppqn.
  - Swing and resolution are latched at `t == 0`, so the step grid can't drift.
- **Start:** `Start()` rewinds to step 0, and the first step plays at the next `t == 0` (an eighth-note boundary). Bar alignment: §6.4.
- **Settings** (`DrumSeqSettings`, bytes):
  - mode;
  - X, Y, chaos;
  - 3 fills, 3 Euclidean lengths;
  - swing, resolution;
  - 4 keys (BD, SD, HH, RND; `0xff` = mute) and 3 accent keys, accent mode;
  - velocity, accent velocity, gate ticks.

  They're read at the next step, so UI-thread writes need no lock (single bytes).
- **Notes:** `Tick` returns up to 8 events (`key 0..23`, velocity, on/off), offs first. The glue plays them with:
  - `MakeMidiNote(&n, key, vel)` `0x1a85014`;
  - `VoiceCtl::NoteOn(&n, 0)` `0x1a85750` / `VoiceCtl::NoteOff(&n)` `0x1a85840`, from the sequencer thread only, as tombola does.

  Each voice gets a note off after `gate_ticks` (default 6). On a retrigger, the off comes first. `AllNotesOff()` releases everything.
- **Accents:** state bits 3–5 are the per-part accents in drums mode, or the cycle starts in Euclidean mode (upstream's accent/reset outputs). Two modes:
  - velocity: `accent_velocity` vs `velocity`;
  - key: the part's `accent_key` (e.g. open hat for an accented hat).
- **RND:** bit 7 can play a 4th key (off by default).
- **RAM:** 72 bytes per instance (one for the drums).

## 4. Verification (`make compare`, `out/compare.txt`)

**Reference:** upstream's **unchanged** `grids/pattern_generator.cc` and `resources.cc`, compiled on the host.
- The avrlib submodule isn't in `mutableinstruments/`, so `ports/grids/shim/` supplies the few avrlib/avr-libc pieces they use.
- The `Random` shim (LFSR `0xb400`, seed `0x21`) and the `op.h` C versions were checked against pichenettes/avril's source.

**Method:** the port and upstream run side by side, keeping their state across every scenario, so a divergence would propagate. After every call the harness compares:
- `state()`, `step()`, `swing_amount()`;
- beat and first-beat;
- the LFSR state.

| Scenario | Calls | Result |
|---|---|---|
| Drums sweep: X, Y ∈ 17 values each (map cell edges 63/64/65, 127–129, 191–193, 0, 255 …) × 6 fill sets × 6 chaos × output-clock on/off × swing on/off × 1/3/6 pulses, two bars each | 11,985,408 | 0 mismatches |
| Euclidean sweep: every length (step 8) × every density (step 8), 3 parts offset, output-clock on/off × 1/3/6 pulses | 589,824 | 0 mismatches |
| Random walks: 300 seeds × 4000 ticks; knobs, mode, output-clock, swing and gate mode change at random; random Reset, Retrigger, pulse-counter runs, falling edges | 2,402,360 | 0 mismatches |
| **Total** | **14,977,592** | **bit-exact** |
| OP-1 layer: 384 runs (2 resolutions × 6 swings × 4 gate lengths × 8 seeds, 16 bars each, knobs moving) | 147,456 steps, 351,986 events | 0 errors |

**OP-1 layer checks:**
- each step's state equals upstream's `TickClock(3/6)` with the same settings;
- note-on count = triggered parts;
- steps on their swing onsets, no steps closer than 8 ticks;
- exactly 32 (or 16) steps per bar;
- keys 0..23, velocities 1..127, no on-while-on or off-while-off, gates ≤ `gate_ticks`, no hanging notes after AllNotesOff.

**Sensitivity:** changing the accent threshold 192 → 193 in the port makes `make compare` fail (305 layer errors plus generator mismatches). The check is live.

## 5. CPU and size (`make sim`, `make size`)

Blackfin simulator, `-O2`; the device has run 1.5–1.8× the simulator in earlier ports. The Blackfin checksum equals the host's.

| | cycles avg | max |
|---|---|---|
| `Tick` without a step (gate countdown) | 384 | 494 |
| `Tick` with a step (generator + notes) | 1201 | 1832 |
| `TickClock(3)` alone | 665 | 781 |
| `Tick` overall | 443 | — |

- At 120 BPM (192 ticks/s): **0.085 MHz** average; 0.35 MHz even if every tick cost the worst step. At 300 BPM: 0.21 MHz.
- That's negligible next to Rings or Clouds; it runs only while the drum sequencer plays.

**Size (freestanding objects, `-O2`):**
- code 872 B (generator) + 1120 B (layer);
- const data 6.5 KB: nodes 2400 B + Euclidean LUT 4096 B. The LUT could be computed instead, but the table keeps Euclidean mode exactly upstream's.
- No `.data`, `.bss`, `.init_array` or helper calls.

## 6. OP-1 integration design (not implemented)

### 6.1 Host: the **pattern** sequencer, swapped by the orange click

Which stock drum sequencer to host on (each instrument picks its own type; `docs/sequencer-interface.md` §2):

| Type | For | Against |
|---|---|---|
| **pattern** (0), recommended | <ul><li>It's the OP-1's step drum sequencer, and its data `drumseq` (`notes` = a 24-bit key mask per step, `length`, `swing`) has exactly the shape of Grids' output. A **"print" action can freeze the current Grids bar into stock pattern data**, then the orange click back to stock shows it as editable steps. No other host offers that.</li><li>Its controller doesn't use the encoder clicks [I]: key down `0x1a07a58` handles `0x24`/`0x25` and music keys (`0x19c316c`), and returns 0 for the rest.</li><li>No conflict with Marbles on tombola.</li></ul> | <ul><li>A well-used drum sequencer; it stays one click away.</li><li>One player object shared by synth and drum, so hooks must check the instrument.</li><li>`drumseq` is range-asserted at boot (`pattern_data.cpp:57`), so "print" must write valid JSON.</li></ul> |
| finger (3) | default on both instruments; thematically close (preset grooves per key) | the most-used sequencer; its keys select patterns, which collides with key ideas (§6.5); more controller surface |
| endless (1) | one player object per instrument (clean isolation); already has `euclidean` fields | special key paths (keys outside 0..23, notes kept on key-up), data JSON with asserts |
| sketch (4) | rarely used on drums [I] | `UiFrame` special case for screen 46; gesture-oriented screen |
| arpeggio (5) | rarely used on drums [I]; no data table | the most-used sequencer on synth; key semantics are "held notes" |
| tombola (2) | — | taken by Marbles; one shared player object |

**The swap:**
- The `sequencer` table keeps `drum_sequencer = pattern`, so stock firmware still boots. **Never write a new name there**: it's fatal at boot (`SeqIds.cpp:41`).
- A per-instrument marker (Grids on/off) lives in a private table (§6.6). In a first version it's in RAM, default on for drums.
- The **orange click** on the pattern screen toggles the marker:
  - AllNotesOff on the outgoing side;
  - Grids `Start()` on the incoming side.
- Grids answers for the **drum instrument** only (`[0x1de4c40] == 1`). Synth pattern stays stock. Grids on synth (3 parts → 3 notes) could come later.

**Words to redirect** (plain SDRAM data words through the image patch table). Stock values were read from `OP1_vdk.elf` [C]. Each hook tail-calls stock when the marker is off or the instrument is synth.

| what | word | stock |
|---|---|---|
| pattern player KeyDown | `0x19110d8` | `0x01a08c84` |
| pattern player KeyUp | `0x19110e0` | `0x01a087b4` |
| pattern player OnLeave | `0x19110e8` | `0x01a08720` |
| pattern player Tick | `0x19110f0` | `0x01a08ffc` |
| pattern player UiFrame | `0x19110f8` | `0x01a086ee` |
| pattern player AllNotesOff | `0x1911100` | `0x01a08bc0` |
| pattern controller (vtable `0x191101c`, set by ctor `0x1a07514`) key down / shifted / key up | `0x1911030` / `0x1911038` / `0x1911040` | `0x01a07a58` / `0x01a07ad0` / `0x01a07980` |
| pattern controller knob | `0x1911048` | `0x01a07bd0` |
| pattern controller screen id / enter | `0x1911060` / `0x1911050` | `0x01a074cc` / `0x01a076b4` |
| SeqManager key down (clicks while the sequencer is off, if wanted) | `0x1911550` | `0x01a426b8` |
| pattern screen Update / Init | `0x1911120` / `0x1911128` | `0x01a0967c` / `0x01a092ac` |

The pattern controller's constructor builds four CtlKnobs (at +8, +0x28, +0x48, +0x68; defaults 50, 16, 15?, 2: swing, length, …, play mode [I]). Grids doesn't touch them.

### 6.2 Ticks → steps

The player `Tick(t)` hook (marker on, drum instrument) runs:

```
n = grids.Tick(t, ev); for each ev: MakeMidiNote(&m, key, vel); on ? NoteOn(&m, 0) : NoteOff(&m)
```

- 96 ticks/beat → a 32nd step every 12 ticks, a sixteenth every 24. Swing delays every second sixteenth by up to 8 ticks (§3).
- The tempo is the OP-1's (`tempobpm`), so Grids follows tempo changes and MIDI/tape sync for free.
- Only the live instrument's player is ticked [C], so Grids plays while the drums are selected. That's stock behaviour for every sequencer.

### 6.3 Parts → drum keys

- The OP-1 drum kit has 24 keys (0..23 = notes 53..76, no octave in drum mode).
- **Default mapping:** BD = key 0 (F), SD = key 2 (G), HH = key 8 (C#).
  - Source: the shipped `op1.db` demo drum pattern (`drumseq`, `is_synth` 0) uses key 0 on the downbeats, key 2 on beats 2 and 4, and key 8 as the hat line [C data, I meaning].
  - Accent keys (accent mode "key"): 1, 3, 9 (the neighbours: e.g. a second kick, rim, open hat).
  - RND: off, or key 12.
- **Choosing sounds:** see the assign mode in §6.5 (white click, then press a key).
- **Per-kit memory:** the mapping is stored with Grids' settings (one per instrument, not per kit). Per-kit mappings would need a key in the kit's preset JSON: rejected, since presets must stay stock-valid.

### 6.4 Start, stop and bar alignment

- Grids `Start()` is armed by the player's AllNotesOff, which `SetPlaying(false)` and type switches call [C]. It's also armed by OnLeave and by the orange swap.
- The first step then waits for `t == 0` (an eighth boundary).
- True bar alignment (step 0 on the tape's/metronome's beat 1) needs a bar or beat position from the firmware. `[0x18c187c]` is a mod-4 tick counter, not a beat counter. That's a probe question (§7). Until then, step 0 is the first eighth after the sequencer starts.

### 6.5 Controls (proposal; decision for the user)

Encoders, and shift held as a temporary second page (performance-friendly):

| | Blue | Green | White | Orange |
|---|---|---|---|---|
| **MAP** (default) | map X | map Y | chaos | swing |
| **FILL** (shift held) | BD fill | SD fill | HH fill | accent (velocity spread) |
| **Euclidean**, MAP | BD length | SD length | HH length | swing |
| **Euclidean**, FILL (shift) | BD hits | SD hits | HH hits | accent |

Clicks (orange reserved for the swap):

| Click | Plain | With shift |
|---|---|---|
| blue | mode: drums ↔ Euclidean | resolution 32nds ↔ 16ths |
| green | **re-roll**: new chaos draw at the next bar (reseed the LFSR) | accent mode: velocity ↔ alternate key |
| white | **assign**: cycles BD → SD → HH → RND → off. While a part is selected, the next key press sets that part's sound (swallowed) | RND voice on/off |
| orange | **swap** Grids ↔ stock pattern | **print**: freeze the current bar into the stock `drumseq` data (§6.1) |

**Held keys** (KeyDown/KeyUp hooks). Options, decision for the user:
1. **Pass-through** (recommended default): KeyDown returns 0, so keys sound directly. You finger-drum over Grids.
2. **Assign** (after the white click): the key becomes the selected part's sound; returns 1.
3. **Fill on hold** (optional performance mode): holding a part's own key raises that part's fill by +64 (or to max) while held, like riding the fill knob. Holding any other key adds it as an extra part that follows HH's pattern.

**Threads:**
- Knob, click and key hooks run on the UI thread and only write settings bytes and flags.
- Tick (sequencer thread) is the only caller of NoteOn/NoteOff, as tombola does.

### 6.6 Storage

- **A private op1.db table** `grids(is_synth INTEGER, data TEXT)`, created with `CREATE TABLE IF NOT EXISTS` through the db helpers (`docs/sequencer-interface.md` §8). Unknown tables are ignored by stock firmware [I].
- JSON e.g.:

  ```
  {"on":1,"mode":0,"x":128,"y":128,"chaos":0,"fill":[128,128,128],"len":[120,120,120],
   "swing":0,"res":1,"keys":[0,2,8,255],"akeys":[1,3,9],"amode":0,"vel":90,"avel":127,"gate":6}
  ```

- Loaded when the pattern player is first built. Written from the UI thread, debounced (e.g. 2 s after the last change), never from the sequencer thread.
- **Never** a new name in `sequencer`.
- **First version:** RAM only, defaults as above, marker on for drums.
- "Print" writes the stock `drumseq` row (`is_synth` 0), with values inside its asserted ranges.

### 6.7 Screen: `grids.svg` (the user draws it)

Swapped in on the pattern screen (Update/Init words) when the marker is on, as MULTI and Clouds do. Elements to name in the SVG:
- **map:** a 5×5 grid (the 25 nodes) with a cursor dot at (X, Y). In Euclidean mode, three concentric rings instead (length, hits, playhead per part).
- **three 32-step lanes** (BD, SD, HH):
  - per step, a bar whose height is the interpolated level (`PatternGenerator::ReadDrumMap`, cheap enough per frame);
  - a horizontal threshold line at `~fill`;
  - lit cells for hits, a brighter mark for accents;
  - a playhead column. On a 16th grid, draw only even steps.
- **chaos:** jitter or scatter on the lanes (the perturbation is per bar and per part; it can be drawn exactly).
- **swing:** offset marks on the off-beat columns.
- **encoder legend:** blue, green, white and orange labels for MAP and FILL (the page shown while shift is held), the mode, the assign target, and the key each part plays.

## 7. Open questions for a device probe

A wrapper around the pattern player words, logging only (method of Test 16):
1. **Tick rate:** 96 ticks per beat and `t` wrapping at 48 as an eighth note (shared with `docs/sequencer-interface.md` §10). Do ticks run while the transport is stopped, and during tape play/record?
2. **Bar position:** where the firmware keeps beat/bar (metronome, tape), so Grids' step 0 lands on beat 1.
3. **Drum kit layout:** are keys 0 / 2 / 8 kick / snare / hat in the factory kits and on DBox? Which keys hold second kicks and open hats?
4. **Velocity:** do drum voices (sampler, DBox) respond to `MidiNote.velocity`? If not, accents need the alternate-key mode (or a gate-length accent).
5. **Note off:** do drum sounds care about NoteOff (one-shot vs gated)? This sets the default gate length.
6. **Pattern controller:** confirm that clicks `0x3b..0x3e` are unused on the pattern screen with the sequencer on, and what the enter and screen-id words do when bypassed.
7. **Shared player:** with pattern selected on both instruments, confirm hooks see the drum instrument through `[0x1de4c40]`, and what OnLeave does on a synth ↔ drum switch.
8. **`drumseq` limits** for "print": the maximum `length` (16? 32?) and the asserted ranges (`pattern_data.cpp:57`).
9. **Voice lock:** sequencer-thread NoteOn while the user plays keys (`VoiceCtl.cpp:443`).

## 8. Decisions for the user

1. **Host:** pattern (recommended, enables "print"), finger, or endless. Orange click = swap in every case.
2. **Controls:** the MAP / FILL (shift-held) layout and click assignments of §6.5.
3. **Held keys:** pass-through (default) and assign, plus optionally "fill on hold".
4. **Default sounds and accents:** keys 0 / 2 / 8; accents as velocity or as alternate keys (1 / 3 / 9); RND voice off or on.
5. **Swing:** independent of chaos (recommended, an OP-1 extension), or upstream's coupling (swing replaces chaos).
6. **Resolution default:** 32nds (all steps, as Grids' internal clock) or 16ths (as Grids at 4 ppqn).
7. **"Print to pattern"** (shift + orange): wanted in the first version?
8. **Storage:** RAM-only first version, then the private `grids` table.
9. **License:** Grids is **GPL v3**. Rings, Clouds, Marbles and the other ports are MIT. Shipping this port inside the image makes that image (or at least this part) GPL v3. Confirm that's acceptable before integrating.
10. **Naming:** the OP-1 already has a synth engine called "grid" (`docs/engine-interface.md`); call this one "Grids" on screen to avoid confusion.

## Decisions (the user, 2026-10-17)

1. **Licence:** GPL v3 is fine; the whole project will be open source.
2. **Host:** the stock **pattern** sequencer, on drums.
3. **Controls:** the proposed layout (§6); the user passes it to the designer for `grids.svg`.
4. **Held keys:** **fill while held**.
5. **Accents:** louder hits on the same key by default; if a second key is assigned for accents, accents play that key instead.
6. **Swing and chaos:** both, separately.
7. **Resolution:** **16ths** by default.
8. **Print to pattern:** yes, in the first version.
9. **Settings:** RAM only first, then a private op1.db table.
10. **Name:** no "Grids" label: **pattern shows as "MULTI"** in the sequencer list, and the orange click swaps Grids ↔ stock pattern (as phase and phone became "multi").
