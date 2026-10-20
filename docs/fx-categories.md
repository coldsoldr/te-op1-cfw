# Effect categories

The user's decision (2026-10-19): some stock effect types become **categories**. A category keeps its stock effect as engine 0 and hosts new engines of its kind. The **orange click** on the FX page steps through them (shift: back), the way MULTI does for phase and Rings.

| Shown as | Stock type (index) | Engines in click order (id) | In the image |
|---|---|---|---|
| reverb | spring (4) | spring (0), plate (1), hall (2), clouds (3) | spring, plate, clouds (fx3); + hall (fx6) |
| modulation | cwo (7) | cwo (0), ensemble (1), shift (2) | cwo hosted (fx4); + ensemble (fx5), shift (fx8) |
| dirt | punch (2) | punch (0), drive (1), tape (2), fold (3), crush (4) | punch, crush (fx3); + drive (fx4), fold (fx7), tape (fx9: tapesat.svg) |
| delay | delay (0) | delay (0), tape echo (1) | delay hosted (fx4); + tape echo (fx10) |
| phone | phone (3) | stock only | (Clouds left it in fx1) |

- **Code:** `ports/fx/op1/fx_host.{h,cc}` (the host), `fx_screen.h` (the screen calls), `half_rate.h` (the half-rate path the reverbs, ensemble and tape echo share), `two_page.h`, `screen_anim.h`. Engine glue: `ports/fx/op1/<name>_fx.cc`; cores: `ports/fx/src/<name>/` (each with `docs/<name>-port.md`).
- **Build:** `ports/fx/engines.mk`. `make op1-seq FX_ENGINES="plate drive …"` (ports/rings) and `make sim-fx FX_ENGINES=…` (ports/clouds) take the engines to include; each engine is `op1/<name>_fx.cc` plus `src/<name>/*.cc *.S` and `-DOP1FX_<NAME>`. `sim-fx` runs every included engine through `tests/sim_engines.h`. Clouds is the first engine: `ports/clouds/op1/clouds_fx.cc` (`op1clouds::kCloudsEngine`).
- **Screen briefs** for the new engines: `docs/fx-briefs/`.
- **Research:** `docs/effects-research.html`.
- **Device tests:** Test 37 (`op1_246_rings_fx1.op1`).

## Names

The FX list is built by `0x19707d8` from `SELECT id, type FROM fx_types`. The patched query (`ports/rings/op1/patches.cc` `kTypeListQuery`) shows the category names with `CASE type WHEN 'spring' THEN 'reverb' …`.

The name is display only:
- the FX browser finds its current entry by id (`0x1a01954`: `SELECT id FROM fx_types WHERE type='%s'` on the stored name, then `0x1a172d4` matches by `[entry+0x1c]`);
- presets and saves keep the stock names.

Only the sequencer chooser looks up by the shown name (`docs/sequencer-interface.md` §12).

Two more facts about the list:
- The query is formatted into an 8 KB static buffer (`0x196e2f0` → vsnprintf at `0x1bab8e0`, size `0x2000`).
- The list font draws lowercase as capitals and has only 15 capital glyphs, so names are lowercase.

## The engine marker

The marker is kept in the position's stored values (fx_params, saved with the preset). **Every word stays within 0..32767:** the synth patch check (`0x1a037a8`, `patch.cpp:390`) tests each effect value against the FX Ranges with `0x1a1df4e` and stops the boot with "Invalid synth patch … error 24" on a failure. fx1 used bit 15 (negative) and a saved slot stopped the boot (Test 37). fx2 also patches that test away (`ports/rings/op1/patches.cc` `kNeverInvalid`, Test 38).

| Word | Bits | Owner |
|---|---|---|
| `values[4]` | 12-14 = 6 (0x6000..0x6fff) | a hosted engine (bit 15 never set) |
| `values[4]` | 8-11 | the engine id (1..15, stable per host) |
| `values[4]` | 0-7 | the engine's own |
| `values[5..7]` | 0-14 | the engine's own |

Why this pattern: factory presets store 0, 4000 or 8000 in `values[4..7]` (cwo, delay and nitro 0; spring, punch, grid and phone 8000; the master `[4000 ×8]`); their bits 12-14 are 0 or 1. Stock code never writes `values[4..7]`.

fx1's values (any negative `values[4..7]`) are converted where the host meets them: the engine fx1 marked keeps its settings through its `migrate`; anything else becomes stock.

How the orange click changes the values:
- **To stock:** it writes the type's factory `values[4..7]` back, because stock may read them.
- **To an engine:** it clears them, calls the engine's `enter`, then sets the marker.

Where each entry reads its values from:
- **Mono entry:** the engine and its settings from the live slot's stored values (`0xff801444 + slot × 16`, drums `0xff8015c4 + kit × 16`); the encoders from the passed copy (`0xff801724`), which the effect LFO modulates (clamped to the FX Ranges). Spring's FX Ranges give `[4..7]` an LFO scale of 32767, so the copy's `[4..7]` can't be trusted.
- **Stereo (master) entry:** the passed values, which are `0xff8016c4` itself.

**Clouds' layout under the marker** (`clouds_settings.h`):

| Word | Bits | Holds |
|---|---|---|
| `values[4]` | 0-1, 2, 3-7 | the mode, the page, q3 bits 6-10 |
| `values[5]` | 0-10, 11-14 | q0, q3 bits 2-5 |
| `values[6]` | 0-13, 14 | q1, q3 bit 1 |
| `values[7]` | 0-13, 14 | q2, q3 bit 0 |

q0..q3 are the hidden page's values as offsets from the defaults, cut to 11, 14, 14 and 11 bits. `sim-fx`'s checksums are unchanged.

## The engine interface

`op1fx::Engine` (`fx_host.h`) has these fields:
- `id` and `svg`;
- `process(position, slot, l, r, values, ranges)`;
- `stop(position)`: another engine or stock took the position over, and the engine must restart;
- `enter(values, ranges)`;
- `click(...)`: blue, green and white;
- `screen_init(screen)`, which runs at boot with the screen's document set to the engine's SVG;
- `screen_update(screen, position, values, ranges)`, after which the host draws.

What the host does:
- It normalises nothing itself: it passes the host type's four encoder ranges, read from the FX Ranges table at run time, with the host table's copy as a fallback.
- Positions are `POSITION_MONO` (the live synth slot or drum kit; one mono effect object exists) and `POSITION_MASTER`.
- When the engine at a position changes, it stops the previous engine first. Later engines may share one memory arena per position: only one effect plays at a position.

Release handling: a click's release is swallowed only if the host consumed its press (`ports/rings/op1/input.cc`, `fx_pressed`).

## Firmware words per host

From the explore of 2026-10-19, checked against `OP1_vdk_dxe0_01000000.bin`. Each pair is slot word → stock value.

| Host | Mono process | Stereo process | Stereo vtable | Screen Update | Screen Init | SVG |
|---|---|---|---|---|---|---|
| spring (4) | `0x19113d0` → `0x1a326e8` | `0x19113f0` → `0x1a32ffc` | `0x19113dc` | `0x1911418` → `0x1a33f90` | `0x1911420` → `0x1a33abc` | rymd.svg |
| cwo (7) | `0x190f588` → `0x194480c` | `0x190f5a8` → `0x19450fc` | `0x190f594` | `0x190f558` → `0x1942788` | `0x190f560` → `0x1943178` | bode.svg |
| punch (2) | `0x1910c30` → `0x19e6260` | `0x1910c50` → `0x19e662c` | `0x1910c3c` | `0x1910c78` → `0x19e6a18` | `0x1910c80` → `0x19e6790` | mllp.svg |
| delay (0) | `0x190fb80` → `0x197d8c4` | `0x190fba0` → `0x197dc42` | `0x190fb8c` | `0x190fc80` → `0x1984f3c` | `0x190fc88` → `0x1984a88` | delay.svg |

**Encoder ranges** (FX Ranges table `0x1de5c50 + type × 48 + knob × 6`, built at boot by `0x1a1e914`; simulator dump `%TEMP%\op1fx\fxranges.bin`):

| Host | blue | green | white | orange | `[4..7]` LFO scale |
|---|---|---|---|---|---|
| spring | 1344..16448 | 7744..16448 | 0..16384 | 0..32767 | **32767** |
| cwo | 0..32767 | 0..32767 | 0..32767 | 0..32767 | 0 |
| punch | 1344..12480 | 0..32767 | 1536..25088 | 0..32767 | 0 |
| delay | 1024..11264 | 3276..32767 | 0..16384 | 0..32767 | 0 |

**To add a host's first engine:**
1. Add the engine to that host's `engines` list in `fx_host.cc`.
2. Define its `Fx<Host>Mono/Stereo/ScreenInit/ScreenUpdate` entries (as the reverb ones).
3. Add its four words to `patches.cc`.

## Open questions

- Does the master have an effect LFO that writes `0xff8016c4` in place? If so, spring's scale would move the master's marker. Test 37 step 6 checks the synth side.
- How master settings persist: no master save call is known. Clouds' master clicks write the live values.
- The display width of "modulation": Test 37 step 1.
