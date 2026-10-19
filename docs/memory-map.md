# OP-1 firmware 246: app memory map and free regions

Static analysis of `OP1_vdk.ldr` (firmware 246) to find where we can safely add about 256 KB of new code and about 200 KB of DSP buffers as an extra plain LDR block. Addresses are runtime addresses. Nothing here has been tested on the device yet.

Tags used throughout:
- **[C] confirmed**: seen directly in code or data.
- **[I] inferred**: follows from the evidence but wasn't directly observed.

A caveat that applies to everything below: all app L1 blocks (L1 code and L1 data) are XTEA-encrypted in bytes 0–7 of every 24-byte chunk. The chunking restarts at the start of each block [C, measured per-block entropy]. So about a third of the L1 code and data can't be read, and references made only from encrypted L1 bytes can't be ruled out. SDRAM code is fully readable.

## 1. Method
The analysis scripts are throwaway files in `%TEMP%\op1mem\` (not in the repo).
- `blocks.py`: groups the LDR blocks (`tools/ldr_dump.py` `parse()`) into contiguous runs and splits load bytes from FILL bytes.
- `pairs2.py`: streams `OP1_vdk.dis` and pairs every `Rx.H = imm` with the matching low half in the same register within 16 lines.
  - The low half can be `Rx.L = imm` or a full-register load `Rx = imm (X|Z)`. The compiler often uses the second form, e.g. `P0 = 0xc48 (X); P0.H = 0xffc0`. Looking only at `.L`/`.H` pairs misses most MMR and many SDRAM constants.
  - Result: 16,848 constants.
- `synth.py`: finds constants built with `Rx = imm; BITSET(Rx, n)` or `Rx <<= n`. For example, 0x02000000 is built with `BITSET(R0, 25)`.
- `words.py` / `words2.py`: scan the loaded image (data sections and L1) for aligned 32-bit words that point into each candidate region.
- `calls.py` / `callers.py`: index of CALL and JUMP.L targets. `d.sh` runs `bfin-elf-objdump --start/--stop` on the ELF in WSL.
- `l1dump.py`: dumps L1 with the encrypted bytes masked as `??`. `tbcplb.py` finds te-boot's plain CPLB table for comparison.

## 2. LDR load map (OP1_vdk.ldr, 1 DXE, 801 blocks, FINAL → 0xffa00000) [C]

| Range | Size | Content | Notes |
|---|---|---|---|
| `0x01000000–0x01001ea8` | 7.8 KB | initialised data plus small FILLs | some FILLs use non-zero patterns (`0x8001`, `0x40004`, …) |
| `0x01001ea8–0x018ab7cc` | 9,083,172 B FILL | .bss | holds the TLSF pool `0x0106b7cc–0x018ab7cc` (§3) and other bss (`0x0101e5e8`, `0x0106a5ec…` are referenced) |
| `0x018ab7cc–~0x01914000` | ~420 KB | const/data (strings, tables) | heap table at `0x018e628c` |
| `~0x01914000–0x01aa7078` | ~1.6 MB | code | code/data split is a heuristic (density of ILLEGAL decodes) [I] |
| `0x01aa7078–0x01dfff34` | 3,509,948 B FILL | .bss | heap descriptors `0x01dffc40`, VDK and driver state, etc. |
| `0x02000000–0x02025800` | 150 KB | mostly FILL plus small init data | display buffers (§5) |
| `0x02040000–0x027d1fe0` | 7,938,016 B FILL | one big array | returned by getter `0x191b320` (5 callers). 7,938,016 B = 3,969,008 16-bit samples ≈ 90.0 s mono at 44.1 kHz. Probably an audio (sampler/tape) cache [I] |
| `0x027d1fe0–0x027ffb04` | 187,172 B FILL | array | used at `0x1a1bbf0` |
| `0x03000000–0x0303ffff` | 262,143 B FILL | second display buffer area (§5) | |
| `0xff800000–0xff802fb4` | 12 KB | L1 data A: encrypted CALLBACK data plus FILLs | CPLB tables at `0xff802c34` / `0xff802ccc` |
| `0xff900000–0xff907e00` | 31.5 KB | L1 data B: encrypted data plus FILLs | highest referenced address `0xff907df8` |
| `0xff907e00–0xff907ef0` | 240 B | CALLBACK\|IGNORE block | **not loaded** (callback parameters) |
| `0xffa00000–0xffa0bfb4` | 48 KB | L1 code, encrypted | entry point; crt0 at `0xffa00000` |

Not loaded by the app LDR: `0x00000000–0x00ffffff`, `0x01dfff34–0x01ffffff`, `0x02025800–0x0203ffff`, `0x027ffb04–0x02ffffff` and `0x03040000–0x03ffffff`.

**Linker template [I]:** te-boot (same toolchain, plain L1) loads `0x01000000–0x01f21208` and `0x02040000–0x02f3ffff`. So the shared LDF has these SDRAM segments:

| Segment | App use |
|---|---|
| `0x00000000–0x00ffffff` | heap 0 |
| `0x01000000–0x01ffffff` | code, data, bss |
| `0x02000000–0x0203ffff` | display, 256 KB |
| `0x02040000–0x02ffffff` | big bss |
| `0x03000000–0x0303ffff` | display, 256 KB |
| `0x03040000–0x03ffffff` | heap 1 |

The two app gaps, `0x01dfff34–0x01ffffff` and `0x027ffb04–0x02ffffff`, are the unused tails of the linker's code segment and big-bss segment.

## 3. Heaps, pools and stacks

### VisualDSP++ C heaps (`___heap_table` at `0x018e628c`) [C]
```
{ base 0x00000100, length 0x00fffefc, id 0 }   -> heap 0 = 0x00000100-0x00fffffc (all of low SDRAM)
{ base 0x03040000, length 0x00fbfdb0, id 1 }   -> heap 1 = 0x03040000-0x03fffdb0
{ 0 }                                          -> terminator
```
- **Init:** `heap_init` `0x1a9efb4` walks the table and calls `heap_install` `0x1a9f208`. The heap list head is at `0x01dffc40` and the count at `0x01dffc44`.
- **Allocation:**
  - `malloc` `0x1a9f348` = `heap_malloc(0, n)` `0x1aa0318`, with 42 callers.
  - Heap 1 is used by `0x1a3420c` (callers `0x1978d72`, `0x1a51dae`) and `0x1a5deb4`.
  - VDK picks a heap index from a table at `0x018c1f88` (`0x1a9c168`) [C].
- **So all of low SDRAM (`0x100`–16 MB) is heap.** Whatever te-boot left there is overwritten at runtime. Not usable [C].
- Heap 1 stops `0x250` bytes short of the top of SDRAM. That leaves room for te-boot's context block at `0x03ffff00`, which the app reads:
  - data pointer at `0x018d2454` → `0x03ffff00`;
  - code at `0x196169c` and `0x19616ce` reads `0x03ffff02` and `0x03ffff04` [C].

### TLSF pool (main `new`/allocator pool) [C]
- `init_memory_pool(0x840000, 0x0106b7cc)` at `0x1a5e364` (called from `0x19eff1a`).
- Pool = **`0x0106b7cc–0x018ab7cc` (8.25 MB)**. It lies inside the first big FILL.
- Allocation:
  - alloc wrapper `0x1a5e3b0`, 154 callers;
  - free wrapper `0x1a5e310`, 842 callers;
  - mutex handle at `0x01dfb5e8`.
- Supporting strings:
  - `init_memory_pool (): …` at `0x018fd3b8`;
  - `te_new.cpp:31 out of memory. tried to alloc %i; used %i; max %i` at `0x018fcad4`. This is the operator new path [I].
- **USB heap:** `0x1a71350` mallocs 0x18000 (96 KB) from heap 0 and makes it a second TLSF pool (`udheap.c`), base stored at `0x018c1ea8` [C].

### Stacks
- **crt0** at `0xffa00054` sets `SP = 0xff803fb0`, then `USP = SP; FP = SP` [C].
- **Supervisor/interrupt stack:** in L1 data A, growing down from `0xff803fb0` toward the end of the loaded data at `0xff802fb4` (≈4 KB) [I for the lower bound].
- **VDK thread stacks:** allocated from the C heaps through the VDK heap index table [I]. `OP_Main.cpp:138 !((int)start_of_stack & 3)` and `Thread %i: stack is busted!` show runtime stack checks [C].

## 4. Cache and CPLB configuration

### CPLB tables
CPLB setup runs in L1 at `0xffa09410`:
- `I0 = 0xff802c34` (dcplbs table) → `DCPLB_ADDR0` (`0xffe00100`) and `DCPLB_DATA0` (`0xffe00200`);
- `I0 = 0xff802ccc` (icplbs table) → `ICPLB_ADDR0` (`0xffe01100`) and `ICPLB_DATA0` (`0xffe01200`) [C].

The tables are `{addr, flags}` pairs ending with `{0xffffffff, 0}`. Some words are encrypted; those addresses are filled in from the 4 MB stride (marked `(i)`).

| Page (4 MB) | DCPLB flags | ICPLB flags |
|---|---|---|
| `0xff800000` (L1 data, i) | `0x0003401f`: 4 MB, write-through, locked, not cached | — |
| `0x00000000` | `0x0003109f`: write-back cached, locked | enc |
| `0x00400000` | enc | `0x00031005` |
| `0x00800000` (i) | `0x0003109f` | `0x00031005` |
| `0x00c00000` | `0x0003109f` | enc |
| `0x01000000` | enc | `0x00031005` (addr i) |
| `0x01400000` (i) | `0x0003101d`: write-back cached | `0x00031005` |
| `0x01800000` | `0x0003101d` | enc |
| `0x01c00000` | **enc** | `0x00031005` (addr i) |
| `0x02000000` (i) | `0x0003501d`: **write-through** | `0x00031005` |
| `0x02400000` | `0x0003101d` | enc |
| `0x02800000` | **enc** | `0x00031005` (addr i) |
| `0x02c00000` (i) | `0x0003101d` | `0x00031005` (address and flags both visible) |
| `0x03000000` | `0x0003501d`: **write-through** | enc |
| `0x03400000` | enc | `0x00031005` (addr i) |
| `0x03800000` (i) | `0x0003101d` | `0x00031005` |
| `0x03c00000` | `0x0003101d` | enc |
| `0xef000000` (boot ROM) | enc | `0x00021005` (addr i) |
| `0xffa00000` (L1 code) | — | `0x00020003` |

What the table shows:
- Every 4 MB SDRAM page has an entry in both tables. **All of SDRAM is mapped, cacheable and executable** [C for the visible entries, I for the encrypted ones]. te-boot's plain table (`0xff802564…`) has the same one-entry-per-4-MB layout, with every SDRAM page valid [C], which supports this.
- Each table has 18 entries but only 16 hardware slots. The VisualDSP CPLB manager must therefore swap entries on CPLB misses [I].
- The display pages (`0x02000000`, `0x03000000`) are write-through for DMA coherency [C flags].

### L1 memory control
- `DMEM_CONTROL` is set at `0x1aa179e` (bank A only, `0x8`, or both banks, `0xc`) and ENDCPLB at `0xffa096ba` [C].
- L1 data B's upper half (`0xff904000–0xff907e00`) is loaded as SRAM. So the data cache must be bank A only, which means **`0xff804000–0xff807fff` is data cache** [I].
- The ICPLBs mark everything cacheable, so the instruction cache is on and `0xffa10000–0xffa13fff` is I-cache, not SRAM [I].

## 5. DMA buffers found

| DMA | MMR base | Setup code | Buffers |
|---|---|---|---|
| DMA1 / DMA2 (PERIPHERAL_MAP `0x4000` / `0x3000` → SPORT0 TX/RX [I]) | `0xffc00c40` / `0xffc00c80` | `0x191ac00–0x191ad30` (also SPORT0 `0xffc00800/0820`) | descriptor chains in **L1 data A**: `0xff800370`, `0xff800770`, `0xff800b70` [C]. Audio codec I/O lives in L1, not SDRAM. |
| DMA0 (PPI → display [I]) | `0xffc00c00` | `0x1a30030–0x1a300aa`, `0x1a36b30` | `0x02000000`, `0x020001de` (built with `BITSET 25`) [C] |
| MDMA0 | `0xffc00f00/f40` | `0x193e74c–0x193e98c` | called with `0x02000000` and `0x03000000` (`0x19f08d6`, `0x19f08e0`). `0x1941558` takes the same two buffers. `0x1a2fbd0` chooses between `0x03000000` and `0x02000000` (double buffering [I]) [C] |
| (display copy and clear) | — | `0x197649a–0x1976548` | fixed addresses `0x02025620`, `0x02027e00` (320×240 halfword copy), memset `0x0202d000` len `0x5400`, `0x0202b800` len `0x1800`, `0x02000000` len `0x5000` [C] |
| DMA3 with the NAND controller (`0xffc03700`) | `0xffc00cc0` | `0x1a5e5f0`, `0x19ea0xx` | buffers passed in by callers (heap) plus `0xff802b68` [C/I] |
| USB DMA | `0xffc03cxx` | not traced | heap (USB pool) [I] |

**Tape and samples:**
- The only large static audio-sized buffer is the `0x02040000` array (7.94 MB ≈ 90 s mono 16-bit) [C size; I purpose].
- The tape (63.5 MB) is streamed from `/user/tape_c.raw`. Its working buffers come from heap 1 or this array [I].
- None of these reach `0x027ffb04` or above [C for the static arrays].

## 6. Candidate free regions

| # | Region | Size | Evidence it is free | Evidence against / risk | Verdict |
|---|---|---|---|---|---|
| A | **`0x02c00000–0x02ffffff`** | 4 MB | Not loaded by the LDR [C]. Not in the heap table [C]. No `.L/.H`, `(X)`+`.H`, BITSET or shift constant points into it [C]. The only data words in range are numeric tables (semitone/pitch tables at `0x018c12e8` and `0xff906d1c…`, packed halfword tables at `0x018eaxxx`) and the CPLB entry [C]. Has its own CPLB page; ICPLB address and flags `0x00031005` both visible, DCPLB flags `0x0003101d` visible [C]. 3.9 MB above the last static array. | DCPLB address word is encrypted (i). The stock app never touches this page, so its CPLB miss path is never exercised [I]. Wild pointers and encrypted-L1-only references can't be ruled out statically. te-boot uses it, but te-boot doesn't run in app mode [C, boot chain]. | **Best** |
| B | `0x02800000–0x02bfffff` (plus `0x027ffb04–0x027fffff`) | 4 MB | Same as A [C]. | DCPLB flags for this page are encrypted. Its ICPLB address is encrypted [I valid]. Directly above the 187 KB array ending at `0x027ffb04`, so an overrun there would land here. | Good |
| C | `0x01e00000–0x01ffffff` (plus `0x01dfff34–0x01dfffff`) | 2 MB | Not loaded, not heap, no references [C]. Its DCPLB page (`0x01c00000`) is valid by construction, because the app's busy .bss lives in the same page [I, strong]. ICPLB flags `0x00031005` visible. | Only 204 bytes above the end of .bss (`0x01dfff34`). Shares an unlocked CPLB page with the hottest bss. | Good (second choice; also fine for data) |
| D | `0x02025800–0x0203ffff` | 106 KB | Not loaded | **Used**: hard-coded display buffers `0x02025620`, `0x02027e00`, `0x0202b800`, `0x0202d000` [C] | Reject |
| E | `0x00000000–0x000000ff` | 256 B | Below heap 0 | Null-pointer writes land here; tiny | Reject |
| F | `0x00000100–0x00ffffff` (te-boot leftovers) | 16 MB | — | **Heap 0** [C] | Reject |
| G | `0x03040000–0x03fffdaf` | 15.75 MB | — | **Heap 1** [C] | Reject |
| H | `0x03fffdb0–0x03ffffff` | 592 B | — | te-boot context `0x03ffff00`, read by the app [C] | Reject |
| I | `0x0303ffff` (last byte of the FILL) | — | — | display area | Reject |
| J | L1 data B `0xff907e00–0xff907fff` | 512 B | Not loaded (IGNORE block). Highest reference `0xff907df8` [C] | Encrypted L1 code could still use it [I] | Possible for a few hot variables only |
| K | L1 data A `0xff802fb4–0xff803fff` | ~4 KB | — | System stack (SP `0xff803fb0`) [C] | Reject |
| L | L1 data A `0xff804000–0xff807fff` | 16 KB | — | Data cache [I] | Reject |
| M | Scratchpad `0xffb00000–0xffb00fff` | 4 KB | — | Used by DSP code: `0xffb00000` (16 refs), `0xffb00a00` (14), `0xffb00c10`, `0xffb00600`, `0xffb00790` [C] | Reject |
| N | L1 code `0xffa0bfb4–0xffa0bfff` and `0xffa10000–0xffa13fff` | 76 B / 16 KB | — | Full and I-cache respectively [C/I] | Reject |

## 7. Recommendation

**Put the new block in region A:**
- **Code:** `0x02c00000–0x02c3ffff` (256 KB).
- **DSP buffers:** `0x02c80000–0x02cb1fff` (200 KB). The 256 KB gap from the end of the code is a guard and leaves room for growth.

Keep `0x02f00000–0x02ffffff` empty as a guard below the `0x03000000` display buffers.

How to inject it:
- Code and initialised data go in plain (non-CALLBACK) LDR blocks inserted before the FINAL block.
- Zero-initialised buffers go in a FILL block (16 bytes in the LDR).

Fallbacks:
- **Region C** (`0x01e00000–0x01ffffff`) if A misbehaves. Its CPLB validity is the most certain, because it shares a page with live .bss.
- **Region B** for more space.

**Confidence: medium-high (~85%)** that A is never touched by the stock app.
- Statically there is no reference of any form. The surrounding layout (linker segment tails, explicit heap table, fixed display areas) explains every used range.
- The remaining risk:
  - encrypted L1 code computing addresses there (unlikely, because L1 code mostly calls into SDRAM and the visible parts show no such constants);
  - runtime-computed pointers;
  - an invalid DCPLB entry hidden in the encrypted bytes (unlikely given te-boot's identical table layout with all pages valid).

**Still to verify on the device (in order):**
1. **Canary test (no code needed):**
   - Add FILL blocks that paint `0x02800000–0x02ffffff` and `0x01e00000–0x01ffffff` with a pattern such as `0xA5A5A5A5`. A FILL block's argument is the fill value.
   - The first code hook then checks the canary after heavy use: tape record/play, sampler, USB disk mode, a firmware-sync check, and a long session. It reports by drawing on screen or writing a file.
2. **CPLB check:** the first hook should read the decrypted table words at `0xff802c90` / `0xff802c94` (`0x02800000` and `0x02c00000` DCPLB), or `DCPLB_DATAx` after a first access, to confirm valid write-back flags.
3. **CPLB-miss cost:**
   - Code at `0x02c00000` takes CPLB misses (the manager swaps unlocked entries).
   - If audio-rate DSP code runs from there, measure the cycle cost.
   - Keep the hottest inner loops small, or consider later copying them into L1 (would need L1 space, which we don't have today).
4. **Cache coherency:** if the new code writes to its own code region at runtime, or shares buffers with DMA, flush/invalidate (the region is write-back cached). Code loaded by the boot ROM before caches are enabled needs nothing.
5. **Image size:** confirm the LDR stays well within the 124 NAND blocks (≈15.5 MB); stock uses 17. A 256 KB code block adds 2 blocks.

## 8. Useful addresses
- Heap table `0x018e628c`; `heap_init` `0x1a9efb4`; `heap_install` `0x1a9f208`; `heap_malloc` `0x1aa0318`; `malloc` `0x1a9f348`.
- TLSF: init `0x1a62fec`, malloc `0x1a629fc`, free `0x1a62d02`. Pool wrappers: init `0x1a5e364`, alloc `0x1a5e3b0`, free `0x1a5e310`.
- crt0 `0xffa00000` (SP init at `0xffa00054`). CPLB init `0xffa09410`. Cache control helpers `0x1aa1720–0x1aa1830`.
- Display buffer users: `0x193e85e` (MDMA), `0x1941558`, `0x197649a`, `0x1a2fbd0`, `0x1a30030`.
- Big array getter `0x191b320` → `0x02040000`.

## Master compressor / drive (mixer page 4), 2026-10-19

Looked at for CPU: the user asked whether switching it off would buy anything.
- The page's four settings are copied by `MasterController`'s knob handler (`0x19da2bc`, subscreen 3 = `kCompressor`, jump table `0x18e336c`) to **`0xff80189c`** (4 x int16); subscreen 0 goes to `0xff801928`, 1 (EQ) to `0xff8017cc`, 2 (FX) to `0xff8016c4`.
- `0x1a29d30` (called from `0x19dc25e`, `0x1a4474c`, `0x1a5447e`) converts the four to software floats (`0x1aa7050` = int to IEEE float) into a vector at `0x1ab333c`, when `[0x1ab31d0]` is non-zero (an object pointer, by the look of it; not an on/off).
- No SDRAM code reads `0xff80189c` or the vector apart from those: the DSP is in the Core's L1 code (`0xffa0xxxx`, a third of it XTEA-encrypted), where neither address shows in the plain bytes. There is no bypass flag, and L1 can't be patched without the key (`docs/app-boot-chain.md`).
- Expected cost anyway: a stereo compressor/drive at 44.1 kHz is of the order of 50-200 cycles a sample, 1-4% of the CPU; the firmware's whole idle share is ~23-25% (Test 16). Not a lever for the Rings + Clouds gap (20-30%).
