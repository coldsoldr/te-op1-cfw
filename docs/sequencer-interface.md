# OP-1 sequencer interface (firmware 246)

How the OP-1's six sequencers are built, clocked, controlled and stored, and how they play notes, so Marbles can be hosted as a sequencer. Found from `op1 firmware/analysis/OP1_vdk.dis` and `OP1_vdk.elf`, and from `content/op1.db` / `op1_factory.db`; nothing has run on the device.

Tags:
- **[C]** confirmed from code or data;
- **[I]** inferred.

Methods and known addresses are shared with `docs/lfo-interface.md`, `docs/screen-interface.md` and `docs/engine-interface.md`.

String addresses: `strings -t x` file offset − 0x114 + 0x01000000 (first LOAD segment).

## Summary

| Question | Answer |
|---|---|
| Types | 6, runtime index 0–5: **pattern 0, endless 1, tombola 2, finger 3, sketch 4, arpeggio 5** (`SeqIds.cpp` table `0x18fa634`) [C]. The `seq_types` db ids (1–6, tombola 3) aren't used at runtime: names are [C] |
| Objects | Two layers per type [C]:<ul><li>a **player** (`*Player.cpp`), which makes the notes;</li><li>a **controller** (`*Controller.cpp`), which handles keys and encoders, under `SeqManager` (edit mode 7).</li></ul>Players are lazily built singletons, except endless, which has one per instrument [C] |
| Per instrument | **Synth and drum each have their own sequencer type, player pointer, on flag and data** [C]. Only the live instrument's player is ticked [C] |
| Clock | A **sequencer thread** pends on a semaphore that the audio callback posts [C]. Each wake calls `PlayerManager::Tick` `0x1a0d2c0` [C]. That runs the current player's **`Tick(t)` once per sequencer tick (96 per beat [I])**, with `t` = tick counter mod 48 [C] |
| Notes out | `MakeMidiNote(&n, key, velocity)` `0x1a85014`, then `VoiceCtl::NoteOn(&n, 0)` `0x1a85750` / `VoiceCtl::NoteOff(&n)` `0x1a85840` [C]. These are the same calls the keyboard uses, so a sequencer can play any synth or drum note |
| Keys in | While the instrument's sequencer is on, keyboard keys (and MIDI notes 53–76) go to `player->KeyDown(key, flag)` / `KeyUp(key)` [C]. KeyDown returns 1 to swallow the note or 0 to let it sound too [C] |
| Storage | Not in presets. op1.db [C]:<ul><li>table `sequencer` (`synth_sequencer` / `drum_sequencer` → **type name**);</li><li>per-type data tables `endless`, `drumseq` (pattern), `okseq` (finger), `easseq` (sketch), each keyed by `is_synth`;</li><li>tombola's settings as CtlKnobs in `knobs`.</li></ul>**An unknown name in `sequencer` is a fatal assert at boot** (`SeqIds.cpp:41`) [C] |
| Encoder clicks | Keys `0x3b..0x3e` go to the current type's controller key down, but **only while the sequencer is on** [C]. Tombola's controller ignores every key (base no-op) [C] |
| Host for Marbles | **tombola**: an orange-click swap on the tombola screen; settings in a private op1.db table; the words listed in §9 |

## 1. Source files and classes [C]

From the assert strings:
- **Players:** `..\src\seq\`
  - `pattern_data.cpp`
  - `EndlessPlayer.cpp`, `EndlessData.cpp/.h`
  - `TombolaPlayer.cpp`, `TombolaPhysics.cpp`
  - `ok_player.cpp`, `ok_data.cpp` (finger: "artists", "hands", patterns)
  - `EtchASketchPlayer.cpp`, `EtchASketchData.cpp`, `EtchASketchHelper.cpp` (sketch)
  - `PlayerManager.cpp` ("unknown sequencer type: %i")
- **Ids:** `..\src\shared\SeqIds.cpp`
- **Controllers:** `..\src\main\ctls\SeqManager.cpp`, `EndlessController.cpp`
- **Screens:** `..\src\gui\screens\EndlessScreen.cpp`, `TombolaScreen.cpp`
- **The tick source:** `..\src\dsp\Core.cpp:1206 s_seq_callback_semaphore` (in encrypted L1); thread name `kSeqThread`.

## 2. PlayerManager: players per instrument [C]

`PlayerManager` is a global at **`0x1ab3138`**:

| offset | address | field |
|---|---|---|
| +0x00 | `0x1ab3138` | **drum** player pointer |
| +0x04 | `0x1ab313c` | **synth** player pointer |
| +0x08 | `0x1ab3140` B | drum sequencer playing |
| +0x09 | `0x1ab3141` B | synth sequencer playing |
| +0x0c | `0x1ab3144` | synth sequencer type (index 0–5) |
| +0x10 | `0x1ab3148` | drum sequencer type |
| +0x14 | `0x1ab314c` B | set by `0x1a0cef0(b)`; Tick does nothing while it's set (pause) [I] |
| +0x18 / +0x1c | `0x1ab3150` / `0x1ab3154` | function pointers `0x19e1e10` / `0x19e2360`, set at init [C]; use not traced |
| +0x24 | `0x1ab3170` | Tick's clock accumulator |

**Instrument selector:** `[0x1de4c40]` (0 synth, 1 drum) picks the field everywhere. It is the UI's mode request; the audio side copies it into `0xff801038`.

Functions:

| address | function |
|---|---|
| `0x1a0d030` | `GetCurrentPlayer()` → `&player[instrument]` |
| `0x1a0cf94` | `GetCurrentType()` |
| `0x1a0cf0c` / `0x1a0cf2c` | `IsPlaying()` / `SetPlaying(bool)` (false: also calls the player's AllNotesOff) |
| `0x1a0cfd0` | `StopAll()`: AllNotesOff on both players (used on a synth ↔ drum switch) |
| **`0x1a0d050`** | **`SetPlayerType(index)`**: AllNotesOff on the old player, then a switch through table `0x18f2ef8` builds or gets the new one and stores pointer and type for the current instrument; index ≥ 6 asserts |
| **`0x1a0d2c0`** | **`Tick()`**: §4 |
| `0x1a0d1e0` | `UiFrame(ticks, screen_id)`, called by `Screen::UpdateAll` each frame: accumulates `ticks`, calls the current player's +0x34 while it's playing; screen 46 (sketch) has extra handling |

**Players** (getter, object, vtable) [C]:

| index | type | getter | object | player vtable |
|---|---|---|---|---|
| 0 | pattern | `0x1a08840` | `0x1de33d8` | `0x19110c4` |
| 1 | endless | `0x1987cd8` | synth `0x1cf136c`, drum `0x1cf0adc` | `0x190fd84` |
| 2 | **tombola** | `0x1a65ae0` | **`0x1dfc254`** | **`0x1911e04`** |
| 3 | finger | `0x19ed460` | `0x1ddc5a8` | `0x1910e9c` |
| 4 | sketch | `0x198c754` | `0x1cfe8c8` | `0x190ff0c` |
| 5 | arpeggio | `0x1a46400` | `0x1df5568` | `0x19117bc` |

Every getter except endless returns the same object for both instruments. So if synth and drum pick the same type, they share one player object; its per-instrument data is reloaded from the `is_synth` rows [I].

## 3. The player vtable [C roles from the callers, I names]

Entries are `{int16 adjust, fn}`, 8 bytes, starting at +0x0c (as the LFO and screen vtables).

| slot | name | who calls it, and how |
|---|---|---|
| +0x0c | destructor | — |
| **+0x14** | **`KeyDown(key, flag) → bool`** | keyboard key-down `0x19ac0b0` (`flag` = its second argument) and the MIDI note-on handler `0x1a859e4` (`flag` = 0), when the instrument's sequencer is on and `key` (0..23 = note − 53) is in range. Return 1 = consumed (no direct note); 0 = the note also plays directly |
| **+0x1c** | **`KeyUp(key)`** | keyboard key-up `0x19ac2f8`, MIDI note-off `0x1a858d6` |
| +0x24 | `OnLeave()` [I] | the old instrument's player, from the mode switch `0x19ac3c8`, before `[0x1de4c40]` changes |
| **+0x2c** | **`Tick(t)`** | `PlayerManager::Tick`, once per sequencer tick, `t` = 0..47 (§4). arpeggio resyncs when `t == 0` |
| **+0x34** | **`UiFrame(ticks)`** | `PlayerManager::UiFrame`, each screen frame (Gfx thread). tombola's physics/animation [I] |
| **+0x3c** | **`AllNotesOff()`** | type switch, `SetPlaying(false)`, `StopAll` |

**Tombola's implementation** (state in `0x1dfc258`–`0x1dfc6xx`) [C]:
- **KeyDown** `0x1a658b8`:
  - `flag ≠ 0`: queue a direct note-on (velocity `0x80`);
  - `flag = 0`: under a lock (`0x1a6565c` … `0x1a643cc`), launch a ball (`0x1a64adc`) carrying that key. The ball table is `0x1dfc558 + 20·i`: note, …, `B[+0x10]` active; 16 balls.
- **KeyUp** `0x1a65854`: sets `B[0x1dfc258 + key]` (note-off pending).
- **Tick** `0x1a659ec`: flushes the pending note-offs (`0x1dfc258[128]`), then the pending note-ons (`0x1dfc2d8[128]`, velocity `0x1dfc358[128]`). Collisions in the physics queue the notes.
- **AllNotesOff** `0x1a65948`: deactivates all balls, queues their notes off and flushes.
- **UiFrame** `0x1a65bbc`.
- +0x24 is `RTS`.

## 4. Clock, thread and tempo

- **Audio callback (L1, partly readable)** [C]: `0xffa01988` increments `[0xff8016e0]` (the block counter), checks the semaphore handle, and posts it (`CALL 0x1a89aa8`). It asserts `s_seq_callback_semaphore` if the handle is missing. So: one post per audio block, 689 Hz [I, strong].
- **Sequencer thread** `0x1a4318c` [C]: `loop { PendSemaphore([0x1df50b8]) (0xffa0b6e8); PlayerManager::Tick(); }`.
- **Tick** `0x1a0d2c0`:
  - Returns early when tape/mode flags (`B[0xff80104e]`, `B[0x18b6074]`) or the pause flag are set [C].
  - Sync source `0x1a5e860()` [C]: 0 = internal; 2/4/5 are other paths, probably MIDI clock / tape sync [I].
  - **Internal clock** [C]:
    - each call adds `2^24` to `[0x1ab3170]`;
    - while it's ≥ `[0x1b60524]`, it subtracts and advances one tick:
      - if the instrument's playing flag is set, `player->Tick([0x18c1884])`;
      - then `[0x18c1884] = (+1) mod 48` and `[0x18c187c] = (+1) mod 4`.
  - Ticks also wait for `[0xff801040] == 2` or a running counter (transport state) [I].
- **Tempo** [C]: `SetTempo` `0x1a5e7e4` stores `[0x1b60524] = (2646000 / (96·bpm)) · 2^18` (2646000 = 44100 × 60). With `2^24` per call at 689.0625 calls/s that is **96 ticks per beat** [I: assumes one Tick per block]. The tempo knob is op1.db `knobs` `tempobpm` (+ `tempobpmdecimal`). The LFOs' tempo increment `[0x1aadad4]` (`bpm · 1662149.125`) is set in the same function.
- So a player can count beats from `Tick` alone. `t == 0` every 48 ticks is the stock sync point (half a beat at 96 PPQN, if the probe confirms the rate).

## 5. How notes are produced [C]

- **`MakeMidiNote(MidiNote* n, key, velocity)` `0x1a85014`:**
  - `n->note = key + 53`, plus `12 × octave[slot]` (`[0x1dfe4e0 + [0xff8016d4]·4]`) in synth mode; no octave in drum mode (`[0x1de4c40] == 1`);
  - `n->velocity = velocity`, with −1 meaning 127 in NoteOn;
  - `B[n+8] = 1`; `[n+0xc] = 0`.
- **`VoiceCtl::NoteOn(n, 0)` `0x1a85750`** checks `0 ≤ note < 128`, then dispatches on `[0x1de4c40]`:
  - 0 synth: `0x1a85058` (voice allocation, `Core::LfoTrigger`, `Core::NoteOn`);
  - 1 drum: `0x1a857d0` path;
  - 2: game mode, `0x19e2e64`.
- **`VoiceCtl::NoteOff(n)` `0x1a85840`**: same dispatch (synth `0x1a85058` path with off, drum `0x1a84dac`).
- Every player calls only these. The keyboard does too: direct note-on with velocity `0x7f` through NoteOn, note-off with `0x40` through NoteOff. **A Marbles player can play any note 0..127 on the live instrument with these three calls.** Velocity is passed through; whether engines use it isn't checked.
- **Held keys:** the keyboard keeps its held keys in a vector at `0x1aad47c` and key bytes at `0x1aad488[24]`; MIDI keeps its own at `0x1dfe524`.
- **Endless is special:** keys outside 0..23 also go to it, and it keeps notes on a key-up (`GetCurrentType() == 1` tests in `0x1a858d6`).

## 6. On/off, type choice and the controller (SeqManager) [C]

- **`SeqManager`** is the edit-mode-7 controller (base ctor `0x1a16eec(this, 7)`), singleton **`0x1df500c`** (getter `0x1a42e38`), vptr at +4 = **`0x191153c`**.
- **Fields:**
  - `B[+8]` drum sequencer on, `B[+9]` synth sequencer on;
  - **`ctl[6]` at `0x1df5018`**, one per type:
    - pattern `new 0x88` (`0x1a07514`);
    - endless `0xc8` (`0x198673c`);
    - **tombola singleton `0x1a63f44` → object `0x1dfb780`**;
    - finger `0x48` (`0x19ec330`);
    - sketch `0x48` (`0x198ab94`);
    - arpeggio `0xe8` (`0x1a457bc`).
- **Controller vtable layout** (as the synth controller `0x1911994`): +0x14 key down, +0x1c shifted key down, +0x24 key up, **+0x2c knob(k, int16* delta)**, +0x34 enter, +0x3c, +0x44 screen id, +0x4c on/off notify.
- **SeqManager's own entries** (word → stock):

| slot | word | stock | behaviour |
|---|---|---|---|
| key down +0x14 | `0x1911550` | `0x01a426b8` | If the instrument's sequencer is on: `ctl[type]->KeyDown(key)`, done if it returns 1. Otherwise (or not consumed):<ul><li>`0x24`/`0x25` octave −/+ (Patch arrays `0x1ddd7e8`/`0x1ddd9a8`);</li><li>**`0x15` (the seq key) toggles the sequencer on/off**: flag, `ctl->+0x4c(on)`, `PlayerManager::SetPlaying(on)`, UI updates.</li></ul> |
| shifted key down +0x1c | `0x1911558` | `0x01a42498` | if on: `ctl->+0x1c(key)` |
| key up +0x24 | `0x1911560` | `0x01a42518` | if on: `ctl->+0x24(key)` |
| knob +0x2c | `0x1911568` | `0x01a4259c` | if on: `ctl->+0x2c(k, &delta)`; **the encoders do nothing while the sequencer is off** |
| enter +0x34 | `0x1911570` | `0x01a42904` | `ctl->+0x34(arg)`; unless `arg == 9`, turns the sequencer on (entering the seq screen starts it) [I] |
| +0x3c | `0x1911578` | `0x01a42624` | `ctl->+0x3c()` |
| screen id +0x44 | `0x1911580` | `0x01a4266c` | `ctl->+0x44()` |
| +0x4c | `0x1911588` | `0x01a429d0` | **type selection**: stores the choice (`[0x18b65cc/d0]`), resolves names through `SeqIds`, rebuilds the player (`SetPlayerType` callers at `0x1a42b00/0x1a42cc2/0x1a42dbe`), writes op1.db `sequencer` |

- **Load/save of types:** `0x1a42ba0` and its neighbours:
  - `SELECT value FROM sequencer WHERE key='synth_sequencer'` (and `drum_`);
  - `UPDATE sequencer SET value='%s' …`;
  - names ↔ index through `SeqIds` (`0x1a422ec` index → name, `0x1a42344` name → index; both assert when unknown).
- **Tombola's controller** (vtable **`0x1911ce4`**, vptr at object+4):

| slot | word | stock | behaviour |
|---|---|---|---|
| key down +0x14 | `0x1911cf8` | `0x01966cda` | shared base: `return 0` (keys and clicks unused) |
| shifted key down +0x1c | `0x1911d00` | `0x01966cd6` | base `return 0` |
| key up +0x24 | `0x1911d08` | `0x01966cd2` | base `return 0` |
| **knob +0x2c** | **`0x1911d10`** | **`0x01a63e50`** | k < 5 via table `0x18fdda4`; edits CtlKnobs `0x1dfb788` / `0x1dfb7a4` / `0x1dfb7c0` (restitution / gravity / angle, op1.db `knobs`) under the physics lock |
| +0x34 / +0x3c | `0x1911d18` / `0x1911d20` | `0x01966cce` / `0x01966ccc` | base `RTS` |
| screen id +0x44 | `0x1911d28` | `0x01a63e06` | returns **37** |
| +0x4c | `0x1911d30` | `0x01a63e04` | `RTS` |

- **Encoder clicks** `0x3b..0x3e` (`docs/screen-interface.md` §7) reach `ctl->KeyDown` only while the sequencer is on. Tombola returns 0 for them, so they fall through to SeqManager, which ignores them. **They are free on the tombola screen.** To use them while the sequencer is off too, hook SeqManager's key down `0x1911550` (test `GetCurrentType() == 2`).

## 7. Screens [C]

Built by static initialisers with `Screen::Screen(obj, id, name)` `0x1a3bab0`; vptr at +0xc; Update = vtable +0x1c, Init = +0x24.

| type | object | id | svg | vtable | Update word [stock] | Init word [stock] | name immediates (instr L / H) |
|---|---|---|---|---|---|---|---|
| pattern | `0x1de3eac` | 43 | `pattern.svg` | `0x1911104` | `0x1911120` [`0x01a0967c`] | `0x1911128` [`0x01a092ac`] | H `0x1a09278` |
| endless | `0x1cf1dd8` | 39 | `endless.svg` | `0x190fdc4` | `0x190fde0` [`0x01988f44`] | `0x190fde8` [`0x01988ce8`] | H `0x1988cb4` |
| **tombola** | **`0x1dfc75c`** | **37** | **`tombola.svg`** | **`0x1911ec4`** | **`0x1911ee0` [`0x01a65ebc`]** | **`0x1911ee8` [`0x01a66648`]** | **L `0x1a66394` / H `0x1a66398`** |
| finger | `0x1ddd134` | 45 | `ok.svg` | `0x1910edc` | `0x1910ef8` [`0x019ee9e8`] | `0x1910f00` [`0x019ee2b0`] | H `0x19ee948` |
| sketch | `0x1cfeb10` | 46 | `sketch.svg` | `0x190ff4c` | `0x190ff68` [`0x0198d924`] | `0x190ff70` [`0x0198d738`] | H `0x198d898` |
| arpeggio | `0x1df5d48` | 44 | `simple.svg` | `0x19117fc` | `0x1911818` [`0x01a474b8`] | `0x1911820` [`0x01a47284`] | H `0x1a47230` |
| (play-mode popup) | `0x1de4c88` | 40 | `playmode.svg` | `0x1911194` | `0x19111b0` [`0x01a0dad8`] | `0x19111b8` [`0x01a0d968`] | H `0x1a0da10` |

- The immediates for `kImm16Lo/Hi` patches sit at instruction + 2.
- The L instruction is the one before H [I, checked for tombola].
- The tombola screen's live parameters come from the tombola CtlKnobs (`TombolaScreen.cpp:203/208` assert gravity and restitution in 0..1).

## 8. Data, presets and boot validation

- **Not in synth/drum presets** [C]: the preset JSON keys are `adsr`, `fx_*`, `knobs`, `lfo_*`, `name`, `octave`, `synth_version`, `type`. Sequencer state is global in op1.db [C]:

| table | content (shipped op1.db) |
|---|---|
| `sequencer(key, value)` | `synth_sequencer` = `finger`, `drum_sequencer` = `finger` (type **names**) |
| `endless(is_synth, data)` | JSON: `steps` `[[note, ?] / null]`, `playmode`, `swing`, `timing`, `euclidean`, `euclidean_offset` |
| `drumseq(is_synth, data)` | pattern: `length`, `notes` (24-bit key masks per step), `pm`, `start`, `swing` |
| `okseq(is_synth, data)` | finger: per-pattern `length`, `notes` |
| `easseq(is_synth, data)` | sketch (`CREATE TABLE IF NOT EXISTS` at first use) |
| `knobs(id, data)` | CtlKnob JSON, e.g. `tombolarestitution` 0.87, `tombolagravity` 20.0, `tombolaangle` 0.0; `tempobpm` 125 |

  Arpeggio has no table; its settings live in RAM only or weren't found [I].
- **Boot validation:**
  - `SeqIds` name → index asserts on an unknown name (`SeqIds.cpp:41 unknown sequencer name '%s'`), so **a new sequencer name in `sequencer` would be fatal on stock firmware**, as a new LFO name is [C].
  - The data JSONs are checked by asserts (`EndlessData.h:70 pm`, `ok_data.cpp:39–81` ranges, `pattern_data.cpp:57 length`) [C].
  - CtlKnob values are range-asserted (`CtlKnob.cpp:66–68, 103–104`) [C].
  - **A table the firmware doesn't know is ignored** [I: nothing enumerates tables]. A private table such as `marbles(is_synth, data)` is therefore safe on stock firmware.
- **DB helpers** (as `okseq` uses them, `0x19ed048`–`0x19ed20a`) [C calls, I names]:
  - `snprintf(buf, 0x1000, fmt, …)` `0x1a9ce04`;
  - `db = 0x196dd44()` (op1.db handle);
  - `0x196f950(db, sql)` exec;
  - `0x196fbd0(out, db, sql)` single-value query (SeqManager's `SELECT value …`).

## 9. Recommendation: host Marbles on tombola, swapped by the orange click

**Why tombola:**
- **Theme:** a tombola is a drum of numbered balls, which is Marbles' own metaphor.
- **Its controller uses no keys**, so all four encoder clicks are free. Its knob handler is one simple switch.
- **Its player shows the whole contract in a few short functions:**
  - KeyDown/KeyUp queue keys;
  - Tick flushes queued notes through MakeMidiNote/NoteOn/NoteOff;
  - AllNotesOff;
  - UiFrame.

  A Marbles player is the same shape.
- **Least used, least risk:** its settings are three CtlKnobs, it has no data table and no type-specific boot checks. It's rarely the default (shipped op1.db uses finger).
- **Alternatives:**
  - arpeggio is the most-used sequencer, a worse candidate to take over;
  - endless and pattern have data tables and special key paths (endless keeps notes on key-up).

**The swap:**
- The `sequencer` table keeps the name `tombola`, so stock firmware still boots and plays tombola.
- A marker (Marbles on/off per instrument) and Marbles' settings go in a **private op1.db table** (`marbles`, `is_synth` + JSON data, created with `CREATE TABLE IF NOT EXISTS` through the helpers in §8), loaded when the tombola player is first built.
- A first version can keep them in RAM and default to Marbles.
- The orange click on the tombola screen toggles the marker, and Marbles' AllNotesOff (or tombola's) runs at the swap.

**Words to redirect** (each a plain SDRAM data word through the image patch table; each hook tail-calls stock when the marker is off):

| what | word | stock |
|---|---|---|
| tombola player KeyDown | `0x1911e18` | `0x01a658b8` |
| tombola player KeyUp | `0x1911e20` | `0x01a65854` |
| tombola player OnLeave | `0x1911e28` | `0x01a65872` |
| tombola player Tick | `0x1911e30` | `0x01a659ec` |
| tombola player UiFrame | `0x1911e38` | `0x01a65bbc` |
| tombola player AllNotesOff | `0x1911e40` | `0x01a65948` |
| tombola controller knob | `0x1911d10` | `0x01a63e50` |
| tombola controller key down / shifted / up (clicks; sequencer on) | `0x1911cf8` / `0x1911d00` / `0x1911d08` | `0x01966cda` / `0x01966cd6` / `0x01966cd2` (shared base functions: patch only tombola's vtable words) |
| SeqManager key down (clicks while the sequencer is off, if wanted) | `0x1911550` | `0x01a426b8` |
| tombola screen Update / Init | `0x1911ee0` / `0x1911ee8` | `0x01a65ebc` / `0x01a66648` |

**How Marbles maps onto it:**
- **Clock:** Marbles' T clock runs from `Tick(t)` (96/beat [I]): a rate setting picks the division, and `t == 0` gives a phase reference. Its external-clock input becomes the OP-1 tempo for free.
- **Keys:**
  - held keys (KeyDown returns 1 so they don't sound directly) set the note pool, root or range for X;
  - KeyUp removes them;
  - with no key held the sequencer can idle, or keep the last pool (UI decision).
- **Notes:** on each T gate, `MakeMidiNote(key or raw note, velocity)` then `NoteOn`. The matching `NoteOff` comes after the gate length (Marbles' pulse width), counted in ticks inside Tick.
  - Raw notes: write `n.note` after MakeMidiNote to bypass the 53 + octave mapping.
  - Y (the smooth voltage) can drive velocity.
- **Drums:** the same calls play drum keys when the drum instrument is live. Marbles' drum T model fits there.
- **Threads:** KeyDown/KeyUp and the knob handler run on the UI/key thread, Tick on the sequencer thread, UiFrame on the Gfx thread. Tombola guards shared state with a lock (`0x1a6565c` / `0x1a643cc`). Marbles should pass keys through a small lock-free queue that Tick drains, and only call NoteOn/NoteOff from Tick, as tombola does.
- **CPU:** ~4 MHz for one Marbles at one step per block (`docs/marbles-port.md` §5). It runs only for the live instrument.

## 10. Device probe results (Test 20, seqprobe1, 2026-10-17)

- **96 ticks per beat** confirmed (96/s at 60 BPM, 288/s at 180); `t` wraps at **48** (half a beat).
- The sequencer clock **runs all the time**: stopped transport and tape recording don't stop it.
- KeyDown's `flag` = **1 when shift is held**, 0 otherwise.
- NoteOn/NoteOff from the sequencer thread (as tombola does it) play cleanly, chords included; **velocity is audible** on the engines tried.
- Tombola as both synth and drum sequencer works (one shared player).
- Still open: the private op1.db table (not probed), where the bar position is kept (`[0x18c187c]` mod 4 is a candidate), the voice-lock question under heavy use.

## 11. Open questions before the probe (kept for reference)

A wrapper around tombola's player words (method of Test 16) logging tick and call counts would settle:
1. **The tick rate:**
   - Tick calls per second and player `Tick` calls per beat at a known BPM (expect 689/s and 96/beat);
   - whether `t` wraps at 48 as half a beat;
   - whether ticks run while the transport is stopped, and during tape play/record.
2. **KeyDown's `flag`:** what the keyboard passes (tombola plays the key directly when it's non-zero: shift held? a mode?).
3. **Interactions:**
   - whether `NoteOn` from the sequencer thread while the user plays keys needs the voice lock `B[0x1dfe730]` (`VoiceCtl.cpp:443 voice ctl is locked`);
   - which `[0x18c17d4]` states (2, 3) bypass the sequencer.
4. **Velocity:** whether engines respond to `MidiNote.velocity` (Y → velocity).
5. **Switching:** what the play-mode popup (id 40) and `+0x24 OnLeave` do on a synth ↔ drum switch with tombola on both instruments (one shared player object).
6. **Storage:** confirm that a private op1.db table survives firmware updates and factory resets (op1.db is rewritten by "restore factory defaults").

## 12. Pattern data and the sequencer list (for Grids)

### 12.1 Renaming "pattern" to "multi" in the sequencer list

- **The chooser list is built by the same type-list function** `0x19707d8` with `seq_types` [C]:
  - `0x1970b24` `GetSeqList()` caches the list at `0x1bab8d0` (48-byte entries: the name string, id at +0x1c);
  - its only caller is `0x1a4245c`, which opens the chooser popup.
- **Picking an entry resolves by id** [C]: SeqManager +0x4c `0x1a429d0` runs `SELECT type FROM seq_types WHERE id='%i'` (`0x18fa79c`), which gives the raw db name 'pattern', then `SeqIds` name → index (`0x1a42344`). It writes `UPDATE sequencer SET value='%s' …` (`0x18fa724` / `0x18fa760`) with that raw name. So op1.db keeps 'pattern'.
- **Opening the chooser resolves by the displayed name** [C]. `0x1970ba0(current type index)`:
  1. `0x1a422ec` index → SeqIds name ('pattern');
  2. `0x1a17c28(list, name)` finds the entry by name;
  3. on a miss it calls `0x1919aa8("Presets.cpp:66 can't find %s")`, the fatal assert.

  With `WHEN 'pattern' THEN 'multi'` in the query, the list holds 'multi', so **opening the chooser while pattern is the current type would assert. The query rename alone is not safe.**
- **Safest fix:** redirect the one call at **`0x1970bea`** (stock bytes `06 e3 81 8b` = `CALL 0x1a422ec`) to a wrapper that returns "multi" for index 0 and otherwise tail-calls `0x1a422ec`; then add `WHEN 'pattern' THEN 'multi'` to the query.
  - It is a pc-relative `CALL` (pcrel24, ±32 MB; region A at `0x02c0xxxx` is in range). That needs a new patch kind in `tools/inject_app.py`: encode `0xE300 | (off >> 16 & 0xff)`, `off & 0xffff` with `off = (target − 0x1970bea) / 2`, after checking the stock bytes.
  - The selection path and op1.db stay 'pattern', so stock firmware is unaffected [C].
  - **Not covered:** any other place that shows the sequencer's name from SeqIds directly (none found in the chooser path) [I].
- **Alternatives rejected:** renaming the SeqIds string, or the `seq_types` row, would store 'multi' in op1.db, and stock firmware asserts on that at boot.

### 12.2 Pattern data (for "print")

- **Two `PatternData` objects, built lazily** by the getter **`0x1a083c4()`** [C]:
  - guards `[0x1de2ecc]` / `[0x1de2ed0]`;
  - the db is loaded once (guard byte `0x1de320c`): `SELECT data FROM drumseq WHERE is_synth=%i`, then JSON;
  - it returns **synth `0x1de2ed4`** if `[0x1de4c40] == 0`, otherwise **drum `0x1de3070`**.
- **Layout** (size 0x19c) [C]:

| offset | field |
|---|---|
| +0x00 / +0x01 | dirty bytes (set by `0x193ef3c`) |
| +0x04 | vptr `0x191106c`; its +0x14 = `0x1a08204`, the save |
| **+0x08** | **notes: 16 steps × 24 keys, one byte each (0/1); address `+8 + step·24 + key`** |
| +0x188 | swing (default 50) |
| +0x18c | length (1..16, default 16; `pattern_data.cpp:57`) |
| +0x190 | start (0..15, `start + length ≤ 16`; `:74`) |
| +0x194 | pm (0..2, default 2; `:68`) |
| +0x198 | is_synth |

- **kMaxLength = 16**: a pattern holds at most 16 steps [C]. A Grids bar prints as 16 sixteenths. 32nd resolution would have to drop the odd steps. Accents can't be stored (notes are on/off only) [C/I].
- **Accessors** (this = data object, R1 = step, R2 = key; they assert step < 16, key < 24) [C]:
  - `0x1a07ee0` clear key;
  - `0x1a07f60` get key;
  - `0x1a07fc4` toggle key;
  - `0x1a08030` SetLength(len);
  - `0x1a08080` SetStart(start);
  - `0x1a080d8` SetPm(pm).

  The setters call `0x193ef3c(this)`.
- **Saving** [C/I]: `0x193ef3c(obj)` sets the dirty bytes and posts the saver semaphore `[0x1ba2990]`. The saver thread then calls the object's vtable +0x14 = `0x1a08204`, which runs `UPDATE drumseq SET data='%s' WHERE is_synth=%i`.
- **The player reads the data live** [C]: Tick `0x1a08ffc` → step `0x1a08d24` calls the getter and `0x1a07f60` (get key) every step, and reads `+0x194` / `+0x18c`. Writes show up at the next step.
- **Recommended print** (UI thread, drum instrument live):
  1. `pd = 0x1a083c4()`;
  2. for step 0..15, key 0..23: `B[pd + 8 + step·24 + key] = hit ? 1 : 0`;
  3. `0x1a08030(pd, 16)`, `0x1a08080(pd, 0)`;
  4. `0x193ef3c(pd)` to save.

  Stock pattern then plays and shows the bar after the orange swap, and it persists in op1.db's `drumseq` row (is_synth 0). Byte writes racing the sequencer thread can at worst mix old and new steps within one tick [I].
