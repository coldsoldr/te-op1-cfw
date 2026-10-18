# Device tests

Packages are built here and flashed by the owner. Every test package:
- leaves out te-boot.ldr;
- passes `tools/verify_bootloader.py`;
- decodes to stock firmware 246 with only `OP1_vdk.ldr` changed.

Recovery always works the same way: hold COM at power-on, choose "1. Upload firmware", and copy stock `op1_246.op1`. That path runs before any app code (see `docs/brick-safety.md`).

## How the packages are built

```
make -C ports/rings op1-probe op1-engine      (in WSL)
python tools/inject_app.py "op1 firmware/op1_246/OP1_vdk.ldr" OUT.ldr ports/rings/build/op1_probe.elf --fill-pattern 0xa5a5a5a5
cp -r "op1 firmware/op1_246" "op1 firmware/work_probe"; cp OUT.ldr "op1 firmware/work_probe/OP1_vdk.ldr"
python tools/build_package.py "op1 firmware/work_probe" "op1 firmware/op1_246_rings_probe.op1"
```

`inject_app.py` changes, and only changes:
- the FIRST block's length field and header checksum;
- the phase engine's vtable entries (render `0x1911150`, and for the engine build note-on `0x1911158`);
- the new blocks, inserted before FINAL. The stock FINAL and the trailing IGNORE block are appended byte-identical.

## Test 1: probe v1 (`op1_246_rings_probe.op1`), run 2026-10-14

**Result:**
- **Injected code runs from `0x02c00000`.** The phase engine worked and the probe produced its report.
- **The CPU ramp didn't crash the OP-1.** It overloaded the audio, backed off and recovered.
- **No siren**, so nothing overwrote the free region while it was checked.

**The report itself was unreadable** (`incoming/probe/probe_report.ogg`, decoded with `tools/decode_probe_tones.py`). There were three tones of about 378, 643 and 905 Hz, at twice the expected speed.
- **Cause:** a probe bug. All render calls shared one tone oscillator, so with several calls per block each voice's audio jumped in phase and the pitches came out scrambled. A model of this reproduces the scrambling (`%TEMP%` script, not kept).
- **What it does tell us:** there are several render calls per audio block, and the old gap-based block detection fired twice per block. That points to idle voices being rendered, or extra render calls.

## Test 1b: probe v2 (`op1_246_rings_probe2.op1`)

The report no longer uses pitch. It uses beep counts timed by the CPU's cycle counter (`ports/rings/op1/probe.cc`). Free CPU is measured from one reference voice's call interval. The report also includes the raw call pattern.

It was checked end to end on the host (`tests/host_probe.cc`, a simulated OP-1 with a known clock, free CPU and call pattern), plus `tools/decode_probe_beeps.py`. For a simulated 400 MHz with 55% free, it recovers 399.8 MHz and 55%.

**Procedure:**
1. Flash `op1_246_rings_probe2.op1` through te-boot.
2. Select the phase engine and hold one key for about 45 s.
   - First the normal sound plays.
   - Then the CPU ramps; stutters are expected.
   - Then it beeps: a long sync tone, then groups of short beeps.
3. Keep holding the key and record at least 40 s of beeping (a full report is about 17 s at 400 MHz). Save the file in `incoming/`.
4. A continuous tone with no beeps means something wrote into the free region.

**Decoding:** `python tools/decode_probe_beeps.py RECORDING` prints the clock, the free CPU, the render calls per block, the number of distinct voice objects called, and the calls per second.

**Result (2026-10-14, `incoming/probe/probe2_report.ogg`):**

Decoded numbers, the same in every cycle: `[524, 0, 1, 1, 1, 463]`.

| Measurement | Value | Meaning |
|---|---|---|
| Clock | 399.6 MHz | from the beep timing |
| Reference voice call interval (shortest) | 524 × 1024 = 536,576 cycles = 1.34 ms | |
| Render calls in 2^28 cycles (0.67 s) | 463 | ~689 per second |
| Calls per interval | 1 | |
| Distinct voice objects | 1 | only the sounding voice is rendered; no other render calls |
| Beep pitch | 2760 Hz | the 32-sample square, so the **voice output rate is 88.2 kHz**; 128-sample blocks give 689 Hz |

Probe v1's doubled pitches and halved durations have the same cause.

- **The keyboard octave doesn't affect these numbers.** The recording was made with the keyboard an octave up, but the probe ignores the key's pitch.
  - The beep is a fixed 32-sample square written straight into the output, and the call rate comes from the cycle counter.
  - Re-measured from the recording, the beeps sit at ~2760 Hz.
  - 64-sample blocks at 44.1 kHz would also give 689 calls per second, but would put the beep at 1378 Hz.
- **Free CPU: invalid (0).** The overload test compared intervals against the *shortest* one (1.34 ms), but they jitter around a 1.45 ms mean, so it tripped immediately.

## Test 1c: probe v3 (`op1_246_rings_probe3.op1`)

Probe v3 fixes v2's free-CPU measurement and reads the held key's pitch (`ports/rings/op1/probe.cc`):
- **Calibration (~2 s):** records three things:
  - the reference voice's *mean* call interval over ~1400 calls (the block period);
  - its longest interval;
  - the held key's phase increment.
- **Overload:** judged on the mean interval over each 64-call ramp step.
  - The intervals telescope, so jitter barely moves the mean.
  - A step whose mean is over 2% longer than the period counts as overload.
  - The report gives the last burn that ran clean, less that 2% margin.
- **Report:** five numbers.
  1. `3`, the version;
  2. block period / 1024;
  3. free cycles per block / 1024;
  4. the held key's pitch in 0.1 Hz, assuming 88.2 kHz;
  5. the longest call interval / 1024.

**Checked on the host** (`make host-probe`). The test simulates an OP-1 with 88.2 kHz blocks, calls jittered by up to 8% of a block as on the device, and a held A4. The decoder recovers:
- 55% at 400 MHz;
- 30% with 3 calls per block and stray calls;
- 70% at 380 MHz.

It reads the key as 440.0 Hz in every case.

**Caveats:**
- **The figure is what the audio path leaves.** The UI may run in that time too, so watch the screen during the ramp. If the screen gets sluggish before the audio stutters, the real budget for an engine is lower.
- **Overload could show late.** If the core buffers audio ahead, overload could show a step or two late, which would make free CPU read up to ~1% high.

**Procedure:**
1. Flash `op1_246_rings_probe3.op1` through te-boot.
2. Set the keyboard octave back to 0. Select the phase engine and hold one key you can name (e.g. "the lowest F") for about 45 s.
   - First the normal sound plays (~2 s).
   - Then the CPU ramps (up to ~20 s); stutters are expected near the end.
   - Then it beeps: a long sync tone, then groups of short beeps.
3. Keep holding the key and record at least 40 s of beeping (a full report is about 12 s at 400 MHz). Save the file in `incoming/probe/`.
4. A continuous tone with no beeps means something wrote into the free region.

**Decoding:** `python tools/decode_probe_beeps.py RECORDING` prints:
- the clock;
- the block period and rate;
- the free CPU, in % and MHz;
- the held key as a note name;
- the longest call interval.

It still decodes v2 recordings.

**What the pitch tells us.**
- The stock phase engine does `phase += inc` per sample, with one cycle per 2^32, and its output is consumed at 88.2 kHz.
- So Rings with `f = inc / 2^32 × 88200` plays at the same pitch as the stock engine on any key.
- The reported note checks the absolute tuning against the key held.

**Result (2026-10-14, `incoming/probe/probe3_report.ogg`):** the lowest F held, octave 0. Three complete reports, all identical: `[3, 566, 459, 1746, 603]`.

| Measurement | Value | Meaning |
|---|---|---|
| Clock | 399.8 MHz | from the beep timing |
| Block period (mean) | 566 × 1024 = 579,584 cycles = 1.450 ms | 128 samples → **88.3 kHz** (exact 88.2 kHz is 580,499 cycles; the difference is the ÷1024 rounding) |
| Free cycles per block | 459 × 1024 = 470,016 | **81% of a block, ~324 MHz** |
| Held key | 174.6 Hz | **F3, 0 cents**, assuming `inc = f / 88200 × 2^32`; at 44.1 kHz it would be F2 (87.3 Hz) |
| Longest call interval | 603 × 1024 cycles = 1.54 ms | 107% of a block |

- **88.2 kHz confirmed again** with the octave at 0. The pitch formula gives a sensible note (F3) for the lowest key.
- **Free CPU is large.** The figure has some limits:
  - it is with one phase voice playing and no FX or other tracks;
  - the UI may share that time;
  - the OP-1's per-voice work (envelopes) grows with more voices.
- **Sizing.** The engine moves from 10 to 16 modes per voice (see Test 2).

## Test 2: Rings engine (now `op1_246_rings_engine16b.op1`; 10-mode fallback `op1_246_rings_engine10b.op1`)

The phase engine's render and note-on become Rings' modal model (`ports/rings/op1/engine.cc`), with 6 voices of 16 modes each (`OP1RINGS_MODES_PER_VOICE`).
- **Rate:** Rings runs at 44.1 kHz on 64 samples per call. A 4-point Lagrange 2× upsampler then produces the 128 output samples at 88.2 kHz.
- **Pitch:** Rings' frequency is the OP-1 increment doubled (`f / 44100`), latched at the start of the block.

**In the simulator** (`make sim-engine`, at the real call rate):

| Modes per voice | 1 voice ringing | All 6 ringing | Share of the 324 MHz free (6 voices) |
|---|---|---|---|
| 10 | 37 MHz | 121 MHz | 37% |
| **16** | **47 MHz** | **164 MHz** | **50%** |
| 20 | 53 MHz | 191 MHz | 59% |
| 24 | 59 MHz | 218 MHz | 67% |

- **Knob movement:** earlier benchmarks put moving knobs at roughly +30%, so 16 modes would peak at about 210 MHz (65%). That leaves room for other tracks, FX and the UI.
- **Pitch:** a 220 Hz key gives a 220.0 Hz fundamental, with no energy at 110 Hz.
- **Upsampling images:** at −83 dB.

**Result of the first flash (2026-10-14, `op1_246_rings_engine16.op1`):**
- **It sounds right** ("sounds amazing").
- **No stutters or UI sluggishness.**
- **Slight pops when turning the encoders while a note rings.**

**Cause of the pops: a wrong constant in the mono limiter** (`src/op1_voice.cc`).
- Below the limiter threshold, the gain was `962072674` = 0.896 in Q30, i.e. 1.4 × 0.8 × 0.8, with Rings' 0.8 output factor counted twice.
- Above the threshold, the gain is 1.12 / peak.
- So whenever the peak follower crossed 1.0, the gain jumped by 25% (1.9 dB) at a sub-block boundary.
- Turning an encoder raises the level across that threshold, which made the pop. A decaying note crossing back down made another one (at 0.28 s in the test note).
- Upstream's gain is continuous there: `peak ≤ 1 ? 1 : 1 / peak`, times 1.4 × 0.8.
- **Fixed:** the constant is now `1202590843` = 1.12, which also makes notes below the threshold 1.9 dB louder, matching upstream.

**Evidence** (`make step-params`, `tests/step_params.cc`):
- **Sustained note, fixed parameters:** the worst burst above 9 kHz was −44.7 dB re the note. After the fix it is −75.2 dB. What remains is the limiter's per-sub-block gain update, which is small. The test fails above −60 dB.
- **Blackfin simulator** (assembly mode bank and upsampler, 88.2 kHz output): −47.8 dB before the fix, −70.4 dB after.
- **Raw parameter steps** cause a few clicks of their own, at the same level as upstream's float resonator given the same steps:
  - a structure step of +0.2 gives −56 dB re note (upstream: −61.5 dB);
  - position steps give −63 to −85 dB;
  - brightness and damping steps cause none.
  - Upstream never sees raw steps: its pots are low-passed (`cv_scaler.cc`, coefficient 0.01 per 2 kHz block, ~50 ms). A similar smoother would remove the structure click (−83 to −91 dB in the test). It isn't added yet; it's the next step if any zipper or click remains.

**Packages** (all verified against stock):
- **`op1_246_rings_engine16b.op1`: 16 modes, limiter fixed. Flash this one.** Its image differs from the flashed `engine16` only in the two limiter constants (8 bytes).
- **`op1_246_rings_engine10b.op1`:** 10 modes, limiter fixed, as a fallback.
- The unfixed `op1_246_rings_engine16.op1` and `op1_246_rings_engine.op1` were deleted to avoid mix-ups.

**Second flash (`engine16b`):**
- The user reports it "sounding amazing".
- Encoders confirmed: blue = structure, green = brightness, white = damping, orange = position.
- The ranges and release behaviour feel right.

**Knob smoothing (`op1_246_rings_engine16c.op1`, not yet flashed).** This is `op1/param_smoother.h`:
- **The filter:** a one-pole per voice and parameter, ~50 ms like upstream's `cv_scaler`, updated once per call.
- **Snapping:** each value snaps to the target within 2^-12, so recomputing stops soon after a knob stops. Note-on snaps the voice to the current values.
- **Effect** (`make step-params`, smoothed vs raw):
  - a sudden +0.5 structure step goes from −31 dB to −70 dB re note;
  - a +0.5 position step goes from −42 to −62 dB;
  - all smoothed steps stay below −60 dB, and the test checks this.
- **Sound with still knobs** is bit-identical to `engine16b` (same simulator checksum).
- **Cost:** with all six ringing and three knobs turning continuously, the simulator measures ~226 MHz, about 70% of the measured free CPU. With knobs still it's 164 MHz. If that ever stutters, the coefficients could be recomputed every other call while smoothing.

**To check on the next flash:** encoder turns, fast and slow, on six ringing notes. Listen for clicks and stutters.

## Test 3: FM voice (`op1_246_rings_fm.op1`, not yet flashed)

This is the engine of `engine16c` (knob smoothing included) with Rings' FM voice in place of the modal model (`-DOP1RINGS_MODEL=3`, `make op1-engine-fm`). It's a temporary build until model select (shift + blue) exists.

**The FM voice** (`src/fm_voice_fixed.*`) is a fixed-point port of upstream's `FMVoice`. Each strike triggers its internal envelope.

| Encoder | FM parameter |
|---|---|
| Blue (structure) | frequency ratio, quantized to Rings' table (0.5 to 8, including detuned and irrational ratios) |
| Green (brightness) | FM amount |
| White (damping) | decay; above 0.9 it fades into a sustained oscillator |
| Orange (position) | feedback: centre = none, right = operator feedback, left = phase feedback |

**Against upstream** (`make test-fm`, 48 kHz, 11 scenarios: ratios, pitches up to 1760 Hz, decays, oscillator mode, both feedbacks):
- **Match:** 42–78 dB SNR, with level within 0.01 dB and the same spectral centroid.
- **Ratio tuning:** the modulator ratio is floored to 1/256 semitone, as upstream's `SemitonesToRatio` does. Tuned exactly, it drifted from upstream (5 dB SNR at 1760 Hz).

**In the simulator** (`make sim-engine-fm`):
- **Cost:** ~106 MHz with six voices ringing, 110 MHz with knobs turning (modal: 164 / 226).
- **Discontinuities:** none; the worst high-frequency burst is −74.5 dB.

**To check on the device:**
- the sound across the four encoders;
- the level against the modal build: upstream's FM limiter pre-gain is 0.7, half the modal model's;
- clicks and stutters.

## Test 4: Rings screen (`op1_246_rings_screen1.op1`)

**Result (2026-10-14): it works.**
- The OP-1 boots, the Rings screen shows, and the colours and movement follow the encoders.
- The sound is unchanged.
- **Rounded corners (`rx`) are ignored:** the model and polyphony boxes draw square. Use paths if rounded corners are wanted.
- **Shift + encoder** still gives the stock 1/16 fine adjustment of each parameter. The user wants to keep that, so model and polyphony select will use the encoder push-clicks instead (see `docs/HANDOVER.md` §7).
- The rest of this section is the build description from before the flash.


This is the modal engine of `engine16c` (smoothing included) plus the first Rings screen, in place of the phase engine's screen (`ports/rings/op1/screen.cc`; hook plan in `docs/screen-interface.md` §6a).

**What changes in the package:**
- **`OP1_vdk.ldr`:**
  - the engine image and voice hooks, as before;
  - four more patches from the image's new patch table, applied by `tools/inject_app.py` after checking the stock values:
    - the `PdScreen` getter's filename immediates at `0x1a0cd8e` and `0x1a0cd92`, from `"pd.svg"` to our `"rings.svg"`;
    - the `PdScreen` vtable's Init (`0x1911190`) and Update (`0x1911188`).
- **One new file, `content/display/rings.svg`,** generated from `incoming/rings.svg` by `op1/gen_rings_svg.py`:
  - the metadata is dropped;
  - `pluck` becomes 24 pre-drawn variants;
  - each button gets a `…box` id and a `…num`/`…dots` glyph group.

**What the screen does:**
- **Init** finds the elements and copies the lit and dim colours from the SVG's defaults, so the style byte order doesn't matter. If any id is missing, it leaves the screen static rather than assert.
- **Update** runs every frame. It reads the displayed slot's knob values (`0xff8013c4 + slot × 16`) and:
  - lights 1 + round(5 × structure) rings;
  - moves the brightness and damping handles;
  - moves the position dot and shows the matching pluck line;
  - shows the build's model (modal) and polyphony "all" (all three lit);
  - then draws.
- **Geometry:** `make test-screen` checks the maths against the spec's formulas for every knob value (worst error 0.001 px).

**Expected quirk:** green (brightness) tops out at 0.9, because the phase engine's stock range for that encoder is 0..29491. The engine sees the same 0.9, so the handle stops just below the top tick.

**To check on the device:**
1. **The screen appears.** If the OP-1 fails to boot or the synth screen is blank or frozen, recover with stock `op1_246.op1` through te-boot as usual, and describe what you saw.
2. **Colours:** rings lit in blue / dim, buttons selected / dim, as in the design.
3. **Movement:** the handles, dot and pluck follow the four encoders, and the handles move the right way (up = more).
4. **Rounded corners** on the button boxes (`rx`): do they show rounded or square?
5. **Sound unchanged,** and the screen stays smooth while playing.

## Test 5: model and polyphony on encoder clicks (`op1_246_rings_clicks1.op1`)

**Result (2026-10-15): everything works as expected.**
1. **Clicks:** a click steps forward and shift + click steps back, for model and polyphony. **This confirms the click key codes** (`0x3b` blue, `0x3c` green).
2. **Polyphony 1:** legato carries the ring into the new note with no clicks; a chord sounds only its last note.
3. **Polyphony 2 / 4** limit the voices.
4. **Saving** works, both in the slot and in a snapshot.
5. **No glitches** with fast playing.
6. **Shift + turn** still fine-adjusts.

The rest of this section is the build description from before the flash.


This is `screen1` plus model and polyphony select. The settings are stored in the phase slot's spare preset values `knobs[4]` (model) and `knobs[5]` (polyphony), as `setting × 4096` (`ports/rings/op1/settings.h`). 0, which every existing preset has, means modal and "all", so old presets sound as before.

**Controls** (`op1/input.cc`, synth engine page of a Rings/phase slot only):
- **Click blue:** next model. For now that's modal ↔ FM, the two implemented.
- **Click green:** next polyphony, all → 1 → 2 → 4 → all.
- **Shift + click:** step back.
- **Shift + turn** keeps its stock fine adjustment. White and orange clicks, other pages and other engines are untouched.
- The hooks are the synth controller's key-down, shifted key-down and key-up (`0x19119a8`, `0x19119b0`, `0x19119b8`), through the patch table (`op1/patches.cc`). A click writes the setting and calls `Patch::SetKnobs`, as the stock knob handler does, so it's saved with the preset.
- **The click key codes `0x3b`–`0x3e` are inferred** (`docs/screen-interface.md` §7). If clicks do nothing, the codes are wrong: every other key still goes to stock, so nothing else breaks. The probe in §7(f) would then pin them down.

**Engine** (`op1/engine.cc`, `op1/voice_allocator.h`):
- **Settings are latched at note-on** (note-on's R1 = the slot's knobs). A change applies from the next note and never clicks a ringing voice.
- **Polyphony 1 / 2 / 4:** 60 / 28 / 12 modes per voice, as Rings.
  - When all voices are busy, the new OP-1 voice takes over the oldest voice's resonator state and strikes it at its own pitch, so the ring carries on as on Rings.
  - The stolen OP-1 voice fades out over 5.8 ms; at most two fade at once.
  - A voice stolen before it sounded (a chord) goes silent at once.
- **"All":** 6 voices × 16 modes, bit-identical to before (simulator checksum).
- **Screen:** lights the selected model and polyphony ("all" = all three).

**Checks:**
- `make test-alloc`: allocation, encoding and click stepping.
- **`make sim-engine`, worst cases:**

| Case | Peak |
|---|---|
| Polyphony 1, a note every 17 ms | ~273 MHz |
| Six-note chord in polyphony 1 | ~80 MHz |
| Polyphony 2 / polyphony 4, fast notes | ~205 / ~185 MHz |

  The measured free CPU is ~324 MHz. FM selected through the knobs matches the earlier FM build bit for bit.

**To check on the device:**
1. **Clicks:** blue toggles the model buttons 1 ↔ 4 (modal ↔ FM) and the sound changes from the next note. Green steps the polyphony buttons: all three, then 1, 2, 4. Shift + click steps back.
2. **Polyphony 1:** playing legato, the ring carries into the new note, with no clicks. Chords play only the last note.
3. **Polyphony 2 / 4** limit the voices. Polyphony 1 should sound richer (60 modes).
4. **Persistence:** change model and polyphony, switch to another sound slot and back, then save and reload a preset. The settings should stick.
5. **No glitches** with fast playing in polyphony 1.
6. **Shift + turn** still fine-adjusts.

## Test 6: string models (`op1_246_rings_strings1.op1`)

**Result (2026-10-15):**
1. **Sympathetic strings (screen button 2) crackle when busy** and glitch while an encoder turns, in every polyphony. Read as CPU overload: the simulator put them below the modal model, which plays cleanly, so strings cost more on the device than simulated (memory traffic the simulator doesn't model), and the build had cost spikes: a 64 KB state copy per stolen note, a plucker that never slept, coefficient updates on every glide.
2. **The tuning around the played note sounds good;** keep it.
3. **The mono mix (`out − aux`)** stays.

Fixes in Test 7. The rest of this section is the build description from before the flash.


This is `clicks1` plus Rings' string models in fixed point (`ports/rings/src/string_fixed.*`, `string_voice.*`). Blue clicks now step through five models, in Rings' order: **modal → sympathetic strings → string → FM → quantized sympathetic strings** (screen buttons 1, 2, 3, 4, 5). String + reverb (button 6) comes with the reverb.

**What the models do** (as upstream Rings with the internal exciter):
- **String (3):** one Karplus-Strong string, plucked by a noise burst. Structure sets the dispersion: below 0.24 a "curved bridge" (buzzy), 0.24–0.26 a plain string, above that inharmonic stiffness, and towards the top a noisy, detuned string. Brightness, damping and position as on Rings.
- **Sympathetic strings (2):** the plucked string plus strings that only ring in sympathy, tuned by structure (unisons, fifths, octaves, detuned copies). Polyphony 1 / 2 / 4 gives 8 / 4 / 2 strings per voice; "all" gives 2 per voice on 6 voices.
- **Quantized (5):** the same, tuned to one of Rings' 11 chords, picked by structure.

**Choices to judge on the device:**
- **Tonic.** Rings tunes the sympathetic strings around its FREQUENCY knob. The OP-1 only gives the played pitch, so they're tuned around the played note, as if Rings were played from that knob.
- **Mono mix.** Each string model's voice is upstream's `out − aux`, the signal Rings itself sends per voice when polyphonic: the string minus its pickup tap, so position works as a pickup comb. The modal model still sums `out + aux`, as flashed.

**Checks (all off the device):**
- `make test-string`: 19 scenarios against upstream's float `String`/`Plucker` (same noise sequence): 46–76 dB SNR over the first 0.25 s, levels within 0.1 dB, band spectra within 0.05 dB.
- `make sim-engine`: modal checksum still `a3ae5f5c` (bit-identical). `test-fm`, `test-alloc`, `step-params` pass.
- **CPU (`make sim-model M=n`, simulator, free ≈ 324 MHz):**

| Case | String (2) | Sympathetic (1) / quantized (4) |
|---|---|---|
| "All", 6 voices ringing | ~152 MHz | ~155 MHz |
| "All", 3 knobs turning | ~177 MHz | ~179–183 MHz |
| Polyphony 1, a note every 17 ms (peak) | ~150 MHz | ~268 MHz |
| Polyphony 2 / 4, fast notes (peak) | ~113 / ~173 MHz | ~213 / ~209 MHz |

  A note-on in polyphony 1 copies the stolen voice's 8 strings (~45k cycles, inside the block times above). The simulator doesn't model cache misses; the 24 strings of the polyphony-1 worst case don't fit the 16 KB data cache, so expect some extra on the device.
- **Package:** only `OP1_vdk.ldr` changes, plus `content/display/rings.svg` (identical to clicks1's); `te-boot.ldr` absent; `verify_bootloader.py` passes. The LDR byte-diff shows only the FIRST header, the 7 vtable/patch words, the 2 filename immediates and the appended blocks; the stock tail is intact.

**To check on the device:**
1. **Clicks:** blue steps 1 → 2 → 3 → 4 → 5 → 1 on the screen; shift + click steps back. Each model sounds from the next note.
2. **String (3):** plucked-string sound at the right pitch. Turn structure (blue) through its range: buzzy in the first quarter, clean at about a quarter, then stiffer and, in the last quarter, noisy. Damping at full should sustain almost forever.
3. **Sympathetic (2) and quantized (5):** a plucked note with a halo of sympathetic strings; structure changes their tuning (in 5, steps through chords). Try polyphony 1 (8 strings, richest) and "all".
4. **Polyphony 1, fast legato** in models 2 and 5 (the heaviest case): no stutter or audio dropout.
5. **Modal and FM** sound exactly as before.
6. **Old presets** still load as modal / "all".

## Test 7: string + reverb, lighter strings, CPU meter (`op1_246_rings_strings2.op1`)

**Result (2026-10-15):**
1. **Sympathetic strings (button 2): better,** but still glitches, mostly after a parameter change. The meter shows **4 rings (60–74%) almost all the time** ("all", chords or busy playing).
2. **Model 6 sounds good** but glitches almost constantly: **5 rings (75–89%)** almost all the time.
3. **Calibration:** the **modal model** ("all", a 4–6 note chord) also shows **4 rings**, and plays cleanly. The simulator puts it at ~41% of the CPU, so **the device runs ~1.5–1.8x the simulator's figures** for every model.
4. The white click (`0x3d`) works: the meter shows.

Fixes in Test 8. The rest of this section is the build description from before the flash.

This is strings1 with:
- **Model 6, string + reverb** (screen button 6): Rings' reverb, one per voice (the OP-1 renders voices separately), mixed to mono. Amount and time follow damping, the reverb's low-pass follows brightness, as on Rings. **"All" plays 4 voices in this model** (Rings' maximum polyphony): six reverbs would need ~290 MHz.
- **Lighter string models** (for Test 6's crackles):
  - a stolen voice's strings are handed over instead of copied, and the fading voice gets only the recent history it reads (a few KB instead of 64 KB);
  - the plucker sleeps once its burst has died away;
  - glides end within 0.1 cent, and frequency-only coefficient updates use a table (~600 cycles instead of ~2,500);
  - brightness/damping updates are capped at 2 sympathetic strings per block, in turn (a few ms behind 50 ms of knob smoothing);
  - the plain string loop is in Blackfin assembly (bit-exact with its C).
- **CPU meter:** click the **white** encoder on the Rings engine page to show the engine's CPU use on the six rings instead of structure; click again to go back. Ring *i* lights at (*i* + 1) × 15% of each audio block, with a peak hold that decays over ~0.6 s: **5 rings = 75%**, near the ~81% the OP-1 leaves free. It measures the engine only; the OP-1's own audio work comes on top. The white click code (`0x3d`) is inferred: if the click does nothing, the meter can't be shown, and nothing else changes.

**Checks (off the device):**
- `make test-reverb`: 4 scenarios against upstream's float reverb; wet level within 0.02 dB, 27–52 dB SNR (16-bit rounding in the feedback loop decorrelates long tails); a cleared reverb matches a fresh one.
- `make test-string`: all 19 pass; modal (`a3ae5f5c`) and FM (`567afc0c`) output unchanged; `test-alloc`, `step-params`, `test-fm`, `test-screen` pass.
- **CPU (simulator):**

| Case | strings1 | strings2 |
|---|---|---|
| Sympathetic, "all", 6 voices | ~155 MHz | ~139 MHz |
| Sympathetic, "all", knobs turning | ~179 MHz | ~150 MHz |
| Sympathetic, polyphony 1, fast notes (peak) | ~268 MHz | ~221 MHz |
| Sympathetic, polyphony-1 note-on | ~45k cycles | ~26k cycles |
| String + reverb, "all" (4 voices) / knobs turning | — | ~184 / ~195 MHz |
| String + reverb, polyphony 4, a note every 17 ms (peak) | — | ~291 MHz |

- **Package:** only `OP1_vdk.ldr` changes, plus `content/display/rings.svg`; `te-boot.ldr` absent; `verify_bootloader.py` passes; the LDR byte-diff shows only the FIRST header, the 7 vtable/patch words, the 2 filename immediates and the appended blocks (image end `0x02d472a0`).

**To check on the device:**
1. **Model 6:** a plucked string with Rings' reverb; damping lengthens both the string and the reverb, brightness darkens the reverb.
2. **Sympathetic strings (2):** the Test 6 cases again: busy playing and turning encoders, in polyphony 1 and "all". Still crackling?
3. **CPU meter** (white click) in the heavy cases: sympathetic polyphony 1 with fast legato; "all" with chords; model 6 with four notes; encoders turning. **Report the most rings you see lit** and whether crackles start at a given count.
4. **Modal and FM** unchanged.

## Test 8: string models in assembly (`op1_246_rings_strings3.op1`)

**Result (2026-10-15):**
1. **Sympathetic strings (button 2) sound great, no more glitches.**
2. **Model 6 hangs the OP-1:** one note, then a constant high-pitched buzz; the OP-1 freezes until power-cycled. Cause (found from the code, `docs/HANDOVER.md` §3): `ReverbFixed::ProcessAsm` doesn't wrap the write pointer before calling the assembly kernel, so ~0.74 s after the reverb starts it writes below its buffer, corrupts its own pointers and then random RAM. No test ran the assembly reverb through a wrap. Don't use model 6 on strings3.

The rest of this section is the build description from before the flash.

strings2 with the string models' hot loops rewritten, every one bit-exact with its C reference (`make bench-string`, `make bench-models` compare them; modal and FM output unchanged):
- **Limiter** (every model): in assembly; it was ~104 cycles per sample, taken branches for its attack/release choice and sign. Now ~53.
- **`ShlSat`:** one `<< n (S)` instruction for constant shifts (GCC's code branched; ~10 cycles each, in many places).
- **Stiff string** (buttons 3 and 6): a lean loop, in assembly, for structure 0.24–0.81 where only its stiffness allpass is active (no noise, no curved bridge): ~375 → ~195 cycles per sample.
- **Plain string** (every sympathetic string): scheduled by hand (state loads and stores in the multiplies' parallel slots): ~135 → ~117 cycles per sample.
- **Reverb:** in assembly, on a buffer laid out so every access is a fixed offset (a second copy is written only within one delay length of the write pointer's wrap; 109 KB per voice): ~390 → ~230 cycles per sample.
- **String models run per 64 samples** (one control block per call; Rings scans its knobs at 2 kHz, this is 689 Hz): per-block overhead halves; glides keep their timing.
- Strings' frequencies are cached; the plucker and its DC blocker are skipped entirely once quiet.

**CPU (simulator; the device has run ~1.6x these):**

| Case | strings2 | strings3 |
|---|---|---|
| Modal, "all", 6 voices (reference: 4 rings, clean) | ~165 MHz | ~153 MHz |
| Sympathetic, "all", 6 voices / knobs turning | ~139 / ~150 MHz | ~104 / ~114 MHz |
| Sympathetic, polyphony 1, fast notes (peak) | ~221 MHz | ~173 MHz |
| String + reverb, 4 voices / knobs turning | ~184 / ~195 MHz | ~118 / ~124 MHz |
| String + reverb, polyphony 4, fast notes (peak) | ~291 MHz | ~192 MHz |
| String, 6 voices | ~152 MHz | ~85 MHz |

**Package:** only `OP1_vdk.ldr` changes, plus `content/display/rings.svg`; `te-boot.ldr` absent; `verify_bootloader.py` passes; the LDR byte-diff shows only the FIRST header, the 7 vtable/patch words, the 2 filename immediates and the appended blocks (image end `0x02d27788`).

**To check on the device:**
1. **Sympathetic strings (button 2)** and **model 6**: busy playing and turning encoders. Still glitching?
2. **Meter readings** in the same cases as Test 7 (expected: around 3 rings, below the modal model's 4).
3. **Model 6 and the string model (button 3)** sound as before across the structure range (the lean loop covers 0.24–0.81; outside it the full loop runs).

## Test 9: model 6 hang fixed (`op1_246_rings_strings4.op1`)

**Result (2026-10-15):**
1. **Model 6 no longer hangs** and sounds great, but **glitches beyond polyphony 1** (CPU).
2. The CPU meter shows **4 rings** with model 6 and 4 notes (60–74%; expected ~3).
3. String model (button 3) at structure extremes: no glitches.
4. Models 1–5: all perfect.

The rest of this section is the build description from before the flash.

strings3 with one fix: `ReverbFixed::ProcessAsm` now wraps the write pointer (0 becomes `kSize`) before each assembly run, as the C loop's mask does. Nothing else changes: modal (`a3ae5f5c`) and FM (`567afc0c`) output and every other model are as on strings3.

**The test that would have caught it:** `make bench-string` now runs the reverb for 128,253 samples (about 3.9 wraps, a Clear on the way, block sizes from 1 to 64) with guard words around it, checks the write pointer after every block, and compares the result with a host build running the C loop. Before the fix it failed at the first wrap (write pointer out of range after 32,779 samples, guards broken); after it, Blackfin = host (`c36f2ef8`), guards intact.

**Checks (off the device):** `test-reverb` (4 ok), `bench-string` (both string kernels identical, reverb = host), `bench-models` (limiter `092ec491`), `test-string` (19 ok), `sim-engine` (`a3ae5f5c`), `sim-engine-fm` (`567afc0c`), `sim-model M=5`, `test-alloc`, `step-params`, `test-screen`: all pass. CPU as strings3 (model 6, 4 voices: ~118 MHz simulated; ~124 MHz with knobs turning).

**Package:** only `OP1_vdk.ldr` changes, plus `content/display/rings.svg`; `te-boot.ldr` absent; `verify_bootloader.py` passes; the LDR byte-diff shows only the FIRST header, the 7 vtable/patch words, the 2 filename immediates and the appended blocks (image end `0x02d277d8`).

**To check on the device:**
1. **Model 6 (button 6):** play for well over a second (the hang came ~0.74 s after the first note), single notes and 4-note chords, encoders turning. Does it still hang or buzz? How does it sound? Any glitches?
2. **CPU meter** (white click) with model 6 and 4 notes: how many rings (expected around 3)?
3. **String model (button 3)** at the structure extremes (below ~0.24 and above ~0.81 the full C loop runs, ~375 cycles per sample): any glitches with several notes?
4. **Models 1–5** as on strings3.

## Test 10: green handle reaches the top (`op1_246_rings_strings5.op1`)

**Result (2026-10-16):** the green handle's range is good (fully right reaches the top).

The rest of this section is the build description from before the flash.

strings4 with one display change: the brightness (green) handle is drawn over the encoder's real range, 0..29491 (the phase engine's stock range for green, `docs/screen-interface.md` §4c), so turning green fully right puts the handle at the top. The sound is unchanged (the engine reads the knob as before; modal `a3ae5f5c` and FM `567afc0c` unchanged). `make test-screen` checks the new mapping for every knob value and that 29491 lands at the top (y = 14).

**Package:** only `OP1_vdk.ldr` changes, plus `content/display/rings.svg`; `te-boot.ldr` absent; `verify_bootloader.py` passes; the LDR byte-diff shows only the FIRST header, the 7 vtable/patch words, the 2 filename immediates and the appended blocks (image end `0x02d27800`).

**To check on the device:**
1. Green fully right: the brightness handle at the top; fully left: at the bottom; it tracks in between.
2. Nothing else changes.

## Test 11: shared reverb for model 6 (`op1_246_rings_strings6.op1`)

**Result (2026-10-16):**
1. Model 6: **occasional glitches** remain; the user is happy with it for now.
2. CPU meter **definitely lower: around 2–4 rings** (from 4).
3. Model 6 **sounds great**.
4. Green handle good.

The rest of this section is the build description from before the flash.

strings5 (green handle) with **one reverb shared by every model 6 voice**, as upstream runs one reverb on the sum of its voices, instead of one per voice (Test 9: glitches beyond polyphony 1, meter at 4 rings).
- Each voice plays its dry signal (1 − amount) and sends it to the reverb; the reverb runs once per OP-1 block on the sum, and the voices share its wet signal equally (`op1/reverb_bus.h`; `docs/rings-port.md`, "String + reverb").
- The wet signal arrives one block (1.45 ms) later than before; with one voice, the level stays within ~2 dB of strings5 through a note (the reverb's tone meets the string's at a different phase).
- A steal hands the reverb on with the ring; a new note doesn't swell the reverb; an idle reverb (50 ms with nothing sounding in model 6) restarts empty.
- "All" still plays 4 voices in this model. Other models are unchanged (modal `a3ae5f5c`, FM `567afc0c`).

**CPU (simulator; the device has run ~1.6× these):**

| Model 6 | strings4/5 | strings6 |
|---|---|---|
| "all" (4 voices) / knobs turning | ~118 / ~124 MHz | ~73 / ~77 MHz |
| Polyphony 4, fast notes (peak) | ~192 MHz | ~123 MHz |
| Polyphony 2, fast notes (peak) | ~115 MHz | ~85 MHz |
| Polyphony 1, fast notes (peak) | ~132 MHz | ~101 MHz |

Per part (`make bench-models`): a voice ~15 MHz; the shared reverb ~12 MHz settled, ~20 MHz for 6,312 samples after a restart, once whatever the polyphony. Memory: one 109 KB reverb instead of six (the image is 668 KB instead of 1.2 MB).

**Checks:** `test-reverb-bus` (new, 16 checks), `test-reverb`, `bench-string`, `bench-models`, `test-string` (19 ok), `sim-engine`, `sim-engine-fm`, `sim-model M=5` (output peaks as strings5's), `test-alloc`, `step-params`, `test-screen`, `test-fm`: all pass.

**Package:** only `OP1_vdk.ldr` changes, plus `content/display/rings.svg`; `te-boot.ldr` absent; `verify_bootloader.py` passes; the LDR byte-diff shows only the FIRST header, the 7 vtable/patch words, the 2 filename immediates and the appended blocks (image end `0x02ca30cc`).

**To check on the device:**
1. **Model 6 in polyphony 2, 4 and "all":** chords and busy playing, encoders turning. Still glitching?
2. **CPU meter** in the same cases (expected: 2–3 rings with 4 notes, from 4).
3. **Sound:** does model 6 sound as before? Listen for clicks at new notes and steals (especially polyphony 1), and for the tail: does it carry on across notes?
4. **Green handle** (Test 10) and models 1–5 as before.

## Test 12: the MULTI engine (`op1_246_rings_multi1.op1`)

**Result (2026-10-16):**
1. Boots; slots load.
2. Orange click switches both ways ("it's great"); the screen follows.
3. Rings mostly fine, but **the modal model (button 1) glitches a lot (clicks and pops), particularly while moving the encoders.** Read as CPU overload: six ringing voices with encoders turning simulate at ~202 MHz (~1.6x on the device: ~320 of ~324 MHz free); polyphony 1 with a fading 60-mode voice is worse. Fix in Test 13.
4. Rings and phase notes ring out properly together.
5. Engine choice survives slot changes and a power cycle.
6. Engine browser shows MULTI; picking it and the other browsers work.
7. Preset browser: MULTI folder with the phase presets; they load.
8. Orange does nothing on other engines.

The rest of this section is the build description from before the flash.

strings6 (Rings unchanged) with the phase slot turned into a **MULTI** engine:
- **Two engines per slot: stock phase or Rings,** in `knobs[6]` (0 = phase). **Click orange** on the engine page to switch; the new engine plays from the next note (a ringing note keeps its engine). Saved with the slot and in presets.
  - Phase voices call the stock phase render and note-on (our voice objects are the stock ones); Rings' polyphony leaves them alone.
  - Blue, green and white clicks work as before while the slot plays Rings; on phase they go to the stock firmware (which ignores them).
- **The screen follows the engine:** the stock phase drawing (`pd.svg`, the stock Init and Update) or Rings'. `rings.svg` is now loaded by our Init with the firmware's own SVG loader; on a switch the incoming drawing's cache is flushed, as the firmware does when the screen changes.
- **"phase" shows as "multi"** in the engine browser (shift + T1; the list query reads `CASE type WHEN 'phase' THEN 'multi' ...`) and in the preset browser (the 16 factory phase presets' folder, in `op1_factory.db`). Display only: presets and slots keep `"type":"phase"`, so stock firmware still knows them.
- **Existing Rings slots come up as stock phase** (their `knobs[6]` is 0): one orange click turns each back into Rings, with its model and polyphony.

**Checks (off the device):** `test-alloc` (engine encoding, `Forget`), `sim-engine` (`a3ae5f5c`), `sim-engine-fm` (`567afc0c`), `sim-model M=5` (`7d5e0bee`, as strings6), `test-screen`, `test-reverb-bus`: pass. The new query, run on stock `op1_factory.db`, changes only `synth_types` row 5 (`phase` to `multi`); the drum, fx, lfo and seq lists are unchanged. The db tool refuses a second run; the packaged db differs from stock only in those 16 folders.

**Package:** `OP1_vdk.ldr` and `content/op1_factory.db` change, plus `content/display/rings.svg`; `te-boot.ldr` absent; `verify_bootloader.py` passes; the LDR byte-diff shows only the FIRST header, the 7 vtable/patch words, the query's 2 immediates and the appended blocks (image end `0x02ca33e4`; the PdScreen filename immediates are stock again).

**To check on the device:**
1. **Boot** normally; the 8 slots load.
2. **A phase slot:** the stock phase screen and sound. **Click orange:** the Rings screen appears; the next note is Rings. Click again: back to phase (screen at once, sound from the next note). Any leftover drawing or flicker at the switch?
3. **Rings as before:** models, polyphony, CPU meter, model 6.
4. **Mixed:** hold a Rings note, switch to phase, play: both ring out correctly.
5. **Save and reload:** switch a slot to Rings, change slot and come back, power-cycle: still Rings.
6. **Engine browser (shift + T1):** MULTI where PHASE was, same place; picking it works; other engines and the sampler entry unchanged. Also the drum, FX (T3) and LFO (T4) browsers.
7. **Preset browser** (press the active slot key again): a MULTI folder with the 16 phase presets; they load and play as stock phase.
8. **Orange click** does nothing on other engines (stock behaviour).

## Test 13: the modal model's coefficient budget (`op1_246_rings_multi2.op1`)

**Result (2026-10-16):**
1. Modal in "all" with several notes, encoders turning: **still clicks, worst on the blue encoder (structure).**
2. **Polyphony 1, 2 and 4 are glitch free** on the modal model.
3. The sound follows the encoders smoothly.
4. Everything else fine.

**Follow-up (2026-10-16), modal in "all":**
- 3 notes held: no clicks. Meter **1 ring** held, **3 rings while turning blue**.
- Clicks start when voices are still ringing out and 3 new notes are played, then an encoder turns (more voices sounding).
- 6 notes: **3 to 5 rings** turning blue or green.
- The glitch sounds like a **crackle**.

**Reading:** CPU overload, not update stepping (3 notes, whose voices already take turns updating, are clean). But turning costs far more on the device than simulated: 3 voices' updates are ~5% of the CPU in the simulator, while the meter rose by ~15-45%. Either the updates are several times slower on the device (cache misses on their tables?) or the OP-1's own encoder handling lands inside the engine's measured time. Test 14 measures which.

The rest of this section is the build description from before the flash.

multi1 with one fix for Test 12's clicks and pops on the modal model (button 1) while encoders turn.

**Cause (read as CPU overload):** while parameters move (encoders, knob smoothing, bend), every ringing modal voice recomputed its coefficients on every call, ~860 cycles per mode. Six 16-mode voices with knobs turning simulated at ~204 MHz (~1.6x on the device: over the ~324 MHz free). In polyphony 1 a new note's strike and the fading old voice both recomputed 60 modes.

**Fix (`op1/coefficient_budget.h`):**
- at most 32 modes' coefficients per OP-1 call (two 16-mode voices), plus one voice whatever its size; voices take turns (round robin), a voice that waits keeps its coefficients for that call;
- a strike always updates its voice; a voice fading out after a steal never updates, and nobody updates during a fade (4 calls);
- six voices turning then update two per call, each one every third call (~230 Hz, behind the ~50 ms knob smoothing).

**Checks (off the device):**
- `make step-params`: smoothed steps with updates every 2nd and every 3rd call stay below -60 dB (worst: a sudden +0.5 structure step, -65.5 dB every 3rd call against -69.7 dB every call; position unaffected).
- `make test-alloc`: the budget in index order (two per call, each voice every third call, also after the sounding voices change) and in shuffled orders (no starving: 43-48% of calls, at most 12 calls' wait).
- CPU (simulator):

| Modal case | multi1 | multi2 |
|---|---|---|
| 6 voices ringing, still | ~150 MHz | ~150 MHz |
| 6 voices, 3 knobs turning | ~204 MHz (peak ~206) | ~167 MHz (peak ~168) |
| Polyphony 1, fast notes, knobs turning | ~123 MHz, peak ~219 | ~101 MHz, peak ~187 |

- Modal output with still knobs unchanged (`a3ae5f5c`), FM (`567afc0c`), model 6 (`7d5e0bee`); `test-string` 19 ok, `test-screen`, `test-reverb-bus`, `bench-models` pass.

**Package:** as multi1 (`OP1_vdk.ldr`, `content/op1_factory.db` with the 16 folders, `rings.svg`; `te-boot.ldr` absent; `verify_bootloader.py` passes; image end `0x02ca3614`).

**To check on the device:**
1. **Modal (button 1), "all", 4-6 notes ringing, turning each encoder** fast and slow: still clicks or pops? CPU meter (white click) while turning: how many rings?
2. **Modal polyphony 1**, fast notes while turning: clean?
3. **Sound while turning:** does the sound still follow the encoders smoothly (no stepping or lag)?
4. Everything else as multi1 (phase/Rings switching, the other models).

## Test 14: measuring the coefficient updates on the device (`op1_246_rings_multi3.op1`)

**Result (2026-10-16), modal, "all":**

| Case | Engine reading | Updates reading |
|---|---|---|
| 6 notes, turning blue | 3-4 rings | 0-1 |
| 6 notes, turning green / encoders still | 3-4 rings | 0 |
| 3 notes, turning blue | 3 rings | 0 |

**Reading:** the coefficient updates are cheap on the device too (the budget works; ~4% simulated). The problem is the **base load**: six 16-mode voices take 3-4 rings (45-74%) with the encoders still, so anything more (voices ringing out, the OP-1's own work while turning) reaches the 5-ring zone and crackles. Polyphony 1, 2 and 4 use fewer modes in total and are clean. Per modal voice (simulator): ~35k cycles per call, of which the mode bank ~24k (already ~23.7 cycles per mode per sample, near the minimum for bit-exact Q31 arithmetic), the limiter ~3.4k, the rest ~7k (upsampler, smoothing, extra passes).

The rest of this section is the build description from before the flash.

multi2 plus a diagnostic and one bit-identical saving:
- **CPU meter, second reading:** white click now cycles **hidden -> engine (as before) -> coefficient updates only** (shift + white: back). The second reading is the share of time spent recomputing the modal model's coefficients (`ModalResonator` timed with the cycle counter, `op1rings::profile_update_cycles`). Same ring scale: ring *i* at (*i* + 1) x 15%.
- **The excitation filter's cutoff is cached** (recomputed only when pitch or brightness changes): ~4% less per modal voice; output bit-identical (`a3ae5f5c`, `567afc0c`, `7d5e0bee`).

**Simulator, for comparison:** six 16-mode voices with three knobs turning: ~164 MHz in all; the coefficient updates **~17.5 MHz** (~4% of the CPU: 0 rings on the second reading even at 3x).

**Checks:** `test-alloc`, `step-params`, `test-screen`, `test-reverb-bus`, `test-string` (19), `bench-models`, `test-fm`, `sim-engine`, `sim-engine-fm`, `sim-model M=5`: pass. Package as multi2 (image end `0x02ca37b0`).

**To check on the device** (modal, "all"):
1. 6 notes ringing, **turning blue**: engine reading (one white click) and updates reading (two clicks): how many rings each?
2. The same **turning green**, and **with the encoders still**.
3. **3 notes**, turning blue: both readings.

If the updates reading is high (2+ rings), the updates are much slower on the device than simulated: make them cheaper. If it stays at 0-1 while the engine reading jumps, the extra time isn't our updates (e.g. the OP-1's encoder handling interrupting the render).

## Test 15: lighter modal voices in polyphony "all" (`op1_246_rings_multi4.op1`)

**Result (2026-10-16): "everything sounds great, everything is working a treat."**
- The modal model in "all" is clean now (14 modes).
- **Model 2 (sympathetic strings) and model 6 (string + reverb) still crackle somewhat in "all"; in polyphony 4 both are good.** The user is happy with it as it is; revisit at the end (`docs/HANDOVER.md`, open items).
- **Rings / MULTI is paused here.** multi4 stays on the device.

The rest of this section is the build description from before the flash.

multi3 (meter readings kept) with the modal model's base load cut, against Test 13/14's crackle with many notes while turning (the coefficient updates were only 0-1 rings; six 16-mode voices sat at 3-4 rings before anything else).

**The user's choice: the bit-identical savings, plus 14 modes per voice in "all"** (from 16; polyphony 1, 2 and 4 unchanged). "All" loses the two highest partials of each note.

**Bit-identical savings** (modal output unchanged with 16 modes: `a3ae5f5c`):
- the mode bank sums its two outputs into one buffer (out == aux; integer sums don't depend on order), removing a pass;
- the pickup weights are kept from the last block while position and gains don't change (two `ComputeWeights` per sub-block saved);
- no libgcc divisions in the render path (the coefficient budget's round robin `%`, the weights' `kOne / segment_size`): each was hundreds of instructions;
- the voice index without a division;
- the excitation cutoff cached (multi3).
A per-function instruction profile in the simulator found these (the mode bank is ~70% of a modal voice; its loop is at 23 instructions for 23.7 cycles per mode per sample, the bit-exact minimum here: a variant without the weight's add for still weights saved nothing and was dropped).

**CPU (simulator), modal, "all":**

| Case | multi2 | multi4 |
|---|---|---|
| 6 voices ringing, still | ~150 MHz | ~123 MHz |
| 6 voices, 3 knobs turning | ~167 MHz | ~142 MHz |

**Checks:** modal checksum with 14 modes `ef8618dd` (new reference); FM `567afc0c` and model 6 `7d5e0bee` unchanged; `bench-mode-bank` (new make target: assembly = C = host, two outputs and one), `test-alloc`, `step-params`, `test-screen`, `test-reverb-bus`, `test-reverb`, `test-string` (19), `bench-models`, `bench-string`, `test-fm`: pass. Package as multi3 (image end `0x02ca37d4`).

**To check on the device** (modal, "all"):
1. 6 notes ringing (and voices ringing out plus new notes), turning each encoder, blue especially: still crackling?
2. Engine reading (one white click) with 6 notes, still and turning: how many rings (multi3: 3-4)?
3. Sound: does "all" still sound right with 14 modes?

## Test 16: FX probe (`op1_246_rings_fxprobe1.op1`)

**Result (2026-10-16, second recording; `incoming/A.wav`, `B.wav`, `C.wav`).** Rings model 6, polyphony 4, 4 notes held. Each recording has 3–4 identical complete reports; the clock reads 399.7–400.1 MHz.

| Run | Report | Free CPU (stock phone running) | Stock phone | Voices per call | Longest interval | Gaps |
|---|---|---|---|---|---|---|
| A: master, 4 notes | `[4, 2, 3, 567, 8, 207, 40, 1040, 2]` | 1.4%, ~6 MHz | ~36 MHz | 4.0 | 179% of a block | 2 |
| B: master, nothing playing | `[4, 2, 3, 566, 427, 197, 0, 613, 0]` | 75%, ~300 MHz | ~35 MHz | 0 | 108% | 0 |
| C: synth effect, 4 notes | `[4, 1, 9, 566, 82, 93, 40, 826, 2]` | 14.5%, ~58 MHz | ~16 MHz | 4.0 | 146% | 2 |

- **Rate:** both positions are called ~690 times/s with 64 samples at 44.1 kHz (period 566 × 1024 cycles; the report tone is 1377 Hz, i.e. written at 44.08 kHz).
- **Switched off, an effect isn't called** (2 gaps for the 2 off/on switches in A and C). An effect costs nothing while off, and comes back to stale state when switched on again.
- **The master effect is post-mix:** the drums go through it too (by ear). **The stereo order is (L, R):** the beeps were on the left only.
- **Stock phone costs** ~35 MHz on the master and ~16 MHz as a synth effect (half: the stereo process is two mono ones). Its cost varies a little with its settings (28–31 MHz in the first recording). Clouds takes this time back.
- **The OP-1's own baseline** (nothing playing, no synth) is ~15% (~60 MHz), plus the effect.
- **Rings model 6 with 4 voices costs ~255–290 MHz on the device**, OP-1 per-voice work included: B's free time less A's (master), or less C's corrected for the mono phone (synth). That is ~2× the earlier estimate (~73 MHz simulated × 1.6). The CPU meter's 2–4 rings for this case (Test 11) agree with the higher figure. The likely cause is the data cache: the 109 KB reverb and the string delay lines live in SDRAM, and the simulator models no cache. **Expect the same for Clouds' buffers**, and measure on the device.
- **The synth position's params pointer is not in `0xff801444 + slot × 16`** (position 9 = "other"). Clouds gets the pointer in each call, so this doesn't matter. The address in `docs/next-projects.md` §1 was wrong for the process call.
- The longest intervals (146–179%) were recorded with notes starting inside the timing window (note-on spikes). A and C were close to the limit already.

The first recording (2026-10-16, earlier) gave the same rate and baseline; its run A had 6 voices sounding and the off/on switching before the reset, so its gap count was cleared.

The rest of this section is the build description from before the flash.

The first step of the Clouds project (`docs/HANDOVER.md` §3): measure the effect interface on the device before any Clouds code. It is multi4 unchanged, plus a probe wrapped around the **phone** effect's two process entries (`ports/rings/op1/fx_probe.cc`): mono (the synth and drum effect, vtable word `0x1911308`) and stereo (the master effect, `0x1911328`). Both still call stock phone, so phone sounds as before until a measurement starts.

**How the probe works.** Each entry (mono, stereo) runs on its own:
1. **Idle:** stock phone plays. In the background the probe times its calls in windows of 1024 calls (~1.5 s). Each complete window records the call period, the longest interval, stock phone's cycles per call and how many synth voices rendered per effect call. A pause of over 5 ms between calls (the effect not called at all) counts as a **gap** and restarts the window.
2. **Turning the blue encoder starts the measurement** (once a window has completed): as probe v3, the probe burns more CPU in every call until audio overloads; the last load that ran clean is the free CPU, with Rings and stock phone running.
3. **Report:** beeps instead of the effect's output, repeating (master: left channel only, right silent). Nine numbers: version 4, mono/stereo, position (synth/drum/master), call period, free CPU, stock phone's cost, voices per call, longest interval, gaps.
- **Turning the white encoder** in any state returns to idle (stock phone) and clears the gap count, for another run.
- The beep tone's pitch gives the effect's sample rate (1378 Hz at 44.1 kHz).

**Checks (off the device):**
- `make host-fx-probe` simulates an OP-1 (clock, free CPU, phone cost, voices, gaps, jitter). `tools/decode_probe_beeps.py` (now with v4) recovers every value: stereo, 50% free, 20,000-cycle phone, 6 voices, 2 gaps (the gap before the white reset is cleared), 44.1 kHz from the tone; and mono, 30% free, 4 voices.
- `make op1-engine` still builds multi4 byte-identical; the probe code and the render counter are only in `make op1-fxprobe` (`-DOP1_FX_PROBE`).
- **Package:** against multi4 only `OP1_vdk.ldr` changes. Against stock: `OP1_vdk.ldr` and `content/op1_factory.db` change, `content/display/rings.svg` is added, `te-boot.ldr` is absent. The LDR byte-diff shows only the FIRST header, the 9 vtable/patch words (the two new ones checked against stock phone, `0x01a1bb74` and `0x01a1c25c`), the type-list immediates and the appended blocks; the stock tail is intact. `verify_bootloader.py` passes. Every patch target checked against `bfin-elf-nm`; image end `0x02ca3dfc`; no `.init_array`.
- The stereo wrapper reads the parameters from the caller's stack (`[SP+12]` on entry) and passes them to stock phone the same way; phone's stereo process is its mono process on two sub-objects, 64 samples each (`0x1a1c25c`).

**Procedure.** Rings: the heaviest case, **model 6 (string + reverb), polyphony 4**. Recordings go in `incoming/probe/`; record the OP-1's output (as the probe v3 recordings), at least 45 s of beeps each.

*Run A, master effect (stereo):*
1. Put **phone on the master effect** and switch it on. Stock phone should sound as usual.
2. **By ear:** play some drums. Do they go through phone too (pre or post the drum mix)?
3. On the master effect page, **turn the white encoder** a notch (resets the probe).
4. **Switch the master effect off for ~2 s, then on again; do this twice.** (The report will show 2 gaps if the effect isn't called while off, 0 if it keeps running.)
5. **Hold a 4-note Rings chord**, wait ~3 s, then **turn the blue encoder** a notch (if it's at the end of its range, turn it the other way). Keep holding the chord **~20 s**: stutters near the end are expected. Watch the screen: does it get sluggish before the audio stutters?
6. Beeps start: a long tone, then groups of short beeps. **Check with headphones: beeps on the left only?** You can let go of the chord. Record ≥ 45 s → `fxprobe_master.ogg`.
7. Turn the white encoder to go back to stock phone.

*Run B, master, no notes:* steps 3 and 5 without the chord (nothing playing), → `fxprobe_master_idle.ogg`. Gives the headroom without Rings.

*Run C, synth effect (mono):* switch the master effect off, put **phone on the synth's effect**, and repeat steps 3, 5 and 6 with the chord → `fxprobe_synth.ogg`. (Turning blue/white here means the synth effect's encoders.)

If no beeps come after turning blue: no window had completed yet (wait 2 s and turn blue again). A beep tone that never stops, or a hang, means something is wrong: power off and report.

**What the numbers decide:** the CPU budget for Clouds next to Rings (grain counts, quality, internal rate); whether Clouds must handle being called while switched off; 64 samples at 44.1 kHz for both positions; the left/right order.

## Test 17: Clouds on phone (`op1_246_rings_clouds2.op1`)

**clouds1 (2026-10-17): hung at boot** (TE logo and version, then nothing; recovered with multi4). Likely cause: a restart cleared the whole ~360 KB CloudsCore in one call. In SDRAM that probably takes longer than the 5 ms gap check, so every call restarted again and the audio thread starved the app. A drum call on phone also stopped the synth's Clouds, so phone on both synth and drums restarted it every block. **clouds2** zeroes the core 8 KB per call (~45 calls of dry pass-through, ~2.2k simulated cycles each), then initialises it without clearing again; drum calls no longer touch the synth instance.

**Not flashed yet.** The first Clouds build: multi4 (MULTI engine: phase or Rings) plus **Clouds in place of the phone effect**, as the synth effect (mono) and the master effect (stereo). Only looping delay and granular are implemented. The screen is still phone's stock screen (the Clouds screen waits for `clouds.svg`). Code: `ports/clouds/op1/clouds_fx.cc`, `clouds_settings.h`; hooks in `ports/rings/op1/{patches,input,engine}.cc` under `-DOP1_CLOUDS` (`make op1-clouds`).

**How it behaves:**
- **Phone is Clouds by default** (every preset stores 0 in the effect's values[4..7]). **Orange click** on the FX page swaps Clouds ↔ stock phone; it's saved with the synth preset. On the master it's kept in the live master values (whether those persist across a power cycle is a question for this test).
- While Clouds plays, on the FX page:
  - **blue click:** next mode, looping delay ↔ granular (shift + click: back);
  - **green click:** freeze on/off (not saved);
  - **white click:** page 1 ↔ page 2. The encoders jump to the other page's values.
- **Page 1:** blue = position, green = size, white = pitch (centre = none, ±2 octaves), orange = density (granular: centre = no grains, left = regular, right = random grains; looping delay: diffusion).
- **Page 2:** blue = texture, green = dry/wet (50% by default), white = feedback, orange = reverb.
- Stereo spread is fixed at 50%. Clouds restarts (buffer cleared) when the effect is switched on, swapped from phone, or the synth slot changes: ~65 ms of dry sound while it clears 8 KB per call.
- **The grain count is governed:** when CPU runs short, granular plays fewer grains instead of crackling. Looping delay has a fixed cost (~31 MHz simulated stereo, maybe ~50 on the device).

**Checks (off the device):**
- `make sim-fx` (ports/clouds): both entries in the simulator, plus the settings and click round trips (the page swap returns the encoders to within ±2 steps).
  - Master looping delay ~31 MHz;
  - granular ~31 MHz (~7 grains), dense ~55 MHz;
  - synth looping delay ~20 MHz, granular ~29 MHz;
  - restart steps ~2.2k cycles each (8 KB of the core zeroed per call).
- `make op1-engine` still builds multi4 byte-identical.
- **Package:** against multi4 only `OP1_vdk.ldr` changes. Against stock: `OP1_vdk.ldr` and `content/op1_factory.db` change, `content/display/rings.svg` is added, `te-boot.ldr` is absent.
- **LDR byte-diff against stock:** only the FIRST header, the 12 vtable/patch words, the type-list immediates and the appended blocks differ; the stock tail is intact. The 5 new words were each checked against their stock value: phone mono/stereo process `0x01a1bb74`/`0x01a1c25c`, MasterController key down/shift key down/key up `0x019db7c4`/`0x019d987e`/`0x019db708`.
- `verify_bootloader.py` passes. Every target was checked against `bfin-elf-nm`. The image ends at `0x02d62ac4` (1.4 MB of region A) and has no `.init_array`.

**Procedure.** Record anything interesting into `incoming/clouds/`.
1. **Synth effect.**
   - Pick a synth (stock or MULTI), put **phone** on its effect and switch it on. It should sound like a delay (Clouds' looping delay), not like phone.
   - Turn the four encoders: does each do something (position = delay time, size, pitch, density = diffusion)?
   - Set pitch back to the centre.
2. On that FX page:
   - **click white** (page 2): turn green (dry/wet) to full wet, then white (feedback) and orange (reverb) up;
   - **click white** again: are page 1's settings where you left them?
3. **Click blue:** granular. With density right of centre you should hear random grains; at the centre, silence (dry only); left of centre, regular grains. Try size and pitch.
4. **Click green:** freeze. The grains keep playing the held buffer while you play new notes; click again to release.
5. **Click orange:** stock phone. Click orange again: back to Clouds (same mode and page).
6. **Save/recall:** switch to another synth slot and back. Are the mode and page as you left them? Then power-cycle and check again.
7. **Master effect:** put phone on the master and repeat steps 1–5 quickly (stereo now; the drums go through it too). After a power cycle, are the master's mode and page kept?
8. **CPU:** a Rings chord (model 6, polyphony 4, 4 notes) with granular on the master and density far right. Does it crackle, or just thin out (fewer grains)? The same with looping delay.
9. Anything else you notice (clicks when switching, stuck sound, the screen), in numbered points as usual.

**If something is wrong:** a hang or a stuck loud tone means power off. Recovery as always: hold COM at power-on and flash `op1_246_rings_multi4.op1`.

**Results, clouds2 (2026-10-17):**
1. It boots. Clouds works on the synth effect (mono) and the master effect (stereo). The synth slot settings survive a power cycle.
2. With Rings, Clouds glitches badly (overload); with the sampler it's great. Clouds on synth + Clouds on master crackles; Rings with no FX + Clouds on the master glitches.
3. The drum effect follows the synth slot's Clouds/phone setting (the drum's call was taken for the synth's).
4. Page and engine clicks work on the synth FX page, not on the master or drum FX pages.

## Test 18: Clouds at half rate, drums, master clicks, screen (`op1_246_rings_clouds3.op1`)

**Not flashed yet.** clouds2 plus:
- **Half the CPU:** the core runs at 22.05 kHz between a 2:1 halfband decimator and interpolator; the dry signal and the mix stay at 44.1 kHz (`src/core/halfband.*`, assembly kernels bit-exact with C). Simulator: master looping delay 18.7 MHz (was 30.7), granular 18.8 (31), dense 31 (55); synth 11.8 / 16.5 (19.8 / 29.3). The wet signal's band now ends at ~10 kHz; delays, grains and reverb keep their durations.
- **Drums:** the live instrument comes from the firmware's mode word `[0xff801038]` (0 synth, 1 drum): the drum kit's own values (`0xff8015c4 + kit × 16`) and engine bit, saved with its Patch (`0x1ddd9a8 + kit × 0x38`). Clicks on the drum FX page: drum controller vtable `0x190fa64` words `0x190fa78/80/88`.
- **Master clicks:** the FX page test now reads MasterController's subscreen at `0x1aad5f0` (clouds2 read `0x1aad5d8`, a history index).
- **Screen:** the weather design (`ports/clouds/op1/clouds.svg`, built by `ports/clouds/op1/gen_clouds_svg.py`: arcs to Béziers, metadata stripped) in place of phone's screen while the shown position plays Clouds (PhoneScreen Init/Update words `0x1911358/0x1911350`). The CPU row shows Clouds' own share of the audio period (one segment per ~17%).

**Checks (off the device):** `make sim-fx` and `make test` (kernels: Blackfin = host) pass; the 32 kHz comparison is unchanged; every patch target checked against `bfin-elf-nm` (17 patches, each stock value checked by `inject_app.py`); `verify_bootloader.py` passes; against clouds2 only `OP1_vdk.ldr` changes and `content/display/clouds.svg` is added.

**Procedure:**
1. **Screen:** put phone on a synth's effect, open its FX page. The cloud screen should show. Turn each encoder (cloud moves, resizes, rises/sinks, rain changes); white click: page 2 (wind, puddle, rainbow, thunder arcs); blue click: the mode box moves (granular ↔ looping delay); green: frozen face and snowflake. Orange: phone's stock screen comes back.
2. **Drums:** put phone on the drum kit's effect. With the synth slot on phone (stock) and the drums on Clouds, and the other way round: does each keep its own setting? Do the drum FX page clicks work and survive a power cycle?
3. **Master:** clicks on the master FX page (mode, page, freeze, orange swap). Does the master keep them across a power cycle?
4. **CPU:** the cases that glitched in clouds2: Rings (say model 6, polyphony 4) + Clouds on the master; Clouds on synth + master with the sampler. Note what the CPU row shows and whether it glitches; try looping delay and granular.
5. **Sound:** anything odd from the half rate (dull or gritty wet sound, aliasing on high notes)?
6. Anything else, in numbered points.

**If something is wrong:** power off; recovery: hold COM at power-on and flash `op1_246_rings_multi4.op1`.

**Results, clouds3 (2026-10-17):** sounds great; Rings + Clouds on synth and master together still strains the CPU (accepted for now). Clouds was ~6 dB quieter than the dry signal, even fully dry (recordings in `incoming/`, both normalised on export, so only the noise floor shows it).

## Test 19: unity gain, "multi" in the FX list (`op1_246_rings_clouds4.op1`)

**Not flashed yet.** clouds3 plus:
- **Level:** upstream's output stage soft-limits half the mix (`SoftConvert`: `SoftLimit(x * 0.5)`; the module's hardware makes up the 6 dB), and the port copied it. The OP-1 mix now uses 2 × SoftLimit(x / 2), clipped at full scale: unity for normal levels (−0.16 dB at half scale, −0.5 dB at 0.9 of full scale).
- **Name:** the FX browser shows phone as "multi" (the type-list query also maps `'phone'`; only `fx_types` has that row). Presets still store `phone`.

**Procedure:** a note with the effect off, then Clouds fully dry: the same level? Then the effect list: "multi" where phone was, and choosing it still gives Clouds/phone.

**Results, clouds4 (2026-10-17):** the level with Clouds on now matches; "multi" shows in the FX list. **clouds4 is the current device firmware.**

## Test 20: sequencer probe (`op1_246_rings_seqprobe1.op1`)

**Not flashed yet.** clouds4 plus a probe on the **tombola** sequencer (`ports/rings/op1/seq_probe.cc`, `make op1-seqprobe`), for the open questions of `docs/sequencer-interface.md` §10. Tombola plays as stock, but its screen shows the probe's readout instead of the balls: ten numbers, each with its index drawn small on its left (0–4 left column, 5–9 right column).

| # | Shows |
|---|---|
| 0 | status digits: `1` (version), echo on (0/1), velocity mode (0–3), instrument (0 synth, 1 drum), synth sequencer playing, drum sequencer playing |
| 1 | sequencer ticks in the last second (expect tempo × 96 / 60: 192 at 120 BPM) |
| 2 | tempo × 10 as the firmware's tick clock has it (1200 = 120 BPM) |
| 3 | largest tick phase + 1 (expect 48) |
| 4 | ticks, total (does it move while stopped?) |
| 5 | ticks at phase 0 (expect number 4 / 48) |
| 6 | key presses (thousands) and the last key (units): `012005` = 12 presses, last key 5 |
| 7 | the last key press's second argument (the unknown "flag") |
| 8 | key releases (thousands) and all-notes-off calls (units) |
| 9 | instrument-switch calls (thousands) and screen frames per second (units) |

Clicks on the tombola screen (only while the sequencer is on): **orange** = echo mode on/off; **blue** = next velocity mode (0: 127, 1: 64, 2: 16, 3: alternating 127/16); **green** = clear the counters. **Echo mode**: keys you play are taken by the probe and played back by the sequencer thread (the way Marbles will play notes); stock tombola's balls stop meanwhile.

**Checks (off the device):** builds with no undefined symbols or constructors; the 9 new hooks each checked against their stock value by `inject_app.py` and against `bfin-elf-nm`; `verify_bootloader.py` passes; against clouds4 only `OP1_vdk.ldr` changes and `content/display/seqprobe.svg` is added. The probe doesn't touch op1.db.

**Procedure** (report in numbered points; photos of the screen are fine):
1. Synth mode. Choose **tombola** as the sequencer and turn it on: does the readout appear? Note numbers 0–5 with the transport **stopped** (watch whether 4 moves).
2. Press **play** on the tape: numbers 1–5 again. Then tempo 60 and 180: numbers 1 and 2.
3. Tape **recording**: do the ticks (number 4) keep moving?
4. Press a few keys normally: numbers 6 and 7. Then while holding **shift**, and any other way of playing you can think of (e.g. while recording): does number 7 change?
5. **Orange click** (status digit 2 becomes 1). Play keys: do you hear them, without stuck notes, also fast and with chords? Then **blue clicks** through the velocity modes on two or three engines (e.g. FM, sampler, MULTI/Rings): does the level or tone change with velocity (mode 3 alternates loud/soft per note)?
6. With echo on, hold a note and switch to **drum** mode and back (also with tombola chosen for drums): numbers 8 and 9, stuck notes, anything odd.
7. Orange again (echo off): does stock tombola work as before (with the readout instead of balls)?
8. Anything else.

**If something is wrong:** power off; recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1` (or multi4).

**Results, seqprobe1 (2026-10-17; photos `incoming/stopped.jpg`, `60bpm.jpg`, `180bpm.jpg`):**
1. **Tick rate:** 96 ticks/s at 60 BPM, 288 at 180 BPM: **96 ticks per beat** (one per audio block's clock step, as inferred). The largest phase + 1 is **48** (half a beat); phase-0 ticks ≈ total / 48 (186 against ~8,900).
2. **Ticks run all the time:** the totals keep counting with the transport stopped and while recording. The sequencer clock is free-running, not tied to the tape.
3. **KeyDown's flag:** 0 for a normal key, **1 with shift held** (tombola plays shifted keys directly).
4. **Notes from the sequencer thread** (echo mode): no stuck notes, also fast and with chords. **Velocity is heard** on the engines tried.
5. **Drum mode with tombola:** the same readout and the same random drum triggers (the shared player object works for both instruments).
6. Echo off: stock tombola plays as before (the readout replaces its balls, as designed).

## Test 21: Marbles on tombola (`op1_246_rings_marbles2.op1`)

**Not flashed yet.** clouds4 plus **Marbles as the synth sequencer**, on tombola (`ports/marbles/op1/marbles_seq.*`, `marbles_op1.cc`; `make op1-marbles` in `ports/rings`). Marbles is on by default; the **orange click** on the tombola screen swaps Marbles ↔ stock tombola. Settings are in RAM only (reset at power-on). **marbles2** has the user's **Orbit** screen (`ports/marbles/op1/marbles.svg`, mappings in `marbles-screen.md`); marbles1 (a digit readout) was never flashed.

**How it plays:**
- The T clock is locked to the OP-1 tempo (96 ticks per beat). Each gate plays a note: **T1 with X1, T2 with X2, T3 with X3**, so gates overlap into chords. T2 is the steady clock; T1/T3 follow the T model. Y sets the velocity.
- **Scale mode** (default): notes = the root (the last key you pressed; C at first) + X over two octaves, quantized by steps to the scale.
- **Keys mode** (shift + white): notes come only from the chord you hold; it stays latched after you let go, until you start a new chord.
- **Shift + key** plays that key straight through (not into Marbles).

**Controls** (white click = page):

| Page | Blue | Green | White | Orange | Blue click | Green click |
|---|---|---|---|---|---|---|
| T | rate (1 bar … 1/32, triplets between) | bias | jitter | deja vu amount | T model (shift: back) | T deja vu off/on/locked |
| X | spread | bias | steps | length (1–16) | scale (shift: back) | X deja vu off/on/locked |

Shift + white = scale ↔ keys mode. Shift + encoder = fine (if the OP-1 passes shifted encoders here: see 9 below).

**Screen (Orbit):** the blue core grows with rate / spread, the green diamond slides along the axis (bias), the white triangle climbs its track (jitter / steps), the orange column fills (deja vu / length; the tick is the locked middle). The three gate dots on the rings light with T1 / T2 / T3. Right column: the model (page T) or scale (page X) marker, the deja vu state of the shown page, scale/keys, and the page.

**Checks (off the device):** `make sim-seq` (ports/marbles), host and Blackfin: notes on the 1/8, 1/16 and 1/4-triplet grids (also after rate changes), keys mode plays only the latched chord, deja vu locked × length 4 repeats exactly, full jitter stays near the grid, no stuck or doubled notes; ~2,900 cycles per tick (~0.6 MHz at 120 BPM). `make test` (the port's component tests) still passes. The image: no undefined symbols or constructors, ends at `0x02d2c934`; the 10 new hooks checked against their stock values and `bfin-elf-nm`; `verify_bootloader.py` passes; against clouds4 only `OP1_vdk.ldr` changes and `content/display/marbles.svg` is added (`gen_marbles_svg.py`: metadata stripped, the example state checked).

**Procedure** (numbered points as usual):
1. Synth mode, choose tombola, turn the sequencer on: does Marbles play (notes at 1/8 by default, in time with the metronome/tape)?
2. Press a key: does the melody move to that root? Shift + white, hold a chord, release: only those notes?
3. Turn each encoder on both pages: does each do what the table says, and does its indicator move? Is the speed right (roughly how many detents does a full sweep take)?
4. Blue clicks through the T models and scales; green clicks through deja vu: with deja vu locked and a short length, does it loop?
5. Jitter up: does the rhythm loosen but stay roughly in time?
6. Shift + key: played straight through?
7. Turn the sequencer off and on, change octave, switch to drums and back: any stuck notes?
8. Orange: stock tombola back (balls screen), orange again: Marbles.
9. Hold shift and turn an encoder: does it change more finely (shifted encoders reaching Marbles), or not at all?
10. Anything else.

**If something is wrong:** power off; recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

**Results, marbles2 (2026-10-17):**
1. Plays in time. 2. Root and keys mode work nicely. 3. Every encoder works; the speed is good (about three full turns for a whole range). 4. Model and deja vu clicks move through the screen, but it's hard to tell what they do. 5. Jitter good. 6. Shift + key good. 7. No stuck notes, **but Marbles also plays on the drum instrument** (tombola's player is shared). 8. Orange swap good. 9. Shift + turn gives fine steps.
- **Screen bug:** the four encoder indicators sat at the top of the screen, not in the orbit. The parser folds a group's `translate` into its children, so setting an element's transform dropped its wrapper's offset.
- The screen is hard to read without the description: to be reworked with the designer.

**marbles3** (`op1_246_rings_marbles3.op1`, not flashed yet): the indicators carry their wrappers' offsets; Marbles steps aside on the drum instrument (stock tombola there, Marbles on synth).

**marbles4** (`op1_246_rings_marbles4.op1`, not flashed yet): marbles3 plus the **steps** encoder remapped to Marbles' musical half only (left = all 12 notes, right = the root only; the old left half glided a voltage, which as notes repeated one pitch). The screen is still Orbit (with the offsets fixed); a v2 brief went to the designer (`docs/marbles-screen-brief.md`).

## Test 22: Marbles with the "Dots" screen (`op1_246_rings_marbles7.op1`)

**marbles5 didn't boot (2026-10-17):** `..\src\gui\lib\Parser.cpp:231 x_end != std::string::npos` at startup, while loading `marbles.svg`. The design wrote `transform="matrix(0.5,0,0,1,119.5,0)"`; every stock SVG writes `matrix(a b c d e f)` with spaces, and the parser looks for a space after each number. Recovery: COM at power-on, clouds4. **marbles6:** the same firmware, with `gen_marbles_svg.py` now dropping the design's example transforms (all on driven elements, which the code positions on the first frame) and rewriting any `matrix()` with spaces. Every other syntax form in the file was compared against files that booted (Orbit, Clouds, Rings, stock): only `matrix` was new.

**marbles6 didn't boot either (same assert):** the transforms weren't the cause. The assert's code (`0x19fb7c8`, called while reading an element's id) looks for `"_x"` and then a closing `"_"`: the parser decodes Illustrator's `_xHH_` escapes in ids. The Dots ids `loop_x` and `leg0_x`…`leg3_x` have `_x` with no closing `_`. **marbles7:** `gen_marbles_svg.py` renames `_x` → `X` in ids (`loopX`, `leg0X`…) and rejects any id still containing `_x`; the screen code uses the new names. The parser's other content asserts were checked too: no exponents in numbers (`Parser.cpp:103`), at most 2 decimals (`:116`; Rings has always used 2), ASCII ids (`:577`).

**Not flashed yet.** marbles4 (steps remapped; indicators fixed; no Marbles on drums) plus the designer's v2 screen (`ports/marbles/op1/marbles.svg`, mappings `marbles-screen.md`, brief `docs/marbles-screen-brief.md`). The core now keeps a 16-step history (`MarblesSeq::history_gates/height`; `make sim-seq` checks it).

**What the screen shows:**
- **History** (top left): the last 16 clock steps, newest on the right; three lanes, T3 top, T2 middle (the steady clock: every step), T1 bottom. A dot = that gate played a note on that step; its height in the lane = the note (scale mode: over the two octaves; keys mode: over the held chord).
- **Loop brackets:** above the lanes = rhythm deja vu, below = pitch deja vu. Hidden = off, dim = on, bright = locked. They span the last *length* steps.
- **Legend** (bottom): four icons in encoder colours (they change with the page: that's the page indicator) and a level bar under each.
- **Chips** (right): the T model (page T, marker under 7 dots) or scale (page X, under 6 bars); scale/keys source.

**Procedure** (numbered points; a photo or two helps):
1. Do the dots scroll in time, one column per clock step, with T2's lane full?
2. Turn rate, bias, jitter: does the history change as you'd expect (speed, which outer lane is busier, irregular gaps)?
3. Page X: spread and steps: do the dot heights spread out / settle onto fewer heights?
4. Green click on each page: does the bracket appear (dim), turn bright (locked), and vanish (off)? With locked and a short length, do you see the pattern repeat under it?
5. Are the legend icons and bars in the right places, and do the bars follow the encoders? Do the chips move with the blue click and the source toggle with shift + white?
6. Does the screen now make sense without the description? What's still unclear?
7. Anything else, in numbered points.

**If something is wrong:** power off; recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 23: Grids on drums + Marbles on synth (`op1_246_rings_seq3.op1`)

**Not flashed yet.** marbles7 (Clouds, Marbles with the Dots screen) plus **Grids as the drum sequencer** on stock **pattern** (`ports/grids/op1/grids_seq.*`, `grids_op1.cc`; `make op1-seq` in `ports/rings`). Grids answers only while the drum instrument is live; synth pattern stays stock. Grids is on by default; the **orange click** on the pattern screen swaps Grids ↔ stock pattern, **shift + orange prints** the last bar into stock pattern. Settings in RAM only. **seq3** has the designer's **"Panel + kit"** screen (`ports/grids/op1/grids.svg`, mappings `grids-screen.md`): the map with its dot (X/Y), the kit (hi-hat, snare, kick) lighting on hits and growing on accents, staying lit while a part's key fills it, an arrow at the part being assigned, four bars (the fill page while shift is held), the playhead under 16 ticks, and the icons for mode, resolution, accent (louder / key), random voice and shift. Both screens went through the stricter SVG scripts (no `_x` ids, no transforms, no `display` on elements, numbers checked).

**Not in this build:** showing pattern as "multi" in the sequencer list. The chooser looks the current type up by its shown name (renaming only the list would crash it), and the call that would have to answer "multi" can't reach our code (a CALL reaches ±16 MB; region A is ~19.6 MB away). It needs a small trampoline nearer the app, to be tried as its own test.

**Controls** (drums mode / Euclidean mode):

| | Blue | Green | White | Orange |
|---|---|---|---|---|
| encoders | map X / BD length | map Y / SD length | chaos / HH length | swing |
| shift + encoders | BD fill (hits) | SD fill (hits) | HH fill (hits) | accent |
| click | drums ↔ Euclidean | re-roll chaos (next bar) | assign: BD → SD → HH → BD accent → SD accent → HH accent → off | swap Grids ↔ pattern |
| shift + click | 16ths ↔ 32nds (16ths default) | forget accent keys | random voice on/off | print the last bar into pattern |

- **Keys:** holding a part's key (BD 0, SD 2, HH 8 by default) fills that part up while held; other keys play straight through.
- **Assign:** white-click to a target, then press a key: it becomes that part's sound (you hear it). For an accent target, the pressed key takes that part's accents; pressing the part's own key there goes back to "louder on the same key".
- **Print** writes 16 sixteenths of the last bar (all keys; accents become plain hits) and saves them with stock's own save. **It overwrites the drum pattern, without undo.**


**Checks (off the device):** `make sim-grids` (host and Blackfin): notes on the 16th grid, keys 0/2/8, fill while held doubles the snare's hits, assign moves BD to a new key, accent keys take the accents, play-through, the recorded bar equals what played, no stray or stuck notes. `make compare`: still bit-exact with upstream (~15 M calls). The image: no undefined symbols or constructors, ends at `0x02d30410`; the 10 new hooks checked against their stock values and `bfin-elf-nm`; `verify_bootloader.py` passes; against clouds4 only `OP1_vdk.ldr` changes and `content/display/marbles.svg` and `grids.svg` are added.

**Procedure** (numbered points):
1. Drum mode, choose pattern, turn the sequencer on: does Grids play a beat on keys 0/2/8 (are those kick/snare/hat on your kit)?
2. X, Y, chaos, swing; shift + the fills: do they change the beat as expected?
3. Hold the snare key: does it fill up? Other keys: played through?
4. Assign a new kick sound (white, then a key), then an accent key for the snare: does it work?
5. Blue: Euclidean (the lengths on the encoders); shift + blue: 32nds.
6. Shift + orange (print), then orange (stock pattern): is the bar there, and still there after a power cycle?
7. Synth mode: pattern and Marbles (tombola) behave as before; any stuck notes switching instruments?
8. Anything else.

**If something is wrong:** power off; recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

**Result (seq3, 2026-10-17):** assigning a new BD crashed the OP-1. The white click cycled the targets, and the crash came on the next key press. **Cause:** SeqManager also passes every keyboard key to the pattern *controller*. Grids' hook let stock's handler (`0x1a07a58`) run for anything that wasn't an encoder click. That handler moves pattern's step cursor and calls the pattern screen (`0x1a095f4` → `0x1a09438`). The screen looks up pattern.svg's `h1`/`h14` through `Screen::FindNode` `0x1a3b580`, which asserts on a missing node, and grids.svg was the shown document. So this is not an assign bug: any key that reached that path could crash.

## Test 23b: the controller key fix (`op1_246_rings_seq4.op1`)

**Not flashed yet.** seq3 with one change: while Grids is active, pattern's controller key down, shift key down and **key up** (newly hooked, `0x1911040`, stock `0x01a07980`) return 0 without calling stock. Grids still gets the keys through the player hooks. With Grids off or in synth mode, stock runs as before. The image has no warnings, no undefined symbols and no constructors. The new hook targets match `bfin-elf-nm`, `verify_bootloader.py` passes, and only `OP1_vdk.ldr` differs from seq3.

**Procedure:** Test 23's, from point 3 (holding keys, then assign). Also check shift + keyboard keys and the octave keys on the Grids screen.

**Seen on seq3 (2026-10-17):** some Marbles and Grids graphics drew stray lines between vertices, for example Marbles' keyboard icon. **Cause:** these paths have several subpaths (`M…z M…`), and the OP-1 joins them with a line. None of the 61 stock screens ever writes a second `M` in a path. Affected: marbles.svg has 3 paths (`src_keys` and the two undriven arrow icons), and grids.svg has 7 (`dot`, `mode_drums`, `res16`, `res32`, `accent_vel`, `accent_key`, `rnd_on`). Clouds and Rings have none.

## Test 23c: one subpath per path (`op1_246_rings_seq5.op1`)

**Not flashed yet.** seq4 plus the following:
- `gen_marbles_svg.py` and `gen_grids_svg.py` split every multi-subpath path into one path per subpath. A driven id stays on the first part, and the others are `id_2`…`id_9`. The scripts refuse a relative `m` and check that no path is left with two subpaths.
- The screen code finds every part and applies visibility, transforms (the Grids dot) and colours (`rnd_on`, `src_keys`) to all of them.
- The new SVGs equal the shipped ones with only the split applied (checked by script).
- `verify_bootloader.py` passes. The hook targets match `bfin-elf-nm`. Against seq4, only `OP1_vdk.ldr`, `marbles.svg` and `grids.svg` differ.

**Procedure:** check that the stray lines are gone: Marbles' keyboard icon and its arrows; Grids' dot, mode grid, 16/32 bars, accent and random icons. Then check that the driven ones still change: the keys icon lights when selected, the dot moves and hides in Euclidean, the mode/resolution/accent icons swap, and random lights up. Then carry on with Test 23b.

**Result (seq5, 2026-10-17): parser error at boot.** **Cause:** the designs write `<path …></path>`, and the split copied only the start tag. So N paths shared one `</path>`, and the XML wasn't well-formed: both files fail Python's XML parser at the first split path. **A second, latent problem:** the parser also rewrites ids, and in an id with two underscores it deletes everything from one `_` to the next (`0x19fba10`, beside the `_xHH_` decoding). So `accent_vel_2` and `accent_key_2` would both have become `accent2`. Recovery: COM at power-on, then clouds4 or seq4.

## Test 23c (again): `op1_246_rings_seq6.op1`

**Not flashed yet.** seq5 corrected:
- The split now copies whole elements, so every part is a complete `<path …></path>`.
- The extra parts are named `idP2`…`idP9`, with no underscore. The screen code looks for those names.
- Both gen scripts check well-formedness with Python's XML parser before writing, refuse ids with two underscores, and check that `<path>` and `</path>` balance.
- Checks:
  - Both new SVGs are, element by element, seq4's with only the split applied.
  - Every SVG in `content/display` parses.
  - `verify_bootloader.py` passes, and the hook targets match `bfin-elf-nm`.
  - Against seq4, only `OP1_vdk.ldr`, `marbles.svg` and `grids.svg` differ.

**Procedure:** as Test 23c above.

## Test 24: Marbles colour pass (`op1_246_rings_seq7.op1`)

**Not flashed yet.** seq6 plus the user's colour-coherence changes to Marbles (2026-10-17):
- **History:** no frame.
- **Chip box (model/scale, the blue click):** the frame stays. The marker is blue `#638BFF`, and the unselected dots/bars are the frame blue `#3854A5`.
- **Pitch source box:** white frame. **The white click now toggles scale/keys, and shift + white turns the page** (swapped).
- **Legend:** no frame. **Green encoder = deja vu (page T) / length (page X); orange (red) encoder = bias** (swapped), with the icons and bars swapped to match. The green and orange clicks are unchanged: green cycles deja vu off/on/locked, and orange swaps Marbles/tombola.
- **Deja vu brackets in green:** dim green `#13704D` when on, green `#00EE92` when locked. The source icons keep white and grey (their colours are now read from `src_scale`/`src_keys`, no longer from the brackets).

The design source `ports/marbles/op1/marbles.svg` was edited by script (the icons moved 156 px between slots; nothing else moved). `make sim-seq` passes on host and Blackfin (white = keys mode, shift + white = page). The image has no undefined symbols or constructors. All 36 word hooks are function starts in `bfin-elf-nm`. `verify_bootloader.py` passes. Against seq6, only `OP1_vdk.ldr` and `marbles.svg` differ.

**Procedure:**
1. Do the frames and colours look right on both pages?
2. White click toggles scale/keys; shift + white turns the page.
3. Green turns deja vu (T) or length (X), and the green brackets follow. Orange turns bias.
4. Clicks: blue changes model/scale (the blue marker moves), green cycles the brackets off → dim green → green, and orange swaps to tombola.
5. Seq6's checks if not done yet (Test 23c: icons without stray lines; Test 23b: Grids keys and assign).

## Test 25: Rings follows the LFO, no CPU meter (`op1_246_rings_seq8.op1`)

**Result (2026-10-18): all five points good.** seq7 plus the following:
- **The Rings screen shows the knobs after the LFO,** as stock screens do. Stock's `ReadInterface` (`0x1a28210`) takes the Core's per-voice record of the modulated knobs: 8 × int16 at `0xff8010b8`, then the slot, the engine type and a flag, with new-flag `B[0xff8010d4]`. `0x196749c` copies a new record into `0x1ab321c`, and the record is used when its slot and type match and its flag is 0. `op1/screen.cc` does the same for knobs 0..3 (structure rings, brightness, damping, position/pluck). Knobs 4..7 (model, polyphony, engine) stay live. When no new record has come for 8 frames (nothing playing), it shows the live knobs again.
- **The CPU meter is gone:** `op1/cpu_meter.h`, its timing in the entry points and the white click. The white click is now free (stock ignores it). The Clouds governor's cycle count is unchanged. `sim-engine` turns on the coefficient-update profiler itself.
- **Screen:** no frame around the main window; the polyphony frame is green (the green click picks polyphony). Both are done in `gen_rings_svg.py`.
- Checks:
  - `op1-engine`, `op1-clouds` and `op1-seq` build.
  - `test-screen` passes, and the modal checksum is still `ef8618dd`.
  - All 36 word hooks are function starts. The imm16 pair is `kTypeListQuery`.
  - `verify_bootloader.py` passes, and every display SVG parses.
  - Against seq7, only `OP1_vdk.ldr` and `rings.svg` differ.

**Procedure:**
1. A MULTI slot on Rings with an LFO (value or random) on the engine, targeting one of the four encoders. While notes play, does the matching drawing move (rings, brightness or damping handle, pluck position)? When notes stop, does it settle back to the knob's value within a moment?
2. Without an LFO: does every drawing follow its encoder as before, with and without notes?
3. White click on the Rings page: nothing happens (no meter).
4. The look: no main frame, and the green polyphony frame.
5. Also stock phase in the MULTI slot with the LFO, to confirm nothing changed there.

## Test 26: Clouds frames, first beta (`op1_246_rings_seq9.op1`)

**Result (2026-10-18): everything looks good. seq9 is the first beta.** seq8 with only `clouds.svg` changed (`gen_clouds_svg.py`): the main window's frame is white (#DED7FF) and the bottom-right window has no frame. The image is seq8's. `verify_bootloader.py` passes and every display SVG parses. Against seq8, only `clouds.svg` differs.

**Procedure:**
1. Clouds on the phone effect slot: the main window has a white frame, the top-right window keeps its blue frame, and the bottom-right window has none.
2. A quick look at the rest (Rings, Marbles, Grids, Clouds sound) to confirm it boots and behaves like seq8.

## Test 27: Rings' buffers in L1 (`op1_246_rings_seq10.op1`)

**Result (2026-10-18):** 1. the tape animation is still just as choppy; 2. a bit better with 4 voices; 3. Rings + Clouds the same as before; 4. every model sounds the same; 5. the rest the same. **So the SDRAM stalls on these buffers were not the main cost:** either the data cache is on after all, or the time goes elsewhere (our other SDRAM state, the OP-1's own per-voice work, the clock). The change is kept (bit-identical, slightly better). Next: measure on the device instead of inferring.

**Built and flashed:** seq9 plus one change: Rings' per-call buffers move from SDRAM to the L1 scratchpad. Aimed at the user's two reports after Test 25: the tape animation gets choppy while recording Rings with more than three voices (the audio records fine), and Clouds and Rings together overload.

**Why:** the OP-1 runs **without a data cache** [I, strong]. Both cacheable halves of L1 data are used as SRAM: code references `0xff804xxx`–`0xff806xxx` (bank A's cache half) and the LDR loads bank B up to `0xff907e00` (its cache half is `0xff904000`+). So every SDRAM access by our DSP code stalls on the bus. The simulator models no memory latency, which would explain the device costing 1.6–4× the simulated cycles (Tests 7, 16). The mode bank reads and writes its output buffer once per mode and sample (6 voices × 14 modes × 44.1 kHz, ~3.7 M times a second), and the string kernels do the same with their mono sum per string.

**What moved** (`ports/rings/src/scratch.h`, `0xffb000c0`–`0xffb005bf`, 1280 bytes): the voice's 64-sample block and its reverb share (engine), the upsampler's input, the modal sub-block's excitation, scaled input and the mode bank's halved input (it was on the stack), and the string models' excitation, sympathetic and mono buffers (modal, strings and upsampler share one union). The scratchpad holds the live synth engine's voice objects from `0xffb00000` (stock phase's, 6 × 28 bytes, whenever our render runs), the effect objects from `0xffb00600` and the LFO objects from `0xffb00a00`; every scratchpad reference in the readable code is one of those. Nothing in the scratch outlives a render call.

- Checks:
  - All six models are **bit-identical** to the committed build in the simulator (`sim-model` M=0..5: `ef8618dd`, `4ab50a82`, `8d5a070f`, `567afc0c`, `97c1539d`, `7d5e0bee`), also with the scratch at its L1 address.
  - `bench-mode-bank` (assembly = C = host), `bench-string`, `test-string`, `test-reverb-bus`, `step-params`, `test-alloc` and `test-screen` pass.
  - `op1-engine`, `op1-clouds` and `op1-seq` build. All 36 word hooks are function starts; the imm16 pair is `kTypeListQuery`. `verify_bootloader.py` passes. Against seq9, only `OP1_vdk.ldr` differs.
- **Risk:** encrypted L1 code might use the scratchpad between `0xc0` and `0x5c0` too (none of its readable fragments does). That would show as noise, wrong sounds or a crash while Rings plays.

**Procedure:**
1. Rings (modal, polyphony "all") in a MULTI slot, recording to tape, chords of 4–6 notes: is the tape animation smooth now, or less choppy than seq9?
2. The same with the other models (strings, model 6 in polyphony 4).
3. Rings + Clouds on the synth effect, then on the master too: better than seq9 (less or no crackle)?
4. Does every model sound exactly as before? Listen for anything new: noise, garbage, clicks. Also switch slots (stock engines, MULTI on phase) and to drums and back while notes ring.
5. A quick look at the rest (Marbles, Grids, Clouds alone).

**If something is wrong:** power off; flash seq9 back through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 28: CPU probe (`op1_246_rings_cpuprobe1.op1`)

**Results (2026-10-18, photos in `incoming/Test 28/`):** Rings modal, polyphony "all".

| Case | 0 clock | 1 Rings MHz | 2 peak ‰ | 3 Clouds MHz | 4 fps | 5 gap ms | 6 voices | 7 sweep | 8 hot | 9 L1 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 idle | – | – | – | – | 42 | 24 | – | 116.0 | 12.0 | 12.0 |
| 2 one note | 399 | 28.8 | 286 | 0 | 47 | 24 | 1.0 | 209.0 | 12.0 | 12.0 |
| 3 three notes | 399 | 40.6 | 447 | 0 | 47 | 24 | 2.2 | 115.8 | 12.0 | 12.0 |
| 4 four notes | 399 | 102.6 | 313 | 0 | 47 | 24 | 6.0 | 116.0 | 12.0 | 12.0 |
| 5 six notes | 399 | 147.3 | 431 | 0 | 47 | 25 | 6.0 | 609.9 | 12.0 | 12.0 |
| 6 six, recording | 399 | 155.1 | 448 | 0 | 33 | 48 | 6.0 | 576.3 | 12.5 | 12.0 |
| 7 four + Clouds | 399 | 109.0 | 532 | 81.2 | 27 | 71 | 4.5 | 168.1 | 12.0 | 12.0 |
| 8 six, sympathetic strings | 399 | 148.0 | 751 | 0 | 41 | 46 | 4.0 | 150.5 | 12.0 | 12.0 |

**What they show:**
- **The clock is 399 MHz, and the OP-1 has a data cache:** 1 KB of SDRAM read repeatedly costs the same as L1 (12.0 = the loop itself). That's why seq10 changed little (Test 27); the no-cache inference was wrong. A **cache miss costs ~100 cycles** (the idle sweep, 116 − 12); the higher sweep values are runs interrupted by the audio (1024 misses take longer than a block).
- **Rings costs about what the simulator says:** ~25 MHz per modal voice (six voices 147 MHz; `bench-models` 20.9 MHz for a 16-mode voice before the upsampler, smoother and OP-1 glue). The earlier "1.6× the simulator" came from the CPU meter's indirect readings. So simulator savings carry over.
- Voices: with four notes in "all", the OP-1 still renders six voices (released notes ring on, the OP-1's envelope release keeps them alive): four notes cost as much as six.
- **The screen is starved, not the audio:** 47 fps whenever Rings plays alone, even at 147 MHz; recording drops it to 33 fps with 48 ms gaps (case 6), and Clouds (81 MHz on the device, ~2× its simulated cost: cache misses on its large buffers) plus Rings drops it to 27 fps with 71 ms gaps (case 7).
- Peaks: a single block can take 29–75% of the block period (note-ons and coefficient updates; strings worst), so peaks, not the average, decide audio glitches when Clouds is added.

**Built:** A diagnostic build: seq10 plus `ports/rings/op1/cpu_probe.cc` (`make op1-cpuprobe`). Test 27 showed the L1 buffers weren't the main cost, so this measures instead of inferring. **The white click on a Rings page** swaps the drawing for a readout of ten numbers (the sequencer probe's `seqprobe.svg`, added to the package; index on the left, two columns). Values are for the last second and hold when nothing plays. Everything else is seq10.

| # | Value |
|---|---|
| 0 | core clock, MHz |
| 1 | Rings, MHz × 10 (all MULTI render and note-on time) |
| 2 | the busiest block's Rings time, ‰ of the block period |
| 3 | Clouds, MHz × 10 |
| 4 | screen frames per second |
| 5 | longest gap between two frames, ms |
| 6 | Rings voices rendered per block × 10 |
| 7 | SDRAM, 64 KB sweep: cycles per load × 10 (cache misses) |
| 8 | SDRAM, 1 KB read repeatedly: cycles per load × 10 (≈ 9 with a data cache, ≈ 7 without) |
| 9 | L1 scratchpad, same loop: cycles per load × 10 (the loop's own cost) |

- Checks: `op1-cpuprobe` and `op1-seq` build, no undefined symbols, no constructors; the 36 word hooks are function starts and the imm16 pair is `kTypeListQuery`; `sim-engine` still `ef8618dd`; `verify_bootloader.py` passes; `seqprobe.svg` is byte-identical to the one seqprobe1 booted with. Against seq10: `OP1_vdk.ldr` and the added `seqprobe.svg`.

**Procedure** (Rings modal, polyphony "all", in a MULTI slot; white click to show the readout; wait ~2 s per case, then note all ten numbers):
1. Nothing playing (after playing something, so values exist; just note 7, 8, 9).
2. One note held.
3. Three notes held.
4. Four notes held.
5. Six notes held.
6. Six notes held while recording to tape (is it choppy now, and do 4/5 change?).
7. Four notes held with Clouds on the synth effect (granular).
8. Optional: case 5 with a string model (button 2).

A photo of the screen per case is as good as typing the numbers. White click again for the drawing.

**If something is wrong:** power off; flash seq10 or seq9 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 29: Rings mode budget, earlier sleep, Clouds yields (`op1_246_rings_cpuprobe2.op1`)

**Result (2026-10-18): all three changes worsened the sound of Rings and Clouds (the user couldn't single one out). Reverted; the user flashed seq10 back. The user's priority: Rings + Clouds (granular, master) together at full quality; the animation is second.** The user chose three savings after Test 28. `op1_246_rings_cpuprobe2.op1` has them plus the Test 28 readout (white click on a Rings page). `op1_246_rings_seq11.op1` is the same without the readout (the next beta if this works).
- **Earlier sleep:** a Rings voice's tail stops computing below ~ -72 dBFS for ~12 ms (was -96 dBFS; `RingsVoice::kSleepLevel`). A sleeping voice now skips the whole render path (`op1/engine.cc`), except string + reverb's carriers, which play the shared reverb.
- **Mode budget in "all"** (modal model, `TrimVoices` in `op1/engine.cc`): the voices still ringing share 60 modes (Rings' own budget, polyphony 1). A chord's notes (struck within ~50 ms) share equally, up to 14 each (six-note chord: 10 each). Older tails take what's left, newest first, down to 4 each. A voice only loses modes at a note-on (masked by the strike) and gets its full count back when struck again. Modes that come back start from rest (`ModalResonator::set_resolution`). Simulator: six ringing voices 122.8 → 96.3 MHz; turning 141.9 → 116.7.
- **Clouds yields:** the governor's share for Clouds plus our engines is 0.40 of the period (was 0.75; `kShare` in `clouds_fx.cc`). From Test 28, 37% kept the screen at 47 fps and 48% dropped it to 27. With six Rings voices (~100 MHz) Clouds gets ~60 MHz: fewer grains. Clouds alone (~81 MHz, 20%) is untouched.
- Checks:
  - One voice still `ef8618dd`, and every model's single-voice checksum is unchanged.
  - Clouds `sim-fx` checksums match the committed build.
  - `test-governor`, `test-alloc`, `step-params`, `test-screen` and `bench-mode-bank` pass.
  - All four images build. Hooks are function starts (the imm16 pair is `kTypeListQuery`), `verify_bootloader.py` passes, and only `OP1_vdk.ldr` differs from cpuprobe1 / seq10.

**Procedure** (cpuprobe2; Rings modal, polyphony "all"; readout on with the white click; note the numbers as in Test 28):
1. Repeat Test 28's cases 4, 5, 6 and 7 (four notes, six notes, six recording, four + Clouds granular). Is the tape animation smooth now while recording with 4–6 voices?
2. Listen: do six-note chords and long tails still sound right? Any clicks when a new note strikes over ringing tails, or tails that end too early?
3. Rings + Clouds on the synth effect, then on the master too: does it play well together now? Does Clouds sound thin while Rings is busy?
4. Clouds alone: unchanged?

**If something is wrong:** power off; flash seq10 or seq9 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 30: CPU probe v3, stages and caches (`op1_246_rings_cpuprobe3.op1`)

**Not flashed yet.** seq10's sound (the Test 29 changes reverted; committed as `3f4919b`) plus the probe's third version (`op1/cpu_probe.cc`, `op1/sled_bfin.S`, Clouds' `src/core/profile.h`). The question: Clouds costs 3-5× its simulated cycles on the device; is that instruction cache misses (Clouds' code is 30 KB, Rings' 29 KB, the cache 16 KB) or pipeline stalls in its branchy C? The fix differs. **The white click on a Rings page cycles: drawing → page 0 → page 1 → page 2 → drawing.** Values cover the last second; MHz × 10 unless said.

| Page 0 (Test 28's) | Page 1: Clouds | Page 2: Rings and the caches |
|---|---|---|
| 0 clock MHz | 0 Clouds, both instances | 0 Rings |
| 1 Rings | 1 master (stereo) | 1 mode bank |
| 2 busiest block ‰ | 2 synth (mono) | 2 coefficient updates |
| 3 Clouds | 3 decimator + interpolator + mix | 3 rest of the voice |
| 4 frames/s | 4 grain scheduling | 4 glue (smoothing, upsampler, fades) |
| 5 longest frame gap ms | 5 overlap-add (grain kernels) | 5 instruction cache: miss cycles per 32-byte line |
| 6 voices/block × 10 | 6 feedback + record write | 6 12 KB code warm: cycles/instr × 10 (10 = fits) |
| 7 SDRAM 64 KB sweep | 7 diffuser + pitch + filters + reverb | 7 32 KB code warm: cycles/instr × 10 |
| 8 SDRAM 1 KB hot | 8 active grains: master × 100 + synth | 8 SDRAM 24 KB hot: cycles/load × 10 |
| 9 L1 hot | 9 grain cap: master × 100 + synth | 9 SDRAM 12 KB hot |

Page 2's cache tests run on the Gfx thread only while page 2 shows (a few ms a second). Checks: `op1-cpuprobe` and `op1-seq` build; `sim-engine` `ef8618dd`, Clouds `sim-fx` checksums unchanged; hooks are function starts and the imm16 pair is `kTypeListQuery`; `verify_bootloader.py` passes. Against cpuprobe1 only `OP1_vdk.ldr` differs.

**Procedure** (photos as before; wait ~2 s per case; white click to the page):
1. Nothing playing: page 2 (the cache numbers 5-9 are what matter here).
2. Rings modal "all", six notes held: page 2, then page 0.
3. Clouds granular on the **master**, nothing playing but Clouds running (e.g. after a note, with reverb/density where you normally use them): page 1.
4. Rings six notes + Clouds granular on the master: page 1, then page 0.
5. The same with Clouds on the synth effect instead of the master: page 1.
6. Optional: case 4 with density turned up high (many grains): page 1.

**If something is wrong:** power off; flash seq10 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.
