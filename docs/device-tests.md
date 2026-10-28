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

**Results (2026-10-18, photos in `incoming/Test 30/`; the user also saw page 2's 5 bounce 99-191, 7 72-130, 8 844-1741 with nothing playing):**

| Case | Page | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 nothing playing (held values) | 2 | 1622 | 1125 | 0 | 361 | 136 | 99 | 10 | 72 | 1123 | 230 |
| 2 Rings six notes | 0 | 399 | 1609 | 461 | 0 | 47 | 25 | 60 | 1268 | 231 | 231 |
| 2 Rings six notes | 2 | 1611 | 1124 | 0 | 355 | 132 | 188 | 12 | 346 | 6129 | 230 |
| 3 Clouds granular, master, no notes | 1 | 852 | 852 | 0 | 112 | 65 | 0 | 142 | 514 | 0 | 100 |
| 4 Rings six + Clouds master | 0 | 417 | 1823 | 488 | 986 | 14 | 160 | 60 | 5675 | 231 | 231 |
| 4 Rings six + Clouds master | 1 | 989 | 989 | 0 | 134 | 69 | 0 | 163 | 603 | 0 | 100 |
| 5 Rings six + Clouds synth | 1 | 788 | 0 | 788 | 94 | 204 | 46 | 142 | 284 | 1 | 101 |
| 6 case 4, density high | 1 | 1004 | 1004 | 0 | 140 | 71 | 0 | 167 | 605 | 1 | 101 |

(The probe's load loop costs 23 cycles this time, page 0's 8 and 9: v3's loop has a compare instead of a mask. Case 4 page 0's clock reads 417: the window ran long.)

**What they show:**
- **Caches:** both 16 KB. A line miss is ~100 cycles idle (page 2's 5 = 99; 64 KB sweep 127 − 23) and up to ~190 with the audio running. 12 KB of code or data fits (6 = 10, 9 = 23 = the loop); 24 KB of data and 32 KB of code don't (8 = 112 per load, 7 = 7.2 per NOP). Under load the sled tests include audio interrupts (7 = 346, 8 = 6129): ignore those.
- **Rings (161 MHz, six modal voices):** the mode bank is 112 MHz (the simulator: 88, so 1.28×: pipeline stalls in the 23-instruction loop, since its data is in L1 now), the rest of the voices 36, the glue (smoothing, upsampler, fades) 13. No coefficient updates while the knobs are still.
- **Clouds on the master costs 85-100 MHz with no grains at all:** diffuser/reverb stage 51-60 MHz, feedback + record write 14-17, decimator/interpolator/mix 11-14, grain scheduling 6.5-7, overlap-add 0. Density high changed nothing (case 6 = case 4).
- **The governor's grain cap reads 1 on both instances** (9 = 100 / 101), and active grains 0-1. If right, Clouds has been playing at most one grain: the lateness detector fires and `Cut` assumes Clouds' cycles scale with the cap, but ~80 MHz of its cost is fixed, so the cap collapses to 1 and the ceiling holds it there. To verify (late-call count, ceiling) and to hear the difference, next.
- Rings + Clouds master together (case 4): 182 + 99 MHz = 70% of the CPU, frames 14/s with 160 ms gaps: the screen is starved and the audio near its edge (peak block 49% for Rings alone).

**Against the simulator, stage by stage** (`make sim-fx` built with `-DOP1CLOUDS_PROFILE` prints the stages; scenario `master_gr_full`: granular, density .8, texture .9, feedback .3, reverb .5; `master_ld` for looping delay):

| Stage | Simulator, granular full | Simulator, looping delay + reverb + diffusion | Device, master (cases 3-6) | Ratio |
|---|---|---|---|---|
| decimator + interpolator + mix | 5.3 | 5.3 | 11-14 | 2.1-2.6 |
| player less overlap (scheduling / looper) | 3.6 | 3.0 | 6.5-7 | 1.8-2.3 |
| overlap-add (4 grains) | 8.6 | 0 | 0 | — |
| feedback + record | 4.8 | ~4.8 | 14-17 | 3-3.5 |
| diffuser + pitch shifter + filters + reverb | 5.5 | ~15 | 51-60 | 3.5-4 (looping delay) or 9-11 (granular) |

Every stage is 2-4× the simulator, whatever its algorithm (the asm kernels included), with one exception: if the master was in granular mode, the diffuser/reverb stage is 9-11×, which no cache model explains; if it was in **looping delay** mode (0 grains, the overlap-add at 0, density changing nothing, all as measured), it's 3.5-4× like the rest. So the master was most likely in looping delay during Test 30 (to confirm with the user). Either way the reading of the governor's cap (1) stands: with ~85 MHz of Clouds' cost fixed (not proportional to grains), `Cut` can't bring its cycles under the ceiling, so one late call or one Rings peak collapses the cap to 1, and the ceiling holds it there for ~17 s. Granular next to Rings therefore plays with 0-1 grains.

**Reading:** a uniform 2-4× across stages, with 16 KB caches and a 100-190-cycle miss, means Clouds runs with cold caches every call: its per-call code (ProcessBlock 6 KB, Play 3.2 KB, Process 2.7 KB, Schedule 3.8 KB, the kernels) plus Rings' (~10 KB) exceed the 16 KB instruction cache, and its ~22 reverb streams, the diffuser's 16, the record buffer and the scratch arrays exceed the data cache between calls. Levers, in order: (1) the governor's cut must act on the grain-proportional cost only, and recover fast; (2) shrink Clouds' per-call code path (parameter math only when a value changes; the 6 KB ProcessBlock is mostly inlined software-float work) so Rings' and Clouds' hot code both stay resident; (3) if still short, process two OP-1 calls per core run (halves the cache refills, +1.45 ms latency); (4) Rings' mode bank is 1.28× the simulator from pipeline stalls: interleave two modes per loop (bit-exact), ~20 MHz.

**Built:** seq10's sound (the Test 29 changes reverted; committed as `3f4919b`) plus the probe's third version (`op1/cpu_probe.cc`, `op1/sled_bfin.S`, Clouds' `src/core/profile.h`). The question: Clouds costs 3-5× its simulated cycles on the device; is that instruction cache misses (Clouds' code is 30 KB, Rings' 29 KB, the cache 16 KB) or pipeline stalls in its branchy C? The fix differs. **The white click on a Rings page cycles: drawing → page 0 → page 1 → page 2 → drawing.** Values cover the last second; MHz × 10 unless said.

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

## Test 31: cpuprobe3 again, the master in granular mode

**Results (2026-10-18, photos in `incoming/Test 31/`).** The user confirmed the master was in looping delay during Test 30. Cases 1 and 2 (Clouds alone) showed held values: the probe's window only advanced in RingsRender (fixed in v4: the window runs from the screen thread).

| Case | Page | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 3 Rings six + Clouds granular master, usual settings | 0 | 399 | 1825 | 498 | 820 | 13 | 162 | 60 | 5542 | 231 | 231 |
| 4 the same, density high | 0 | 399 | 1824 | 500 | 844 | 13 | 135 | 60 | 1465 | 231 | 231 |
| 4 the same, density high | 1 | 853 | 853 | 0 | 142 | 0? (cropped; the sum says ~200) | 59 | 178 | 275 | 100 | 100 |

**What they show:**
- **The governor's cap is 1 next to Rings** (9 = 100: master cap 1), one grain active (8 = 100), whatever the density. Confirmed: granular next to Rings plays one grain. The overlap-add of that one stereo grain costs 5.9 MHz on the device (the simulator: ~1.4 per grain).
- Clouds granular master at one grain: 85 MHz: resampling + mix 14.2, scheduling ~20 (by the sum; Test 30's synth case read 20.4), overlap-add 5.9, feedback + record 17.8, diffuser + reverb 27.5. The simulator for the same settings: 5.3 / 3.6 / 1.4 / 4.8 / 5.5. Scheduling is the worst ratio (~5×): a per-sample loop with a random call, software-float divisions per call, and `Schedule`'s software-float work on every new grain (short grains at cap 1 schedule one almost every call).
- **Rings next to Clouds costs 182 MHz, 21 more than alone** (Test 30: 161): the instruction cache contention goes both ways.
- Together: 182 + 85 = 267 MHz, 67% of the CPU, 13 frames/s, gaps 135-162 ms; Rings' peak block 50%.

## Test 32: governor fix and Clouds' parameter caches (`op1_246_rings_cpuprobe4.op1`)

**Results (2026-10-18, photos in `incoming/Test 32/`):** pops, clicks and crackles, typically at note onsets; page 2's master late-call count climbed into the thousands within a minute.

| Case | Page | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 Clouds alone, usual | 1 | 499 | 499 | 0 | 98 | 68 | 14 | 76 | 192 | 0 | 100 |
| 2 Clouds alone, density high | 1 | 477 | 477 | 0 | 95 | 66 | 43 | 75 | 184 | 100 | 100 |
| 3 Rings six + Clouds usual | 1 | 639 | 639 | 0 | 119 | 116 | 27 | 113 | 244 | 0 | 100 |
| 3 | 0 | 399 | 1767 | 486 | 675 | 20 | 111 | 60 | — | — | — |
| 4 Rings six + Clouds dense | 1 | 723 | 723 | 0 | 121 | 117 | 79 | 117 | 273 | 100 | 100 |
| 4 | 0 | 399 | 1785 | 483 | 736 | 16 | 158 | 60 | — | — | — |

- **The cap was still 1, even with Clouds alone:** the lateness detector (the mean call interval against a learnt period) fired thousands of times on the firmware's own call jitter, and its ceiling pinned the cap. The learnt period (the smallest mean seen) is biased low by that jitter; a 1/1024 threshold then trips constantly. Replaced (cpuprobe5): an overrun is a call that comes **two audio blocks** after the previous one (the firmware's block counter at `0xff8016e0`), which is what actually crackles.
- The caches helped: Clouds alone 48-50 MHz; next to Rings 64-74 (Test 31: 85); Rings 177-179 (182); 16-20 frames/s (13). Next to Rings every Clouds stage costs ~1.5x its alone figure (Rings' code and data evict it).
- Still at the edge: Rings 178 + Clouds 70 = 62% of the CPU before the firmware's own work (~25% idle, more with six voices playing): the onset pops are real overruns, which a governor can't fix at one grain. The fixed costs must come down (Rings' mode bank and glue, Clouds' remaining per-call code).

**Built:** seq10's sound plus two changes, and the probe (`op1_246_rings_seq11.op1` is the same without the readout):
- **The governor** (`src/core/governor.h`) tracks Clouds' fixed cost and its cost per grain apart (the granular player times its overlap-add) and sets the cap to what the grain budget buys: `cap = (share x period - engines - fixed) / per_grain`. A smaller cap applies at once; a larger one grows a grain every 8 calls (~12 ms). Late calls: only a mean interval over ~16 calls above the learnt period + 1/1024 sets a ceiling (on the grain cycles, 7/8), which relaxes slowly and re-tests every few seconds. `test-governor` (with the device's costs): no late calls with measured load, one short burst per re-test with hidden load, full cap back within 0.4 s of Rings going. **So granular next to Rings now plays as many grains as fit instead of one: a denser Clouds than seq9/seq10 next to Rings, by design.**
- **Clouds' parameter maths runs only when a value changes** (`granular_player.cc`: the density/texture/hint-derived seeding values, the per-grain size/pitch/position geometry and the width/window envelope; `clouds_core.cc`: the feedback, texture and pitch-shifter coefficients). Bit-identical: every `sim-fx` checksum is unchanged. The traced per-call code footprint (`make hot-set`, `tools/hot_set.py`) went from 310 to 226 lines (9.7 to 7.1 KB) in steady state; Rings' six voices are 134 lines (4.2 KB).
- The probe's window now runs from the screen thread (Clouds-alone cases read live); page 2's 8 and 9 are the master's and the synth's late-call counts.
- Checks: `test-governor`, `test` (kernels) and `sim-fx` pass; `sim-engine` `ef8618dd`; `op1-seq` and `op1-cpuprobe` build; hooks are function starts, the imm16 pair is `kTypeListQuery`; `verify_bootloader.py` passes.

**Procedure** (as Test 31, page 1 unless said; the sound matters as much as the numbers):
1. Clouds granular on the master, usual settings, nothing playing: page 1 (now live).
2. The same with density high.
3. Rings six notes + case 1: page 1 and page 0.
4. Rings six notes + case 2: page 1 and page 0. Numbers 8 (grains) and 9 (cap): the cap should now be well above 1 and move with the load.
5. Listen: Clouds granular next to Rings: denser than before? Any crackle? Does the grain count audibly pump as the cap moves (it shouldn't: a grain more every 12 ms)?
6. Page 2's 8 (master late calls): note it after a minute of playing Rings + Clouds. A handful is fine; hundreds mean the lateness detector fires on the firmware's own jitter.

**If something is wrong:** power off; flash seq10 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 33: lateness by the block counter (`op1_246_rings_cpuprobe5.op1`)

**Results (2026-10-19, photos in `incoming/Test 33/`):** page 2's 8 stayed at 0; page 1's 9 settles at 101 when Rings plays; no pops without Clouds.

| Case | Page | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 Clouds alone, usual | 1 | 1001 | 481 | 520 | 158 | ? | 114 | 123 | 386 | 105 | 1940 |
| 2 Clouds alone, density high | 1 | 1350 | 821 | 529 | 160 | 205 | 424 | 130 | 407 | 2409 | 3239 |
| 3 Rings six + Clouds usual | 1 | 1056 | 498 | 558 | 181 | 209 | 76 | 145 | 418 | 201 | 101 |
| 3 | 0 | **460** | 1591 | 407 | 1028 | 26 | 74 | 60 | 5641 | 231 | 231 |
| 4 Rings six + Clouds dense | 1 | 1048 | 524 | 524 | 171 | ? | 122 | 138 | 394 | 202 | 101 |
| 4 | 0 | **452** | 1639 | 623 | 1071 | 20 | 212 | 60 | 5874 | 231 | 231 |

- **A synth Clouds instance ran in every case** (page 1's 2: 52-56 MHz, 5-9 grains), which Test 32 didn't have (its 2 read 0). Clouds "alone" was 100 MHz for the two instances; with Rings, the load is Test 32's plus ~52 MHz.
- **Clouds alone works as designed:** at density high the master's cap is at its maximum (32), 24 grains play, 82 MHz for the master; at the usual settings one grain plays (cap 19: the cost per grain is overstated while only one grain runs; it settles as grains are added).
- **The block counter can't see the overload.** Page 0's clock read 460 and 452 for a 399 MHz core: the probe divides a second's cycles by the blocks the counter advanced, so the counter advanced 13-15% less than real time. The counter counts the blocks the audio thread processes (`0xffa01988`, the thread's per-block callback), so when the thread falls behind the firmware drops blocks and the counter never skips: page 2's 8 stayed at 0 while 13% of the audio was dropped (the pops). Rings' and Clouds' MHz readings in cases 3-4 are inflated by the same factor (Rings ~140 real, 159 read), and the screen rate improves (26 frames/s) because the audio thread does less.
- **The cap is 1 next to Rings by the budget** (0.70 x period less Rings, the synth instance and the master's fixed cost leaves nothing), not by the lateness ceiling. Right, and still an overload: the fixed costs are the problem, as Test 32 said.
- **No pops with Rings alone:** Rings' own peak fits.
- EE-197 (ADI's latency note for this core, `incoming/EE-197.pdf`, untracked; the gist in `docs/rings-op1-reference.md`): the MAC/ALU/shifter chain has no pipeline latencies, so the mode bank's 30 cycles per mode and sample on the device against 23.7 simulated are memory: the modes' cache lines (SDRAM behind the 16 KB data cache; a line 100-190 cycles) and a cycle an iteration for a 64-bit first instruction off 64-bit alignment.

**Built (`op1_246_rings_cpuprobe6.op1`, `op1_246_rings_seq13.op1`):** see Test 34.

cpuprobe5 was cpuprobe4 with the governor's overload detection replaced: a call that comes two audio blocks after the previous one counts as an overrun (the mean-interval test fired on jitter and pinned the cap at 1); the share is 0.70 of the period. `op1_246_rings_seq12.op1` is the same without the readout. Bit-identical otherwise (`sim-fx` checksums unchanged; `sim-engine` `ef8618dd`). `test-governor` models the lag: no late calls with measured load, at most a skipped block per re-test of a hidden load.

**Procedure:** as Test 32 (cases 1-4, page 1, plus page 0 for 3 and 4), then:
5. Page 2's 8 (master late calls) after a minute of Rings + Clouds: it should now count real overruns only (tens at most; each is a skipped block).
6. Listen: with Clouds alone the cap should be at its maximum (page 1's 9 = 3200 for the master at 32); next to Rings it should settle where the pops stop, and the sound should be denser than seq10's one grain. Note whether the onset pops remain with Rings alone (no Clouds): that tells whether they are Rings' own peak.

**If something is wrong:** power off; flash seq10 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 34: mode bank prefetch, overloads by the call interval (`op1_246_rings_cpuprobe6.op1`)

**Results (2026-10-19, photos in `incoming/Test 34/`):** pops whenever Rings is at polyphony "all" with Clouds on; Clouds sounded pumpy, tails sometimes cut abruptly. Test 33's synth Clouds was not intentional.

| Step | Page | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 nothing, no Clouds | 2 | 6 | 0 | 0 | 0 | 6 | 261 | 284 | 297 | 0 | 0 |
| 2 master Clouds alone | 2 | 31 | 0 | 0 | 0 | 31 | 263 | 339 | 359 | 0 | 1002 |
| 3 Rings six + master Clouds | 0 | **471** | 2014 | 559 | 527 | 33 | 41 | 60 | 2906 | 231 | 231 |
| 3 | 2 | 2031 | 0 | 0 | 1866 | 165 | 264 | 334 | 346 | 397 | **1094** |
| 4 + synth Clouds | 0 | **445** | 1982 | 627 | 853 | 16 | 122 | 60 | 3029 | 231 | 231 |
| 4 | 2 | 1968 | 0 | 0 | 1812 | 156 | 262 | 336 | 346 | 295 | **1071** |

- **The kernel bench:** new kernel warm 26.1 cycles per mode and sample (24.0 simulated), cold 28.4 idle / 33.4 under load; the old kernel cold 29.7 / 34.6. The prefetch buys ~1.3 cycles (4%): the misses are not the main excess under load, and the model the user plays doesn't run the mode bank at all (page 2's 1 and 2 read 0: a string model; its cost is the "rest" at 186 MHz read).
- **The interval detector works:** 9 read 1094 and 1071 (9% and 7% of the blocks dropped over the last calls), 8 counted overruns (397, 295); the clock on page 0 read 471 and 445 (18% and 11% dropped over its second). With nothing playing it read 1002: no false trips.
- **The budget, per processed block:** Rings six voices 2014 = 292k cycles = **50% of a period**; the master Clouds 527 less the idle synth share = ~70k = 12%; the firmware's own work on top. The pumping and cut tails are the dropped blocks themselves (chunks of audio missing), not the grain cap.
- **Conclusion:** Rings at polyphony "all" (six voices) plus Clouds on the master does not fit this CPU at full quality, and the remaining sound-identical savings are single-digit percent. See the Test 35 proposal. `op1_246_rings_seq13.op1` is the same without the readout. Changes, all sound-identical (`sim-fx` checksums unchanged, `sim-engine` `ef8618dd`, `bench-mode-bank`: assembly = C = host):
- **The mode bank kernel** (`src/mode_bank_bfin.S`) prefetches the mode two ahead at each mode pass (`PREFETCH`, the data cache fills while the current mode's 32 samples run) and 64-bit-aligns its loop tops. Simulated cost 24.0 cycles per mode and sample (23.7 before: the prefetch and an MNOP per mode).
- **The governor** counts an overrun when the mean call interval over ~64 calls is 2% above the nominal period as well as on a skipped block (Test 33: the counter can't see dropped blocks; the interval can; the nominal period and a 2% margin are far above the jitter that caught Test 32's learnt-period version). `test-governor` passes with the thread-behind interval modelled.
- **The probe's page 2:** 5-7 bench the kernel from the screen thread on 28 modes x 32 samples (cycles per mode and sample x 10): 5 the new kernel warm (a second run), 6 the new kernel cold (after a 64 KB sweep evicts the modes), 7 the old kernel cold. 8 is the master's overruns (both kinds), 9 the master's mean call interval in permille of the nominal period (1000 = real time; 1130 = 13% of the blocks dropped). The instruction cache tests are gone (known: 16 KB, a miss 100-190 cycles).

**Procedure:**
1. Nothing playing, no Clouds: page 2. Note 5, 6, 7 (the kernel bench). Expected: 5 ~ 240-260 (the loop alone), 6 well under 7 if the prefetch works. Also page 2's 9 with Clouds granular on the master and nothing else: it should read ~1000 (and tell whether idle calls are evenly spaced).
2. Rings "all", six notes + Clouds granular on the master (usual settings), **no synth Clouds** (check the synth FX slot): page 0 (0 should read 399 if nothing is dropped; more = dropped blocks), page 2 (0 Rings total, 1 mode bank: Test 32 had ~112 MHz for six voices; 8 overruns; 9 the interval permille).
3. The same with the synth Clouds on as in Test 33: page 0 and page 2 again.
4. Listen in 2 and 3: pops?

**If something is wrong:** power off; flash seq10 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 35 (proposal): where the combination does fit (`op1_246_rings_cpuprobe6.op1`, no new build)

Rings' cost is per voice (~8% of a period each), so the question is how many voices fit next to the master Clouds. Same image as Test 34, synth FX slot not Clouds, Clouds granular on the master, usual settings:
1. Rings polyphony 4, four notes held: page 0 (0 should read 399) and page 2 (9 should read ~1000, 8 should stop counting). Pops?
2. If 1 pops: polyphony 2, two notes. The same readings.
3. If 1 is clean: Clouds at density high with polyphony 4. The same readings.
The clean case is the documented limit: the engine stays as it is, the user picks the polyphony.

## Test 36: Marbles and Grids run from the keys (`op1_246_rings_seq14.op1`)

**Not flashed yet.** seq13 plus the sequencer transport the user asked for (2026-10-19): Marbles (tombola, synth) and Grids (pattern, drums) no longer run on their own while the sequencer is on. As the OP-1's own sequencers:
- **Hold a key (or chord) to run.** The run starts on the key press: Marbles' clock and deja vu loops restart there, Grids' bar starts there (step 1 on the press). A tape armed to record on the first key therefore starts with the sequence.
- **Shift while holding latches** (hold): the run goes on after the keys go up.
- **The next key press takes over** (Marbles: a new root in scale mode or a new chord in keys mode; Grids: nothing changes but the transport) and clears the latch, so its release stops the run. The last key up stops it.
- Marbles: a key pressed with shift already down still plays through (sounds as itself, no transport). Grids: keys don't sound themselves any more (no fill-on-hold, no play-through); the assign capture still auditions its key.
- Checks: `sim-seq` and `sim-grids` pass with the transport cases added; `op1-seq` builds; hooks are function starts; `verify_bootloader.py` passes.

**Procedure:**
1. Synth, tombola sequencer on (Marbles): nothing should play until a key is held. Hold a key: it plays; move up the keyboard: the playback pitches up (scale mode); release: it stops. Hold a chord, press shift, release the keys: it keeps running. Press a new key: it takes over; release: it stops.
2. Arm the tape to record, hold a key: the recording and the sequence should start together.
3. Drums, pattern sequencer on (Grids): the same transport with any drum key; the bar's first step should land on the key press; shift while holding latches.
4. Note anything odd: a stuck note, a run that doesn't stop, the first note late.

**If something is wrong:** power off; flash seq13 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

## Test 37: effect categories, Clouds in reverb (`op1_246_rings_fx1.op1`)

**Not flashed yet.** seq14 plus the first step of the effect categories (the user's decision, 2026-10-19; `docs/fx-categories.md`). No new effect yet: this build proves the host, the renames and the Clouds move.
- **The FX list shows the categories:** spring as **reverb**, cwo as **modulation**, punch as **dirt**; delay keeps its name; phone is **phone** again (not "multi"). The stored names are unchanged, so presets and saves keep the stock names.
- **Phone is stock phone only.** Clouds is no longer on it.
- **Clouds is an engine of reverb:** on the reverb (spring) FX page, the orange click steps spring → Clouds → spring (shift: back). Entering Clouds starts it afresh in granular, page 1. Clouds' clicks are unchanged (blue mode, green freeze, white page). Its encoders now use spring's ranges, so the knobs' travel can feel slightly different.
- **Factory presets stay stock:** the engine marker is `values[4]` bit 15, which no stock preset sets. A step back to spring puts spring's factory values in `values[4..7]`.
- **Not carried over:** slots saved with Clouds on phone open as plain phone.
- Encoder-click releases are swallowed only when the press was ours (before: every click release on a phone FX page).
- Checks: `sim-fx` passes, its seven checksums identical to the phone-hosted build's on phone's ranges (the move doesn't change the sound); marker, orange cycle and master click tests added. `op1-seq` and `op1-cpuprobe` build with no undefined symbols and no constructors. The 36 word hooks are function starts, and the spring words match stock in the dump. `verify_bootloader.py` passes. Against seq14 only `OP1_vdk.ldr` differs.

**Procedure:**
1. **The list:** open an effect list (synth FX, drum FX, master FX). Do "reverb", "modulation" and "dirt" fit and read correctly? Is "modulation" cut off? A photo, please.
2. **Stock is stock:** pick reverb, modulation, dirt and delay in turn. Each should show and sound exactly like stock spring, cwo, punch and delay. Phone too.
3. **Clouds on the synth:** synth FX = reverb, orange click. The Clouds screen should appear and Clouds should play (granular). Check blue, green and white as before. Orange again: back to the spring screen and sound.
4. **Clouds on the master:** master FX = reverb, orange click, and the same checks. Also the drum FX page.
5. **Persistence:** with Clouds on a synth slot, switch slots and come back, then power-cycle. Does the slot come back as Clouds? Same question for the master.
6. **LFO:** with an effect LFO on the synth, does reverb/Clouds stay Clouds? (Spring's LFO range reaches the marker words; we read the stored values to avoid that.)
7. Note anything odd: a crash on the reverb page, a wrong screen, clicks doing the wrong thing.

**If something is wrong:** power off; flash seq14 through the te-boot menu, or recovery: hold COM at power-on and flash `op1_246_rings_clouds4.op1`.

**Result (the user, 2026-10-19):**
1. Everything fits nicely in the list.
2. Reverb, modulation, dirt and delay sound the same as stock.
3. Clouds works on reverb.
4. A Clouds slot persists.
5. **Assigning an effect LFO with Clouds as the effect crashed the OP-1, and since then it doesn't boot:** "Invalid synth patch phase:classic … error 24". An LFO on the effect by itself was fine. Separately, the LFO didn't animate the Clouds screen.

**Why:**
- **error 24 is the synth patch check's effect-value range test** (`patch.cpp:390`, check `0x1a037a8`). For each of the 8 effect values it calls `0x1a1df4e(type, k, &value)`, which tests the FX Ranges table (`0x1de5c50`). The first failure returns 24, and the caller stops in the fatal-error handler `0x1919aa8`, which disables interrupts, prints and halts.
- **fx1's marker set bit 15 of `values[4]`, a negative value, outside spring's 0..32767 for `values[4..7]`.** So any synth slot saved with Clouds fails the check at boot. The LFO assignment most likely went through the same check when the patch was saved.
- **No animation:** the mono entry read the slot's stored values for everything, so the effect LFO's modulated copy (`0xff801724`) never reached Clouds' sound or screen.

## Test 38: recovery and the in-range marker (`op1_246_rings_fx2.op1`)

**Not flashed yet.** fx1 with three fixes:
- **The patch check no longer rejects effect values.** One instruction in the check's loop, `CC = R0 == 0` at `0x1a040ac` (`0x0c00`), becomes `CC = R0 < R0` (`0x0880`, never true). It's a new patch kind (`kHalf16`, `tools/inject_app.py` "insn16"); the LDR's bytes were disassembled before and after. **This is what lets the OP-1 boot again with the saved slot.** It's the first instruction patch of SDRAM code; immediate patches of SDRAM code are proven (the list query).
- **The marker stays within 0..32767:** `values[4]` bits 12-14 = 6 (0x6000..0x6fff), bits 8-11 the engine id. Factory values (0, 4000, 8000) don't match it. Clouds' settings are repacked so every word stays in range (`clouds_settings.h`); the precision is the same, except the hidden page's fourth value, which keeps 11 bits.
- **fx1's values are converted** wherever the host meets them: Clouds keeps its mode, page and hidden values; anything else becomes stock spring. The conversion is in RAM: the next knob turn or click on that slot's FX page saves it. Until then the old values stay in the database, which the patched check now accepts.
- **The effect LFO reaches Clouds:** the encoders come from the passed (LFO) copy; the engine and settings still come from the stored values. The screen shows the encoders as last played.
- Checks: `sim-fx` passes with its seven checksums unchanged. New tests: the marker within range, the scenario encoding round trip, and fx1's values migrated (Clouds kept; an unknown engine becomes stock). `op1-seq` and `op1-cpuprobe` build with no undefined symbols and no constructors. The 36 word hooks are function starts. `verify_bootloader.py` passes. Against seq14 only `OP1_vdk.ldr` differs.

**Procedure:**
1. Hold COM at power-on and flash fx2 through the te-boot menu ("1. Upload firmware"). **Does it boot?** If it stops with another "Invalid … patch" message, photograph it and stop there.
2. Go to the slot that had Clouds: is it still Clouds (reverb, Clouds screen)? Turn a knob on its FX page once (that saves the converted values), then power-cycle: does it boot and stay Clouds?
3. **The LFO:** on that synth, assign an effect LFO. Does Clouds' screen move with the LFO (position, size, pitch or density, whichever it modulates)? Does it sound modulated? No crash?
4. Repeat 3 with stock spring (reverb, orange back to spring) to make sure the LFO on spring still works.
5. If you used Clouds on the drum or master FX in Test 37, check those boot and play too.

**If something is wrong:** power off; hold COM at power-on and flash fx2 again. **Until the slot has been re-saved (step 2), only fx2 boots:** stock, clouds4 and seq14 all have the original patch check, so the saved slot would stop their boot too. The fallback beyond that would be te-boot's factory reset, which also wipes the display gamma files (`docs/brick-safety.md`): ask first.

**Result (the user, 2026-10-19):** 1. Boots. 2. The slot is still Clouds and survives a power cycle after a knob turn. 3. The Clouds screen follows the LFO and the sound is modulated, with no crash. 4. The LFO on spring works. 5. Everything is working correctly. **fx2 is the new baseline**, and the user's database has been re-saved, so builds with the stock patch check boot again.

## Test 39: plate (reverb) and crush (dirt) (`op1_246_rings_fx3.op1`)

**Not flashed yet.** fx2 plus the first two new engines.
- **Plate** (reverb: spring → plate → clouds): Dattorro's figure-of-eight plate at 22.05 kHz behind Clouds' halfband filters (`ports/fx/src/plate/`, `docs/plate-port.md`; five assembly kernels, bit-exact against C).
  - Page 1: blue decay, green damp, white pre-delay (0–200 ms), orange mix.
  - Page 2: blue size (8 steps), green diffusion, white motion, orange low cut.
  - Clicks: white swaps the pages, green freezes (not saved).
  - Mix 0 is exactly the dry signal and mix 1 the wet, equal power between. Clouds' crossfade, in contrast, has the dry at −3 dB at its mix 0.
- **Crush** (dirt: punch → crush): sample-rate and bit reduction at 44.1 kHz.
  - Blue: rate, 44.1 kHz → 500 Hz.
  - Green: bits, 16 → 2.
  - White: jitter.
  - Orange: mix.
  - No clicks apart from orange.
- **Simulated cost:**

  | Engine | Stereo | Mono |
  |---|---|---|
  | Plate | 11.4 MHz | 8.3 MHz |
  | Crush | 2.5 MHz | 2.4 MHz |

  - About 5.3 MHz of the plate's stereo cost is the half-rate resampling and the mix, the same as Clouds'.
  - The device has cost Clouds 2–4× its simulated figure (cold caches, Tests 30–31), so the plate may well need 20–40 MHz on the master.
- **Checks:**
  - `sim-fx` passes with Clouds' seven checksums unchanged. New tests:
    - plate: the restart call and mix 0 are dry, a burst leaves a tail (mono and stereo), and two page swaps round-trip in range;
    - the orange click cycles stock → plate → Clouds on the synth and the master.
  - `op1-seq` and `op1-cpuprobe` build with no undefined symbols and no constructors.
  - The image ends at about 1.27 MB of region A's 4 MB.
  - All 42 hook targets are symbols in the image.
  - `verify_bootloader.py` passes.
  - Against fx2, only `OP1_vdk.ldr` differs, and `content/display/plate.svg` and `crush.svg` are added.

**Procedure:**
1. Hold COM at power-on and flash fx3 through the te-boot menu. **Does it boot?** Is the Clouds slot from Test 38 still Clouds?
2. **Plate on a synth:** FX page → reverb, orange click once (spring → plate). Does the plate screen show, with the engine dots on the second dot?
   - **Page 1:** turn each knob. Do the tail (blue decay), the highs' envelope (green damp), the gap before the tail (white pre-delay) and the mix bar follow? Do they sound as named?
   - Mix fully down: is the sound exactly dry, with no level drop?
3. **Page 2:** white click.
   - Does the page indicator move, and do size, diffusion ticks, motion and low cut show and act?
   - On a size step there is a deliberate dip of about 30 ms in the wet signal. Is it smooth, or a click?
   - White again: is page 1 back as you left it?
4. **Freeze:** green click. Does the indicator turn green and the tail hold indefinitely? Click again to release.
5. **Loudness:** decay near the top with a loud held chord. Does it get harsh or distorted? The core saturates above about −10 dBFS input at decay > .9; tell me how it sounds.
6. **Master:** plate on the master FX. Does it play cleanly with a few synth voices? Any crackle?
7. **Crush:** a synth FX page → dirt, orange click once (punch → crush). Do rate, bits, jitter and mix sound and show as named? Shift + orange: back to punch?
8. **Cycling and saving:**
   - Orange on the reverb: spring → plate → clouds → spring.
   - Leave a slot on plate (page 2 shown) and another on crush, then power-cycle. Do both come back as left?
9. **LFO:** an effect LFO on the plate. Does the screen follow, with no crash?

**If something is wrong:** power off; hold COM at power-on and flash fx2 again. fx2 has the same patch check and marker. A slot saved on plate loads there as stock spring, because fx2's reverb host turns an unknown engine into stock. fx2 has no dirt host, so a slot saved on crush would give stock punch crush's words in `values[4..7]`. If fx3 still runs, switch such slots back to punch before going back.

## Test 40: one new engine per build (`op1_246_rings_fx4.op1` … `fx10.op1`)

**Not flashed yet.** Each build is the previous one plus one engine, so the first build that misbehaves names the engine at fault. **Do Test 39 (fx3) first.**

| Build | Adds | Category (orange order) |
|---|---|---|
| fx4 | **drive** | dirt: punch → drive → crush |
| fx5 | **ensemble** | modulation: cwo → ensemble |
| fx6 | **hall** | reverb: spring → plate → hall → clouds |
| fx7 | **fold** | dirt: punch → drive → fold → crush |
| fx8 | **shift** | modulation: cwo → ensemble → shift |
| fx9 | **tape** (saturation) | dirt: punch → drive → tape → fold → crush |
| fx10 | **tape echo** | delay: delay → tape echo (all nine engines) |

Tape's screen is `tapesat.svg`, because the stock `tape.svg` is the tape screen's own and must not be replaced (the packaging now refuses any stock name).

**New in all of them: cwo and delay are hosted.** Their process and screen words now go through the host (8 new hooks). With no engine selected they call the stock code, so stock **cwo and delay must look and sound exactly as before**, on the synth, drum and master FX. That's step 1 of each build's test.

**Simulated cost** (whole engine; the half-rate ones include about 5.3 MHz stereo for resampling and mixing):

| Engine | Stereo | Mono | Rate |
|---|---|---|---|
| drive | 3.6 MHz | 2.6 MHz | 2× oversampled |
| ensemble | 8.2 MHz | 4.6 MHz | half |
| hall | 11.9 MHz | 8.7 MHz | half |
| fold | 3.5 MHz | 2.4 MHz | ADAA at 1× |
| shift | 4.9 MHz | 3.8 MHz | full |
| tape | 5.2 MHz | 3.2 MHz | 2× oversampled |
| tape echo | 9.2 MHz | 5.9 MHz | half |

The device has cost 1.5–4× the simulated figures.

**Checks, every build:**
- `sim-fx` passes for each engine on its host: mix 0 gives the dry signal (bit-exact for drive, fold and shift), mix 1 changes the sound, each engine is within its budget, and the orange click steps through the engines in order. Clouds' seven checksums are unchanged.
- No undefined symbols and no constructors.
- All 50 hook targets are symbols in the image, and the image ends below `0x02da0800`.
- `verify_bootloader.py` passes.
- Against fx2, only `OP1_vdk.ldr` and the engines' SVGs differ.

**Procedure, per build** (flash with COM held at power-on, te-boot menu):
1. **Boot and stock:** does it boot? Do stock cwo, delay, punch and spring sound and look unchanged?
2. **The new engine:** find it by orange clicks in its category. Does its screen show, with its engine dot lit?
   - Do the four knobs act and show as named?
   - Mix fully down: is the sound exactly dry?
3. **Its clicks:**
   - **hall:** white swaps pages; green freezes (decay at the top shows ∞ too).
   - **shift:** blue toggles frequency shift ↔ ring mod; the arrow and ring icons light in turn.
   - **tape echo:** blue steps the head pattern (head 3; 2+3; 1+2+3; 1+3; shift: back), and the lit heads follow. Green holds: the loop turns green and the input is cut.
4. **Master:** put it on the master FX with a few voices playing. Any crackle?
5. **Save:** leave a slot on it and power-cycle. Does it come back?

**Per-engine things to listen and look for:**
- **drive:** the wet lags the dry by 9 samples, so at a partial mix and low drive there can be a slight comb (first notch about 2.5 kHz). Is it audible? Tone is a tilt drawn as a line through a pivot. The screen flips that line with a negative scale; if the line vanishes or draws wrong at a dark setting, tell me.
- **ensemble:** mix 50% is the classic chorus; 100% is pure vibrato. Voices up = ensemble, about 2–4 dB quieter. Is it wide on the master?
- **hall:** size is 16 steps, crossfaded rather than muted. Do size changes sound smooth? At infinite decay (the top) with input still coming in, the tail builds until it saturates, by design.
- **fold:** the level is only roughly compensated: quiet input comes up and hot input goes down at high fold.
- **tape:** wobble is wow at the bottom of the knob and flutter at the top (about 17 cents peak). Age darkens it, with a small bump at 90 Hz and no hiss. The screen's reels shake with the wobble. Also check that the stock **tape** screen (the tape page) still looks normal.
- **tape echo:** 30 ms – 1.5 s. Turning time while it repeats should bend the pitch like tape, with no clicks. Feedback self-oscillates above about 0.9 of the knob, and age adds wow, darkening and drive. At full feedback with age 0, the wet can carry a small steady DC; it's bounded.
- **shift:** ring mode is about 6 dB quieter than shift mode. Fine near 0 Hz (the knob is cubic: ±2 kHz at the ends).

**If something is wrong:** power off; hold COM at power-on and flash the previous build (fx3 back to fx2). Slots saved on an engine a build doesn't have load as that category's stock effect. The engine's words stay in `values[4..7]`, and only spring is known to ignore them, so where the newer build still runs, click such slots back to stock first.

**Results (2026-10-20, the user):**
- **fx3:** crush is perfect. Plate sounds great, but its freeze icon renders strangely. Both work on synth and master.
- **fx4:** drive sounds great. Its white (tone) line looks strange; the user will revisit it in the design.
- **fx5:** ensemble looks and sounds great; the green (depth) bar renders slightly wrong.
- **fx6:** hall is perfect, apart from the green early-reflections icon.
- **fx7:** fold is perfect.
- **fx8:** shift sounds great, but most of its elements render wrong.
- **fx9:** **doesn't boot**: "Can't find SVG object 'track1'". The package on disk was the first fx9 attempt, which had replaced the stock `tape.svg` with the tape engine's. The rebuild with `tapesat.svg` had been refused because `build_package.py` won't overwrite an existing file, and the check of its output was missed. All the other packages match their work dirs; fx9 was rebuilt, and the packaging now removes the old package and compares the package's contents with the work dir.
- **Overall:** an engine's settings were lost when the orange click moved away from it and back: each engine started afresh at its defaults.
- **Rendering cause:** every element reported is a polyline that **doubles straight back on itself**: the snowflakes (plate, hall), the early stems (hall), the H bars (ensemble depth, shift spread), shift's comb and arrow. The OP-1 draws those wrong. Fold's sharpest turns (27°) draw correctly. tapesat's reels had one too (not seen, fx9 didn't boot).
- fx10 not flashed.

## Test 41: settings kept per engine, redrawn shapes (`op1_246_rings_fx11.op1`, `fx12.op1`)

**Not flashed yet.** fx11 is fx9's set (everything except tape echo); fx12 has all nine engines. Both add:
- **Each engine remembers its settings.** Every synth slot, drum kit and the master keeps each engine's values, stock's too, while the OP-1 is on. The orange click brings them back. An engine not played since power-up starts at its defaults. Only the shown engine's values are saved with the preset, so after a power cycle the others start afresh. Loading another preset into a slot doesn't clear what the slot remembers for its other engines.
- **No stroke doubles back.** The stems are 1 px wide (up, across, down); freeze is a six-pointed star; the depth and spread bars are thin outlined bars; shift's arrow has an outlined head; the tape reels' hubs are triangles. `gen_fx_svg.py` now refuses any turn under 10°.

Checks: sim-fx passes (with new checks: Clouds' values and stock's knob come back after orange clicks). No undefined symbols, 50/50 hooks, `verify_bootloader.py` PASS, each package's contents identical to its work dir, the stock `tape.svg` intact.

**Procedure:**
1. **fx11 boots** (fx9's failure). Is the stock **tape** page normal?
2. **Settings:** on a synth slot, set plate's knobs, orange to hall, change those, orange back: plate as you left it? On to stock spring: its knobs as they were? The same on the master.
3. **Shapes:** plate and hall's freeze star (green when frozen), hall's early stems, ensemble's depth bar, shift's comb, spread bar and arrow. Do they draw cleanly?
4. **Tape** (dirt, fx11): the Test 40 tape notes; the reels shake with wobble.
5. **fx12:** tape echo, the Test 40 notes and procedure.

**If something is wrong:** COM at power-on, flash fx8 (good in Test 40).

**Result (2026-10-20, the user):** "everything is looking amazing".

## Test 42: the beta, with op1repacker's mods (`op1_246_beta.op1`)

**Not flashed yet.** fx12 (the same `OP1_vdk.ldr`) plus three of op1repacker 0.2.6's content-only mods, applied to a copy of fx12's work dir (`op1repacker modify work_beta --options iter presets-iter filter gfx-tape-invert`):
- **iter:** the `synth_types` row (11, 'iter'). The hidden engine is playable.
- **presets-iter:** 11 community iter presets (folder `iter`). Non-sampler factory presets total 171, within the 176 budget. Their effects are filter ×5, phone ×3, punch ×2 and cwo ×1, all with stock values (none carries the engine marker), so they play as stock dirt and modulation.
- **filter:** the `fx_types` row (2, 'filter'). It shows as "filter"; it isn't hosted.
- **gfx-tape-invert** ("tape at the top"): `tape.svg` moved by the tool's patch. It is well-formed XML with the same ids as stock.

The conflicts noted in `docs/next-projects.md` are gone: Rings lives on phase, not iter, and Clouds lives in reverb, not filter.

Checks: against fx12 only `op1_factory.db` and `tape.svg` differ; `verify_bootloader.py` PASS; the package's contents are identical to `work_beta`.

**Procedure:** boot; the tape page has the tape at the top (and still works: record, play, loop); iter is in the synth list with its 11 presets; filter is in the effect list on synth and master; the categories and Rings/Clouds/Marbles/Grids as in fx12.

**If something is wrong:** COM at power-on, flash fx12.

## Test 43: the aligned module screens (`op1_246_beta2.op1`)

**Not flashed yet.** beta2 = fx13 (all nine engines, with the redesigned Rings, Clouds, Marbles and Grids screens from `docs/module-briefs/designs/`, approved by the user 2026-10-20) plus the same op1repacker mods as beta (identical `op1_factory.db` and `tape.svg`). Each module's code was updated to the new ids. The device SVGs are generated by `ports/fx/op1/gen_fx_svg.py` with 0 warnings and hold every id the code looks up. Host tests pass: sim-fx (Clouds' checksums unchanged), sim-grids, Marbles' sim-seq and test, and Rings' screen maths. Packaging: 50/50 hooks, no undefined symbols, `verify_bootloader.py` PASS, and the package's contents are identical to `work_beta2`. Clouds' CPU meter is gone from the screen.

**Procedure:** boot. Then on each screen:
- Rings: the model dots (blue click) and the voice ticks (green click); brightness and damping fill as bars; position dot.
- Clouds (reverb, 4th dot): the mode dots, page squares, freeze star, position across the width, pitch.
- Marbles: the page squares, the moving model/scale dot, marks and brackets.
- Grids: the dot and the assign arrow move (group transforms), the state icons, the bars.

Look for anything static or misplaced.

**If something is wrong:** COM at power-on, flash `op1_246_beta.op1`.

## Test 44: drive's valve (`op1_246_beta3.op1`)

**Not flashed yet.** beta3 = beta2 with drive's white bias line replaced by a valve (`docs/fx-briefs/designs/drive-valve.md`, approved 2026-10-20). As bias rises, the filament and glow blend from cold to hot, and up to three heat lines appear. The blend goes byte by byte between colours read from the file: cold from eng0's dim line and fill, hot from the valve's envelope (new id `envelope`). It doesn't depend on the style's byte order. This is the first screen that writes computed in-between colours. Against beta2 only `OP1_vdk.ldr` and `drive.svg` differ. Checks: 50/50 hooks, no undefined symbols, `verify_bootloader.py` PASS, and the package's contents are identical to `work_beta3`.

**Procedure:** dirt → drive. Turn white from 0 to the top: does the filament go from dim to white, the glow build, and the heat lines appear one by one (at 1/4, 1/2 and 3/4)? Everything else on drive is as before. If the colours come out wrong (odd hues, flicker), the fallback is 5 fixed steps (drive-valve.md).

**If something is wrong:** COM at power-on, flash `op1_246_beta2.op1`.

**Result (2026-10-20, the user):** "The tube graphic looks amazing ... the glow is fantastic." Computed in-between colours written each frame work on the device (a per-byte blend between colours read from the file).

## Test 45: iter's lab screen (`op1_246_beta4.op1`)

**Not flashed yet.** beta4 = beta3 + op1repacker's `gfx-iter-lab`: `content/display/iter.svg` replaced by op1repacker's `iter-lab.svg`, which is the only file that differs from beta3. Checks: well-formed XML, no `_x` ids, no exponents, no arcs, ASCII ids. Every id of the stock iter.svg is present except the root's `Layer_1`. `verify_bootloader.py` PASS, and the package's contents are identical to `work_beta4`.

**Procedure:** boot; pick iter (shift + T1). Does the lab screen show and animate with the knobs?

**If something is wrong:** COM at power-on, flash `op1_246_beta3.op1`.

## Test 46: the module switch and the builder app's first packages (`op1_246_beta5.op1`, `_off`, `_mix`)

**Not flashed yet.** The one firmware change for the builder app (docs/librarian-handover.md Part 0): every module is compiled in, and a data word in the image, `op1_module_mask` (`ports/fx/op1/modules.h`, one bit per module: plate 0, hall 1, clouds 2, drive 3, tape 4, fold 5, crush 6, ensemble 7, shift 8, tape echo 9, Rings 10, Marbles 11, Grids 12), switches them on and off. The builder patches the word in the shipped ELF before injection (`tools/inject_app.py --set-word op1_module_mask=0x...`). A module whose bit is clear: an effect engine is skipped by the orange click, and a slot saved on it plays stock (`fx_host.cc`: `EngineFor` returns null, `HostOfType` and the master lookup count only enabled engines, the screen's document isn't loaded); Rings' clicks are ignored and a MULTI slot plays phase (`input.cc` `MultiSlot`, `engine.cc` `NoteOn`); Marbles and Grids stay stock tombola and pattern (`Active()` and `Click()` in their glue). Every screen file still ships. The type list still shows multi / reverb / dirt / modulation whatever the mask (the query patch is in the image; the open question in the handover).

Three packages, all made by the app's pipeline (`python -m app.backend.build`) from the stock `op1_246.op1`, each verify_bootloader PASS, package = work dir, 51 hooks checked against the ELF's symbols:

| Package | Mask | Features | Expect |
|---|---|---|---|
| `op1_246_beta5.op1` | `0x1fff` | beta4's set: everything, iter + presets + lab screen, filter, tape at the top | = beta4 (the work dir's SVGs and db rows are identical to work_beta4's, bar the iter rows' JSON formatting, now compact) |
| `op1_246_beta5_off.op1` | `0x0000` | none (no op1repacker mods either) | stock behaviour, except the list names (multi, reverb, dirt, modulation) and the phase preset folder shown as multi |
| `op1_246_beta5_mix.op1` | `0x160a` | Rings, hall, drive, tape echo, Grids, iter, filter | reverb: spring → hall only; dirt: punch → drive only; modulation: cwo only (no orange click); delay: delay → tape echo; Marbles absent (tombola stock), Grids present; no clouds, plate, tape, fold, crush, ensemble, shift |

The simulator (`make sim-fx`) passes with the refactored host (bit-identical checksums). The ELF is `app/image/op1_seq.elf` (sha256 in `app/manifests/image.json`).

**Procedure:**
1. `beta5`: boot; a quick pass over multi (Rings), a reverb slot (spring → plate → hall → clouds), dirt, modulation, delay, Marbles, Grids, iter. Same as beta4?
2. While on beta5, save a synth sound on **clouds** (reverb) and one on **fold** (dirt), and a drum kit on **tape echo**; save a multi sound on **Rings**.
3. `beta5_off`: boot. Those saved sounds must play **stock** (spring, punch, delay; phase) and nothing must crash; the orange click on an FX page must do nothing; the multi slot's clicks do nothing; tombola and pattern are stock. The list names still read multi / reverb / dirt / modulation.
4. `beta5_mix`: boot. Reverb's orange click goes spring → hall → spring; the sound saved on clouds plays spring; the one on fold plays punch; the drum kit on tape echo plays tape echo; the Rings sound plays Rings; Grids on drums, tombola stock on synth; iter and filter in their lists.
5. Report in numbered points.

**If something is wrong:** COM at power-on, flash `op1_246_beta4.op1`.

**Result (2026-10-20, the user):** "everything worked as expected, there were no issues." The module switch works: a slot saved on a disabled engine plays stock, the orange click skips what's off, Rings off plays phase, the mixed set behaves as listed. The user's one decision: **"all off" should provide the stock firmware experience**, so the list names must follow the mask too (Test 47).

## Test 47: the list names follow the mask (`op1_246_beta6.op1`, `_off`, `_mix`)

**Not flashed yet.** After Test 46 the user asked that a firmware with every module off be the stock experience. The engine browser's query, which renamed phase / spring / cwo / punch to multi / reverb / modulation / dirt in every image, is now a named writable buffer in the image (`op1_type_list_query`, 151 bytes, `ports/rings/op1/patches.cc`) that the builder rewrites before injection (`tools/inject_app.py --set-string`): a stock type shows as its category's name only while one of that category's modules is on (phase→multi with Rings; spring→reverb with plate, hall or clouds; punch→dirt with drive, tape, fold or crush; cwo→modulation with ensemble or shift), else the stock query `SELECT id, type FROM %s` verbatim. Two more things follow the mask: the factory phase folder is renamed to multi only with Rings on, and the MULTI screen doesn't load rings.svg with Rings off (`screen.cc`: Update then always runs stock). The image is rebuilt (`app/image/op1_seq.elf`, sha256 f3e8bfe0…; 0 undefined symbols, no static constructors; `make sim-fx` passes). The strings and masks were read back from each package's injected LDR.

| Package | Mask | Features | Lists | Expect |
|---|---|---|---|---|
| `op1_246_beta6.op1` (12287945 bytes) | `0x1fff` | beta5's set (everything, iter + presets + lab screen, filter, tape at the top) | multi, reverb, dirt, modulation, delay | = beta5 |
| `op1_246_beta6_off.op1` (12255972 bytes) | `0x0000` | none | **phase, spring, punch, cwo, delay** | stock in every way: the stock query, the phase folder named phase, no rings.svg loaded; the saved sounds from Test 46 play stock |
| `op1_246_beta6_mix.op1` (12307650 bytes) | `0x160a` | Rings, hall, drive, tape echo, Grids, iter, filter | multi, reverb, dirt, **cwo**, delay | as Test 46's mix, but the modulation list entry reads cwo (no modulation module is on) |

**Procedure:**
1. `beta6_off`: boot. The synth engine list reads **phase** (not multi), and its preset folder is **phase**; the FX list reads **spring, punch, cwo, delay**; the phase screen is stock; the sounds saved in Test 46 play stock. Anything at all that differs from stock?
2. `beta6_mix`: boot. The FX list reads reverb, dirt, **cwo**, delay; the synth list multi; the folder multi; the rest as Test 46's mix.
3. `beta6`: boot. Same as beta5?
4. Report in numbered points.

**If something is wrong:** COM at power-on, flash `op1_246_beta5.op1`.

**Result (2026-10-20, the user), and the hang that came first:** after Test 46 the user re-flashed beta5 and it **hung on the logo**; so did beta4, beta6 and beta6_mix, while **stock booted**. The disk was backed up with the Librarian (its first real use: 91 files, `app/data/backups/op1-backup-20261020-162837`): every preset well-formed, no limit near, four slot states carrying an engine marker (synth slot 1 hall, slot 2 fold, slot 8 drive, drum kit 1 drive; slot 1 had been saved on clouds and switched to hall by the orange click under beta5_mix). Bisect: `beta6_off` boots (markers ignored); beta6 boots with the four slot files' markers reset to stock; **the original slot 1 (hall) put back hangs beta6**. So: **a slot saved on hall, live at power-on, hangs the boot.** Cause (read, then fixed in Test 48): `fx/op1/half_rate.h` restarts a position after a pause over 5 ms between calls, measured from the *start* of the previous call; a restart re-initialises the core (hall clears 76 KB), and at boot, with cold caches and the firmware loading files, that one call takes over 5 ms, so the next call restarts again, every call does, the audio interrupt starves the boot. Clouds had the same loop in clouds1 and measures its pause from the end of the call; plate, hall, ensemble and tape echo (the half-rate engines) didn't. Drive, tape, fold, crush and shift restart only on the host's stop, so they can't loop. The list names themselves (`beta6_off`: phase / spring / punch / cwo; `beta6`: multi / reverb / dirt / modulation) were seen in passing and are right.

## Test 48: the restart loop fixed (`op1_246_beta7.op1`, `_off`, `_mix`)

**Result so far (2026-10-20, the user): beta7 boots with the original slot 1 (hall) on the disk**, the exact file that hung beta6: the cause and the fix are confirmed. Steps 1 (the other three slot files), 3 and 4 are still to come; the three files are being put back with the Librarian (delete the cleaned file, import the original from the backup), which is also its first write test.

beta7 = beta6 + `fx/op1/half_rate.h`: the pause is measured from the end of the previous call (`End()`, which plate, hall, ensemble and tape echo call last, on a restart too), as Clouds does, so a long restart never reads as a pause at the next call. Nothing else changes (`make sim-fx` passes with the same checksums). Same three feature sets as Test 47.

**Procedure:**
1. With beta6 (or `beta6_off`) in disk mode, **put the original slot files back** from the backup: `app/data/backups/op1-backup-20261020-162837/synth/user/1.aif`, `2.aif`, `8.aif` and `drum/user/1.aif` over the same paths on the OP-1. Eject.
2. Flash `op1_246_beta7.op1`. **It must boot** with slot 1 on hall, slot 2 on fold, slot 8 on drive and kit 1 on drive, and they must sound as saved. Power-cycle twice more.
3. Leave a slot on **plate**, on **tape echo** and the master on **ensemble**; power-cycle. Boots?
4. `beta7_off` and `beta7_mix`: the Test 47 checks (stock names with everything off; cwo with no modulation module).
5. Report in numbered points.

**If something is wrong:** COM at power-on, flash `op1_246_beta6_off.op1` (boots whatever the slots hold), disk mode, copy the cleaned slot files from the scratchpad's `slots-cleared` folder back, then any build.

## Test 49: Clouds' entry defaults, the Librarian's second round (`op1_246_beta8.op1`, `_off`, `_mix`)

**Not flashed yet.** beta8 = beta7 + Clouds' entry defaults: the orange click into Clouds now lands on the settings of the user's snapshot `synth/snapshot/20261020_2024.aif` (2026-10-20, "the clouds setting of this patch should be the default settings for the clouds fx"), decoded from its stored words and rounded to clean values (`clouds_settings.h` `kEnterDefaults`, `kEnterMode`): **looping delay**, page 1 position 41 % / size 100 % / pitch 0 / density 100 %, page 2 texture 50 % / dry-wet 31 % / feedback 28 % / reverb 66 %. It starts on page 1 (the snapshot was saved showing page 2). The encoders are written on entry the way the white click writes them. Saved Clouds presets decode as before (the encoding base is unchanged). `make sim-fx` passes, with a new check of the entry on both pages. Same three feature sets as Test 47; the mask and the list names unchanged.

The rest of Test 48 moves here (beta7 is proven on the hall slot only).

**Procedure:**
1. Flash `op1_246_beta8.op1`. It must boot with slot 1 on hall and the other three originals back (slot 2 fold, slot 8 drive, kit 1 drive), sounding as saved. Power-cycle twice.
2. On a slot with spring: orange click to Clouds. **It must be in looping delay with the snapshot's sound** (the four encoders at position 41 %, size full, pitch centre, density full; white click shows page 2 at texture half, mix a third, feedback a quarter, reverb two thirds). The master too. Does it sound like `20261020_2024` with spring's knobs on that slot the same?
3. Leave a slot on **plate**, one on **tape echo** and the master on **ensemble**; power-cycle. Boots?
4. `beta8_off` and `beta8_mix`: the Test 47 checks (stock names with everything off; cwo with no modulation module).
5. Report in numbered points.

**If something is wrong:** COM at power-on, flash `op1_246_beta7.op1` (Test 48's, the same without the Clouds defaults) or `op1_246_beta6_off.op1`.

## Test 50: hundekraft 3.0.3 and our first kennel plugin (`hundekraft-3.0.3-noboot.op1`, `crush.knl`)

**Not flashed yet.** The direction changed on 2026-10-26 (`docs/kennel-roadmap.md`): our modules
become plugin files for hundekraft's kennel core instead of our own injected firmware. This test
installs hundekraft 3.0.3 and loads the smallest of our effects, crush, packed as a `.knl`
(`ports/knl/cs_crush/`, `tools/knl_pack.py`, 11,608 bytes, stereo, with its screen).

**The package.** `incoming/hundekraft/hundekraft-3.0.3.op1` (13,359,401 bytes, SHA-256
`39c3d475…41dce`, = its `build.json`) **contains `te-boot.ldr`** (the stock TE-BOOT v2.30), so
`tools/verify_bootloader.py` fails it: never flash that file. `op1 firmware/hundekraft-3.0.3-noboot.op1`
(12,602,900 bytes) is the same tar repacked by `tools/build_package.py` without the bootloader:
verify **PASS**, `pkg_check` 197 files, 0 mismatches against the unpacked original
(`op1 firmware/work_hundekraft303/`). Same recovery as always: COM at power-on, flash any known package.

**Going back afterwards:** hundekraft saves sounds with its own type names (crush, or any of its
engines you try). Stock and our firmware halt at boot on an unknown type ("Invalid synth patch …
error 22"). Before flashing anything else, set every synth slot, drum kit and the master back to
stock engines and effects (or restore the backup from step 1).

**Procedure:**
1. Back up the disk (disk mode, copy the drive; or the Librarian's backup).
2. COM at power-on, flash `op1 firmware/hundekraft-3.0.3-noboot.op1`. Boots? Use it for a while:
   the lists, the screens, the clock pages, what a saved sound does. Notes on its UX are part of the
   result (the user wants ideas from it).
3. Install the plugin: OP-1 on USB in normal mode (not disk mode), Chrome at
   https://hundekraft.github.io/op1-workshop/ (if that site has moved past 3.0.3 and insists on a core
   update, serve the local copy instead: `python -m http.server 8000` in
   `incoming/hundekraft/op1-workshop-main`, open http://localhost:8000/). **Your plugins** → add
   `build/knl/cs_crush.knl` → **Update OP-1** → read the review → send → **T1** on the OP-1 → it restarts.
4. Shift + T3 on a synth sound: **cs crush** is in the effect list (hundekraft has its own crush; ours is prefixed, every plugin of ours will be). Choose it. Blue rate, green bits,
   white jitter, orange mix: the sound and the screen (variant set, bit ticks, jittering clock ticks,
   mix bar) follow. Shift + knob fine-adjusts.
5. The same on a drum kit, and on the master (mixer): stereo there (each side holds its own sample).
6. Save a sound with crush on it; power-cycle. Still crush, same settings? The workshop's
   "Your plugins" lists it as installed?
7. Remove it in the workshop (Remove → Update OP-1 → T1). The OP-1 boots; the sound plays through
   the stock filter and keeps the name crush; reinstalling brings it back.
8. Report in numbered points.

**If something is wrong:** COM at power-on, flash `op1_246_beta7.op1` (ours, Test 48) after step
"going back", or stock `op1_246.op1` after a factory reset.

## Test 51: Rings at 22.05 kHz, upsampled 4x (`op1_246_beta8_r22.op1`)

**Not flashed yet.** The user's question (2026-10-26): does rendering Rings at half its rate
appreciably change the sound, and does it let Rings and Clouds run together? `op1_246_beta8_r22.op1`
(12,262,289 bytes) is **beta8 with one file changed, the app image**: every other file is
byte-identical (checked), so an A/B against beta8 is the rate and nothing else. Bootloader check PASS,
51 hooks checked; built by `python -m app.backend.build --image ports/rings/build/op1_seq_r22.elf`
with beta8's feature list (`make op1-seq-r22` in `ports/rings`, WSL).

**What changed in the image** (`ports/rings/src/rate_config.h` and the files it names; the 44.1 kHz
build is proven unchanged: its load segment is byte-identical to the shipped `app/image/op1_seq.elf`):
- Rings renders 32 samples a call at 22.05 kHz; a 47-tap half-band FIR (Kaiser 60 dB: flat to 9 kHz,
  -0.7 dB at 10 kHz, -59 dB from 13 kHz, so no audible images) takes it to 44.1 kHz, then the old
  2x Lagrange stage to the 128 samples at 88.2 kHz. Delay 0.59 ms. Pitch word x4.
- Kept in seconds: the reverb's delay lines and modulated taps (halved, C and assembly), the limiters'
  attack/release (coefficients doubled, C and assembly), the modal modes' Q (upstream's `1 + f q` with
  f a fraction of the rate would ring every mode twice as long: q halved), the string loop's
  brightness FIR (remapped to the same loss per pass at low frequencies), the strings' glide.
- Checks at the new rate: limiter assembly = C, reverb assembly = host C, reverb bus = reference,
  mode bank assembly = C = host (`make bench-models bench-string test-reverb-bus bench-mode-bank
  RINGS_RATE_FLAGS=...`).
- Simulator audio, voice 0's output of the same note and knobs at both rates, band energies: modal
  within 0.6 dB in every band below 9 kHz, level within 0.1 dB; string + reverb within 0.4 dB;
  plain string level within 0.9 dB, its highest partials (55 dB down) ring somewhat longer. Above
  9 kHz the 22.05 kHz build has nothing (-6 dB at 11 kHz, gone by 13 kHz): that is the trade.

**Expected CPU** (simulator, six voices, 44.1 -> 22.05 kHz): modal 123 -> 82 MHz (turning 143 ->
101), string 89 -> 68, string + reverb 76 -> 54, FM 95 -> 68. Less than half because the per-block
work (coefficients, smoothing, note-ons, the upsampler) doesn't scale. On the device Test 34 measured
Rings' six modal voices at 161 MHz; expect ~105-110, so Rings "all" + Clouds on the master (~70 MHz)
should sit near 45% of the CPU before the firmware's own ~25%, where Test 34's 62% + 25% dropped blocks.

**Procedure** (after Test 49's beta8 checks, so beta8 is the reference):
1. Flash `op1_246_beta8_r22.op1`. Boots; Rings slots play; power-cycle twice.
2. **Sound A/B**, same preset on beta8 and on r22 (switch back and forth as needed): the modal
   model at polyphony 4 with brightness high (the top octave is where the two differ), a string
   model (dispersion on), string + reverb (its room and modulation should be the same size), FM.
   Attack, tuning, decay length, brightness, the reverb tail. Is the loss of the band above 9 kHz
   audible on the OP-1's output? Where?
3. **Rings + Clouds**: Rings polyphony "all" on a slot, Clouds (granular, then looping delay with
   reverb) on the master, six-note chords held and retriggered, knobs turning: pops, pumping, cut
   tails (Test 34's symptoms)? Then a synth Clouds on another slot too.
4. The Rings tape animation with more than 3 voices while recording (the old choppiness).
5. Report in numbered points: sound (which models, what differs, acceptable or not), CPU.

**If something is wrong:** COM at power-on, flash `op1_246_beta8.op1` (or `beta7`).

**Result (2026-10-26, the user):** "I don't notice a difference in sound quality. I think there is a
slight boost in performance." So the 22.05 kHz build is the one to carry forward (the roadmap's
phase 2, Rings as a kennel synth, starts from it). The user then moved to hundekraft (Test 50).

**Result, step 4 (2026-10-26, the user): "ok it works, amazing!"** cs crush loads, plays and shows its
screen on hundekraft 3.0.3. The user asked for the category dots to go and for every effect to be
ported.

### Test 50, round 2: the eight other effects (`build/knl/cs_*.knl`)

**Not installed yet.** All nine effects are kennel plugins now (`docs/kennel-plugins.md`: ids,
knobs, presses, what differs from the firmware build; `ports/knl/`, `make` in WSL). Category dots
removed from every screen.

**Procedure:**
1. Workshop → Your plugins → add the eight files from `build/knl/` (cs_drive, cs_tape, cs_fold,
   cs_shift, cs_ensemble, cs_plate, cs_hall, cs_tape_echo; cs_crush is installed) → Update OP-1 → T1.
   The review should list eight new plugins and no conflicts.
2. Each one on a synth sound: the list entry, the sound, the screen without dots, every knob.
3. Presses: plate and hall: press blue/green/white (the screen switches to page 2, that knob turns
   the page-2 parameter, press again to return), press orange (freeze: the tail holds, the freeze
   mark lights). Tape echo: press blue, turn blue through the four head patterns (the heads on the
   screen follow), press green (hold). Shift: press blue, turn blue past half (ring mode).
4. The master (mixer) on cs_hall and cs_plate (stereo, width on hall's page 2) and cs_tape_echo
   (ping-pong heads as before).
5. Save a sound with page-2 settings; power-cycle: kept? CPU: hall on the master plus a synth
   plate while playing six-note chords, as the firmware build managed.
6. Report in numbered points.

## Test 52: the whole plugin set on hundekraft (`build/knl/cs_*.knl`, twelve files)

**Round 1 (2026-10-26): memory, not sound.** All twelve installed: the MIDI sequencer (one of
hundekraft's own plugin files) stopped loading, and the workshop's Refresh answered "The OP-1
refused the inventory request (io)". A clean hundekraft rebuild, a stock reflash and a factory
reset changed nothing: plugin files live in NAND `/yaffs2/plugins`, outside every package, and the
browser re-sent the "Your plugins" list with each USB update. Cause: kennel's arena is 2 MB usable
for every plugin file (hundekraft's own ~45 take 1.14 MB); our first builds kept Rings' 573 KB of
voices and reverb there, and Clouds a 360 KB core per position, so the loader ran out while
loading hundekraft's files (user files load first), and the inventory text, built in the same
arena, failed, which blocks Remove. Rings plus one Clouds did the same. Way out (worked): the safe
start. Switch off within a few seconds of the tape screen; the next boot skips every plugin file
("plugin files not loaded"), Refresh works, Remove → Update OP-1 → T1 deletes them.

**Round 2: the big state is outside the arena** (`ports/knl/common/cs_sdram.h`, region A
0x02C00000.. where our firmware ran; `docs/kennel-plugins.md` "Sizes"). cs_rings, cs_clouds and
cs_clouds_delay rebuilt: all twelve now take 392 KB of the arena, hundekraft complete beside them.
Install: safe start if the device is still full, then Your plugins → add the three new files (same
ids: the review says replaced) and the rest → Update OP-1 → T1. Refresh afterwards: every file
`loaded`, the MIDI sequencer among hundekraft's. Then the procedure below. New things to watch: the
first Rings note after a boot clears 576 KB (one block, a click at most, once); a Clouds position
clears its slot for ~65 ms of pass-through when created, as the half-rate restart did.

**Between the rounds (2026-10-27):** with the eleven first-build effects and Clouds installed, a
note on any sound hung the OP-1, a stock engine with its effect off included; with every cs plugin
switched off it played. The master effect is the one position a sound's own settings don't change,
so a Clouds build on the master is the suspect; the name was lost before it could be read. The user
uninstalled everything to start from hundekraft alone, and the plugins go back in groups (small
effects, modulation, the big three, cs_clouds on the master, cs_clouds_delay, cs_rings last). Not
ours: a drum memory saved on Gin with Gin not loaded crashed every boot at the stock exception
screen (`SEQSTAT 0x21`, `RETX 0x3`: illegal instruction at address 3, a null function pointer).
Pointing the slot elsewhere fixed it; persistent state, so reflashes don't touch it. Worth
reporting to hundekraft (their guide says a missing provider should fall back, not crash).

**Procedure:**
1. Workshop → Your plugins → add every `build/knl/cs_*.knl` not yet installed → Update OP-1 → T1.
   The review: new plugins, no conflicts.
2. **cs rings** in the synth engine list (Shift + T1). Play; the screen (rings.svg, no engine dots).
   Press blue, turn blue through the six models; press green, turn green through all / 1 / 2 / 4;
   press again to return to the knobs. Model 6 (string + reverb) at "all" plays 4 voices as before.
   Save a sound on a model and polyphony; power-cycle: kept? The 22.05 kHz sound (Test 51).
3. **cs clouds** and **cs clouds delay** on a synth sound and on the master. Delay: the user's
   snapshot settings (position 41 %, size full, density full; dry/wet 31 %, feedback 28 %, reverb
   66 %) on entry. Presses: blue/green/white page 2 (the screen shows page 2 while any is pressed),
   orange freeze (the face freezes). Granular: density off-centre makes grains; texture on blue.
4. **Rings + Clouds together**: cs rings at "all" with cs clouds delay on the master and chords:
   pops, pumping, cut tails? Then a synth cs clouds as well. The governor no longer sees Rings'
   cycles (it did in the firmware build), only its own lateness.
5. Everything saved; power-cycle; the OP-1 boots with every slot as left. Then Remove one plugin in
   the workshop and boot: its sounds fall back to the stock engine and keep their name.
6. Report in numbered points, with anything in the UX that reads wrong.

**Going back to our firmware or stock afterwards:** set every slot, kit and the master off the
cs_ plugins first (an unknown type halts the boot), or restore the backup.

## Test 53: rings (was cs_rings) with its new screen and one-step page-2 words (`build/knl/rings.knl`)

**2026-10-27.** The user's first round with the twelve on the device: cs_rings' presses did work
(blue, then turn blue, changed the model) but nothing on the screen said so, and the six models
took 21 clicks of the encoder (kennel moves a page-2 word by about 1536 per click over the full
range). Rebuilt: a setting is now the word in clicks, one model or polyphony step per click,
cycling (`settings.h` `RangeSetting`, rounded to the nearest click). The screen is new
(`ports/knl/rings/gen_rings_knl_svg.py`, the design canvas "Rings kennel screen"): six rings
lit by structure, eight green rays (brightness), a white pad sliding in over the rings (damping),
the orange strike dot on the radius (position), a ripple per voice growing from the core to the rim
and fading with the voice, the core's glow (the drive valve's blend), and at the right the MODEL
and POLY rows with stroked labels, dots, ticks and a page-2 bracket. Per-voice level and age come
from the engine (`RingsScreenVoices`). Composition 8 px left, 12 px up for the viewing angle.

**Procedure:**
1. Remove `cs_rings` in the workshop (its sounds fall back to the stock engine: resave them on
   rings), add `rings.knl`, Update OP-1 -> T1. The id is plain `rings` now (the user's choice).
2. The idle screen: rings, rays, pad, strike dot, MODAL, ALL. Turn each knob: rings light from
   the core out (blue), rays light anticlockwise from the lower left (green), the pad lifts off
   the rings as white turns clockwise (it sits deep in at zero: in Rings more damping is a longer
   ring), the dot moves out along the radius (orange). Round 2 (2026-10-27, after the user's
   photo of round 1): the composition is larger (rings to r 54, rays from y 9, the pad's foot at
   y 134) and the pad's travel is reversed, as the user asked.
3. Press blue: a blue bracket around MODEL, the label white, the rings dim. Each click of blue is
   one model: MODAL, SYMP, STRING, FM, QUANT, VERB, then round again. Press blue to return.
   The same with green: bracket around POLY, ALL, 1, 2, 4 one click each.
4. Play: a white ripple leaves the core on each note, reaches the rim in about a third of a
   second and fades; chords make several. The core flares and the lit rays flash on a strike.
   The POLY ticks count the voices sounding: bright green per sounding voice from the left,
   dim green for the rest the setting allows, and they drop on key up (round 3: the user
   missed a playing-voice count, so the ticks show it outright and the engine reports note-off
   to the screen). **Watch the ripple's line width:** if it gets
   thicker as it grows, the renderer scales strokes with the matrix (unverified, screen-interface.md
   section 6); then say so and the ripple will be redrawn as fills.
5. Damping at zero (pad deep in): ripples die before the rim. Brightness at zero: no rays, a black core.
6. Save a sound on a model and polyphony, power-cycle: kept, and the label matches.
7. Report in numbered points.

## Test 54: bitcrusher (was cs_crush) with its new screen and the input gain (`build/knl/bitcrusher.knl`)

**2026-10-27.** Renamed to plain `bitcrusher` (the user's choice; hundekraft's own is `crush`). New:
an input gain on blue's page 2 (word 5, -6 dB at 0, 0 dB at the centre default, +6 dB at the top,
about 0.6 dB a click; it drives the crusher only, the dry side of the mix is untouched, and the
crushed signal clips at full scale). New screen (`ports/knl/bitcrusher/gen_crush_knl_svg.py`, the
canvas "Bitcrusher screen" option C, https://claude.ai/artifact/MMa1PBoYe8Q3XbPvxUdx9V): the gain as
a white gauge at the far left (bar up or down from the 0 dB mark, warming with the input level),
the mix as an orange gauge at the far right, and between them the staircase over a dim sine (eight
variants by rate), the green bit ladder, the clock row along the top (two halves shaken apart by
jitter) and a white sampling dot running the steps. The file is 29 KB (113 clock ticks and the
readout's glyph sets); hundekraft's check passes.

**Procedure:**
1. Remove `cs_crush` in the workshop, add `bitcrusher.knl`, Update OP-1 -> T1. Resave any sound
   that used cs crush (it fell back to the stock effect, keeping its name).
2. Idle: the two gauges, the ladder, the staircase, the clock row, the dot. Turn each knob: blue
   changes the step width and the clock pitch together; green lights fewer ladder ticks as bits
   fall; white shakes the clock row; orange fills the right gauge from the foot.
3. Play: the left gauge's bar brightens with the input; the dot steps along the staircase,
   faster at higher rates.
4. Press blue: the staircase dims, a bracket around the left gauge, a readout above it (`+0.0`
   at the default). Turn blue: the bar grows up from the centre to `+6.0` and down to `-6.0`,
   the sound driving harder into the crusher at +6 (at mix 0 the dry sound must not change).
   Press blue again: back. Save a sound at +3, power-cycle: `+3.0` again.
5. Report in numbered points.

**First report (2026-10-27):** installed; white (jitter) did nothing on the screen. Cause: the clock
row's shake was scaled by `>> 14` instead of `>> 6`, at most 0.016 px (`crush_knl.cc`). Fixed and
rebuilt (36,740 bytes); the row's two halves now shake apart up to 4 px, more as white rises. Replace
`bitcrusher.knl` in the workshop and rerun point 2 for white. Note for the manual: the gauge at the
far left is white-coloured but is the input gain, set by blue on page 2, not by the white knob.

## Test 55: shift + a page-2 knob moves in fine steps (`build/knl/bitcrusher.knl`, `cs_plate.knl`, `cs_hall.knl`, `cs_clouds.knl`, `cs_clouds_delay.knl`)

**2026-10-27.** The user: one click of blue on the bitcrusher's gain frame moves it 0.4 dB; shift + blue
should move it 0.1, as the stock knobs do under shift, on every module. Kennel's dispatcher steps a
page-2 word a click regardless of shift, so the plugins now do it (`knl::Fine`, `ports/knl/common/
knl_fx.h`): once a render block the effect compares its page-2 words with what it saw last; a change of
up to three clicks while shift is held is rewritten in the stored words as a fine step a click (a tenth
of a dB for the gain, a quarter click for plate, hall and Clouds); larger jumps pass. **The unknown:**
whether the dispatcher reads the stored word back for its next click (then the fine steps hold) or
keeps its own running value (then each shift click would show a fine step and the next unshifted click
would jump to where the coarse count is). Point 3 tells.

**Procedure:**
1. Replace the five plugins in the workshop, Update OP-1 -> T1.
2. Bitcrusher, press blue, readout `+0.0`. Hold shift, turn blue one click at a time: `+0.1`, `+0.2`,
   `+0.3` ... one digit a click, both ways. Release shift, one click: about +0.6 from there (not a jump
   back to a whole number of coarse clicks).
3. Shift + ten clicks, then release shift and click once: the value continues from where shift left it.
   If it instead snaps to a coarse count, report what it showed; the dispatcher keeps its own value and
   the plugin-side approach has to change.
4. Save a sound at `+1.3`, power-cycle: `+1.3`.
5. Plate (blue page 2, size) and Clouds (blue page 2, texture): shift + a click moves the screen's
   page-2 element about a quarter of an unshifted click; unshifted clicks unchanged.
6. cs_shift's mode and the tape echo's head pattern: shift + a click still changes the mode by one.
7. **Blue and green turn the stock way now** (the user's ask, same build): blue clockwise raises the
   sample rate (narrower steps, a higher clock pitch), green clockwise adds bits (more ladder ticks).
   The defaults mirror the old ones (24767, 24767), so a new sound crushes as before; a sound saved
   with the old build keeps its words and so now sounds mirrored: reset its rate and bits.
8. Report in numbered points.

**Result (2026-10-27): bitcrusher "perfect"** (the user): the shake, the tenth-of-a-dB shift steps on
the gain (so the dispatcher does read the stored word back: the plugin-side fine step holds) and the
knob directions. Plate, hall and the two Clouds builds with the quarter-click shift steps (points 5-6)
not reported yet.

## Test 56: drive with the schematic screen and an input gain (`build/knl/drive.knl`, was cs_drive)

**2026-10-27.** The user's pick from the canvas "Drive screen" (design A "Schematic",
https://claude.ai/artifact/KAb4QAzStA5Eo9vvq4nztk; `docs/fx-briefs/designs/drive-schematic.md`): the
signal path as a circuit: jack, the input gain as a pot with a blue wiper, the clipper's two blue
diodes filling from the tip with drive, the triode with its filament and glow warming with bias, the
tilt EQ as its S response, the output arrow, the orange mix gauge. New in the core: an input gain on
blue's page 2 (word 5, -6..+6 dB, 0 dB at the centre, shift for tenths) that scales the clipper's gain
only (the auto-level and the dry side don't see it). Built with the bitcrusher (whose gain table moved
to `common/knl_fx.h`).

**Procedure:**
1. Remove `cs_drive` in the workshop, add `drive.knl` (plain `drive`: hundekraft has none), Update
   OP-1 -> T1. Resave any sound that used cs drive (it falls back to the stock effect).
2. Idle: the circuit. Turn blue: the diode triangles fill from the tip in seven steps, rays appear at
   the top of the range. Green: the S after the tube rises to the right (bright), flattens at the
   centre, flips (dark). White: the filament under the cathode goes from dim to white, the glow
   behind it comes up. Orange: the right gauge fills from the foot.
3. Press blue: the diodes dim, a blue readout `+0.0` above the pot. Turn blue: the wiper arrow slides
   along the resistor, `-6.0` at the left end to `+6.0` at the right; shift + a click moves it a tenth.
   The sound: at +6 the clipping is heard harder, at -6 cleaner, with no level jump; at mix 0 nothing
   changes. Press blue again: the diodes return. Save at +2.0, power-cycle: `+2.0`.
4. Report in numbered points.

**Result (2026-10-27): drive "perfect"** (the user): installed as plain `drive`, the schematic screen,
the gain and its readout, the S tilt. Next in the UI round: tape, fold (dirt), then ensemble and
shift (modulation), then tape echo.

## Test 57: tapesat (was cs_tape) with the aligned reels screen and an input gain (`build/knl/tapesat.knl`)

**2026-10-27.** The user's pick from the canvas "Tape screen" (design A "Reels, aligned",
https://claude.ai/artifact/TKsEMfeTbLg34RCNroqFkJ; `docs/fx-briefs/designs/tapesat.md`): the firmware's
reels without the box, spread over the width: the blue gain gauge at the far left, bigger reels with
their green hubs, the saturation wave on the tape rail, the head, the wow -> flutter scale on the
gauges' foot row, the low-pass glyph and specks at the right, the orange mix gauge. New in the core: an
input gain on blue's page 2 (word 5, -6..+6 dB, 0 dB at the centre, shift for tenths) that adds to the
saturation drive only (the level make-up and the dry side don't see it). Renamed `tapesat` (hundekraft
has its own `tape`).

**Procedure:**
1. Remove `cs_tape` in the workshop, add `tapesat.knl`, Update OP-1 -> T1. Resave any sound that used
   cs tape (it falls back to the stock effect).
2. Idle: the two gauges, the reels, the tape, the scale, the glyph. Turn blue: the wave on the tape
   flattens in eight steps. Green: the hubs shiver, slowly and a little at the bottom of the range,
   fast and wider at the top, and the marker slides wow -> flutter. White (clockwise = a newer tape,
   the user's ask after the first build; the default mirrored to 22937): anticlockwise the glyph's
   corner walks left and the glyph greys, specks appear above it one by one (six); clockwise it
   clears. A sound saved with the first build keeps its word and so now reads mirrored: reset its
   age. Orange: the right gauge fills from the foot.
3. Press blue: a bracket round the left gauge, `+0.0` top left. Turn blue: the bar grows up from the
   centre to `+6.0` and down to `-6.0`; shift + a click moves a tenth. The sound: at +6 the tape
   saturates harder, at -6 cleaner, with no level jump; at mix 0 nothing changes. Press blue again:
   back. Save at `-2.0`, power-cycle: `-2.0`.
4. Report in numbered points.

**Result (2026-10-27): tapesat "good"** (the user), with white mirrored. Next: fold.

## Test 58: wavefolder (was cs_fold) with the aligned wave screen and an input gain (`build/knl/wavefolder.knl`)

**2026-10-27.** The user's pick from the canvas "Fold screen" (design A "Wave, aligned",
https://claude.ai/artifact/CscKQMR3VBtoGF1GQaTpNN; `docs/fx-briefs/designs/fold-aligned.md`): the
firmware's folded wave without the box, drawn large over the width between two dim fold lines, the
blue gain gauge at the far left, the green offset line, the white low-pass glyph at the right (as
tapesat's), the orange mix gauge. New in the core: an input gain on blue's page 2 (word 5, -6..+6 dB,
0 dB at the centre, shift for tenths) that scales the folder's gain only (the level make-up and the dry
side don't see it). Renamed `wavefolder` (the user's name).

**Procedure:**
1. Remove `cs_fold` in the workshop, add `wavefolder.knl`, Update OP-1 -> T1. Resave any sound that
   used cs fold (it falls back to the stock effect).
2. Idle: the two gauges, the wave between its lines, the offset line, the glyph. Turn blue: the wave
   grows wiggles in its tops and bottoms in eight steps. Green: the offset line slides up and down.
   White: the glyph's corner walks left as tone darkens. Orange: the right gauge fills from the foot.
3. Press blue: a bracket round the left gauge, `+0.0` top left. Turn blue: the bar grows up from the
   centre to `+6.0` and down to `-6.0`; shift + a click moves a tenth. The sound: at +6 more folds, at
   -6 fewer, with no level jump; at mix 0 nothing changes. Press blue again: back. Save at `+1.5`,
   power-cycle: `+1.5`.
4. Report in numbered points.

**Result (2026-10-27): wavefolder "good"** (the user). The dirt family is done (bitcrusher, drive,
tapesat, wavefolder). Next: ensemble.

## Test 59: ensemble (was cs_ensemble) with the drift screen and an input gain (`build/knl/ensemble.knl`)

**2026-10-27.** The user's pick from the canvas "Ensemble screen" (design C "Drift",
https://claude.ai/artifact/DzykGtr3epWihZ7qJEUfoM; `docs/fx-briefs/designs/ensemble-drift.md`): each
voice a vertical string swaying about its home mark, L above and R below, green depth ticks either side
of each mark, the blue sweep dial and rate bar at the right, the blue gain gauge at the far left, the
orange mix gauge. New: an input gain on blue's page 2 (word 5, -6..+6 dB, 0 dB at the centre, shift for
tenths) on the wet path only (the plugin scales the decimated input; the dry side doesn't see it). The
id is plain `ensemble` now (the user's name; hundekraft has none).

**Procedure:**
1. Remove `cs_ensemble` in the workshop, add `ensemble.knl`, Update OP-1 -> T1. Resave any sound that
   used cs ensemble (it falls back to the stock effect).
2. On a synth: one lane (the R lane and its strings hidden). On the master: both lanes. Idle: the
   strings sway about their marks at the rate, the three a third of a turn apart, the R lane
   opposite; the dial's dot circles at the same rate. Turn blue: faster, the bar grows. Green: the
   sway widens and the ticks spread with it. White: one, two, three strings per lane (their marks
   and ticks with them). Orange: the right gauge fills from the foot.
3. Press blue: a bracket round the left gauge, `+0.0` top left. Turn blue: the bar grows up from the
   centre to `+6.0` and down to `-6.0`; shift + a click moves a tenth. The sound: the chorused voices
   louder or softer against the dry; at mix 0 nothing changes. Press blue again: back. Save at
   `-1.0`, power-cycle: `-1.0`.
4. Report in numbered points.

**Result (2026-10-27): ensemble "good"** (the user), installed as plain `ensemble`. Next: shift.

## Test 60: freq_shift (was cs_shift) with the dial screen, the gain on blue and the mode on green (`build/knl/freq_shift.knl`)

**2026-10-27.** The user's pick from the canvas "Freq shift screen" (design B "Dial",
https://claude.ai/artifact/4JwECGQ5XeJKoNRp7mLViS; `docs/fx-briefs/designs/freq-shift-dial.md`): shift
as a needle on a bipolar arc spanning the width, two needles with a white spread arc on the master,
green feedback arcs sweeping the way the sound climbs, the mode marks top left in green, the blue gain
gauge at the far left, the orange mix gauge. **The control map changed:** blue press → the input gain
(word 5, -6..+6 dB, the wet path only, shift for tenths); green press → the mode (word 6, one click
flips shifter / ring; it was blue's). Renamed `freq_shift` (the user's name; the workshop shows "freq
shift"). **The unknown:** the needles turn by a full affine matrix (rotation), the first use of the
renderer's b and c terms; every earlier screen only scaled and translated.

**Procedure:**
1. Remove `cs_shift` in the workshop, add `freq_shift.knl`, Update OP-1 -> T1. Resave any sound that
   used cs shift (it falls back to the stock effect).
2. On a synth, idle: the arc, one needle straight up at 0 Hz, two green arcs, the arrow lit. Turn blue:
   **the needle swings** right for up, left for down, to the end ticks at the ends of the range (if it
   instead stretches, skews or stays put, the rotation isn't supported: report what it does). Green:
   one to four arcs, sweeping right for up, flipping to the left for down. White: the white arc under
   the rim widens with spread (the knob's value; on a synth the sound doesn't change, spread is a
   stereo effect). Orange: the right gauge fills from the foot.
   *First report (2026-10-27): no sign of white on a synth, and no sign of the green press: fixed in
   the rebuild (the arc shows everywhere; a green bracket round the mode marks while green is on
   page 2).*
3. On the master: two needles; white parts them, a white arc between them that follows the shift
   angle.
4. Play: dim ripples spread from the hub out to the rim and fade, more and brighter the louder
   the input (the sonar). Silence: none.
5. Press green: a green bracket round the mode marks. Turn green a click: the ring lights, the arrow dims; the needles mirror either side of
   the top at the carrier's angle, the feedback arcs sit centred on the top; the sound is the ring
   modulator; white still shows (the arc at the carrier's angle; on the master the carriers part).
   Another click: back. (Shift + a click: still one mode per click.)
   *Second report (2026-10-27): no sign of white in ring mode: fixed in the rebuild, with the
   ripples added at the user's ask.*
6. Press blue: a bracket round the left gauge, `+0.0` top left. Turn blue: the bar grows up from the
   centre to `+6.0` and down to `-6.0`; shift + a click moves a tenth. The sound: the shifted (or
   ring-modulated) side louder or softer; in ring mode harder at +6; at mix 0 nothing changes. Press
   blue again: back. Save at `+2.0` in ring mode, power-cycle: `+2.0`, ring mode.
7. Report in numbered points.

**Result (2026-10-27): freq_shift "looks great"** (the user), after two rounds: the green bracket,
the spread arc on every position and in ring mode, the sonar ripples. **The rotation matrix works on
the device** (the needle swings): the renderer's transform is a full affine. Session ended here; next:
tape echo, then plate / hall / Clouds when installed.


## Test 61: tape_echo (was cs_tape_echo) with the schematic screen, the gain on blue, the heads on white and the tempo lock; tape_drive (was tapesat) renamed (`build/knl/tape_echo.knl`, `build/knl/tape_drive.knl`)

**2026-10-28.** The user's pick from the canvas "Tape echo screen" (design B "Schematic",
https://claude.ai/artifact/KAu8e82XAZyk9QAYPJ6sdK; `docs/fx-briefs/designs/tape-echo-schematic.md`): the
Space Echo's signal path, record head, the white tape wave (age), three play heads sliding with the
time, the sum, the return path with green chevrons (feedback), a blue dot travelling the tape, the
family gauges. **The control map changed:** blue press → the input gain (word 5, what reaches the
record head, shift for tenths); white press → the head pattern (word 7, one click per pattern; it was
blue's); green latches hold as before. **New: shift + blue on the time page locks the time to the
project tempo's divisions** (1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4; a division a click,
clockwise = longer; the readout top left names it). **The unknowns:** (1) the tempo is read from the
stock firmware's LFO tempo word (`[0x1aadad4]`, `knl::TempoIncrement`); if hundekraft's core keeps it
elsewhere the plugin reads nonsense and falls back to 120 BPM, so the lock would be right at 120 BPM
only; (2) the lock rewrites the stored page-1 word the way the fine steps rewrite the page-2 words,
proven for page 2, not yet for page 1. Also renamed: `tape_drive` (was tapesat), the same build with
its id and screen file renamed.

**Procedure:**
1. In the workshop remove `cs_tape_echo` and `tapesat`, add `tape_echo.knl` and `tape_drive.knl`,
   Update OP-1 -> T1. The lists show "tape echo" and "tape drive". Resave any sound that used them.
2. tape echo on a synth, idle: the signal path, head 3 lit (the default pattern), two chevrons, the
   tape nearly flat (age 30 %), the gain bar at 0 dB, the mix gauge. Turn blue: the three heads slide
   right (longer) and left (shorter), the dot runs faster at short times. Green: one to four chevrons.
   White: the tape wave grows and breathes. Orange: the right gauge.
3. Play a note: echoes as before (the DSP is unchanged). Press green: the input arrow and record head
   dim, the return path and the glyph top right turn green, all four chevrons; the tape loops what it
   holds. Press again: back.
4. Press white: a white bracket round the heads, "3" top left. Turn white a click: "2+3", heads 2 and 3
   lit; again "1+2+3", again "1+3", again "3". Press white: back to age.
5. Press blue: the bracket round the gain gauge, "+0.0". Turn: the bar up and down, +6 .. -6 dB; shift
   + turn: tenths. More gain = a louder, more saturated echo; the dry sound unchanged. Press: back.
6. **The tempo lock.** Set the tempo to 120 (the metronome / tempo screen). On the time page, hold
   shift and turn blue one click clockwise: the time jumps to a division and its name reads out top
   left ("1/8", "1/8." ...), the heads jump with it, and blue 16th ticks (every 4th taller) appear
   over the tape from the record head while shift is held, head 3 sitting on a tick. Further clicks:
   the next division up, down for anticlockwise; the list ends at 1/16T and 1/4. Play a note with the
   sequencer or metronome running: the echoes land on the beat (at "1/4", one per beat; at "1/8T",
   three per beat). Release shift and turn blue normally: the readout disappears, the time is free.
7. Change the tempo to 90 and lock again: "1/4" should be 667 ms (slower repeats than at 120). If
   the repeats do not follow the tempo (the same at 90 as at 120), the tempo word isn't at the stock
   address on this core: report, and the sequencer feature's tempo source becomes the alternative.
8. tape drive: as Test 57, only the name changed.
9. Report in numbered points.

**Round 1 (2026-10-28): the screen and the renames fine ("looks ok for now"); the tempo lock wrong.** At 90 BPM
only 1/16 and 1/16T could be reached, at 120 only 1/16, 1/16T and 1/8T, and with head 3 alone the
repeats were not on the metronome. Read as unknown (2): the reachable divisions were the ones next to
the knob's own time, so the stock knob keeps its own copy of a page-1 value and writes it back on the
next click; the rewritten word held only until then. **Round 2:** the word is never rewritten. Each
shifted click is a step through the list and the plugin feeds the core the division's time in place
of the word (`TimeLock::Word`) until a turn without shift frees it; the lock isn't saved with the
sound. Repeat steps 6 and 7: all eight divisions reachable at 90 and 120, the repeats on the beat;
releasing shift and turning blue returns the time to the knob. If the repeats are still off the beat
while the readout says "1/4", unknown (1) is the cause: time a "1/4" against the metronome at 120
(500 ms) and at 90 (667 ms) and report which, if either, it matches.

**Round 2 (2026-10-28): "works and is sounding great. the tempo syncs nicely now."** So the tempo word
is at the stock address on hundekraft's core too (unknown 1 settled), and the lock as plugin state is
the right shape. **An install lesson on the way:** replacing the installed `tape_echo` in place (the same
id, Update OP-1 → T1) left the OP-1 on the "kennel: updated" screen, black; the safe start (power off
within a few seconds of the first screen, power on) got it back, and the same file added fresh after
a Remove booted fine. Replace a plugin by removing it first, restarting, then adding the new file.
**Round 3:** three longer divisions added, 1/2T, 1/4. and 1/2 (they fit the 1.5 s ceiling from 54, 60
and 80 BPM up; below that the walk skips them). Install `build/knl/tape_echo.knl` (remove, restart,
add). At 120: shift + blue clockwise past "1/4" reads "1/2T", "1/4.", "1/2" (667, 750, 1000 ms). At 50:
"1/4" is the last (1/2T would be 1.6 s).

**Round 3 (2026-10-28): "it's on and is great."** Test 61 done; tape echo is finished.

## Test 62: marbles and grids, the two sequencer features (`build/knl/marbles.knl`, `build/knl/grids.knl`)

Both are new ids: add them through the workshop (Your plugins → Update OP-1 → T1), no remove needed.
After the restart:

1. **The list.** On the synth, shift + SEQ (the sequencer chooser): "marbles" and "grids" after the
   stock and hundekraft entries. If one is missing, the workshop's inventory says why (API, an
   import, memory). Pick marbles: its screen (the "Dots" design: three lanes, the legend bars, the
   chip row, the page squares) replaces finger's.
2. **The transport.** With the sequencer on (SEQ lit), hold a key: Marbles runs from the press,
   chords from the gates within two octaves above the key, marks scrolling in the lanes. Release:
   silence. Hold a key, press shift, release the key: it keeps running; press another key: it takes
   over (a new root); release: it stops. Shift already down + a key: the key plays itself.
3. **The encoders** on the marbles screen: blue the rate (the first legend bar steps through the
   divisions), green deja vu, white jitter, orange bias; shift + turn: fine. Press blue: the chip
   marker moves (the T model); shift + blue: back. Press green: the bracket above the lanes appears
   dim (on), then bright (locked), then goes (off). Press white: the keyboard icon lights (keys mode:
   hold a chord, Marbles picks from it). Shift + white: the X page (the legend icons change): spread,
   length (the lower bracket's width), steps, bias; press blue there: the scale.
4. **Settings persist.** Change a few, pick another sequencer and marbles again, then restart: the
   same (table `marblesseq`). A sound saved with marbles comes back with it.
5. **Marbles on the drums.** Pick marbles on the drum instrument: holding a key plays the kit with
   Marbles' rhythms (a pitch picks a sound).
6. **grids on the drums.** Pick grids: its screen (the map, the kit, the four bars, the state icons).
   Hold a key: the bar runs on keys 0, 2 and 8 (kick, snare and hat in the factory kits), the
   playhead moving under the ticks, the kit pieces lighting on their hits. The latch and the
   take-over as in 2.
7. **The encoders:** blue and green move the map dot (the groove morphs), white chaos, orange swing;
   shift + turn: the fill bars (the shift icon shows). Press blue: Euclidean (the octagon; the bars
   are lengths); shift + blue: 32nds. Press green: another chaos draw. Press white: the assign arrow
   at the kick; press a key: the kick is that sound now (it auditions); white again: the snare, the
   hat, then the three accent keys, then off (the arrow goes). The keys are the transport again only
   once the arrow is gone. Shift + white: the die lights (the random voice on key 12).
8. **Print.** After a bar has played, shift + orange press; pick the stock "pattern" sequencer: it
   shows and plays the last bar (16 steps).
9. **Settings persist** as in 4 (table `gridsseq`). grids on the synth: the parts play as notes.
10. **Removal.** Remove both, restart: the OP-1 boots; a sound saved with "marbles" or "grids" runs as
    finger and keeps its name; add them back and it comes back.
11. Report in numbered points.

If the transport runs but the notes land off the grid (steps not on the metronome's eighths), the
finger-slot player's tick counter is not the half-beat phase tombola's was (Test 20): report, and
the plugin counts its own phase from the first tick of a run instead.
