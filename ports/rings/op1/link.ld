/* Links code injected into the OP-1 app (firmware 246) into the free SDRAM
 * region found in docs/memory-map.md. tools/inject_app.py turns the PT_LOAD
 * segments into extra LDR blocks (and .bss into a FILL block). */

OUTPUT_FORMAT("elf32-bfin")
ENTRY(_op1_header)

MEMORY
{
  injected (rwx) : ORIGIN = 0x02c00000, LENGTH = 0x00400000
}

SECTIONS
{
  .header : { KEEP(*(.op1header)) } > injected
  .text : { *(.text .text.*) } > injected
  .rodata : { *(.rodata .rodata.*) } > injected
  .data : { *(.data .data.*) } > injected
  .bss (NOLOAD) : ALIGN(4)
  {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > injected
  /* C symbols get a leading underscore on bfin: __image_end in C. */
  ___image_end = .;

  /* Nothing may need run-time initialisation: there is no crt0. */
  .init_array : { *(.init_array*) *(.ctors*) }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.fini_array*) *(.dtors*) }
}
