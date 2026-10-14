# Emulating the OP-1 (BF524)

## Verdict
- **CPU-level emulation works today.** The GNU sim (`sim/bfin`, from gdb 15.2) has a `bf524` model and runs real te-boot code: crt0, C runtime and early init, about 800 distinct instructions, until the first Boot ROM call.
- **Booting to a usable UI is a substantial project.** Three gaps need closing, in rising order of difficulty: Boot ROM services, the board's off-chip peripherals, and the app's scrambled L1 code.
- **Emulation can't prove a flash is safe.** The bricking risk is the NAND boot-block write in the flasher, and on a real unit that write involves unit-specific calibration and OTP state. Safety comes from never writing the bootloader (`docs/brick-safety.md`). An emulator is useful for testing custom *app* code before it goes on the device.

## Toolchain
`tools/build_bfin_toolchain.sh` (run in WSL Ubuntu) builds binutils 2.43 and gdb 15.2 for `bfin-elf` into `~/op1-toolchain/bfin`:
- `bfin-elf-objdump` disassembles. Use `tools/ldr_dump.py X.ldr --elf X.elf` first to turn an LDR into a loadable ELF.
- `bfin-elf-run --model bf524 --environment operating X.elf` is the simulator. Add `--trace-insn=on --trace-file=F` for an instruction trace.
- `bfin-elf-gdb` (`target sim --model bf524 --environment operating`) gives interactive debugging.
- Ghidra has no stock Blackfin module, and no SLEIGH spec turned up. Binary Ninja has the community `arch-blackfin` plugin.

## What the sim models for BF52x (`sim/bfin/machs.c`)
- Core, CEC/SIC interrupts, DMA, PLL, watchdog, RTC, UART0/1, SPI, GP timers ×8, GPIO ports, PPI, TWI, OTP, the NFC (NAND flash controller), EBIU async and SDRAM controllers.
- **Boot ROM: mapped, but empty.** `bfroms/bf526-*.h` are zero-length stubs, since ADI's ROM code isn't redistributable.

## Experiments (firmware 246)

| Image | Result | Blocker |
|---|---|---|
| te-boot DXE 0 (init) | Runs, then spins forever at `0xffa00632` | Polls `EBIU_SDSTAT` (`0xFFC00A1C`) bit 0 (SDRAM controller idle), which the sim never sets |
| te-boot DXE 1 (main) | crt0, C init, early main, about 800 PCs | Calls Boot ROM `0xEF000018` (`bfrom_OtpCommand(OTP_INIT, timing)`). The ROM is empty, so it faults and VDK's fatal handler loops in `IDLE` at `0xffa03860` |
| OP1_vdk | Faults on its first instruction | Its L1 code is XTEA-encrypted (8 of every 24 bytes; key in OTP page 0xD0, see `app-boot-chain.md`). It also expects the init DXE to have set up SDRAM |

Boot ROM entry points te-boot uses:
- `0xEF000018` OtpCommand
- `0xEF00001A` OtpRead
- `0xEF00001C` OtpWrite (key programming)
- `0xEF000038` SysControl (PLL/voltage)

## Work needed, in order
1. **Small sim patches (days):**
   - report SDRAM idle in `dv-bfin_ebiu_sdc.c`
   - add high-level-emulation stubs for the four ROM entry points above. OtpRead would return either a captured OTP image or zeros.
2. **App L1 decryption:** the transform is known (XTEA, 8 of every 24 bytes, see `app-boot-chain.md`), but the key is in OTP. Read it once from a unit with a small SDRAM patch, then decrypt offline.
3. **NAND and YAFFS2 (weeks):**
   - Back the sim's NFC with a NAND chip model, using page geometry from te-boot's `NAND_NUM_BLOCKS` / `NAND_PAGES_PER_BLOCK` constants.
   - Build a YAFFS2 image holding `content/`.
4. **Board peripherals (weeks to months):**
   - display (CMEL OLED; driven over SPI/PPI)
   - GPIO_UI1–5 key and encoder expanders (SPI)
   - codec (TWI plus SPORT audio)
   - accelerometer, battery gauge, FM radio, RTC
   - The sim needs `--hw-device` models for each. Start with the display and keys, since those make the UI usable.

Another route: [vapier's QEMU Blackfin fork](https://github.com/vapier/qemu) is faster, but its author calls it unfinished. Its core comes from the same GNU sim, so it has the same peripheral gaps.
