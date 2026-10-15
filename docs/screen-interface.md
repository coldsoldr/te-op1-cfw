# OP-1 screen and input interface (firmware 246)

How an engine screen (SVG graphics) and the encoders/keys work in the OP-1 app, worked out from `OP1_vdk.dis`, so we can give the phase engine a Rings screen (`incoming/rings.svg`) and catch shift + encoder turns. Read-only analysis, in the style of `docs/engine-interface.md`.

- **confirmed**: read directly from instructions or data;
- **inferred**: deduced from behaviour or naming, not proven.

**Vtable convention.** VisualDSP vtables use 8-byte entries `{int16 this-delta, pad, fn}`. A virtual call is `P = [obj + vptr_offset]; this += W[P + k]; CALL [P + k + 4]`. Below, "slot +0x1c" means the function word at vtable `+0x1c` (its delta is at `+0x18`). Every slot listed here has delta 0.

**Where the code lives.** Everything in this doc is plain SDRAM code (`0x019xxxxx`–`0x01axxxxx`), readable in the disassembly. The only L1 calls on these paths are RTOS primitives (`0xffa0b6e8` semaphore pend, `0xffa00134` post, `0xffa0a1e0` the `std::vector` out-of-range throw) and the audio core.

## 1. The phase engine screen class (`PdScreen`)

The code sits right after the Pd voice code in `Pd.cpp` (`0x1a0bf24`–`0x1a0ce0a`).

**Singleton getter `0x1a0cd50`** (confirmed). It's a function-local static at **`0x1de4bd8`** (guard word `0x1de4bd4`); `Screen::RegisterAll` (`0x1a3bce4`, the list of every screen's getter) calls it at boot. On first call it:
1. calls the base constructor `Screen::Screen(this = 0x1de4bd8, id = 7, svg = "pd.svg")` at `0x1a3bab0`;
2. stores the PdScreen vtable **`0x191116c`** at `this + 0x0c`;
3. zeroes `+0x24` and `+0x28`;
4. registers its destructor for exit (`0x1aa2b14(0x18b61fc)`).

The screen id is 7, the same number as the engine type id (phase = 7).

**Vtable `0x191116c`** (confirmed). It has 4 slots; the next class's vtable starts at `0x19111a4`.

| slot | word address | fn | role |
|---|---|---|---|
| +0x0c | `0x1911178` | `0x1a0cdc8` | deleting destructor: resets the vptr to the base `0x1911474`, calls `Screen::~Screen` `0x1a3b408`, frees if bit 0 of R1 |
| +0x14 | `0x1911180` | `0x1a3b3f6` | inherited `IsActive(this, cur_id)`: returns `this->id == cur_id` |
| +0x1c | **`0x1911188`** | **`0x1a0c0a0`** | **`Update(this, ticks)`**: the per-frame draw, called only while this screen is active |
| +0x24 | **`0x1911190`** | **`0x1a0cca0`** | **`Init(this)`**: looks up the SVG nodes once, after the document has loaded |

**Object layout** (confirmed from the constructor, `Init` and `Update`; at least 0x30 bytes):

| offset | field |
|---|---|
| +0x00 | `document_` (the parsed SVG, set by `Screen::LoadAll`) |
| +0x04 | screen id (7) |
| +0x08 | SVG file object (8 bytes, from `0x1a50350(obj, "pd.svg")`) |
| +0x0c | vptr |
| +0x10 | node `blue` |
| +0x14 | node `green` |
| +0x18 | node `white` |
| +0x1c | node `red1` |
| +0x20 | node `red2` |
| +0x24, +0x28 | bounding box of `red1` captured in `Init` (node slot +0x1c, then `0x19463dc`) |
| +0x2c | synth instance last drawn (copied from `[0x1df4e38]` at the start of `Update`) |

**`Init` (`0x1a0cca0`)**: `this->blue = Screen::GetNode(this, "blue")`, and the same for `green`, `white`, `red1` and `red2` (names at `0x18f2e18`, 8 bytes apart). It never looks up `static` or `new`; those are drawn as part of the document.

**`Update` (`0x1a0c0a0`)** (confirmed). It has a 0x380-byte frame and uses soft-float throughout:
1. `inst = [0x1df4e38]`, then `p = GetSynthParams(inst)` (`0x1a28210`): a `std::vector<float>` of **8 normalised knob values (0..1)**, described in §4c. Pd uses `p[0]`–`p[3]` (`vector::at`, so it throws if the vector has fewer than 4).
2. It reads a level from the voice code's accumulator (`0x1a0bf24` decays and returns `0x1de4b90`; the Pd voice adds into it every render), which animates the red curve.
3. For each of `blue`, `green`, `white`, `red1`, `red2` it builds a 3×3 fixed-point matrix and calls `Node::SetTransform(node, m)` (`0x19ead58`). `red1` and `red2` use products of matrices (`0x1a0bfa4` = 3×3 multiply).
4. It calls `Screen::Draw(this)` (`0x1a3b4c8`), which renders the whole document into the frame buffer.

Pd only moves and scales existing shapes; it never changes colours, points or visibility.

## 2. The Screen base class and SVG API

### Screen (`..\src\gui\Screen.cpp`, base vtable `0x1911474`)

| function | address | args | does | status |
|---|---|---|---|---|
| `Screen::Screen` | `0x1a3bab0` | R0 this, R1 id, R2 svg filename (or 0) | vptr `0x1911474` at +0x0c; id at +4; `document_ = 0`; `+8 = new SvgFile(filename)` (`0x1a5e3b0(8)`, `0x1a50350`); `s_screens[id] = this` (table `0x1df4e54`, 0x44 entries) | confirmed |
| `Screen::~Screen` | `0x1a3b408` | R0 this, R1 flags | frees the SVG file object (`0x1a50394`) | confirmed |
| `Screen::IsActive` | `0x1a3b3f6` | R0 this, R1 id | `return this->id == id` | confirmed |
| `Screen::SetRaster` | `0x1a3b44c` | R0 raster | `s_raster` (`0x1df4e34`) = raster | confirmed |
| `Screen::GetRaster` | `0x1a3b490` | | returns `s_raster` | confirmed |
| `Screen::GetScreens` | `0x1a3ba34` | | returns `&s_screens[0]` (`0x1df4e54`) | confirmed |
| **`Screen::Draw`** | **`0x1a3b4c8`** | R0 this | renders `document_` into `s_raster` (the document's slot +0x14, with a static identity matrix at `0x1b60fd8`) | confirmed |
| **`Screen::GetNode`** | **`0x1a3b580`** | R0 this, R1 id string | `return Node::Find(document_, id, 1)` (`0x19eb8ac`); asserts on a missing document or id | confirmed |
| `Screen::MakeText` (name inferred) | `0x1a3b5d0` | R0 this, R1 template id, R2 align (0 left, 1 centre, 2 right) | builds a text node from a template node and its line-style colour (`Screen.cpp:113–147`) | confirmed calls; purpose inferred |
| `Screen::RemoveNode` | `0x1a3b7d0` | R0 this, R1 id | finds the node and deletes it from the tree (`0x19ebdbc(document_, node)`, a recursive remove-and-delete) | confirmed |
| **`Screen::GetLineStyle`** | **`0x1a3b854`** | R0 this, R1 id | finds the node; `ls = node.slot+0x2c()` (its line-style id); `node.slot+0x24(ls)` (for a group, this makes **every child** use that id); returns `&document_->linestyles[ls - 1]` (20-byte entries, vector at `doc+0x6c/+0x70`) | confirmed |
| **`Screen::GetFillStyle`** | **`0x1a3b96c`** | R0 this, R1 id | the same with slots +0x3c/+0x34; returns `&document_->fillstyles[fs - 1]` (4-byte entries, vector at `doc+0x5c/+0x60`) | confirmed |
| `Screen::LoadAll` | `0x1a3ba50` | R0 `s_screens` | for every registered screen: if there's no `document_`, load it (`0x1a503d8(file)`), then call slot +0x24 (`Init`) | confirmed |
| **`Screen::UpdateAll`** | **`0x1a3bb20`** | R0 `s_screens` | the per-frame pass, §3 | confirmed |

### SVG file loading (`svg_loader.cpp`, `Parser.cpp`)

- `SvgFile` ctor `0x1a50350(obj, name)` stores the name; `0x1a503d8(obj)` parses it and returns the document. The parser (`Parser.cpp`, rapidxml) opens **`/yaffs2/content/display/%s`** (format at `0x19fb480`), so a new file `content/display/rings.svg` in the package is loaded with the name `"rings.svg"` (confirmed).
- **Supported elements** (string table `0x18eff50`–`0x18effd8`): `svg`, `g`, `line`, `polyline`, `polygon`, `ellipse`, `circle`, `rect`, `path`. Attributes include `fill`, `stroke`, `stroke-width`, `stroke-linecap`, `stroke-linejoin`, `stroke-miterlimit`, `opacity`, `transform` (`matrix`, `translate`, `scale`) and `viewBox` (confirmed).
  - rings.svg uses only these. **`<rect rx>` (rounded corners) is probably ignored:** no stock SVG uses it (inferred).
- **Styles are per element.** The parser appends one line style per styled element (`0x1976b30` is a plain `push_back` that returns the new 1-based id) and does no de-duplication (confirmed). So writing a colour into one element's style changes only that element, except through `GetLineStyle` on a group (see the table above).

### Node and Shape objects (`..\src\gui\lib\Node.cpp`, `Shape.cpp`)

Node (group) vtable `0x1910cfc`, Shape (leaf) vtable `0x19115d4`. Node fields (confirmed):

| offset | field |
|---|---|
| +0x00..+0x20 | local transform: 9 × int32, **fixed point with 10 fractional bits (1.0 = `0x400`)** |
| +0x24 | id (`std::string`, SSO: length at +0x38, capacity at +0x3c) |
| +0x44/+0x48 | children (`std::vector<Node*>` begin/end; groups only) |
| +0x50 | **byte: visible** (render is skipped when 0) |
| +0x54 | vptr |
| +0x5c..+0x68 | Shape: bounds (4 words) |
| +0x6c | Shape: `uint16` fill-style id (0 = none) |
| +0x6e | Shape: `uint16` line-style id |
| +0x74.. | Shape: sub-paths (vector of 32-byte entries) and cached rasterisation; `+0x84` byte = cache valid |

**Matrix layout** (inferred from Pd): row-vector convention, `m[0] m[4]` = scale x/y, **`m[6]` = translate x, `m[7]` = translate y**, `m[8]` = `0x400`. Pd's `white` (a dashed horizontal line) gets a value only in `m[7]`, and `blue`/`green` only in `m[6]`. Identity = `{0x400,0,0, 0,0x400,0, 0,0,0x400}`.

| slot | Node (group) | Shape (leaf) | meaning |
|---|---|---|---|
| +0x0c | `0x19ebafc` | `0x1a43528` | destructor |
| +0x14 | `0x19eb818` | `0x1a4333c` | `Render(raster, parent_matrix, …)`: skipped if `B[+0x50] == 0` (confirmed) |
| +0x1c | `0x19eadcc` | `0x1a434d8` | get bounds (R1 = out) |
| +0x24 | `0x19eae24` | `0x1a434ae` | **`SetLineStyleId(id)`**: Shape `W[+0x6e] = id`; a group sets it on every child (confirmed) |
| +0x2c | `0x19eb748` | `0x1a434b6` | `GetLineStyleId()`: Shape `W[+0x6e]`; a group returns its first child's non-zero id |
| +0x34 | `0x19eae80` | `0x1a434be` | `SetFillStyleId(id)`: only if the current fill is not 0 |
| +0x3c | `0x19eb7b0` | `0x1a434ce` | `GetFillStyleId()` |
| +0x74 | `0x19eb430` | `0x1a43506` | **invalidate** the cached rasterisation (Shape: `B[+0x84] = 0`) |

**The calls a screen needs** (all confirmed unless marked otherwise):

| operation | how | notes |
|---|---|---|
| find element | `Screen::GetNode(this, "id")` `0x1a3b580` → `Node*` | asserts if missing ("Can't find SVG object '%s'", `Node.cpp:80`) |
| move / scale | **`Node::SetTransform(node, const int32 m[9])` `0x19ead58`** | copies the matrix and calls slot +0x74 (invalidate) only if it changed; children inherit it; 202 call sites |
| show / hide | `B[node + 0x50] = 0 / 1` | written inline by stock screens (667 sites); no invalidate needed |
| stroke colour | `p = Screen::GetLineStyle(this, "id")` `0x1a3b854`, then write `p[0..3]` | bytes R, G, B, A (from the parser's `#%02x%02x%02x` handling; byte order inferred); stock screens keep `p` and write the colour every frame (cluster, many others) |
| fill colour | `p = Screen::GetFillStyle(this, "id")` `0x1a3b96c`, then write `p[0..3]` | the same; only for elements whose fill isn't `none` |
| swap style | `node.slot+0x24(styleId)` / `+0x34` | ids from another node's slot +0x2c/+0x3c, stored as int16 (e.g. `0x1a5afbc`) |
| delete element | `Screen::RemoveNode(this, "id")` `0x1a3b7d0` | used for template nodes after reading their colours |
| polyline points / circle cx | no stock API found | the points live in the Shape's sub-path vector (+0x74, 32-byte entries; Shape slots +0x54/+0x64 append to it). Not mapped; use transforms or pre-drawn variants instead (§6) |

### Compared with other engine screens

- **St (string), `st.svg`:** static object at `0x1df6b9c`, id 3, vtable **`0x19118f4`** (destructor `0x1a4f864`, Update `0x1a4f8a8`, Init `0x1a4f500`). `Init` looks up 22 nodes named `g%i` plus a few others. `Update` reads the same `GetSynthParams` vector, toggles `B[node+0x50]` to show or hide the `g%i` steps, and uses `SetTransform`.
- **Fm, `fm.svg`:** singleton getter `0x19a4c50` (object `0x1d0598c`), id 0, vtable **`0x190ffdc`** (destructor `0x19a4cf0`, Update `0x19a4d34`, Init `0x19a4a3c`).
- **Cluster (`cls.svg`):** `Init` (`0x1960a70`) reads the colours of template nodes `on%i` with `GetLineStyle` and then deletes them with `RemoveNode`.
- Every engine screen has the same 4-slot shape: destructor, inherited `IsActive`, `Update`, `Init`. No screen receives input; input goes to controllers (§4).

## 3. Redraw and frame timing (confirmed unless marked otherwise)

**Frame loop.** The Gfx thread (`Gfx_thread.cpp`, loop at `0x19ab880`):
1. waits on a semaphore (`[0x1d0851c]`), which is posted by `0x19efe98`. `OP_Main` registers that function as the display driver's frame callback (`0x1a2fac4(0x19efe98)` at `0x19f07de`), so a frame is drawn after each display transfer (inferred: the rate is set by the LCD driver and wasn't measured);
2. calls `0x19b2674`, which clears or prepares the raster (`0x1a23442`), then `Screen::UpdateAll(Screen::GetScreens())`;
3. pushes the frame out (`0x1aa185c`, `0x1a2fb18`).

**`Screen::UpdateAll` (`0x1a3bb20`)** runs once per frame:
1. It pops a MainInterface event (`0x19d7d70`). If the current screen id changed, or the heap use is above 8 MB (`0x1a5e398() > 0x7a1200`), it sets `B[0x1df4e40]` and calls `document.slot+0x7c` on every screen, apparently to flush the cached rasterisations (inferred).
2. `ticks` = the change in the counter at `[0xff8016e0]` since the last frame, low 22 bits, sign-extended.
3. For each of the 0x44 screens: `if (s.IsActive(cur)) s.Update(ticks)`, where `cur = [0x18b65c8]` is the current screen id.
4. Then it calls `0x1a0d1e0(ticks, cur)` and `MainInterface::ClearKnobTouched` (`0x19d7ed0`).

**So `Update` is the per-frame callback,** and it runs only while that engine's screen is showing. There's no "request redraw" API: every active screen redraws itself completely each frame by calling `Screen::Draw` at the end of `Update`.

**Knob-touched flags.** `MainInterface` keeps `B[0x1dd7f74 + k]` (k = 0..3), set by the knob dispatcher when encoder k turns and cleared after each frame. `MainInterface::KnobTouched(k)` (`0x19d7c84`) reads them, which lets a screen tell which encoder moved this frame.

## 4. Input

### Path from hardware to the controller (confirmed)

1. **Producers.** Key events go into `s_key_queue` (`0x1dd5044`: 256 × `{u8 key, u8 state}`) via `KeyHandler::PushKey` (`0x19c2f24`, called from `0x19efe5a`/`0x19efe88`). Encoder ticks add to `s_knob_acc[4]` (`0x1dd5250`, int32) via `KeyHandler::AddKnob(knob, delta)` (`0x19c3044`, asserts `knob < kKnobCount` = 4).
2. **The KeyHandler thread** (`0x19c2cc0`) wakes on its semaphore, then:
   - pops each key and calls **`KeyDispatch(key, pressed)` `0x19b6c74`**;
   - for each encoder `k` with `|acc| > 1`, takes the even part (`acc & ~1`), leaves the remainder, and calls **`KnobDispatch(k, &delta)` `0x19b6c0c`** with `delta = taken << 9` (int16). So one detent is ±0x400 (inferred from the `<< 9` and the 2-count step).
3. **`KnobDispatch(k, &delta)`:**
   - marks knob `k` touched (`0x19d7e78`);
   - **if SHIFT is held (`MainInterface::IsShift()` `0x19d7c6c` → `B[0x1dd7f78]`), `k += 4`**;
   - calls **`active_controller.slot+0x2c(ctrl, k, &delta)`**.
4. **`KeyDispatch(key, pressed)`:**
   - key `0x2c` also records its state in `B[0x1dd4d95]`;
   - on press: `IsShift()` ? `ctrl.slot+0x1c(key)` (**shifted key**) : `ctrl.slot+0x14(key)` (key down). If it returns 0, the global handler runs (`0x19ac9b8` shifted, `0x19ac4b4` normal);
   - on release: `ctrl.slot+0x24(key)`, else the global handler `0x19acd60`;
   - then `ctrl.slot+0x44()` and an event record (`0x19d7c94`).
5. **Active controller.** `ModeSwitcher::GetActive()` `0x19e97fc` returns `s_active_controller` (`0x1dd8bc8`), defaulting to the mode-2 controller. Controllers are `{int mode; vptr; …}` in `s_controllers` (`0x1dd8bd0`). `ModeSwitcher::SetMode(m)` `0x19e9738` calls `old.slot+0x3c()` (leave) and `new.slot+0x34(old_mode)` (enter); the current mode is at `[0xff907d58]`.

### (a) Per-controller virtual methods (confirmed)

Synth controller (`synth_controller.cpp`, mode 0, constructor `0x1a508d4`, vtable **`0x1911994`**):

| slot | word address | fn | role |
|---|---|---|---|
| +0x0c | `0x19119a0` | `0x1a50a4c` | destructor |
| +0x14 | `0x19119a8` | `0x1a51058` | key down (R1 = key) |
| +0x1c | `0x19119b0` | `0x1a50904` | **shifted key down** |
| +0x24 | `0x19119b8` | `0x1a5056c` | key up |
| +0x2c | **`0x19119c0`** | **`0x1a505bc`** | **`OnKnob(this, knob 0..7, int16 *delta)`** |
| +0x34 | `0x19119c8` | `0x1a50a90` | enter mode |
| +0x3c | `0x19119d0` | `0x1a508a8` | leave mode |
| +0x44 | `0x19119d8` | `0x1a504e8` | after-key hook |
| +0x4c | `0x19119e0` | `0x1a50f8c` | ? |

Screens get no input calls. They redraw from the stored knob values each frame (§3).

### (b) SHIFT (confirmed, except which physical key)

- **The shift flag is `B[0x1dd7f78]`.** `MainInterface::SetShift` (`0x19d7c78`) is called with 1 by the global key-down handler for **key `0x26`** (jump table `0x18ddcac`, case `0x19ac856`) and with 0 by the key-up handler for the same key (`0x18ddeac`, case `0x19acf94`). That key 0x26 is SHIFT is inferred: it's the only held modifier that feeds the knob and key dispatch.
- **A synth controller does receive shift + encoder:** as knob index **4..7** (blue 4, green 5, white 6, orange 7, assuming encoders 0..3 are blue, green, white, orange, which matches the params order confirmed on the device).
- **What stock engines do with it.** `OnKnob` (`0x1a505bc`) switches on the synth sub-page `[0x1b4ee88]` (table `0x18fb268`): 0 = engine, 1 = envelope, 2 = FX, 3 = LFO, the T1–T4 pages. On the engine page it calls **`Ranges::ApplyKnob(k, &delta, type, knobs)` `0x1a1dfec`**. Each `(type, knob)` has an 8-byte range entry `{int16 min, int16 max, int16 scale, int16 mode}` in a table at **`0x1de5890` + type × 64 + knob × 8** (8 entries per synth type, built at boot by the `Ranges.cpp` static initialiser `0x1a1e914`, so they aren't readable statically):
  - **mode 0 or 1:** a shifted knob (4..7) becomes **a fine adjustment of knob k−4**: `delta >>= 4`, then a clamped add into `knobs[k-4]`;
  - **mode 3:** the knob index is used as is, so knobs 4..7 are **separate stored parameters**;
  - **mode 2:** special per-knob handling (envelope-like).
  - The other pages have their own handlers:
    - envelope: `0x19f69b0`, then `Patch::SetAdsr` `0x1a00c98`;
    - FX: `0x1de5c50` ranges, `0xff801444` values; it also maps `k > 3` to a fine adjustment (`0x1a5069a`);
    - LFO: `0x1de5740` ranges, `0xff8014c4` values; special cases for `k == 4`.

  **The table, obtained by running the initialiser** (`0x1a1e914`) in the GDB Blackfin simulator (`bfin-elf-gdb`, `target sim --environment operating`, SDRAM section only, from its entry to its return; the dump of `0x1de5890..0x1de5c8f` is below):

  | type | knob 0 | knob 1 | knob 2 | knob 3 | knobs 4..7 |
  |---|---|---|---|---|---|
  | 7 phase | 0..32767 m0 | 0..29491 m0 | 0..32767 m0 | 0..32767 m0 | 0..32767, mode 0 |
  | 3 string | 64..8256 m0 | 512..24064 m0 | 0..16384 m0 | 8256..16448 m0 | mode 0 |
  | 0 fm, 1 digital, 4 cluster, 5 pulse, 11 dna, 13 voltage, 14 iter | | | | | mode 0 |
  | 2 drwave | m1 | m1 | m1 | m0 | m1, m1, m1, m0 |
  | 6 sampler | all mode 2 | | | | |
  | 12 dsynth | all mode 3: **8 independent parameters** | | | | |

  **So on the phase engine, shift + encoder is a 1/16-step fine adjustment of the same parameter** (confirmed from the code path plus the simulated table), and **stock code never writes phase's `knobs[4..7]`**. Every factory phase preset stores them as 0 (§5).

### (c) Where the engine parameters live (confirmed)

- **Live values:** `int16 knobs[8]` per synth instance at **`0xff8013c4 + inst × 16`** (L1 data bank A; 8 instances × 8 knobs, zeroed at `0x19686a2`). The current instance is **`[0xff8016d4]`** (synth; the drum instance is at `0xff8016d8`, chosen by `[0x1de4c40]`), and the engine type of an instance is at `[0xff8016e4 + inst × 4]`.
- **Writer:** `SynthController::OnKnob` → `Ranges::ApplyKnob` (a clamped add into `knobs[]`), then `Patch::SetKnobs(&s_patch[inst], knobs)` (`0x1a00d14`, patch objects at `0x1ddd7e8 + inst × 0x38`). That writes all **8** values into the patch JSON key `"knobs"` and marks the patch dirty (patch slot +0x34).
- **Reader for screens:** `GetSynthParams(inst)` (`ReadInterface.cpp`, `0x1a28210`) returns a static `std::vector<float>(8)` (`0x1ab320c`): `(knobs[i] - min) / (max - min)` per range entry (`ReadInterface.cpp:100 range != 0.f`); a non-finite result becomes 0.5 (inferred). It also substitutes values from a temporary override (`0x196749c(…, 0x1ab321c)`, probably a preview or knob-lock state).
- **Reader for the engine:** the voice's `params` (render R2) is the Core's copy of these values. The Core is in encrypted L1, so the exact path isn't visible. However, the plain-SDRAM wrappers `Core::NoteOn(v)` (`0x1967854`) and `Core::NoteOff(v)` (`0x19678f0`) call voice slot +0x1c (note-on) and +0x2c (note-off) with **R1 = `&knobs[core_inst][0]`**, where `core_inst = [0x1aaac0c]`. NoteOn only does this when `core_inst == [0xff8016d4]`. So **note-on receives a pointer to all 8 knob values** (confirmed; Pd and the current Rings port ignore it). This corrects `docs/engine-interface.md`, which says note-on gets only `this`.
- **Range:** per engine, from the Ranges table (`min..max` int16). For phase it is **0..32767 for blue, white and orange, and 0..29491 for green** (simulated table, §4b).

## 5. Presets and parameters

**A synth preset is a JSON text** (confirmed: `patch.cpp` code plus the databases in `content/`). For example, a factory phase preset from `op1_factory.db` `synth_presets`:

```
{"adsr":[3136,10746,22527,8768,5117,7712,4000,4000],"fx_active":true,
 "fx_params":[9186,0,25088,32767,8000,8000,8000,8000],"fx_type":"punch",
 "knobs":[31733,29491,26598,19439,0,0,0,0],"lfo_active":true,
 "lfo_params":[11776,3068,11264,2000,0,0,0,0],"lfo_type":"value",
 "name":"bass phase","octave":-2,"synth_version":1,"type":"phase"}
```

**Where presets are stored** (sqlite):

| table | db | columns | what |
|---|---|---|---|
| `synth_slots` | `op1.db` (user disk) | `id` 0..7, `patch` | the 8 live synth sound slots (`kSoundSlotCount = 8`; slot = the "instance" above) |
| `synth_user` | `op1.db` | `id`, `patch`, `folder` | user-saved presets |
| `synth_presets` | `op1_factory.db` | `id`, `patch`, `folder` | factory presets |
| `synth_types` | `op1_factory.db` | `id`, `type`, `default_params` | default `knobs[8]` per engine; **phase = `[8000,0,0,0,0,0,0,0]`** |
| `knobs` | `op1.db` | `id TEXT`, `data TEXT` | **not presets**: global `CtlKnob` settings (tempo, MIDI channel, mic gain…), JSON `{"max":…,"value":…}` per id (`CtlKnob.cpp`, `0x19693d4` save, `0x196947c` load) |

**In the code** (confirmed):
- **Patch objects:** one per slot at `0x1ddd7e8 + slot × 0x38`; JSON value at +4, vptr at +0x34.
- **`Patch::Apply` (`0x1a010f8`)** copies the JSON into the live state:
  - `"type"` → `[0xff8016e4 + slot×4]`;
  - **all 8 `"knobs"` → `0xff8013c4 + slot×16`**;
  - `"adsr"` → `0x1b60ef0 + slot×16`;
  - then FX, LFO and the rest.
- **`Patch::SetKnobs(patch, int16 knobs[8])` (`0x1a00d14`)** writes all 8 back into `"knobs"`, then calls patch slot +0x34 (mark dirty or save; inferred). `Patch::SetAdsr` (`0x1a00c98`) is the same for `"adsr"`.
- **The JSON library is jsoncpp-like:**
  - `0x19beb70` const member get, `0x19bea94` const index, `0x19be0c0` asInt;
  - `0x19bfeb4` member ref (creates), `0x19bfaf8` index ref, `0x19bd890` int ctor, `0x19bddc0` assign, `0x19bdd18` dtor;
  - output style: `{\n   "max" : 220, …}` in the `knobs` table.

**Spare per-preset storage for model (0..5) and polyphony (0..3):**

1. **`knobs[4]` and `knobs[5]` of the phase engine: recommended.**
   - Stock code never writes them for phase: its range entries 4..7 are mode 0, so shift + encoder redirects to fine adjustment of knobs 0..3 (§4b).
   - All 16 factory phase presets, the stock phase slot in `op1.db`, and the phase default store 0 there, which maps naturally to model 0 (modal) and poly state 0.
   - They are saved and restored with every preset and slot (`Patch::SetKnobs` / `Patch::Apply`, all 8 entries).
   - The voice can read them for free: note-on gets `R1 = &knobs[slot][0]` (§4c).
   - Side effect: `GetSynthParams` normalises them by `/32767`, which is harmless.
   - Store small integers (e.g. `model × 4096`) so a hand-edited or old preset with 0 still decodes as model 0.
2. **A private JSON member** (e.g. `"rings":[m,p]`) through the jsoncpp functions above.
   - It probably survives saving and loading, since the patch keeps the whole JSON object (inferred).
   - It needs more code and more reverse engineering of the JSON API. It's not needed given option 1.
3. **A global `knobs`-table entry via `CtlKnob`:** global, not per preset. Not suitable.

## 6. Hook plan

**Status (2026-10-14):** plan (a) is implemented in `ports/rings/op1/screen.cc` and not yet run on the device (`docs/device-tests.md`, Test 4).
- The four patches go through the image header's patch table (`op1::Patch`, applied by `tools/inject_app.py` after checking the stock values).
- Colours are copied from the SVG's default-state elements, so the byte order doesn't matter.
- `pluck` uses 24 pre-drawn variants.
- **On the device it works** (2026-10-14). `<rect rx>` is ignored, so rounded corners draw square.
- **Plan (b) is dropped:** the user keeps shift + encoder as stock fine adjustment. Model and polyphony select will use encoder push-clicks, whose key path is still to be mapped (§4 key dispatch).

Everything here is plain SDRAM: either 4-byte vtable-word patches or 16-bit immediate patches, plus code in our image at `0x02c00000`. Nothing needs encrypted L1.

### (a) Rings screen in place of the phase screen

1. **Load our file.**
   - Add `content/display/rings.svg` to the package.
   - Repoint the constructor's filename from `"pd.svg"` to a `"rings.svg"` string in our image by patching the two immediates in the PdScreen getter: `0x1a0cd8c: R2.L = 0x2e40` (bytes `02e1 402e`; patch the imm16 at **`0x1a0cd8e`**) and `0x1a0cd90: R2.H = 0x018f` (bytes `42e1 8f01`; imm16 at **`0x1a0cd92`** → `0x02c0`).
   - The getter runs at boot, before any vtable hook could, so this must be a static patch.
   - Alternatives:
     - replace `content/display/pd.svg` itself with the Rings drawing: no code patch, but the ids are then ours only;
     - or have our `Init` build and load a second `SvgFile` (`0x1a5e3b0(8)`, `0x1a50350(obj, "rings.svg")`, `0x1a503d8(obj)`) and swap `document_`.
2. **Hook `Init`:** word **`0x1911190`** (`0x01a0cca0` → ours).
   - `RingsInit(this)`: look up our nodes with `Node::Find(this->document_, id, 0)` (`0x19eb8ac`). The last argument 0 returns NULL instead of asserting; the assert handler `0x1919aa8` looks fatal.
   - Keep pointers in our own globals: the PdScreen object is a 0x2c-byte singleton at `0x1de4bd8`, with no room for more fields.
3. **Hook `Update`:** word **`0x1911188`** (`0x01a0c0a0` → ours). `RingsUpdate(this, ticks)` runs once per frame while the phase screen is showing:
   - `slot = [0x1df4e38]` (the slot being displayed; inferred), `k = (int16*)(0xff8013c4 + slot*16)`;
   - map structure, brightness, damping and position from `k[0..3]` (0..32767), plus model and poly from `k[4]`, `k[5]`;
   - update the nodes (table below), then call **`Screen::Draw(this)` `0x1a3b4c8`** (required: Update must draw).
   - No soft-float is needed: the matrices are Q22.10 integers.

| rings.svg element | operation (§2) |
|---|---|
| `ring0..5` (ellipses), `core` (circle) | `p = Screen::GetLineStyle(this, "ringN")` once in Init; per frame write `p[0..2]` = lit `63 8B FF` or dim `38 35 4D`, `p[3]` unchanged |
| `brightness`, `damping` (groups) | `Node::SetTransform(node, {0x400,0,0, 0,0x400,0, 0,ty,0x400})` with `ty = (handleY - 44) × 1024` (or `- 64` for damping) |
| `posdot` | `SetTransform` with `m[6] = (x - 44) × 1024` |
| `pluck` (polyline, middle vertex moves) | no point-editing API is mapped. Simplest: put N pre-drawn variants in the SVG (e.g. `pluck0..pluck23`, one per 4 px) and show one with `B[node+0x50]`. Alternatives: edit the Shape sub-path (not mapped yet), or two line segments with general affine matrices (stroke-width scaling unverified) |
| `model0..5`, `poly0..2` | each group mixes two colours (box vs glyph). `GetLineStyle` on a group would **merge all children to one style**. Either add ids to the parts in the SVG (`model0box`/`model0num`, `poly0box`/`poly0dots`) and use `GetLineStyle`/`GetFillStyle` per part, or walk `node+0x44` children and write each child's style (`document_->linestyles[W[child+0x6e]-1]`) directly |

Stroke widths, line caps and draw order come from the SVG as usual. Writing colours each frame is cheap (no invalidation); `SetTransform` only invalidates when the matrix changes.

### (b) Shift + blue / shift + green on the phase engine only

**Hook `SynthController::OnKnob`:** word **`0x19119c0`** (synth controller vtable `0x1911994` slot +0x2c, stock `0x01a505bc`).

```
int RingsOnKnob(void *ctrl /*R0*/, int knob /*R1*/, int16_t *delta /*R2*/) {
    int slot = *(int *)0xff8016d4;
    int type = *(int *)(0xff8016e4 + slot * 4);
    int page = *(int *)0x1b4ee88;                 // 0 = engine page (T1)
    if (page == 0 && type == 7 && (knob == 4 || knob == 5)) {
        // shift+blue (4) = model, shift+green (5) = polyphony
        accumulate *delta; step with hysteresis (one detent = ±0x400, inferred);
        int16_t *k = (int16_t *)(0xff8013c4 + slot * 16);
        k[knob] = new_value;
        Patch_SetKnobs((void *)(0x1ddd7e8 + slot * 0x38), k);   // 0x1a00d14, persists
        return 1;
    }
    return orig_OnKnob(ctrl, knob, delta);        // 0x1a505bc
}
```

- **Phase only:** other engines, pages (envelope, FX, LFO) and shift + white/orange keep their stock behaviour. Only phase's fine adjustment on blue and green is lost.
- **The voice side:** `note_on(this, R1 = knobs)` can latch `knobs[4]`/`knobs[5]` per strike, and render can read `0xff8013c4 + [0x1aaac0c]*16` for live changes.

## Open questions

- **Frame rate** of the Gfx thread (set by the display driver callback `0x1a2fac4`). Measure with a cycle-count probe in `RingsUpdate`.
- **Matrix convention:** confirm `m[6]`/`m[7]` = translate and Q10 scaling (strongly suggested by Pd's `white`).
- **Colour byte order:** confirm R, G, B, A.
- **Detent size:** the `delta` per encoder detent seen by `OnKnob` (`<< 9` from the accumulator; is one detent ±0x400?).
- **Render params:** is render's `params` pointer (R2) the same `&knobs[slot][0]`? Then `params[4]`/`params[5]` would carry model and poly directly. The Core's render call is in encrypted L1; a probe can compare R2 with `0xff8013c4 + 16*slot`.
- **Shape points:** the sub-path layout (+0x74, 32-byte entries) and how to change polyline points or circle centres in place. Needed only if pre-drawn `pluck` variants are unacceptable.
- **`<rect rx>`:** does the parser honour rounded corners? No stock SVG uses them.
- **Writing knobs from the UI thread:** is it safe to write `knobs[4..5]` from `OnKnob`, which runs in the KeyHandler thread? Stock `Ranges::ApplyKnob` writes the same array from the same thread, so it should be.
- **Exact meaning** of `0x1df4e38` (the displayed slot) and `0x1aaac0c` (the Core's active slot). They're expected to match while the synth screen shows.

## 7. Encoder push-clicks

**Status (2026-10-15):** the hook (d) is implemented in `ports/rings/op1/input.cc` and applied through `op1/patches.cc`. **It works on the device** (`op1_246_rings_clicks1.op1`), which confirms `0x3b` = blue click and `0x3c` = green click.

**Summary.** The four encoder push switches reach the controller as **ordinary keys**. They go through `s_key_queue`, `KeyDispatch` and the active controller's key slots, with both press and release. The codes are **`0x3b` blue, `0x3c` green, `0x3d` white, `0x3e` orange**. That assignment is strongly supported but inferred, because the code that builds them is in encrypted L1 (evidence in (b)). In synth mode, stock firmware ignores them. They can be consumed by hooking the synth controller's key slots: `+0x14`, `+0x1c` and `+0x24`.

### (a) Where key codes come from (confirmed unless marked)

The buttons are read through **MCP23017 I2C GPIO expanders**. Every read asks for 2 bytes from register `0x12` (GPIOA/GPIOB). The callback context is `bank << 8 | reg`.

| device | role | evidence |
|---|---|---|
| `0x22` | **master**. Its GPIO low 4 bits are the interrupt lines of the four key expanders. Its **upper 12 bits are the encoder pins**, inverted (`(pins >> 4) ^ 0xfff`) and stored in `W[0x1001c48]` | Interrupt handler `0xffa03794` (registered `0x1a9c4ca(0xc, …)` at `0x19ae6e2`) reads `0x22` reg `0x12` with callback `0xffa03e1a`, which tests bits 0..3 and reads `0x23`/`0x24`/`0x25`/`0x26` (L1 `0xffa03f3c`..`0xffa04092`) |
| `0x23`, `0x24`, `0x25`, `0x26` | **key banks 3, 4, 5, 6** (contexts `0x312`, `0x412`, `0x512`, `0x612`) | `0x19ae7cc` (plain SDRAM) and L1 `0xffa03f50`..`0xffa0405e` |
| `0x20` | other "master gpio" (`gpio.cpp:400 master_gpio_init_done`; all registers read with `callback_store_all`) | `0x19ae5a8` |

- **Key scan** (L1 `0xffa03bb4`, `gpio.cpp:112 gpio_bank_num < … old_keys`). For each changed bit of `old_keys[bank]` (`0xff801860 + bank*2`), the key code is `bank*16 + bit - 0x30`. The visible `R7 += -0x30` sits in a 16-bit loop with `R7 += 1`, so the code range is `0x00..0x3f`. The scan calls the key callback `[0xff801870]`.
- **Key callback** `[0xff801870] = 0xffa04a10`, set by `0x19ae024` from `OP_Main` at `0x19f0832`. It packs `{u8 key, u8 state}` and calls `KeyHandler::PushKey` (`0x19c2f24`), then the wake function `0x19c2f04`. Its first encrypted 8 bytes hold only `LINK` and `B[SP+0xc] = R0`, so **the code is passed through unchanged** (inferred from block alignment). Compare the plain SDRAM twin `0x19efe40`, which adds `0x38`.
- **SDRAM GPIO keys** (`0x19efe40`, registered with `0x19ae0c8` at `0x19f084a`): raw index + `0x38`, so they produce **`0x38`** and **`0x39`**. Those two are the BF524 pins `B[0xff90785d]`/`B[0xff90785e]`. The global handler stores them in `B[0x18b621c]`/`B[0x18b621d]` and calls audio routing (`0x195b1fc`, `0x19deaec`). They are probably jack sensing (inferred).
- **`0x19efe6c`** pushes a fixed virtual key **`0x3f`** with state 1, from bytes at `0x18e62bc`. It is installed at `0x18c1eb8` by `0x1a6ee48` (USB/MIDI side, inferred).
- **Encoders** (callback `[0xff801874] = 0xffa04a44`, set by `0x19ae008`) are given the 12 encoder bits of device `0x22`.
  - **Loop 1** (`0xffa04a90`..`0xffa04ae4`) decodes each encoder's 2 quadrature bits with the 16-entry table at `0x18e64ac` (`0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0`). It feeds `KeyHandler::AddKnob(k, delta)`; `AddKnob` has no plain-SDRAM caller.
  - **Loop 2** (`0xffa04b06`..`0xffa04b70`) tests one bit per iteration (`R0 >>>= R7; BITTST(R0,0)`), with `R4` iterations and `R7`, `R6` each `+= 1` per iteration. For each changed bit it builds **`B[SP] = R6` (key code), `B[SP+1] = (bits & mask) != 0` (state)**, then calls PushKey and `0x19c2f04`.
  - So **the push switches become key events in `s_key_queue`, with codes `R6 .. R6+3`, sent on both edges** (confirmed structure). `R6`'s start value is in an encrypted 8-byte block (`0xffa04b00`..`0xffa04b07`), so it isn't readable directly.

### (b) The key code map (names inferred, handler behaviour confirmed)

**The MIDI controller mode states the numbering.** `0x190f6ec` (mode 11, near `opzcontroller_template`, `midi_knobs_relative`) has key-down `0x1963dbc` with a 64-entry table at `0x18d2a44` (`key < 0x40`). For every non-note key without a special case, it sends **CC = key + 5** with value 127 (`0x1963f4c`: `R7 += 5; 0x19e3004(R7, 0x7f)`). Matching the CC numbers to the codes:

| code | key (CC) | stock handling |
|---|---|---|
| `0x00` | help (5) | global: mode 9, help flag `B[0xff9077f4] = 1` |
| `0x01` | metronome (6) | mode 4 (tempo) |
| `0x02`..`0x05` | synth, drum, tape, mixer (7..10) | modes 0..3; all four held → mode `0xe` |
| `0x06`..`0x09` | T1..T4 (11..14) | `Keys.cpp` "track key" (`0x19c318c`); synth: sub-page 0..3 |
| `0x0a`, `0x0b`, `0x0c` | lift, drop, scissors (15..17) | synth: `0x1a50aa8` (copy patch), `0x1a50b40` (drop; sampler-specific) |
| `0x10`..`0x14`, `0x2d`..`0x2f` | keys "1..8" (21..25, 50..52) | `Keys.cpp` sound index `0x19c31e6`: `0x2d..0x2f` → 0..2, `0x10..0x14` → 3..7; global: select sound slot |
| `0x15` | seq (26) | mode 7 (`SeqManager`) |
| `0x21`, `0x22`, `0x23` | rec, play, stop (38..40) | tape rec ("Out of clips"); play sends MIDI `0xfa`/`0xfb`, stop sends `0xfc` |
| `0x24`, `0x25` | (41, 42) | synth: octave −/+ (`0x1a86158(∓1)`, `VoiceCtl.cpp`) |
| `0x26` | **shift** (43) | `SetShift(1)`/`SetShift(0)`. **This confirms §4b's inference** |
| `0x2b` | mic/input (48) | mode 5 (`mic_line_screen.cpp`) |
| `0x2c` | COM (49) | mode 6; also recorded in `B[0x1dd4d95]` by `KeyDispatch` |
| 24 codes | keyboard | `0x0d..0x0f`, `0x16..0x20`, `0x27..0x2a`, `0x30..0x35` (`IsNoteKey` `0x19c316c`, table `0x18e0a80`) |
| `0x36`, `0x37` | (59, 60) | no handler found |
| `0x38`, `0x39` | SDRAM GPIO (see (a)) | global `0x19ac94c`/`0x19ac964` |
| **`0x3b`..`0x3e`** | **encoder push blue, green, white, orange (64..67)** | see below |
| `0x3f` | virtual (USB/MIDI) | MIDI mode `0x1964bb0` |

**Why `0x3b`..`0x3e` are the encoder clicks** (inferred, strong):
1. Expander keys can only produce `0x00..0x3f`. Buttons and keyboard fill `0x00..0x37`, the SDRAM GPIO uses `0x38`/`0x39`, and `0x3f` is virtual. That leaves four consecutive free codes, `0x3b..0x3e`, for loop 2's four consecutive codes (`0x3a` is unused anywhere).
2. **The only stock consumer of `0x3b`/`0x3c` is the mic/line input controller** (mode 5, vtable `0x1910b4c`, key-down `0x19def54`). It uses them for the **FM-radio popup**:
   - `0x3b` commits or closes the popup (`0x19df0b4`);
   - `0x3c` opens or cancels it (`0x19df024`);
   - the popup (`fmpopup.svg`, driven by `0x19a4010`) shows nodes **`blue`** and **`green`** under a **down-arrow** (`<g id="arrow">`: a vertical line with a "V" head), that is "press the blue/green encoder". So `0x3b` = blue and `0x3c` = green, which fixes the base.
3. CC = key + 5 puts them at CC 64..67, which matches the OP-1's published encoder-push CCs (from memory, not checked in this image).

No code compares a key with `0x3d` or `0x3e` (all `0x3d`/`0x3e` compares found are SQL-tokeniser or date code). So **stock firmware doesn't use white and orange clicks anywhere**.

### (c) Stock behaviour on encoder clicks (confirmed)

- **Synth controller (mode 0)** handles keys `0x02`, `0x06..0x0b`, `0x24`, `0x25` only. Every other key returns 0:
  - key down `0x1a51058`: `key < 0x24` → table `0x18fb2c0` for keys 2..`0xb`; `0x24`/`0x25` octave;
  - shifted key down `0x1a50904`: 6, 8, 9 open the preset, FX and LFO browsers; `0x24`/`0x25`;
  - key up `0x1a5056c`: `0x24`/`0x25` only, returning 0.
  - Key down always runs two UI prologues first: `0x1a50204` (lazy init, `0x1df6eec`) and `0x1a501e8`, which hides the popup node `[0x1df6edc]`.
- **Global handlers:**
  - key down `0x19ac4b4`: `key < 0x3a` → table `0x18ddcac`, otherwise → `0x19ac508`. For a non-note key, `0x19ac508` only re-enters help mode (`SetMode(9)`) if the help flag `B[0xff9077f4]` is set;
  - shifted `0x19ac9b8`: `key < 0x30` → table `0x18dddc0`, otherwise note keys only;
  - key up `0x19acd60`: `key < 0x3a` → table `0x18ddeac`, otherwise note keys only.
- **So in synth mode an encoder click does nothing** (except in help mode). White and orange do nothing anywhere.
- **Press and release are both delivered.** `KeyDispatch` (`0x19b6c74`) does nothing while `B[0x1dd4d94] == 0`. Otherwise:
  - press with shift → `+0x1c` only; press without shift → `+0x14` only;
  - release → `+0x24`, whatever the shift state;
  - **a nonzero return skips the matching global handler**;
  - `+0x44` (synth: `0x1a504e8`, which returns the screen id for the sub-page, e.g. `0x1a3bf44(type)` on page 0) and the event record `0x19d7c94` run after every key, consumed or not.
- **The calling convention:** `R0 = this + W[vt+slot-4]` (0 for every slot of `0x1911994`, checked), `R1 = key` (u8). The return is in `R0`.

### (d) Recommended hook

Hook the synth controller vtable (`0x1911994`). All three are plain SDRAM words, patched through the image patch table like the screen hooks (§6):

| slot | word | stock | ours |
|---|---|---|---|
| +0x14 key down | **`0x19119a8`** | `0x01a51058` | `RingsKeyDown` |
| +0x1c shifted key down | **`0x19119b0`** | `0x01a50904` | `RingsShiftKeyDown` |
| +0x24 key up | **`0x19119b8`** | `0x01a5056c` | `RingsKeyUp` |

```
static inline int rings_page_active(void) {
    int slot = *(volatile int *)0xff8016d4;
    return *(volatile int *)0x1b4ee88 == 0                      // engine sub-page (T1)
        && *(volatile int *)(0xff8016e4 + slot * 4) == 7;       // phase slot = Rings
}
#define IS_CLICK(k) ((unsigned)((k) - 0x3b) < 4)                 // 0x3b blue .. 0x3e orange

int RingsKeyDown(void *ctrl /*R0*/, int key /*R1*/) {
    if (IS_CLICK(key) && rings_page_active()) {
        int slot = *(int *)0xff8016d4;
        int16_t *k = (int16_t *)(0xff8013c4 + slot * 16);
        if (key == 0x3b) k[4] = next_model(k[4]);              // blue  = resonator model
        if (key == 0x3c) k[5] = next_poly(k[5]);               // green = polyphony 1/2/4/all
        Patch_SetKnobs((void *)(0x1ddd7e8 + slot * 0x38), k);  // 0x1a00d14, as OnKnob does
        return 1;                                              // consumed: no global handler
    }
    return orig_KeyDown(ctrl, key);                            // 0x1a51058
}
int RingsShiftKeyDown(void *ctrl, int key) {                    // shift held while clicking
    if (IS_CLICK(key) && rings_page_active()) return 1;         // or step backwards
    return orig_ShiftKeyDown(ctrl, key);                        // 0x1a50904
}
int RingsKeyUp(void *ctrl, int key) {
    if (IS_CLICK(key) && rings_page_active()) return 1;
    return orig_KeyUp(ctrl, key);                               // 0x1a5056c
}
```

- **Act on press, swallow the release.** Stock key-up for these codes is a no-op, so swallowing is only for tidiness. Pass `0x3d`/`0x3e` through, or claim them later.
- **No extra condition on mode is needed.** This vtable is only active in synth mode (mode 0). Browsers and other overlays switch the active controller, so the hook doesn't fire there. `[0x1de4c40]` (synth or drum focus) only matters for the global handler.
- Consumed clicks skip the two key-down prologues (popup hide). That is harmless; call `0x1a501e8` first if a lingering popup ever shows.

### (e) Writing knobs[4]/knobs[5] (confirmed)

- **Same thread, same pattern as stock.** `KnobDispatch` and `KeyDispatch` are both called by the KeyHandler thread (`0x19c2cc0`: keys at `0x19c2d3e`, knobs after them).
  - `SynthController::OnKnob` on the engine page (`0x1a50610`): `Ranges::ApplyKnob(k, &delta, type, knobs = 0xff8013c4 + slot*16)` at `0x1a5063e`, then **`Patch::SetKnobs(0x1ddd7e8 + slot*0x38, knobs)` at `0x1a50646`**. It takes no lock and doesn't touch the Core.
  - Our key handler runs in the same thread, so writing `k[4]`/`k[5]` and calling `0x1a00d14` is exactly what stock does for `k[0..3]`.
  - `SetKnobs` copies all 8 values into the patch JSON `"knobs"` and calls the patch's +0x34 (dirty/save), so the values persist per slot and per preset (§5).
- **Phase's range entries 4..7 are mode 0.** Shift + encoder never writes `k[4..7]`, so our values aren't overwritten by stock knob handling (§4b).
- **No redraw nudge is needed.**
  - `RingsUpdate` reads `k[4]`/`k[5]` every frame (§3).
  - `GetSynthParams` recomputes from `knobs[]` on every call.
  - The after-key hook still returns the engine screen id.
  - The voice sees the new values at the next note-on (`R1 = &knobs[slot][0]`, §4c), or at once if render reads `0xff8013c4 + [0x1aaac0c]*16`.
- **Small race (inferred, low risk).** The audio Core can read `knobs[]` while we write one int16. The write is atomic, and stock `ApplyKnob` has the same race.

### (f) Open questions and a probe

- **Prove the codes on the device.** `0x3b..0x3e` rest on the free-code argument plus the FM popup. For a one-flash confirmation, make `RingsKeyDown`, `RingsShiftKeyDown` and `RingsKeyUp` record the last `(key, state)` in a global before any filtering. `RingsUpdate` then shows `key - 0x38` in binary on `ring0..5` and the press state on `core`. Expected: blue lights `0b000011`, green `0b000100`, white `0b000101`, orange `0b000110`.
- If the codes turn out different, only `IS_CLICK` changes. The hook words and the method stay the same.
- **Unexplained:** `0x36`/`0x37` (no handler, maybe unconnected pins), and the exact meaning of `0x0a..0x0c` and `0x24`/`0x25`. The names come from the CC = key + 5 rule plus handler behaviour.
