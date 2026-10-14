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

## Test 1: probe (`op1_246_rings_probe.op1`)

The phase engine's render becomes `ports/rings/op1/probe.cc`. It's injected at `0x02c00000`; the rest of `0x02c00000–0x02ffffff` is filled with `0xa5a5a5a5`.

**What it answers:**
1. Does injected code run from the free region (the phase engine still sounds normal)?
2. What is the CPU clock?
3. How much CPU is free?
4. Does the OP-1 render idle voices?
5. Does anything else write to the free region?

**Procedure:**
1. Flash through the te-boot menu. Boot normally.
2. Select the **phase** engine. Set an envelope with full sustain, and preferably turn the effect off.
3. **Hold one key for about 45 seconds.**
   - First the normal phase sound plays.
   - The CPU load then ramps up slowly. Brief stutters near the end are expected.
   - Then the sound turns into a repeating sequence of three tones, each 1.5 s, separated by 0.5 s gaps and then a 2 s pause.
4. Keep holding a key and read the three tones' frequencies in Hz:
   - with a tuner app that shows Hz,
   - or record about 15 s with a phone and save the file in `incoming/`.
5. Optionally, then use tape, the sampler and USB disk mode for a while and check whether you hear a **two-tone siren**.

**Reading the tones:**

| Tone | Meaning | Example |
|---|---|---|
| 1 | CPU clock in MHz, as Hz | 400 Hz means 400 MHz |
| 2 | free CPU in MHz, as Hz | 230 Hz means about 230 MHz free while the phase engine plays |
| 3 | 100 × (fewest voices rendered in any block) + 50 Hz | holding one key: 150 Hz means only playing voices are rendered; 650 Hz means all 6 always are |
| siren (1000/1500 Hz) | something wrote into the free region | the region isn't safe; report it |

**If it goes wrong:**
- **Freeze or reboot loop when the phase engine is selected:** the region isn't executable, or it's in use. Flash stock via te-boot.
- **No change after 45 s while holding a key:** report it, with the engine and key used.

## Test 2: Rings engine (`op1_246_rings_engine.op1`)

The phase engine's render and note-on become Rings' modal model (`ports/rings/op1/engine.cc`): 6 voices with 10 modes each. In the simulator this costs about 31 MHz with one voice ringing and about 116 MHz with all six.

**To check:**
- Pressing a key gives a struck, bell- or plate-like resonance at the key's pitch.
- Which encoder does what. The code assumes params 0–3 = structure, brightness, damping, position; the screen still shows the phase engine graphics.
- The output level, and whether there's distortion.
- Stutters when playing many notes. If so, rebuild with fewer modes (`OP1RINGS_MODES_PER_VOICE`).
