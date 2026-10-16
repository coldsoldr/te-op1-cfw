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
