# How the OP-1 boots the app, and whether a modified app will run

Static analysis of te-boot v2.30 and OP1_vdk 246. Addresses are runtime addresses. The init DXE was disassembled with `tools/ldr_dump.py te-boot.ldr --dxe 0 --elf` and `bfin-elf-objdump`.

## Answer
**No integrity check exists on the app image.** The only checks are:
- the outer package CRC-32, which we regenerate;
- the per-block LDR header checksums (XOR = 0), which `ldr_dump.serialize()` regenerates.

The app's L1 code is partly **encrypted**, but it is not authenticated. Modified apps can be built as long as the encrypted blocks are carried over byte for byte.

## Boot chain
1. **Boot ROM** boots te-boot from the NAND boot blocks and runs te-boot's **init DXE** (INIT block, code at `0xffa00000`, 2.5 KB).
2. Init DXE (`0xffa00000`):
   - Context pointer at `[0xff804004]` → `0x03ffff00` (top of SDRAM).
   - `0xffa00514`: `bfrom_OtpCommand(OTP_INIT)` / `bfrom_OtpRead` read **OTP page 0xD0 (128 bits) into ctx+0x10**: this is the XTEA key, word-swapped after reading.
   - PLL/SDRAM/peripheral setup (`0xffa00590`, `0x698`, `0x8a8`, `0x1e0`, `0x334`, `0x2d8`, `0x194`).
   - `0xffa0042c`: reads a register from the on-board device at address `0x25` (ID `0x60`) and returns `!bit12`. Most likely the **COM key** state.
   - Reads OTP page `0xD8` (a configuration flag) and combines the two. In "app" mode it:
     - sets the ROM's NAND boot start to **block 4** (`[cfg+0x50] = [cfg+0x54] = 4`);
     - installs the **decrypt callback** `pBS->pCallBackFunction = 0xffa1007e`.
   - Otherwise it returns, and the ROM carries on into te-boot's main code (the menu).
3. In app mode, the **ROM loads `OP1_vdk.ldr` from NAND block 4**. It calls the callback for each `CALLBACK` block, then jumps to `0xffa00000`.
4. te-boot's main code never loads the app itself. After a menu action it reboots with a watchdog reset (`0x169ec`: `WDOG_CTL = 0x0FF0`) and the init DXE decides again.

So **whether te-boot runs is decided before any app code is loaded.** Holding COM at power-on reaches te-boot no matter what the app contains.

## The callback: XTEA, 8 bytes out of every 24
- `0xffa10000`: standard XTEA decipher (delta `0x9E3779B9`, 32 rounds, 128-bit key at ctx+0x10).
- `0xffa1007e`: the ROM callback.
  - For each CALLBACK buffer it decrypts **the first 8 bytes of every 24-byte chunk** in place.
  - The offset carries across buffers via `W[pBS+0x26]`.
  - A counter is kept at ctx+0x20. Nothing is compared or verified.
- Both the callback (`0xffa10000`, 296 B) and its data (`0xff804000`, 188 B) sit in L1 ranges the app never loads into, so they survive while the app loads.
- Confirmed on the data: in the 48 KB L1 instruction block, byte entropy is **7.99** bits/byte at positions 0–7 mod 24 (ciphertext) and **~6.77** elsewhere (plain Blackfin code; te-boot's L1 is 6.65).
- 45 CALLBACK blocks (80,324 bytes, all L1) are encrypted this way. All SDRAM blocks (≈23 MB of image, all the main application code) are **plain**.
- **The key is the same on every unit.** TE ships one `OP1_vdk.ldr`, and the flasher (`0x1388`) writes it to NAND unchanged, so every unit must hold the same key in OTP page 0xD0. We don't have it, and XTEA has no practical known-plaintext attack.

## NAND layout
- Boot area: `NUM_NAND_BOOT_BLOCKS` = **128 blocks × 128 KB = 16 MB**. This is the write-lock check in `nand.cpp` (block < 0x80); block = page >> 6, 64 × 2 KB pages.
- te-boot is written from block 0 (at most 0x3ff00 bytes, 2 blocks).
- **OP1_vdk.ldr is written raw from block 4** (`0x1388`, 128 KB chunks padded with 0xFF). Room: **124 blocks ≈ 15.5 MB**, against 17 blocks for stock. The YAFFS2 filesystem lies beyond the boot area.

## What this means for custom firmware
- **SDRAM code and data can be patched freely**, or new plain blocks added. Use `parse()`, edit, then `serialize()`. The serializer reproduces both stock images byte for byte.
- **Encrypted L1 blocks must be copied over unchanged.** Two thirds of their bytes are readable, though, which helps reverse engineering.
- Patching L1 code itself means either (a) appending plain (non-CALLBACK) blocks that overwrite L1 after decryption (the ROM applies blocks in order), but only for code regions we fully understand, or (b) obtaining the key.
- **Getting the key** (needed for full L1 disassembly and for emulating the app): a small SDRAM patch that calls `bfrom_OtpRead(0xD0)`, or copies decrypted L1, and writes the result to a file on the USB disk. This is a later, optional step.

## Remaining uncertainty
- This is static analysis. The Boot ROM isn't available to read, but ADI's BF52x ROM has no authentication, only header checksums (and optional CRC through an init-code callback, which isn't used here).
- The app could in theory check itself at runtime. Nothing points to that, but the first on-device test settles it.
- The COM-key reading of `0xffa0042c` and the role of OTP page 0xD8 are inferred. TE's documented "hold COM at power-on" behaviour fits.

## Verified on the device (2026-10-14)
1. **Stock 246 flashed through the te-boot menu.** The updater showed CRC OK, unpacking, "Updating te-boot…", **"te_boot skipped"**, then "Writing firmware…" and done. This confirms the unit runs TE-BOOT v2.30 (the skip only happens when the running bootloader's version string matches) and that the recovery path works.
2. **Patched app flashed** (`op1_246_cow.op1`). `OP1_vdk.ldr` was changed in 3 bytes only: the play-mode label at `0x18f2f8c`, `poly` → `cow`. The package was built with `tools/patch_app.py` and `tools/build_package.py`, with no te-boot.ldr inside. **It booted normally and the play-mode screen showed COW.**

This confirms there is no integrity check on the app image, and that edits to plain SDRAM blocks round-trip through the LDR writer, packaging and the te-boot flasher.
