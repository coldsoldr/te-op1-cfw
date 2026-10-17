# OP-1 LFO interface (firmware 246)

How the OP-1's LFOs work, from static analysis of `OP1_vdk.dis` (2026-10-16), mapped the way `docs/next-projects.md` §1 maps the effects, to plan hosting Mutable Instruments Marbles on an LFO slot (`docs/marbles-port.md`). Nothing here has been tried on the device.

Tags:
- **[C]** confirmed from code or data;
- **[I]** inferred.

Addresses are runtime addresses in `OP1_vdk.ldr`. The Ranges table was obtained by running its static initialiser (`0x1a1e914`) in the GDB Blackfin simulator, as for the engine Ranges (`docs/screen-interface.md` §4b); script `%TEMP%\op1lfo\run.gdb`.

## Summary

| Question | Answer |
|---|---|
| Types | 7: tremolo 0, value 1, random 2, element 3, midi 4, bend 5, crank 6 [C] |
| Objects | One LFO object **per voice** (6 synth, 12 drum) in L1 scratchpad `0xffb00a00`, plus one global **"fx LFO"** object at `0xffb00c10` [C] |
| Hookable? | **Yes.** Every LFO method is plain SDRAM, reached through plain-data vtables [C] |
| Process ABI | `process(this, const int16* lfo_params, int stage)`, returns 1 if it did something [C] |
| Call rate | **689.0625 Hz** (once per 64-sample block at 44.1 kHz) [I, strong: the tempo-sync constant] |
| Output | No output buffer: the LFO **writes its modulation into the destination's data** (a scratch copy of the engine knobs, the envelope, the FX params, or the per-sample pitch / gain arrays) [C] |
| Parameters | `lfo_params[8]` per slot at `0xff8014c4 + slot*16`, ranges in the LFO Ranges table `0x1de5740` [C]. **random uses params 0–3; 4–7 are stored but never change** (scale 0) [C] |
| Presets | `lfo_type` (name), `lfo_params[8]`, `lfo_active`; unknown names and out-of-range params are **fatal at boot** [C] |
| Host for Marbles | **random** (type 2): replace its process/trigger vtable words; marker and settings in `lfo_params[4..7]` |

## 1. Types and source files [C]

`lfo_types.cpp` builds a `{name, id}` table at **`0x1dd58e4`** (7 entries, writer `0x19c9c88`; names at `0x18e13c8`, 8 bytes apart). `NameToId` is `0x19c9c10`: it **asserts** (`lfo_types.cpp:41 not found %s`, handler `0x1919aa8`) on an unknown name, then returns 7. `IsValid` is `0x19c9bc4`.

| id | name | module | screen (id, SVG) | factory db default `lfo_types.default_params` |
|---|---|---|---|---|
| 0 | tremolo | LfoDual.cpp | 24, `duallfo.svg` | `[16000,0,0,16000,0,0,0,0]` |
| 1 | value | LfoSingle.cpp | 25, `singlelfo.svg` | `[1000,4000,2000,2000,0,0,0,0]` |
| 2 | random | LfoRnd.cpp | 26, `rndlfo.svg` | `[2000,4000,2000,24000,0,0,0,0]` |
| 3 | element | LfoReRoute.cpp | 27, `reroutelfo.svg` | `[2000,4000,2000,2000,0,0,0,0]` |
| 4 | midi | LfoMidi.cpp | 28, `midilfo.svg` | `[2000,2000,2000,2000,2000,2000,2000,2000]` |
| 5 | bend | LfoBend.cpp | 29, `bendlfo.svg` | `[7200,7100,32767,0,0,0,0,2048]` |
| 6 | crank | LfoCrank.cpp | 30, `cranklfo.svg` | `[0,16384,2000,2000,0,0,0,0]` |

The module ↔ name mapping comes from the factory jump tables (§2): each case calls into the module named by its assert strings. The factory db (`content/op1_factory.db`, table `lfo_types`) lists all seven. Other LFO-related sources: `lfo_factory.cpp`, `lfo_bypass.cpp` ("unknown lfo target", `0x19c97b8`), `ReadInterface.cpp` (LFO params for screens, `0x1a295b4`), `ScreenId.cpp`.

Use in the 160 non-sampler factory presets: value 71, tremolo 53, element 27, random 10, bend 5, crank 3, midi 0 (counts include inactive LFOs).

## 2. Factory and objects [C]

`lfo_factory.cpp` has three reset functions, each `type < 7` and a jump table:

| function | address | args | jump table | invalid-type assert |
|---|---|---|---|---|
| fx LFO reset | `0x19c99ac` | R0 type | `0x18e1304` | `lfo_factory.cpp:45` |
| synth LFO reset | `0x19c9a24` | R0 type, R1 voice | `0x18e1358` | `:74` |
| drum LFO reset | `0x19c9ac0` | R0 type, R1 voice | `0x18e13ac` | `:103` |

The Core calls them when the slot's LFO type changes (`0x1967a58` synth, `0x1967ac0` drum; full reset `0x1967b28`):
- `[0x1aaac44]` (= Core `0x1aaac0c + 0x38`) = fx-LFO object for the synth; `[0x1aaac48]` (`+0x3c`) for the drum;
- `[0x1aaad6c + v*4]`, v = 0..5: per-voice synth LFO objects; `[0x1aaad84 + v*4]`, v = 0..11: drum;
- before resetting to **bend** it calls `0x19c9cf4`, to **crank** `0x19cae08` (type-specific global resets).

**Objects live in the L1 data scratchpad**, placement-constructed by per-type getters (`i < kSynthVoiceCount` / `kDrumVoiceCount` asserts). Synth and drum share the per-voice area (only one is live):

| type | per-voice object (synth getter / drum getter) | size | fx-LFO object |
|---|---|---|---|
| tremolo | `0x19cc664` / `0x19cc6ac` (ctor `0x19cbd88`) | 28 B (`0xffb00a00 + v*0x1c`) | none (fx reset returns 0) |
| value | `0x19d0290` / `0x19d02f0` | 12 B | `0x19d034c` |
| random | `0x19cf340` / `0x19cf3d0` | 44 B (`0xffb00a00 + v*0x2c`) | `0x19cf460` |
| element | `0x19ce3a4` / `0x19ce3f0` | 4 B (vptr only) | `0x19ce43c` |
| midi | `0x19cd2ec` / `0x19cd338` | 4 B | `0x19cd384` |
| bend | `0x19ca6e8` / `0x19ca734` | 4 B | `0x19ca780` |
| crank | `0x19cb62c` / `0x19cb678` | 4 B | `0x19cb6c4` |

All fx-LFO objects sit at **`0xffb00c10`** (just after 12 × 44 bytes). The random object: `+0` vptr, `+4` int16 level (its envelope), `+8` int32 step counter, `+0xc` int16[8] random targets, `+0x1c` int16[8] smoothed values. The 4-byte types keep their state in globals.

**A hook must not enlarge these objects** (the area is packed and shared): keep only the vptr there and Marbles' state in our SDRAM, keyed by slot.

## 3. Vtables [C]

VisualDSP layout as elsewhere (`{int16 delta, fn}` 8-byte entries, every delta 0 here). Every LFO vtable has three methods:

| slot | role | ABI |
|---|---|---|
| +0x0c | deleting destructor | resets the vptr to the class base, frees if R1 bit 0 |
| **+0x14** | **process** | `R0 this, R1 const int16* lfo_params, R2 stage` → R0 = 1 if handled, else 0 |
| **+0x1c** | **trigger** (note-on) | `R0 this, R1 const int16* lfo_params` |

Word addresses (stock values in brackets):

| type | object | vtable | +0x14 process word | +0x1c trigger word |
|---|---|---|---|---|
| tremolo | synth & drum | `0x19107fc` | `0x1910810` [`0x019cbe48`] | `0x1910818` [`0x019cbdca`] |
| value | synth | `0x191097c` | `0x1910990` [`0x019cfaf8`] | `0x1910998` [`0x019cfd5a`] |
| value | drum | `0x191099c` | `0x19109b0` [`0x019cfdac`] | `0x19109b8` [`0x019cff16`] |
| value | fx | `0x19109bc` | `0x19109d0` [`0x019cff68`] | `0x19109d8` [`0x019d023e`] |
| **random** | **synth** | **`0x191090c`** | **`0x1910920` [`0x019cea70`]** | **`0x1910928` [`0x019ced74`]** |
| **random** | **drum** | **`0x191092c`** | **`0x1910940` [`0x019cedc0`]** | **`0x1910948` [`0x019cef66`]** |
| **random** | **fx** | **`0x191094c`** | **`0x1910960` [`0x019cefb0`]** | **`0x1910968` [`0x019cf2f4`]** |
| element | synth / drum / fx | `0x191089c` / `0x19108bc` / `0x19108dc` | `0x19108b0` / `0x19108d0` / `0x19108f0` | `0x19108b8` / `0x19108d8` / `0x19108f8` |
| midi | synth / drum / fx | `0x191082c` / `0x191084c` / `0x191086c` | `0x1910840` / `0x1910860` / `0x1910880` | `0x1910848` / `0x1910868` / `0x1910888` |
| bend | synth / drum / fx | `0x191071c` / `0x191073c` / `0x191075c` | `0x1910730` / `0x1910750` / `0x1910770` | `0x1910738` / `0x1910758` / `0x1910778` |
| crank | synth / drum / fx | `0x191078c` / `0x19107ac` / `0x19107cc` | `0x19107a0` / `0x19107c0` / `0x19107e0` | `0x19107a8` / `0x19107c8` / `0x19107e8` |

Every process and trigger function is **plain SDRAM** (`0x19c9d44`–`0x19d026x`); `%TEMP%\op1lfo\vtabs.py` prints the full table with stock values.

## 4. When the methods run

**Trigger (+0x1c)** [C]: `VoiceCtl.cpp`'s note-on (`0x1a848de` etc.) calls `Core::LfoTrigger(voice)` `0x19673dc` right before `Core::NoteOn` (`0x1967854`). It calls `lfo[voice].+0x1c(lfo, &lfo_params[slot])`, then the fx LFO's +0x1c, only when `lfo[voice] != 0` and the slot's `lfo_active` byte (`B[0x1aaac0c + 0x1c + slot]`) is set. Drum: `0x196735e` (params `0xff801644 + inst*16`). random's trigger resets its level: 0 if `params[3] < 0x3999` (fade in), 0x7fff otherwise.

**Process (+0x14)** is called from the Core's voice loop in **encrypted L1** (`0xffa009a0`..; the readable fragment at `0xffa00ad4`–`0xffa00b04` loads the voice's LFO pointer from `0x1aaad6c`, checks `lfo_active`, then `CALL [vtable+0x14]` with `R2 = 1`). The call sites aren't fully readable, so the stage protocol is reconstructed from what each process function accepts:

| stage (R2) | object | meaning | evidence |
|---|---|---|---|
| 0 | synth voice | before render: engine knobs and pitch/volume | random/value/crank modulate engine knobs via `Ranges::ApplyKnob`; tremolo/crank rewrite the 128-entry pitch array [C] |
| 1 | synth voice | envelope (ADSR) | random/value/crank/bend call the envelope handler `0x19f69b0` [C]; the visible L1 call passes 1 [C] |
| 2 | fx LFO | synth effect parameters | random/value/bend/crank fx process, FX Ranges `0x1de5c50`, fx type `[0xff801734 + slot*4]` [C] |
| 3 | drum voice | drum engine (tremolo accepts 0 or 3) | [C] for tremolo; drum processes of random/value ignore the stage [C] |
| 4 | fx LFO | drum envelope (3 values, ranges `0x1de56c8`) [I] | random fx handles dest 1 only with stage 4 [C] |
| 5 | fx LFO | drum effect parameters | fx type `[0xff801754 + inst*4]` [C] |

**Call rate: 689.0625 Hz per object** [I, strong]:
- the value LFO advances a 32-bit phase by one increment per call, and its tempo-synced increment is `bpm * 1662149.125` (`0x1aadad4`, set at `0x1a5e840`: float `0x49cae629`). That's 2^32 per 1/16 beat **at 689.0625 calls per second** (2^32 / (60 × 689.0625 × 16) = 1662149.1). Its free rate tops out at 2^32/32 per call = 21.5 Hz at that call rate;
- one call per 128-sample voice block at 88.2 kHz = one per 64 samples at 44.1 kHz, the effects' block (Test 16);
- the screen's frame `ticks` come from a counter `[0xff8016e0]` incremented in the Core's audio callback (`0xffa01992`, next to `s_seq_callback_semaphore`) [I: probably one per block].

Per-voice objects are processed once per sounding voice per block (as render is, `docs/engine-interface.md`) [I]: **a per-voice LFO's state advances only while its voice is processed.** Whether idle voices' LFOs run is unknown (device probe).

## 5. How the output reaches destinations [C]

There is no LFO output value. Each process **modifies the destination's data in place**, after the Core has prepared it:

| destination | how the LFO applies it |
|---|---|
| engine knobs | `Ranges::ApplyKnob(k, &delta, engine_type, 0xff801724)` `0x1a1dfec`: a clamped add into an **8 × int16 scratch copy at `0xff801724`**, engine type `[0xff8016e4 + slot*4]`. The voice loop references this buffer before render (`0xffa009d2`), so it is most likely the `params` pointer render receives [I] |
| envelope | `0x19f69b0(k, &delta, 0xff801724)` for k = 0..3 (the same scratch buffer, holding the ADSR at stage 1) |
| effect | `0x1a1de68(&delta, &fx_ranges[type][k], 0xff801724 + 2k)` for k = 0..7: `value = clamp(value + delta * scale)` with the FX Ranges table |
| pitch | multiplies the voice's **128 per-sample phase increments at `0xff801950`** (u32) in place (tremolo, crank `p3>>12 == 1`, bend) |
| volume | scales a **128 × int16 gain array at `0xff801d50`** in place (crank `p3>>12 == 0`, tremolo) [C: written; its use as the voice gain is I] |

**Depth handling** (random, `0x19cea70`): `amount = level * params[0]` (Q15 multiplies); each delta is `smoothed[k] * amount`, then scaled by the destination range's `scale` field inside the apply function. The fx-LFO version uses each FX range's own scale.

**The target selector** is a parameter, decoded as `params[2] >> 11` (random, element, bend, crank: 0 engine, 1 envelope, 2 effect, 3 pitch/volume where supported) or `>> 12` (value). random's range (1024..5120) only reaches 0..2, so **stock random never targets pitch**.

## 6. The random LFO in detail [C]

`LfoRnd` synth process `0x19cea70`, stage 0 + target 0 (engine) or stage 1 + target 1 (envelope); fx process `0x19cefb0` for target 2:
1. `counter++`; when `counter > ((0x7fff - params[1])^2 >> 16) >> 6` (0..255 calls), draw 8 new int16 targets from the shared LCG at `0xff802bc8` (`x = x * 0x19660d + 0x3c6ef35f`) and reset the counter. So **params[1] = speed** (new values every 1 to 256 calls: 689 Hz down to 2.7 Hz).
2. Smooth: `y = y * 0x7000 + x * 0x1000` (Q15, 1/8 per call).
3. `amount = level * params[0]` (**params[0] = depth**); apply `smoothed[k] * amount` to 4 engine knobs, 4 ADSR values or 8 FX params.
4. Level envelope from **params[3]**: below 0x4000 the level rises by `(p3 >> 2)^2 + 1` per call (fade in); otherwise it decays by a table factor `0x106a8f4[31 - (p3 >> 10)]`.
5. Writes `smoothed[0] * amount` to `W[0x1dd5c6c]` for the screen; returns 1.

The drum process (`0x19cedc0`) handles target 0 only.

## 7. Parameters, ranges and encoders [C]

- **Live values:** `int16 lfo_params[8]` per synth slot at **`0xff8014c4 + slot*16`** (drum: `0xff801644 + inst*16`). Type per slot `[0xff801384 + slot*4]` (drum `[0xff8013a4 + inst*4]`). Active flag `B[0x1aaac28 + slot]` (`0x196726c` reads it, `0x19672b8` writes it).
- **LFO Ranges table `0x1de5740`**: 7 types × 8 entries × `{int16 min, int16 max, int16 scale}` (48 bytes per type). `ApplyRange` `0x1a1de68`: `v = clamp(v + delta * scale (Q15), min, max)`; `IsLfoParamValid(type, k, &v)` `0x1a1df9c` checks min..max.

| type | p0 | p1 | p2 | p3 | p4 | p5 | p6 | p7 |
|---|---|---|---|---|---|---|---|---|
| tremolo | 0..32440 s10485 | ±32767 s10485 | ±32767 s10485 | 0..32767 s10485 | 0..32767 **s0** | s0 | s0 | 0..32767 s32767 |
| value | 0..16384 | ±32767 | 1024..11264 | 1024..15360 | s0 | s0 | s0 | s0 |
| **random** | **0..32767 s10485** | **0..32767 s32767** | **1024..5120 s10485** | **0..32767 s10485** | **0..32767 s0** | **s0** | **s0** | **s0** |
| element | 1024..7168 | ±32767 | 1024..7168 | 1024..15360 | s0 | s0 | s0 | s0 |
| midi | 1024..15360 ×4 | | | | 1024..7168 ×4 (all used) | | | |
| bend | 1024..15360 | 1024..7168 | ±32767 | ±16352 | 0..16352 | s0 | s0 | 1024..3072 |
| crank | ±16352 | 0..32767 | 1024..7168 | 1024..15360 | 0..16352 | s0 | s0 | s0 |

("s0" = scale 0: the encoder can't change it. Full dump: `%TEMP%\op1lfo\ranges_all.bin`.)

- **Encoders:** `SynthController::OnKnob` (vtable word `0x19119c0`, stock `0x01a505bc`) jumps through the page table `0x18fb268`; **the LFO page entry is `0x18fb274` → `0x1a5071e`**. It needs `lfo_active` (else it shows a hint, `0x1a50168(…, 4)`), then:
  - knob index k = 0..3 (blue, green, white, orange), **k + 4 with shift**;
  - tremolo and value: shift + blue = fine adjustment of p0 (`delta >>= 4`);
  - bend and crank: shift + blue ignored; crank knob 0 and bend knob 3 have a special accumulating path;
  - everything else, **including random's shifted knobs: `ApplyRange(&delta, &ranges[type][k], &params[k])`**. For random k = 4..7 that's a scale-0 add: **shift + encoder changes nothing** on the random LFO page;
  - then `Patch::SetLfoParams(patch, params)` `0x19ff734` (JSON `lfo_params`, marks dirty).
- **Clicks:** encoder clicks arrive as keys `0x3b`–`0x3e` on the synth controller's key slots (`docs/screen-interface.md` §7). Stock ignores them on every synth page, so an LFO-page hook can use them, sharing the MULTI engine's key hooks (`0x19119a8` / `0x19119b0` / `0x19119b8`) with a `page == 3` test (`[0x1b4ee88]`).

## 8. Screens [C]

`ScreenId` for LFOs is `0x1a3c01c` (table `0x18f80e8`): type t → screen 24 + t. The after-key hook returns it on the LFO page. Each LFO screen has the 4-slot screen vtable (destructor, IsActive, Update, Init):

| type | object | SVG name immediates (L / H) | vtable | Update word [stock] | Init word [stock] |
|---|---|---|---|---|---|
| tremolo | (getter) | `0x19818ce` / `0x19818d2` | `0x190fc14` | `0x190fc30` [`0x01981d28`] | `0x190fc38` [`0x01981624`] |
| value | `0x1df6034` | `0x1a48792` / `0x1a48796` | `0x1911824` | `0x1911840` [`0x01a48f9c`] | `0x1911848` [`0x01a48424`] |
| **random** | **`0x1de6158`** | **`0x1a3049e` / `0x1a304a2`** | **`0x1911384`** | **`0x19113a0` [`0x01a30b00`]** | **`0x19113a8` [`0x01a30238`]** |
| element | `0x1de6040` | `0x1a2cb5e` / `0x1a2cb62` | `0x191135c` | `0x1911378` [`0x01a2d34c`] | `0x1911380` [`0x01a2c93c`] |
| midi | `0x1dd87a0` | `0x19e4210` / `0x19e4214` | `0x1910bbc` | `0x1910bd8` [`0x019e49e8`] | `0x1910be0` [`0x019e3f78`] |
| bend | (getter) | `0x193db8a` / `0x193db8e` | `0x190f25c` | `0x190f278` [`0x0193de00`] | `0x190f280` [`0x0193d948`] |
| crank | (getter) | `0x1969002` / `0x1969006` | `0x190f7f4` | `0x190f810` [`0x01968c10`] | `0x190f818` [`0x019690a0`] |

The random screen is built by a static initialiser (`0x1a30488`: `Screen::Screen(0x1de6158, 26, "rndlfo.svg")`, vptr `0x1911384`). `rndlfo.svg` ids include `graphstart`, `blue`, `blueanim`, `graphend`, `greenline`, `greendot`, `whiteanim`, `fx`, `synth_1_`, `drum`, `envstart`, `envend`, `env_1_`, `redanim`, `adsr`, `dynaenv`. Swapping the drawing per mode works as on the MULTI engine: a second `SvgFile` loaded in our Init, the `document_` swapped in our Update.

## 9. Presets and boot validation [C]

- The patch JSON keys are `lfo_type` (name string), `lfo_params` (8 ints) and `lfo_active` (bool). `Patch::Apply` (`0x1a00f00` part):
  - `lfo_type` → `NameToId` → `[0xff801384 + slot*4]`;
  - all 8 `lfo_params` → `0xff8014c4 + slot*16`;
  - `lfo_active` → `0x19672b8`;
  - then `0x1a85f28(type)` (LFO reset).
- **Boot validation** (`0x1a037a8`, LFO part `0x1a041be`–`0x1a042c2`): `NameToId(lfo_type)` (an unknown name trips the assert), exactly 8 params, and **each param within its type's min..max** (`IsLfoParamValid`; failure → error 27). Any validation error of a saved slot ends in **`patch.cpp:390 Invalid synth patch … error %i`, the fatal assert** (`0x1a05264`), the same as an unknown engine type. (Old patches with `synth_version < 2` and tremolo get params 4..7 zeroed first.)
- **Consequences for a host:**
  - **a new LFO type name would hang stock firmware at every boot** if a slot saved with it were loaded: never add a name;
  - **params must stay inside the host type's ranges.** For random: p0, p1, p3 in 0..32767, **p2 in 1024..5120**, **p4..p7 in 0..32767 (non-negative)**;
  - stock random never changes p4..p7 (scale 0), and **all 10 factory random presets store 0 there**, so a nonzero p4..p7 can mark "Marbles" and carry its settings: 4 × 15 bits;
  - on stock firmware such a slot plays as the stock random LFO: harmless.

## 10. Recommendation: host Marbles on "random", swapped by the orange click

**Why random:**
- its theme (random modulation) is Marbles';
- its OnKnob path is the plain ApplyRange one: no special cases to fight;
- p4..p7 are free and range-valid for 0..32767;
- it has synth, drum and fx objects;
- it's in only 10 factory presets (stock behaviour kept when the marker is 0);
- its 44-byte object holds a vptr we keep.

crank (3 presets) is rarer but has special knob paths and float code; tremolo is used in 53 presets; element (27) owns the sensors.

**The swap:** an orange click on the LFO page (random selected) toggles a Marbles marker in `lfo_params[7]` (stock random when 0), as the MULTI engine toggles phase/Rings. On the 4 encoders, Marbles' controls (`docs/marbles-port.md` §6); the screen swaps between `rndlfo.svg` and a Marbles drawing.

**Words a patch would redirect** (stock values to verify before patching):

| what | word | stock | ours |
|---|---|---|---|
| random synth process | `0x1910920` | `0x019cea70` | per-voice apply (and the per-block Marbles tick) |
| random synth trigger | `0x1910928` | `0x019ced74` | note-on: optional clock reset / external tick |
| random fx process | `0x1910960` | `0x019cefb0` | the effect destination |
| random fx trigger | `0x1910968` | `0x019cf2f4` | pass-through |
| random drum process / trigger | `0x1910940` / `0x1910948` | `0x019cedc0` / `0x019cef66` | pass-through at first |
| RndLfoScreen Update / Init | `0x19113a0` / `0x19113a8` | `0x01a30b00` / `0x01a30238` | Marbles screen (swap) |
| SynthController OnKnob | `0x19119c0` | `0x01a505bc` | LFO page + random + marker → Marbles controls |
| key down / shifted / up | `0x19119a8` / `0x19119b0` / `0x19119b8` | (already hooked by MULTI) | add the `page == 3` cases |

Each hook falls through to the stock function when the marker is 0, so stock random stays bit-for-bit as it is.

## 11. Open questions (device probe)

The call sites are in encrypted L1, so a probe should log, in a wrapper around random's synth and fx process (same method as the FX probe, Test 16):
1. **calls per second per object** and the `stage` values (expect 689 Hz, stages 0/1 per voice, 2 for fx);
2. **whether idle voices' LFOs are processed**, and in which order the voices are;
3. whether `0xff801724` holds the knob copy at stage 0 and the ADSR at stage 1 (compare with `0xff8013c4 + slot*16` and the slot's ADSR);
4. whether `[0xff8016e0]` advances once per block (a cheap tick clock for Marbles);
5. whether per-sample pitch modulation from a random-hosted process at stage 0 is heard (writing `0xff801950`), since stock random never does it.
