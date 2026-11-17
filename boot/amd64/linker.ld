OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

/* Keep the Limine request block in its own writable, non-executable segment.
 * The bootloader updates the requested base revision and response pointers. */
PHDRS
{
  limine_requests PT_LOAD FLAGS(0x06);
  text            PT_LOAD FLAGS(0x05);
  rodata          PT_LOAD FLAGS(0x04);
  data            PT_LOAD FLAGS(0x06);
}

SECTIONS
{
  . = 0xffffffff80000000;
  __kernel_start = .;

  .limine_requests : ALIGN(8)
  {
    KEEP(*(.limine_requests_start))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end))
  } :limine_requests

  . = ALIGN(CONSTANT(MAXPAGESIZE));
  .text : { *(.text.boot) *(.text .text.*) } :text

  . = ALIGN(CONSTANT(MAXPAGESIZE));
  .rodata : { *(.rodata .rodata.*) } :rodata
  .note.gnu.build-id : { *(.note.gnu.build-id) } :rodata

  . = ALIGN(CONSTANT(MAXPAGESIZE));
  .data : { *(.data .data.*) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data

  __kernel_end = .;

  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.note .note.*)
    *(.got .got.*)
    *(.gnu.lto_*)
  }
}
