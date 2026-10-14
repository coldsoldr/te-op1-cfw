# OP-1 firmware 246: baseline

Reference record of the stock package. The binaries are TE's and stay in the gitignored `op1 firmware/` folder.

## Identity

| Item | Value |
|---|---|
| Package | `op1_246.op1`, 13,039,128 bytes |
| Package SHA-256 | `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9` |
| Firmware | `R. 00246`, "Rev. 00246; 2022/11/09 16:17:00; 8.1.11.1" |
| Bootloader | `TE-BOOT v2.30`, built as "Rev. 00244; 2022/03/15 10:27:00" |
| CPU | ADSP-BF524C2 (Blackfin; the string appears in both images) |

| File | Size | SHA-256 |
|---|---|---|
| `te-boot.ldr` | 261,604 | `f6233b974bdb6c92f39e73401f206721924ef3ce52abd66b642a5350cebcad93` |
| `OP1_vdk.ldr` | 2,171,624 | `f79d299f6f9019c998013a4176223a6a1bc999a2d8c54451346861f6ea455c74` |

## Package format

```
[4 bytes]  CRC-32 (little endian) of everything after it   -> 0xcc08445c, valid
[rest]     LZMA "alone" stream: props 0x5d (lc=3 lp=0 pb=2), dict 8 MiB, unknown size
             -> 26,368,000-byte GNU tar, 117 entries, owner 0/0 "Admin"
```

Tar contents:

```
OP1_vdk.ldr            main firmware (VDK RTOS app)
te-boot.ldr            bootloader
content/
  op1_factory.db       synth/fx/lfo/seq/drum types + factory presets (SQLite)
  op1.db               user state: knobs, master, sequencer, slots, fatsync
  tape.db              tape position/loop/clips
  kerntable.db         font kerning (73 pairs)
  display/*.svg        61 UI screens/graphics (rendered on device)
  audio/               40 .raw files: drum/, synth/, factory_drum/, factory_synth/, speech/op1patch.raw
                       (preset_drum/ and preset_synth/ are empty)
```

## LDR images (from `tools/ldr_dump.py`)

Both files are BF52x LDR streams. Every header's XOR checksum is valid, and both parse exactly to EOF.

**te-boot.ldr**: 2 DXEs, 118 blocks
- DXE 0: init code. It loads 2.5 KB into L1 instruction SRAM at `0xffa00000` plus a little L1 data, then an INIT block calls `0xffa00000`. This is most likely the PLL and SDRAM controller setup that the boot ROM runs before the main load.
- DXE 1: the bootloader proper. Code goes in low SDRAM (`0x00000004`–`0x00029293`, about 165 KB) and L1 (`0xffa00000`, 48 KB). Large FILL blocks zero `0x01000000`–`0x0303ffff`. FINAL jumps to `0xffa00000`.

**OP1_vdk.ldr**: 1 DXE, 801 blocks, **no INIT block**
- SDRAM `0x01000000`–`0x01dfff33` (14.7 MB, code and data), `0x02000000`–`0x020257ff`, `0x02040000`–`0x027ffb03` (8 MB), `0x03000000`–`0x0303fffe`.
- L1 instruction 48 KB at `0xffa00000`, L1 data A 12 KB, L1 data B 32 KB. FINAL jumps to `0xffa00000`.
- With no INIT block, the image relies on SDRAM already being set up, so **te-boot (not the boot ROM) loads OP1_vdk**. te-boot keeps its own code below `0x01000000`, and the app lives from 16 MB upward. SDRAM use reaches `0x0303ffff`, which implies at least 64 MB of SDRAM.

## SQLite schemas (row counts in 246)

`op1_factory.db`
- `fx_types(id, type, default_params)`, 7 rows: 3 grid, 4 punch, 5 delay, 7 phone, 8 spring, 9 cwo, 10 nitro. **ids 1, 2 and 6 are free**; op1repacker puts `filter` at id 2.
- `synth_types(...)`, 10 rows: 1 cluster, 2 digital, 3 drwave, 4 fm, 5 phase, 6 pulse, 7 dna, 8 string, 9 dsynth, 10 voltage. **id 11 is free**; op1repacker puts `iter` there.
- `lfo_types`, 7 rows: tremolo, value, random, element, midi, bend, crank
- `seq_types`, 6 rows: pattern, endless, tombola, finger, sketch, arpeggio
- `drum_types`, 1 row: dbox
- `synth_presets(id, patch, folder)`, 169 rows (16 per synth, 9 sampler). `drum_presets`, 24 rows.

`op1.db`: `knobs`, `master`, `sequencer`, `fatsync(was_mounted, new_firmware_path)`, `endless`, `drum_user`, `synth_user`, `presync_files`, `drumseq`, `okseq`, `synth_slots`, `drum_slots`.
`fatsync.new_firmware_path` suggests the running app spots a firmware file dropped onto the USB disk and passes it to the flasher.

`tape.db`: `clips`, `tape`. `kerntable.db`: `kerntable(a, b, kerning)`.

## Software stack (from strings)
- VDK (VisualDSP++ kernel); toolchain tag `8.1.11.1`
- smx USB device stack (USB mass storage)
- YAFFS2 on raw NAND (`te_yflash2.c`, `nand.cpp` with `NUM_NAND_BOOT_BLOCKS` write-lock)
- FatFS (`bootfatfs`) for the USB disk view
- SQLite (`sqlite3-bf`), libaiff 5.0
- Peripheral drivers named: SPI, TWI codec, GPIO_UI1–5 and GPIO_MASTER, Class D amp, accelerometer, battery gauge, FM radio, RTC, OTP, potentiometer

## Repack round-trip (op1repacker 0.2.6, unmodified tree)
- Output 12,091,918 bytes (under the ~15 MB limit). It still contains te-boot.ldr, so the guard now rejects it. Use `tools/build_package.py`, which drops te-boot.ldr: output 11,993,064 bytes, guard passes.
- All 117 entries are **byte-identical in content** to stock.
- Metadata differs from TE's package:
  - LZMA props `0x66` (lp=1) instead of `0x5d` (lp=0)
  - tar uname `root` instead of `Admin`
  - modes `0666`/`0777` instead of `0100666`/`040666`
  - entry order
- These differences have long been tolerated in the community (op1repacker builds install fine), but they are not stock-identical.

## Scrambled app L1 code
- 45 of OP1_vdk's blocks are `CALLBACK|INDIRECT` (80,324 bytes, all of it L1: data A, data B and the 48 KB L1 instruction image). The loader transforms these payloads at boot.
- The raw L1 instruction payload has 7.40 bits/byte of entropy and 15% of it decodes as illegal instructions. te-boot's own L1 code has 6.65 bits/byte and 0% illegal. So the payload is scrambled or encrypted, not plain code.
- One `CALLBACK|IGNORE|INDIRECT` block (240 bytes at `0xff907e00`) holds repeating 16-byte records (`878ed60f0f4721cb` + 8 zero bytes), most likely parameters for the callback.
- All SDRAM code and data (≈23 MB of image) is plain and disassembles cleanly.
- te-boot's own blocks use no callbacks.
