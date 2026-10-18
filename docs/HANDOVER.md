# Handover: OP-1 custom firmware

Last updated 2026-10-17 (end of session). Read this first in a fresh session; it's self-contained, and the docs it links to have the detail.

**Where things stand (2026-10-17):**
- **On the device:** whatever the user last flashed. Known good: `op1_246_rings_clouds4.op1` (Rings MULTI + Clouds on phone/"multi", Tests 17–19). Recovery is always COM at power-on + flash clouds4.
- **seq3 crashed on a key press on the Grids screen** (Test 23 result: stock pattern's controller looked up pattern.svg nodes in grids.svg). Fixed in seq4 (Test 23b). seq5 (SVG subpath split) didn't boot: its split left unbalanced `</path>` tags. **seq6 (Test 23c) is the corrected split. seq7 (Test 24) adds the Marbles colour pass (green = deja vu, swapped encoders and white clicks). seq8 (Test 25) adds Rings: the screen follows the LFO, no CPU meter, no main-window frame, a green polyphony frame (Test 25 all good). seq9 (Test 26) is the first beta: Clouds' main frame white, bottom-right frame removed. Test 26 good: seq9 is the first beta. seq10 (Test 27) aims at the user's next two items (Rings tape animation choppy with more than 3 voices while recording; Clouds and Rings together): the OP-1 runs without a data cache, so Rings' per-call buffers moved to the L1 scratchpad (`src/scratch.h`), bit-identical in the simulator. Test 27: animation still choppy, Rings + Clouds unchanged, slightly better at 4 voices: not the main cost. Next: measure on the device: `op1_246_rings_cpuprobe1.op1` (Test 28: 399 MHz, data cache present, Rings ~25 MHz a voice on the device ≈ the simulator, Clouds ~81 MHz, the screen starves above ~40% load). cpuprobe2 / seq11 (Test 29): Rings mode budget of 60 in "all", tails sleep at -72 dB, Clouds' governor share 0.40.**
- **Test 23's build: `op1_246_rings_seq3.op1` (also Test 22's Marbles steps):** clouds4 + **Marbles as the synth sequencer** (on tombola, Dots screen) + **Grids as the drum sequencer** (on pattern, "Panel + kit" screen). Build: `make op1-seq` in `ports/rings`; packaging recipe below.
- **Committed and pushed** up to `eff35fa` (Marbles Dots screen). **Not committed yet:** the Grids glue and screen (`ports/grids/op1/`, `tests/sim_grids.cc`, Makefile), the `kCall24` patch kind (`tools/inject_app.py`, `op1.h`), the `_x`/display/transform fixes in `ports/marbles/op1/gen_marbles_svg.py` and the `loopX`/`legNX` ids in `marbles_op1.cc`, `docs/sequencer-interface.md` §12, Tests 22–23 notes, `docs/grids-port.md` §9. Commit only when the user asks.

**Next steps:**
1. The user's Test 23 results (numbered points). Fix what they find.
2. **Show pattern as "MULTI"** (the user's decision): the chooser looks the current type up by its shown name, so the query rename alone would crash it. `GridsSeqName` exists (`-DOP1_GRIDS_RENAME`, off) but the chooser's CALL at `0x1970bea` can't reach region A (pcrel24 = ±16 MB, ~19.6 MB away). Needs a trampoline within 16 MB (e.g. region C `0x01e00000`, untested): its own small test.
3. Later: Grids/Marbles settings in a private op1.db table (RAM only now); the knob speed if the user wants it changed; Rings + 2 Clouds CPU strain (accepted for now).

**SVG lessons (a bad screen file stops the boot until COM recovery):** ids must not contain `_x` (the parser decodes `_xHH_` escapes: `Parser.cpp:231`, marbles5/6); the OP-1 folds group transforms into children (draw driven elements in screen coordinates); no arcs, no exponents, ≤ 2 decimals; `display` only proven on g/line/polyline/rect/path/ellipse. Every design goes through its `ports/*/op1/gen_*_svg.py`, which enforces all of this. Designer briefs: `docs/marbles-screen-brief.md` (v2), `docs/grids-screen-brief.md`.

**Packaging recipe** (from the repo root, `.venv/Scripts/python.exe`): copy a known `op1 firmware/work_*` dir (e.g. `work_seq3`) to a new one; regenerate `content/display/*.svg` with the gen scripts; `tools/inject_app.py "op1 firmware/op1_246/OP1_vdk.ldr" <work>/OP1_vdk.ldr.new ports/rings/build/<image>.elf`, move it over `OP1_vdk.ldr`; `tools/build_package.py <work> "op1 firmware/op1_246_rings_<name>.op1"`; `tools/verify_bootloader.py` must PASS; check every hook target against `bfin-elf-nm`.

**Key docs:** `docs/device-tests.md` (Tests 17–23 and results), `docs/sequencer-interface.md` (sequencers, probe results §10, pattern data/print and the rename §12), `docs/marbles-port.md` §9, `docs/grids-port.md` (Decisions, §9), `docs/clouds-port.md`. Simulator checks: `make sim-seq` (ports/marbles), `make sim-grids` / `make compare` (ports/grids), `make sim-fx` / `make test` (ports/clouds).

**Older (Clouds integration, 2026-10-16):** §3 below; the Clouds sections are history now that clouds4 is on the device.

## 1. Goal and hard rules

Custom firmware for the **original TE OP-1**: Analog Devices Blackfin ADSP-BF524C2 (~400 MHz, no FPU), app firmware 246, bootloader TE-BOOT v2.30. The owner has **one unit**. **The bootloader must never be modified or rewritten.**

Non-negotiable rules (detail in `docs/brick-safety.md`):
1. **Never put `te-boot.ldr` in a package.** Build with `tools/build_package.py`, which drops it. (op1repacker's own `repack` command packs it: never use that.)
2. **Every package must pass `tools/verify_bootloader.py`** before flashing (`build_package.py` runs it).
3. **Never use `FRC` in a package file name** (it forces a bootloader rewrite).
4. **Never put a `.KEY` file on the OP-1 disk** (irreversible OTP key programming).
5. **Install only through the te-boot menu:** hold COM at power-on, then "1. Upload firmware".
6. **Ask the user before anything touches the device.** The user flashes; we prepare and verify. Before asking, diff every package against stock: only the intended files and words may differ (`docs/rings-op1-reference.md` §5 has the procedure).
7. **Recovery:** hold COM at power-on and flash stock `op1 firmware/op1_246.op1`. This works whatever state the app is in.
8. **Before ever flashing stock again, the user restores factory defaults first** (their decision), so no saved slot refers to anything stock firmware lacks.

How the user works:
- They test on the device and report back in numbered points; device tests are logged in `docs/device-tests.md` (procedure before the flash, result after).
- They stay on branch `firmware-analysis-tooling`.
- Commit and push only when they ask.
- Decisions they've made are recorded (§2, §3); don't re-ask them.

## 2. Where things stand

**On the device: `op1 firmware/op1_246_rings_fxprobe1.op1`.** This is multi4 (Test 15: "everything sounds great, everything is working a treat") plus the FX probe on phone (Test 16). Phone sounds normal, but turning its blue encoder starts a CPU measurement (stutter, then beeps); white resets it. Reflash `op1_246_rings_multi4.op1` for plain phone.

**The phase slot is the "MULTI" engine:** each slot plays **stock phase** or **Mutable Instruments Rings** (all six models), switched by an orange encoder click on the engine page. The lists show "multi" for "phase" (display only). Everything about it (controls, architecture, hooks, CPU, tests, package recipe): `docs/rings-op1-reference.md`. Design and validation history: `docs/rings-port.md`.

**Known Rings issues, to revisit at the end** (the user is happy as it is):
- **Model 2 and model 6 crackle somewhat in polyphony "all"; polyphony 4 is fine** (Test 15).
- **Rings model 6 × 4 voices costs ~255–290 MHz on the device** (Test 16): ~2× the simulator estimate, most likely the data cache, since its reverb and strings live in SDRAM. That's the main lever if Rings needs to be lighter.

**Decided with the user (Rings), don't re-ask:**
- sympathetic strings tune around the played note; the string models' mono mix is upstream's per-voice `out − aux`;
- model 6 plays at most 4 voices in "all";
- "all" has 14 modes per modal voice; polyphony 1, 2 and 4 are upstream's;
- the modal and FM output stay bit-identical (checksums: modal `ef8618dd`, FM `567afc0c`, model 6 `7d5e0bee`);
- the brightness handle spans 0..29491 (display only);
- PORTAMENTO → PORTA is dropped: don't propose it.

**Proven on the device** (detail in `docs/device-tests.md`, `docs/engine-interface.md`):
- **Synth voices:** rendered at 88.2 kHz, 128 samples per call, one call per sounding voice per block.
- **Effects:** called at 44.1 kHz, 64 samples per call, ~690 calls/s (Test 16).
- **CPU:**
  - ~300 MHz free with nothing playing;
  - the OP-1's own audio work is ~15% of the period;
  - **the device runs ~1.5–1.8× the simulator's cycle counts** (Rings; Clouds is unmeasured).
- **Injected code at `0x02c00000` runs;** the app has no integrity check. An audio overload is survivable (crackle); a memory-corrupting bug is not (power cycle).
- **Patching works through data only:** vtable words, imm16 immediates, content files. Calling firmware functions from our C++ works.

## 3. Clouds as an effect

The detailed design, validation and numbers are in **`docs/clouds-port.md`**. The effect interface (static analysis) is in `docs/next-projects.md` §1, and the device measurements in `docs/device-tests.md` Test 16. The screen brief for the user's designer is `docs/clouds-screen-brief.md`.

**Decided with the user (2026-10-16), don't re-ask:**
1. **Host: phone.** **The orange click on the FX page swaps each effect between Clouds and stock phone** (as MULTI does for phase/Rings). "Filter" stays free for op1repacker's filter mod.
2. **Both positions:** the master effect (stereo) and the synth effect (mono). One core class, an instance per position.
3. **Modes in order:** looping delay → granular → time stretch → spectral (last, or never).
4. **Controls:**
   - page 1 encoders: position, size, pitch, density;
   - page 2 encoders: texture, dry/wet, feedback, reverb;
   - **stereo spread fixed for now**;
   - clicks: **blue = next mode** (shift = back), **green = freeze**, **white = page**, **orange = Clouds/phone**;
   - shift + turn = fine adjustment;
   - mode and page are saved in `fx_params[4..7]`.
5. **CPU trade-offs are left to us** (quality limits, the governor).
6. The user is drawing **`clouds.svg`** (two pages; brief in `docs/clouds-screen-brief.md`). Until it arrives, Clouds can run with phone's stock screen.

**State of `ports/clouds/`:**
- **Looping delay and granular are done and verified** against upstream (21 scenarios: 52–65 dB SNR, spectra within ~0.5 dB). The remaining gaps are upstream's own float rounding, proven with a double-precision diagnostic copy, or the harness's parameter quantisation. Time stretch and spectral output silence.
- **Assembly kernels**, each bit-exact with a C reference (`make test`; the reverb and diffuser are tested over more than a trip round their buffers, with guard words):
  - grain overlap-add (linear/Hermite × mono/stereo, 16-bit dual-MAC);
  - the stereo reverb;
  - the diffuser.
- **CPU (simulator, one stereo instance):**

  | Config | MHz |
  |---|---|
  | granular, ~7 grains | ~28–30 |
  | granular, ~20 grains | ~51 |
  | granular with reverb, feedback and diffusion | ~49 |
  | looping delay, plain | ~31 |
  | looping delay with diffusion, feedback and reverb | ~49 |
  | mono, roughly | ~0.6× stereo |

  Still in C: the pitch shifter, the texture SVFs, the looper's Hermite reads, dry/wet with the soft limiter.
- **Governor** (`src/core/governor.h`, `make test-governor`): sets the grain cap per call.
  - It takes the call interval, Clouds' own cycles and our engines' cycles.
  - Feed-forward: 0.75 of the period minus the engines.
  - Feedback: overload is detected by the mean interval or a late call. It learns a ceiling on Clouds' cycles for load it can't see.
  - Simulated: no late calls under measured load, settles under hidden load, recovers. The looping delay has no grains, so its fixed cost (~50 MHz on the device, estimated) can still overload next to heavy Rings.

**Firmware facts found for the hookup** (detail in `docs/clouds-port.md` §5):
- **Phone's process vtable words:**
  - mono `0x1911308` (stock `0x01a1bb74`);
  - stereo `0x1911328` (stock `0x01a1c25c`);
  - the stereo process runs the mono one on two sub-objects;
  - the stereo params arrive at `[SP+12]`;
  - a switched-off effect isn't called (treat a gap > 5 ms as a restart).
- **Phone's encoder ranges** (the FX Ranges table, built at boot; obtained by running its initialiser `0x1a1e914` in `bfin-elf-gdb`'s simulator; entries `{min, max, x}` at `0x1de5c50 + type × 48 + knob × 6`, phone = type 3):

  | Encoder | Range |
  |---|---|
  | blue | 204..20480 |
  | green | 3072..17408 |
  | white | 1536..16896 |
  | orange | 0..32767 |
  | knobs 4..7 | 0..32767 |

  Normalise from the table at run time.
- **Synth FX page:** sub-page 2 of `[0x1b4ee88]` in the synth controller (vtable `0x1911994`, whose key words Rings already hooks: `op1/input.cc`).
  - Knob case `0x1a50690`;
  - FX type `[0xff801734 + slot × 4]`;
  - values `0xff801444 + slot × 16`;
  - saved by **`0x19ff664(patch, values)`** (like `SetKnobs` `0x1a00d14`; patches `0x1ddd7e8 + slot × 0x38`).
- **Master FX page:** `MasterController`, primary vtable **`0x1910aac`**:
  - `+0x0c` destructor `0x19d99a0`;
  - `+0x14` key down `0x19db7c4` (cases `0x2d–0x2f`);
  - `+0x1c` shift key down `0x19d987e`;
  - `+0x24` key up `0x19db708`;
  - `+0x2c` knob `0x19da2bc` (uses `0xff8016c4`).
  - **Still to find:** which master subscreen is showing (asserts `subscreen_ <= kCompressor`, state near `0x1aad5d8`), the master FX type variable, and how master FX params are saved (op1.db's `master` table).
- **Phone's screen:** `ptch.svg` immediates `0x1a1c7cc` / `0x1a1c7d0`; PhoneScreen Init/Update words `0x1911358` / `0x1911350` (the PdScreen swap pattern Rings uses).

**Integration done (2026-10-16), awaiting Test 17:**
- **Build:** `ports/rings/Makefile` target `op1-clouds` (`-DOP1_CLOUDS`, `-I../clouds -I..`): the engine sources plus `ports/clouds/op1/clouds_fx.cc` and the Clouds core. `op1-engine` still builds multi4 byte-identical.
- **Glue:** `ports/clouds/op1/clouds_fx.cc`.
  - The mono and stereo process entries replace phone's two words. Each position plays Clouds or calls stock phone.
  - Position: master = the stereo call (values at the passed pointer, `0xff8016c4`). Synth = a mono call whose `self` is the synth effect object (`[0x1aaad60]`; producers per position at `0x1aaad60/64/68`); the drum effect stays phone.
  - The synth's values are read from `0xff801444 + [0x1aaac0c] × 16`, not the passed pointer (Test 16: that is elsewhere).
  - Restart on a gap > 5 ms, after phone, or on a slot change. `Init` clears ~350 KB, ~115k cycles in the simulator; the restarting call passes the input through.
  - The governor is fed with `op1fx::measured_cycles`, a running total that `RingsRender`/`RingsNoteOn` and both Clouds instances add to. Its first 16 calls after a restart are skipped.
- **Settings** (`ports/clouds/op1/clouds_settings.h`):
  - `values[0..3]` are the shown page (the encoders);
  - `values[4]` holds the engine bit (**0 = Clouds: phone is Clouds by default**), the mode index (2 bits, in the user's order), the page bit and the hidden page's first value (11 bits);
  - `values[5..7]` hold the hidden page's other values;
  - hidden values are stored as offsets from defaults: dry/wet 50%, texture 50%, feedback/reverb 0;
  - the white click swaps the encoders' values with the hidden page's, so both pages persist.
  - **Verified in the firmware:** the synth FX knob case (`0x1a50690`) writes `0xff801444 + slot × 16`; shift + turn writes `[k − 4]` with delta / 16, so it never touches `[4..7]`.
- **Clicks:**
  - synth FX page through `op1/input.cc` (sub-page `[0x1b4ee88] == 2`, slot FX type `[0xff801734 + slot × 4] == 3`), saved with `0x19ff664`;
  - **master FX page through MasterController's key words** `0x1910ac0/ac8/ad0` (stock `0x19db7c4/0x19d987e/0x19db708`; stock handles no encoder click there). The page is active when `subscreen_ [0x1aad5d8] == 2`; the knob case's subscreen 2 copies `0xff8016c4`. The master effect counts as phone when the master producer's vtable is `0x1911314`.
  - **No master save call is known.** Clicks write the live `0xff8016c4` values; whether they persist is a Test 17 question.
- **Simulator** (`ports/clouds`: `make sim-fx`, `tests/sim_fx.cc`):

  | Case | MHz |
  |---|---|
  | master looping delay | ~31 |
  | master granular, ~7 grains | ~31 |
  | master granular, dense | ~55 |
  | synth looping delay | ~20 |
  | synth granular | ~29 |

  The settings and click round trips are checked too.
- **Choices made here** (the user may revisit):
  - phone = Clouds by default;
  - stereo spread fixed at 50%;
  - pitch ±24 semitones with a small centre dead zone;
  - freeze isn't saved;
  - the blue click cycles only the implemented modes (looping delay, granular).

**Next steps:**
1. **Test 17 on the device** (the user flashes `op1_246_rings_clouds1.op1`). From its results:
   - Clouds' device/simulator ratio, and the governor's share;
   - whether master settings persist (if not, find the master save: op1.db's `master` table);
   - whether synth/drum detection via the producers holds.
2. **The screen:** the user's `clouds.svg` in place of `ptch.svg`, swapped by the setting (as `pd.svg`/`rings.svg`). Show the mode, page, freeze and governor state. Phone's screen words: §3 facts above.
3. **Later:**
   - the pitch shifter in assembly (~4 MHz);
   - kernel scheduling into parallel slots (~30% of grain cost);
   - `GranularPlayer::OverlapAdd`'s 64-bit division per grain and block (`__divdi3`);
   - time stretch (WSOLA);
   - the screen polish.

**The original plan, for reference (steps 1–3 are done above):**
1. **One image with Rings and Clouds.**
   - Today `ports/rings/Makefile` builds the image (`op1-engine`; the FX probe variant `op1-fxprobe` with `-DOP1_FX_PROBE`).
   - Add Clouds' `src/core/*.cc` and `*.S` with `-I../clouds`. The include roots work: `src/core/...` exists only in clouds, `src/...` in rings.
   - Or make a top-level image target; restructuring the build is in scope.
   - Mind the image size: each `CloudsCore` holds ~0.5 MB at 44.1 kHz (record buffers, reverb 142 KB, diffuser, pitch shifter). Two instances fit region A `0x02c00000–0x02ffffff` with Rings (check `bfin-elf-size`; regions B/C in `docs/memory-map.md` are rated good but untested).
   - No static constructors; no soft-float.
2. **The glue, `ports/clouds/op1/clouds_fx.cc`:**
   - mono and stereo process entries in place of phone's two words;
   - per position: Clouds or stock phone (the orange-click setting), with a call-through to stock phone's process;
   - restart on a > 5 ms gap;
   - encoders normalised from phone's ranges;
   - the governor fed with `Cycles()` around the core and our engines' render cycles (add a counter to `RingsRender`, as the FX probe's `render_calls`);
   - mode and page from `fx_params[4..7]`;
   - a simulator check (`make sim-fx`, Makefile target already named).
3. **Click hooks:**
   - synth FX page: extend `op1/input.cc` (sub-page 2, the slot's FX type = phone 3);
   - master page: hook `MasterController`'s key words. Find the subscreen state first, or start with the synth position only.
   - Persist with `0x19ff664` on the synth side; find the master equivalent.
4. **The screen:** the user's `clouds.svg` in place of `ptch.svg`, swapped by the setting (as `pd.svg`/`rings.svg`). Show the mode, page, freeze and governor state.
5. **Package and device test** (Test 17): the recipe and checks as in `docs/rings-op1-reference.md` §5, plus `ports/clouds` in the image. Measure Clouds' device/simulator ratio (its large buffers may cache worse than Rings') and tune the governor's share.
6. **Later:** the pitch shifter in assembly (~4 MHz), kernel scheduling into parallel slots (~30% of grain cost), time stretch (WSOLA), the screen polish.

## 4. Environment

**Windows 11:**
- `.venv/Scripts/python` (Python 3.11; no numpy). Run tools from the repo root.
- `ffmpeg` is on PATH.

**WSL Ubuntu 20.04:**
- Toolchain in `~/op1-toolchain/bfin/bin/`: binutils 2.43, gdb 15.2, GCC 14.2 C/C++ with newlib.
- Run make like this: `wsl -e bash -lc 'cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/clouds && make …'`.
- `ports/clouds` targets:
  - comparing with upstream: `ref`, `copy`, `copy-check`, `copyd` (double-precision diagnostic), `port`, `port44`, `compare`, `spec`;
  - checks: `test` (kernels: assembly = C), `test-governor`;
  - cost: `profile [S=prefix PROFILE_BLOCKS=n]` (MHz per configuration on Blackfin).
  - `bench` works but is very slow: the harness's float input generator runs as soft-float on Blackfin.
- Simulator: `bfin-elf-run` (L1 isn't mapped; simulator builds use `-DOP1_SIM`). `--trace-insn=on` gives an instruction trace; `ports/clouds/harness/profile.py` / `profile_lines.py` turn it into instructions per function / per source line.
- **Running firmware code in the simulator** (how the FX ranges were obtained):
  1. wrap `op1 firmware/analysis/ldr/OP1_vdk_dxe0_01000000.bin` as an ELF: `bfin-elf-objcopy -I binary -O elf32-bfin -B bfin --change-section-address .data=0x01000000 --rename-section .data=.text,alloc,load,code,contents --set-start ADDR`;
  2. in `bfin-elf-gdb`: `target sim --environment operating`, `load`, break at ADDR, `run`;
  3. set SP, FP and RETS (to a breakpoint), `continue`, `dump binary memory`.

**Gotchas:**
- **Shell heredocs mangle `\n`, `\\` and line continuations** (it happened again this session). Write edit scripts with the Write tool (e.g. into `%TEMP%\clouds_tools\`), then run them; heredocs only for text without backslashes.
- **libgcc divisions are hundreds of instructions on Blackfin**: keep `/` and `%` by non-constants out of audio loops. **`FFloat(double)` at run time is soft-float**: make such constants `static constexpr`.
- **GCC 14 on Blackfin:** selects and saturating shifts become taken branches; hot loops go to assembly with C references.
- **Blackfin assembly rules learnt with the Clouds kernels:**
  - half-register loads (`R0.L = W[..]`) only through **I registers**;
  - a dual MAC's two halves must use **the same two source registers** (pack operands accordingly);
  - `R3.H = R4.L` isn't a move: use `R3 = PACK (R4.L, R3.L)`;
  - only a low half zero-extends (`R4 = R2.L (Z)`; the high: `R5 = R2 >> 16`);
  - a 32-bit extraction from A0 goes to an **even** register (`R2 = A0`), from A1 to an odd one;
  - the `LSETUP` end label must sit on the loop's **last** instruction;
  - immediate load/store offsets up to 65534 bytes are fine outside parallel slots.
  - Keep the `L` registers at 0 (no circular buffers: use Rings' no-wrap buffer with mirrored writes).
- **Use the cycle-accurate profile driver, not guesses**: the first Clouds C build was 2× slower than estimated.
- **The simulator doesn't fault on stray writes and doesn't model the cache.**
- **The image has no startup code or run-time initialisers:** link-time constants only; no `.init_array` / `.ctors`.
- **objdump's `/* R0=… */` annotations can be stale;** trust the `.L`/`.H` pairs. Patch-table imm16 addresses are the instruction address + 2.

**Reverse-engineering material** (outside the repo):
- `op1 firmware/analysis/`: the app's ELF, disassembly (`OP1_vdk.dis`) and the extracted LDR memory (`ldr/`).
- `%TEMP%\op1re\`: indexes `compact.txt`, `calls.txt`, `consts.txt`, `srctags.txt`, `vcalls.txt`; helpers `op1lib.py` (`rd`, `u32`, `cstr`), `show.py START END` (annotated disassembly). The L1 code at `0xffa0xxxx` doesn't disassemble cleanly.
- `%TEMP%\op1inject\`: `check.py` (byte-diffs an injected LDR against stock), `pkgdiff.py` (diffs packages), built LDRs.
- `%TEMP%\clouds_tools\`: this session's scratch scripts (`segsnr.py`: SNR per time segment; `ranges.gdb`). Disposable.

## 5. Other ports (parallel agents) and projects

**Other agents port modules; this session integrates them** (user, 2026-10-16). Their folders and docs, untracked:
- `ports/braids` (MULTI engine; `docs/braids-port.md` when written);
- `ports/plaits` (MULTI engine);
- `ports/streams` (**an effect**: it will need its own slot or a place in the orange-click cycle; settle with the user);
- `ports/marbles` (an LFO; `docs/lfo-interface.md` maps the OP-1's LFO interface);
- `docs/streams-port.md`, `docs/marbles-port.md`.

The announced default, **not yet confirmed by the user:** with 3+ engines, the orange click cycles phase → Rings → Braids → Plaits. Every module shares the CPU budget (§2).

Specced earlier (`docs/next-projects.md`):
- op1repacker mods as `tools/mods.py` (content only; interacts with the preset budget);
- Rings factory presets;
- a builder app;
- small items: CWO → COW display rename, the CPU meter in release builds, the green encoder's range, FM optimisation.

## 6. Git

- Branch `firmware-analysis-tooling`, remote `origin` (`<public repo URL>`). Commit only when the user asks.
- **Uncommitted from 2026-10-16 (this session's work):**
  - modified: `docs/HANDOVER.md`, `docs/device-tests.md` (Test 16), `docs/next-projects.md`, `tools/decode_probe_beeps.py` (v4), `ports/rings/Makefile`, `ports/rings/op1/{engine.cc, patches.cc, patches.h}` (FX probe, under `OP1_FX_PROBE`; `make op1-engine` still builds multi4 byte-identical);
  - new: `ports/rings/op1/fx_probe.*`, `ports/rings/tests/host_fx_probe.cc`, `ports/clouds/`, `docs/clouds-port.md`, `docs/clouds-handover.md` (superseded), `docs/clouds-screen-brief.md`;
  - integration (later session): `ports/clouds/op1/`, `ports/clouds/tests/sim_fx.cc`, `ports/rings/{Makefile, op1/input.cc, op1/input.h}` and more `OP1_CLOUDS` blocks in `ports/rings/op1/{engine,patches}.*`, Test 17 in `docs/device-tests.md`;
  - **other agents' folders:** `ports/braids`, `plaits`, `streams`, `marbles` and their docs.
- **Before committing:** gitignore `ports/clouds/build/` and `ports/clouds/out/` (renders); check the other ports likewise.
- **Never commit TE firmware:** `op1 firmware/` and `mutableinstruments/` are gitignored. `incoming/` belongs to the user and stays out of git.

## 7. Docs and memory

| Doc | Content |
|---|---|
| `docs/clouds-port.md` | **Clouds:** state, layout, validation, CPU, governor, OP-1 hookup facts |
| `docs/clouds-screen-brief.md` | The Clouds screen brief (two pages, controls, SVG constraints) |
| `docs/rings-op1-reference.md` | Rings / MULTI on the device: controls, architecture, hooks, CPU, tests, **package recipe and checks** |
| `docs/rings-port.md` | The Rings port's design, validation and measurements |
| `docs/next-projects.md` | Specs: the OP-1's effect interface (§1), engines, builder app, op1repacker mods, presets |
| `docs/device-tests.md` | Every device test (probes v1–v4, Tests 2–16): procedure and result |
| `docs/engine-interface.md` | Synth engine types, factory, voice vtables, render ABI, pitch |
| `docs/screen-interface.md` | Screen class, SVG API (what can change at run time), input path, presets, encoder clicks |
| `docs/memory-map.md` | App load map; free regions (A `0x02c00000–0x02ffffff` in use; B and C rated good, untested) |
| `docs/brick-safety.md`, `docs/firmware-246-baseline.md`, `docs/app-boot-chain.md` | Safety and groundwork |
| `ports/rings/README.md` | Rings port layout and harness builds |

Persistent notes: `%USERPROFILE%\.claude\projects\I--GitHub-PersonalProjects-OP1-CustomFirmware\memory\` (index `MEMORY.md`).
